# Changelog

## 2026-10-08 — V4.4.1: Aura crash (host sends larger blocks than announced)

### Crash report analysed
Aura 1.0.0 (JUCE 8.0.4 host), IRIS4 4.4.0 VST3. `EXC_BAD_ACCESS`, null write in `_platform_memmove` ← `IrisAudioProcessor::processSubBlock+204` ← `processBlock+464` on the CoreAudio IO thread. This is the same signature as the Oct 6 crash with 4.3.

### Diagnosis (from the disassembly of the installed binary and the crash registers)
- `processSubBlock+204` is the return address of `memcpy(inputBuffer.channels[0], …)`.
  - x19 = 0 → channel 0.
  - x27 = 2 → `numInputCh` = 2.
  - x0 = NULL destination; x1 = valid source; 0x400 bytes = 256 samples.
- So `inputBuffer` reported ≥ 2 channels while its channel 0 pointer was NULL. `AudioBuffer::setSize` can't produce that, so the memory had been overwritten.
- The `buffer` argument was on the stack, so `processBlock` was in its slicing path: IRIS was prepared for 256 samples and Aura sent a larger block.
- Only one IRIS instance was alive (one convolution loader, one set of workers), and no IRIS code was running on any other thread.
- JUCE's VST3 wrapper (`ClientBufferMapperData`, juce_VST3Common.h) sizes its scratch buffers from the host's `maxSamplesPerBlock`, then copies/clears `data.numSamples` samples into them without a bounds check. A host that sends more than it announced makes the wrapper (and the plugin writing into the mapped buffer) write past the heap block. That zeroed the start of IRIS's input-buffer allocation.

### Reproduction
- New harness test `IrisHarness oversize`: a JUCE host loads the VST3, prepares it for 256 samples, then sends 512/1024/10000-sample blocks.
- Against an ASan build of the IRIS VST3: **heap-buffer-overflow, WRITE of size 1024**. The allocation is in `ClientBufferMapper::prepare` ← `JuceVST3Component::setupProcessing`, and the write comes from `processSubBlock` clearing the mapped buffer.

### Fix
- `IRIS_VST/patches/juce-8.0.12-vst3-oversized-blocks.patch`, applied to the local JUCE checkout (`dev/IRIS_VST/JUCE`) and by CMake's FetchContent `PATCH_COMMAND`:
  - Wrapper scratch buffers are sized to at least 8192 samples.
  - Blocks above that capacity output silence instead of writing out of bounds.
  - IRIS already slices large blocks internally, so blocks up to 8192 process normally.
- Version 4.4.1.

### Tests
- ASan VST3 + ASan host, `oversize`: no errors. Right-channel level at block end 0.500 for 256 / 512 / 1024 (dry passes), 0.000 for 10000 (silenced). Before the patch: heap-buffer-overflow at the first 512 block.
- Release Universal bundle, `oversize`: same values. `vst3`: loads as 4.4.1, 200 blocks, no non-finite samples, state round trip, editor, clean unload.
- `auval -v aufx Irs4 IRIS`: AU VALIDATION SUCCEEDED.
- Installed to `~/Library/Audio/Plug-Ins` (4.4.0 replaced; the Sep 24 backup is unchanged).

### Open issues
- **Aura should be fixed too.** It must not send blocks larger than the `maxSamplesPerBlock` it gives `setupProcessing` (VST3 rule), i.e. prepare hosted plugins with the largest chunk `ProcessorGraph::processChunk` can pass. Other JUCE plugins in Aura will corrupt memory the same way; this patch only protects IRIS.
- The AU wrapper wasn't changed (Aura loads the VST3).


## 2026-10-07 (evening) — V4.4.0: audit fixes (branch `audit-fixes`)

Yesterday's fixes were committed first, on their own (`c1255f9`). Everything below is in one follow-up commit.

### Files touched
`PluginProcessor.cpp/.h` (largely rewritten: threading, routing, gain, convolution engine, state, layout), `IrisOSCManager.cpp/.h`, `PluginEditor.cpp/.h`, `ListenerListComponent.cpp/.h`, `IRListComponent.cpp/.h`, `WallListComponent.cpp`, `RoomMapComponent.cpp/.h`, `ControlPanelComponent.cpp`, `CMakeLists.txt`, `compile.sh`, `README.md`, `test_osc.py`, `TESTING.md` (new).

