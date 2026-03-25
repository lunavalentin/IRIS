# IRIS V3 Diagnostic Report

Summary
-------
- Scope: static audit of IRIS V3 (Source/V3) and relevant build logs. Focus on build errors, correctness, thread-safety, OSC integration, and actionable improvements.
- Goal: list concrete problems, severity, where to look, and prioritized remediation steps.

Critical / Build-stopping Issues
--------------------------------
- FlexBox / unsupported API error (High)
  - Symptom: earlier build failed with errors like "no member named 'setLayoutMode' in 'juce::Component'" and "no member named 'LayoutMode' in 'juce::FlexBox'".
  - Files: Source/V3/WallListComponent.cpp (build logs: build_output_wall.txt, rebuild_final.txt).
  - Impact: prevents compilation.
  - Fix: remove calls to non-existent API; use explicit layout/resized logic or correct FlexBox usage per current JUCE version.

- Buses / layout mismatch (High)
  - Symptom: constructor adds mono input + stereo output but `isBusesLayoutSupported()` enforces mono output.
  - Files: Source/V3/PluginProcessor.cpp / PluginProcessorV3.h, CMakeLists.txt (IRIS_VST project config).
  - Impact: host may reject plugin or cause runtime issues.
  - Fix: reconcile declared BusesProperties with `isBusesLayoutSupported()` (choose mono-in/mono-out or stereo as intended). Update channel counts and processBlock handling accordingly.

Concurrency / Thread-safety (High)
---------------------------------
- Manual CriticalSection exit/enter (High)
  - Symptom: `loadLayoutFromJSON()` performs `stateLock.exit()` before calling `addIRFromFile(...)` then re-enters. This manual reentrancy is fragile.
  - Files: Source/V3/PluginProcessor.cpp (layout load/save, addIRFromFile), other callers that manipulate `points` and `walls`.
  - Impact: deadlocks, race conditions, inconsistent state; file I/O while holding locks or unlocking then re-locking is error-prone.
  - Fix: refactor to collect necessary data while holding lock then perform I/O and heavy work outside the lock; use scoped locking consistently. Prefer posting asynchronous tasks for long-running operations.

- Heavy I/O or audio file parsing on message thread (High)
  - Symptom: `addIRFromFile()` reads audio files, performs processing and convolution creation — may be invoked synchronously from UI / layout load.
  - Files: Source/V3/PluginProcessor.cpp (addIRFromFile, addIRFromFileWithID), PluginEditor code that triggers layout load.
  - Impact: UI freeze, real-time safety issues if invoked on audio thread.
  - Fix: Perform file reads and convolution creation off the message/audio thread (background thread or thread pool). Use thread-safe queuing to update state once resources are ready.

OSC / API & Integration (Medium)
--------------------------------
- OSC manager / processor API matching & thread-safety (Medium)
  - Symptom: IrisOSCManager calls processor helper methods (e.g., `addIRFromFileWithID`, `addWallWithID`, `setListenerPosition`, `updateParameterNotifiers`) — ensure signatures exist and are safe to call from OSC thread.
  - Files: Source/V3/IrisOSCManager.cpp, Source/V3/PluginProcessor*.{cpp,h}
  - Impact: crashes, invalid state updates, race conditions when OSC callbacks occur concurrently.
  - Fix: Ensure OSC callbacks marshal actions to the message thread or a safe worker thread; verify processor helper methods are thread-safe or only called via a serialized job queue.

- OSC ports and addresses (Info)
  - Defaults: receiver port 9001, sender target 127.0.0.1:9002. Documented messages: `/listener/pos`, `/ir/pos`, `/ir_list`, outgoing `/iris/weights`.

Audio / DSP Issues (Medium)
---------------------------
- Mono vs Stereo processing mismatch (Medium)
  - Symptom: processSpec.numChannels set to 1; convolver and mixBuffer work in mono internally but host buses may be stereo.
  - Files: Source/V3/PluginProcessor.cpp
  - Impact: incorrect channel handling, potential clipping or silent channels.
  - Fix: Decide on channel model. If host output is stereo, either process per-channel convolution or mix mono output to both channels explicitly. Update processSpec and buffer sizes accordingly.

