# Changelog

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