### Changes (audit IDs from AUDIT.md)
**Threading (C1, C2/N1, C5, N3, N14, C4, M1):**
- `parameterChanged` only sets atomic flags (reprocess pending, listener moved, broadcast mask). The 60 Hz timer does the work on the message thread. Nothing heavy or lock-taking runs on the audio thread any more.
- Lock order is now fixed (`listLock` → `stateLock`). No OSC call is made while holding `stateLock`.
- The audio thread takes no locks. Every model change happens on the message thread or under `stateLock`.
- `suspendProcessing` removed from reprocess.
- The editor polls `structuralChangePending` instead of receiving `callAsync([this]…)` (use-after-free).
- "Ignore my own parameter callback" is now tracked per thread, so concurrent host automation is never dropped (N16).

**Input hardening (C3, M7, M8, N15, state NaN):**
- OSC: every float must be finite, UUIDs must parse, matrix edges are validated, at most 64 remote listeners, a locked local listener can't be moved remotely, and `/iris/param/*` is applied only where that parameter's broadcast flag is on.
- `setStateInformation`: every numeric value is sanitised, and any non-finite parameter is reset to its default.
- IR files: capped at 120 s and 64 channels, `read()` result checked, NaN/Inf samples zeroed, `bad_alloc` caught.
- The OSC socket still listens on all interfaces (TECHNICAL_Description describes LAN use). Hardening the inputs was preferred over binding to 127.0.0.1.

**Audio (H1, H5, M5, M11, N6, N7, N12, M6):**
- Routing: a mono input feeds every IR channel; a mono IR on N-channel input uses one convolver per channel.
- IRs normalised to unity energy. Output Gain default changed from −30 to 0 dB.
- Wet = Mix (no longer scaled by the sum of the weights). IR gains are equal-power (√ of the normalised weights).
- Per-wall attenuation now applies (`1 − opacity·(1 − attenuation)·edgeFade`).
- Edge fade only at free wall ends (corners and T-junctions are sealed), over a fixed distance of 0.03.
- Onset alignment keeps 1 ms of pre-roll and fades it in on all channels.
- Bypass copies the mono input to every output.
- Tail length recalculated on restore. IRs with missing files get zero weight (the others are renormalised). Link matrix saved and restored.

**Convolution engine (N2, H2, H3, H4, M2, M3, N4, N5, N13, N18):**
- `Convolution(NonUniform{4096}, sharedQueue)`: still zero latency, and one loader thread per instance instead of one per convolver.
- IR loaded **before** `prepare()`, so the real engine is built synchronously and there is no dry window.
- Persistent realtime worker threads replace `ThreadPool` + `std::function`: no allocation, they join the host audio workgroup, and a single job runs inline.
- Per-block gain ramps on every IR and on mix/output. A re-activated convolver is reset. Replaced or removed IRs fade out over 30 ms. Old render states are freed only off the audio thread.
- `prepareToPlay` rebuilds convolvers only when rate, block size or channel count changes, and publishes a settled render state (headless/offline renders get reverb from the first block).

**Multi-instance (H6, H7, N8, M9):**
- Listener position is written to the host only when it changes; drags are wrapped in begin/end gestures.
- w1–w3 are no longer written and are marked non-automatable.
- The link matrix reaches instances in the same process, so links work in both directions.
- Layout load broadcasts IR positions, names, locks and walls. Receivers use the sender's IR name.
- `broadcastListener` and `broadcastIRs` are respected.

**UI (H8–H13, M12, N10, N11):**
- Renames are not cancelled by refreshes, and the "⚠" suffix is never saved into a name.
- Wall rows are filled on creation; their edits go through `updateWall`, `setWallName` and `setWallLocked`.
- Cancelling Save Layout writes nothing.
- Remote listener lookups use `find()` (no ghost listeners). The link matrix rebuilds when the set of ids changes.
- The link-matrix CallOutBox is parented to the editor and detached when the editor closes.
- The weight overlay is its own click-through layer (no more 25 Hz full-editor repaint).
- Added a TooltipWindow. Listener clicks hit the drawn (smoothed) dot. Wall moves are clamped as a whole.
- The local listener row follows its id after a restore. The IR list refreshes x/y at 10 Hz.
- The editor shows an orange notice when port 9001 is owned by another app.