Code Quality / Warnings (Low→Medium)
-----------------------------------
- Floating-point equality comparisons (`-Wfloat-equal`) (Low)
  - Symptom: multiple instances comparing floats with `==` or `!=` (e.g., using `d == 0`, `target == 0.0f`).
  - Files: Source/V3/* (PluginProcessor.cpp, PluginProcessorV3.h, others reported in build logs).
  - Impact: unexpected behavior due to precision; builds warn under `-Wfloat-equal`.
  - Fix: Replace equality checks with epsilon comparisons (e.g., `std::abs(x) < 1e-8f`).

- Deprecated `Font` usage (Low)
  - Symptom: warnings about deprecated `Font` constructors in `WallListComponent.cpp`.
  - Fix: Update to current JUCE `Font` API.

- Implicit conversion and signedness warnings (Low)
  - Fix: address by explicit casts or adjusting types.

Potential Logic / Sound Quality Issues (Medium)
--------------------------------------------
- Occlusion math and cumulative multiplication (Medium)
  - Symptom: occlusionFactor multiplies (1.0 - effectiveOpacity) for each intersecting wall; could decay very fast with many walls.
  - Files: Source/V3/PluginProcessor.cpp (updateWeightsGaussian)
  - Impact: audible artifacts or nodes becoming inaudible abruptly; verify intended physical model.
  - Fix: consider additive attenuation model, clamp min/max attenuation, or different combination function.

- Edge diffraction heuristic (Info)
  - Note: `edgeFade` uses linear 20% edge ramps; document and consider tuning parameters or exposing them as parameters.

Testing / Repro Steps
---------------------
- Reproduce build error: run CMake & make in `IRIS_VST/build` (see build logs). Failure previously occurs at WallListComponent compile.
- Exercise OSC: use `test_osc.py` to send `/listener/pos` and `/ir/pos` messages; verify processor updates without crashing.
- Restore layout: load a sample layout JSON with multiple IRs and walls to stress concurrency and large file loads.

Prioritized Action Plan (minimal patches suggested)
-------------------------------------------------
1) Build-fix (Blocker) — Fix WallListComponent compile errors (replace unsupported API usage). (Effort: small — 1-2 hours)
2) Buses / Channel Model (Blocker/High) — Decide mono or stereo internal processing and reconcile `isBusesLayoutSupported()` and BusesProperties. (Effort: small-medium — 2-4 hours)
3) Threading: Move heavy file I/O and convolver creation off message/audio threads; remove manual `stateLock.exit()` patterns. (Effort: medium — 4-8 hours)
4) OSC safety: Marshal OSC callbacks to safe thread context and validate processor API signatures. (Effort: small-medium — 2-4 hours)
5) Warnings cleanup: replace float-equality, update deprecated APIs, fix implicit conversions. (Effort: small — 2-6 hours)
6) Audio polish: review occlusion attenuation model and per-channel convolution strategy. (Effort: medium — 4-8 hours)

Suggested Quick Fixes I Can Produce Now
--------------------------------------
- Create `DIAGNOSTIC_V3.md` (this file) — done.
- Prepare a patch to fix WallListComponent compile errors (replace unsupported API usage). Ask if you want me to prepare this patch.
- Prepare a patch to make convolver creation asynchronous and to avoid manual `stateLock.exit()` — more invasive; I'll produce it if you approve.

Notes & Observations
--------------------
- Version mismatch: UI overlay prints "v3.1.1" while `CMakeLists.txt` project version is `0.0.1`. Consider aligning version metadata.
- Many functions set state and call `onStateChanged()` — ensure `onStateChanged` is safe to call from threads that mutate that state.
- Consider adding unit or integration tests for OSC messages and layout load/save to catch regressions.

Next Steps
----------
- Confirm if you want me to: (A) prepare patches for build errors, (B) implement async IR loading/convolver creation, (C) clean float equality warnings, or (D) any combination. I'll produce focused patches and run a rebuild where applicable.

Appendix: Quick file pointers
----------------------------
- Build logs: IRIS_VST/build_output_wall.txt, IRIS_VST/rebuild_final.txt
- Main audio engine: IRIS_VST/Source/V3/PluginProcessor.cpp, PluginProcessorV3.h
- UI: IRIS_VST/Source/V3/PluginEditor.cpp, PluginEditorV3.h, WallListComponent.cpp
- OSC manager: IRIS_VST/Source/V3/IrisOSCManager.cpp