**Layout JSON (M10, N17):**
- A missing extent value defaults to 1.
- A relative path resolves only next to the JSON file (no fallback to the host's working directory).
- mix, inertia, output_gain and listener are saved and loaded.

**Build (B1):** deployment target 11.0 (libc++ floor); version 4.4.0.

**Session migration:** sessions without `stateVersion` (≤ 4.3) that stored an Output Gain get +30 dB (clamped to +12). Older sessions with no stored gain keep 0 dB.

### Decisions / dead ends
- **N4 (tail cut on reprocess):** a full fix would keep the old convolver ringing on silence while the new one takes the input, which doubles CPU for up to 36 s. Chose a 30 ms crossfade instead: no click and no dry gap, but the reverb tail restarts when Normalize/Align is toggled.
- **N2 memory:** JUCE's `MultichannelEngine` always builds two engines per convolver, even for mono. That can't be changed without patching JUCE, so memory dropped less than CPU.
- **N9 (cross-process sync):** not implemented. It needs multicast or a relay design; the README now states what actually works.
- **Wet = Mix and equal-power weights** change the sound by design (see TESTING.md §3). Easy to revert if the old "dry when far from the IRs" behaviour was intended.

### Tests (all on this Mac, arm64, Apple clang 21, JUCE 8.0.12)
Test program: a console app linking the V4 sources (scratch copy, not in the repo), in Release, ASan+UBSan and TSan builds. Commands are `IrisHarness <test>`.
- `h1`: mono→stereo + stereo IR: L 1.000, R 1.000 (was R 0.000). Stereo in + mono IR: right input → right output only (was nothing / both).
- `m2`: |out| at n=0 right after adding an IR: 0.000 (was 0.767 dry). After a normalize toggle: 0.000.
- `c3`: NaN on every `/iris/param/*` and on listener sync: 0 non-finite samples (was 20480/20480 for mix).
- `m5`: occlusion factor at attenuation 0 / 0.5 / 1: 0.000 / 0.500 / 1.000 (was 0 / 0 / 0).
- `n6`: sealed box, ray through a corner: 0.050 (was 1.000).
- `gain`: fully dry at mix 0: 0.0 dB (was −30.0). Normalised demo IR energies: 0.0 dB each (was +24.6 / +36.2 / +40.2 / +42.4).
- `state`: tail after restore 2.000 s (was 0.000). One IR file missing: wet 1.000 (was 0.500).
- `multi`: B's matrix gets the link; moving B moves A; idle host notifications 2 per second (was 120); layout positions and walls match in B.
- `c1`: worst audio-thread `normalize` automation call 0.0 ms (was 36.3 ms).
- `n1`, `c2`: no deadlock in 10 s each (97M automation calls with timers running; 666 create/delete cycles). Both deadlocked within 2–3 s before.
- TSan `c5`, `n1`, `race`: 0 warnings (before: 6 races plus a segfault). One race in the new code (shared `getArrayOfWritePointers()` on the input buffer) was found and fixed.
- ASan+UBSan `race`, `fstate` (600), `fjson` (600), `fosc` (30 000), `editor` (20 open/flood/close cycles), `h1`, `state`, `multi`: no memory errors.
  - One UBSan report is inside JUCE (`String::getDoubleValue` signed overflow on huge exponents, reached via `APVTS::replaceState`), not IRIS code.
  - `fosc`: 20 000 fresh UUIDs → 64 listeners (was 20 000).
- `migr`: v1 at −30 dB → 0.0 dB; v1 at −20 dB → +10.0 dB; v1 with no gain → 0.0 dB; v2 round trip −6 → −6.
- Plugin build: Universal, `minos 11.0`, 0 libc++ warnings (was 45).
- `auval -v aufx Irs4 IRIS`: **AU VALIDATION SUCCEEDED** (arm64). The x86_64 slice was not validated because Rosetta isn't installed here.
- `vst3`: the installed bundle loads in a JUCE host as "IRIS4 4.4.0 by IRIS", processes 200 blocks without non-finite samples, round-trips state, creates and closes its editor, and unloads cleanly.

### Benchmark: convolution CPU (`IrisHarness perf`, Release, Apple Silicon, 14 cores)
Setup: the 4 demo IRs (0.6 / 8.7 / 21.9 / 36.1 s, 48 kHz), all 4 active (spread 0.6), mono→stereo, white noise, 5 s of audio. Old = commit `c1255f9` built with the same program.

| Block | Old p50 / p99 / p99.9 (ms) | Old over budget | New p50 / p99 / p99.9 (ms) | New over budget |
|---|---|---|---|---|
| 128 (2.67 ms) | 2.287 / 2.567 / 3.262 | 9 / 1875 | 0.021 / 1.008 / 1.853 | 1 / 1875 |
| 512 (10.67 ms) | 1.215 / 1.868 / 6.084 | 0 / 468 | 0.034 / 1.482 / 4.788 | 0 / 468 |

Mean at block 128: 86 % → 2 % of budget. Peak RSS of the same run (`/usr/bin/time -l`): 757 MB → 547 MB. The non-uniform tail runs in periodic bursts, so the worst single block is close to the old one. Percentiles are much lower.

### Deployment
- Old installed bundles (Sep 24, arm64 only) backed up to `Codes/IRIS/installed_backup_2026-09-24/`.
- New 4.4.0 Universal VST3 + AU installed to `~/Library/Audio/Plug-Ins/{VST3,Components}`. AU cache refreshed (`killall AudioComponentRegistrar`).

### Open issues / next steps
- Test in Aura and REAPER/Live: TESTING.md.
- Not done: N9 cross-process sync, background IR loading (the GUI still freezes while long IRs load or reprocess), true tail continuity on reprocess (N4), font deprecations, resizable editor, `ParameterID` version hints.
- Not validated: the x86_64 slice under auval (needs an Intel Mac or Rosetta); pluginval (not installed).


## 2026-10-07 — Verification pass (no plugin code changes)
- Built a Universal Release VST3 + AU in a scratch directory: compiles, `x86_64 arm64`. Found that the 10.13 deployment target is below the SDK's libc++ floor (11.0).
- Built a test harness that links the V4 sources directly (Release, ASan+UBSan, TSan variants) and ran 16 tests. Results are in the new "Verification pass" section of `AUDIT.md`.
- Reproduced: two-instance deadlock (2 s), create/delete deadlock (3 s), TSan data race → segfault on normalize automation, NaN audio from `/iris/param/mix` and from NaN session values, H1 channel routing, −30 dB dry level, 36 ms audio-thread stall, wall attenuation ignored, corner occlusion leak, layout desync between instances, tail 0 after restore, unbounded remote listeners.
- ASan clean: prepare/process race (confirms the Oct 6 fix), state fuzz (1500), JSON fuzz (300), OSC fuzz (15 000).
- Not run: auval/pluginval (would overwrite the installed plugin), editor-dependent items.

## 2026-10-07 — Full audit (no code changes)
- Read-only audit of all V4 sources, CMake, README, demo layout, `test_osc.py`. Findings in `AUDIT.md`: 5 Critical, 13 High, 14 Medium, plus Low items, with a suggested fix order.
- I re-checked these claims myself against the code: per-wall `attenuation` is never used in the weight math; `broadcastListener` is never checked; `setLinkMatrixConnections` is only called from the OSC receive path; normalisation targets RMS 0.1; the mono-input channel count includes `numInputCh`.
- Nothing compiled or run.

## 2026-10-06 — Fix: host crash on DSP start, params not restored, wall opacity default

### 1. Crash when turning DSP on (Aura host, VST3)
**Crash report:** `EXC_BAD_ACCESS` null write in `_platform_memmove` ← `IrisAudioProcessor::processBlock` on the CoreAudio IO thread. The only memmove in `processBlock` is `inputBuffer.copyFrom(...)`, so the destination channel pointer of `inputBuffer` was null.
**Most likely cause:** the host calls `prepareToPlay` (which resizes `inputBuffer` / scratch buffers) while the audio callback is already running. During `AudioBuffer::setSize` reallocation the channel pointers are briefly zero/dangling. Contributing risks in the same function: block larger than prepared size overran buffers; channel counts from the host weren't clamped to what was allocated; >64 active IRs would index past `irScratchBuffers`.
**Changes (`PluginProcessor.cpp/.h`):**
- `prepareToPlay` holds `getCallbackLock()` (JUCE wrappers call `processBlock` under it), floors a 0 block size to 512, and swaps the live `RenderState` to the newly prepared convolvers (old ones were prepared for the old block size).
- New `preparedBlockSize` atomic. `processBlock` outputs silence if not prepared, and splits oversized host blocks into prepared-size slices; the old body is now `processSubBlock`.
- Input/scratch channel counts clamped to allocated sizes; dry path skips missing input channels; active IR count capped at `kMaxParallelIRs`.

### 2. Spread / wall opacity (and other global params) not kept after reopening
**Causes found:**
- `setStateInformation` restores params with `setValueNotifyingHost` → `parameterChanged` → `oscManager.setGlobalParam` broadcast to every other IRIS instance. When a session reloads, each instance restored overwrites the ones before it, so all end up with the last-loaded instance's spread / opacity / inertia / freeze (these broadcast by default; mix doesn't, which is why mix seemed fine).
- OSC/global-sync code wrote the raw APVTS atomic *before* calling `setValueNotifyingHost`. JUCE's parameter adapter then sees "no change", so the APVTS state tree (the `PARAMETERS` block that gets saved) and listeners were never updated.
- Broadcast checkboxes weren't saved, so they reset to "on" every session.
**Changes:**
- New `isRestoringState` flag (RAII-guarded in `setStateInformation`); `parameterChanged` doesn't broadcast while it's set.
- `IrisOSCManager.cpp`: removed the raw `->store()` calls; values go only through `updateParameterNotifiers` (parameter object).
- Broadcast flags saved/restored in a new `BROADCAST_FLAGS` element (older sessions without it keep defaults).

### 3. Wall opacity default 0.8 → 1.0
Parameter default, processor fallback, state-restore fallbacks and `RoomMapComponent` fallback.

**Tests:** `g++ -std=c++17 -fsyntax-only -Wall -Wextra -Wshadow` against JUCE 8.0.12 headers (Linux) on `PluginProcessor.cpp`, `IrisOSCManager.cpp`, `RoomMapComponent.cpp`, `ControlPanelComponent.cpp`: no errors, no new warnings. **Not built or run on macOS yet** — crash fix and state restore still need testing in Aura.

**Open issues / next steps**
- If the crash persists, it would point to Aura calling `processBlock` without the callback lock (worth checking in Aura's `ProcessorGraph`: it should not call `prepareToPlay` on a node while `processChunk` is running).
- `timerCallback` calls `setValueNotifyingHost` for listenerX/Y 60×/s even when unchanged; harmless but spams the host with edits. Consider only sending on change.
- Pre-existing `-Wshadow` warnings for `flags` in `ControlPanelComponent.cpp`.

## 2026-10-06 — Fix: plugin does not load on Intel Macs

**Diagnosis** (inspected `IRIS_VST/build/IRIS4_artefacts/Release/{VST3,AU}/.../MacOS/IRIS4`):
- Both binaries are `Mach-O 64-bit arm64` only — no x86_64 slice. `CMakeCache.txt` has `CMAKE_OSX_ARCHITECTURES` empty, i.e. built with the README's plain `cmake ..`, which only targets the host CPU. (`compile.sh` already passed `arm64;x86_64`, but the README path did not.)
- `LC_BUILD_VERSION` reads `minos 27.0.0` (`CMAKE_OSX_DEPLOYMENT_TARGET` empty → defaults to the build machine's macOS). macOS 26 is the last release for Intel Macs, so even a Universal build with this setting would refuse to load on Intel.
- No architecture-specific code in `Source/V4` (no NEON/SSE intrinsics, no `std::filesystem`), so no source changes needed.

**Changes**
- `IRIS_VST/CMakeLists.txt`: on Apple, default `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` and `CMAKE_OSX_DEPLOYMENT_TARGET="10.13"` before `project()` (only when not set by the user; overrides stale empty cache values).
- `IRIS_VST/CMakeLists.txt`: fixed relative JUCE path `../../../dev/...` → `../../dev/...` (old path pointed outside the repo; it only worked through the hardcoded absolute fallback).
- `IRIS_VST/compile.sh`: also passes `CMAKE_BUILD_TYPE=Release` and the deployment target explicitly.
- `README.md`: documented Universal output and how to verify with `lipo` / `vtool`.

**Tests**
- Not built yet: the sandbox is Linux and cannot run the Apple toolchain. To verify on the Mac:
  `rm -rf build && ./compile.sh`, then `lipo -info` (expect `x86_64 arm64`) and `vtool -show-build` (expect `minos 10.13`), then load in a DAW on the Intel machine.

**Open issues / next steps**
- The old `IRIS_VST/build/` folder contains arm64-only binaries; delete or rebuild it, and replace any copies already sent to Intel users.
- If distributing outside your own machines, the bundle should be code-signed/notarized, or Gatekeeper may block it (separate from the Intel issue).
- The hardcoded `/Users/luna/...` JUCE fallback in `CMakeLists.txt` is still there; fine locally, irrelevant for others (they fall through to FetchContent).
