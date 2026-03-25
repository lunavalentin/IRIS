# IRIS — Spatial IR Convolution Plugin

This repository contains the IRIS spatial convolution plugin and its V2/V3 variants. The latest plugin in the tree is `IRIS3`.

This README documents the latest plugin (IRIS3) exhaustively: features, build & install steps, UI guide, parameters, OSC integration, layout save/load, supported formats, troubleshooting, and a short evolution summary describing differences between v1 → v2 → v3.

If you want screenshots added to this README (recommended for the UI sections), please provide high-resolution PNGs or JPEGs; place them in `docs/screenshots/` and name them `roommap.png`, `controlpanel.png`, `irlist.png`, `walllist.png`. I can embed them once provided.

---

## Contents
- **Overview** — What IRIS3 does
- **Features** — High-level feature list
- **Requirements** — Tools and libs to build
- **Build (macOS)** — Quick build steps (CMake/JUCE)
- **Install** — Where artifacts are produced and how to install
- **User guide** — UI regions, workflow, examples
- **Parameters** — Exposed parameters, ranges and defaults
- **OSC integration** — Incoming and outgoing messages with examples
- **Layout / Persistence** — Save/load layouts and plugin state
- **Supported formats** — Audio file formats for IRs
- **Troubleshooting** — Common issues & fixes
- **Developer notes** — Useful source locations and hooks
- **Screenshots** — Placeholders and instructions
- **Changelog / Evolution (v1 → v2 → v3)** — How functionality evolved

---

**Overview**

IRIS is a spatial convolution plugin that can load multiple impulse responses (IRs) as spatial sources in a normalized 2D room map, compute nearest neighbors and dynamic weights, and render a mono-convolved (wet) signal mixed with the dry input. The plugin supports occlusion/walls, layout saving, OSC control, and per-IR channel selection and preprocessing (onset-trim and normalization).

**Features (IRIS3)**
- Multi-IR points placed on a normalized room map (0.0–1.0 coordinates).
- A dedicated Listener point representing the listener position.
- Per-IR file loading with automatic onset alignment (trim) and RMS normalization.
- Convolution engine per IR (JUCE DSP Convolution), lazy-loaded and prepared with host sample rate.
- Nearest-neighbour selection and Gaussian-weighted blending between active IRs.
- Parameterized smoothing/hysteresis (inertia/τ-in/Tau-out), spread (sigma), and freeze.
- Dry/Wet mix with dynamic fade based on current total IR weight.
- Visual occlusion walls that attenuate IR contributions when they intersect source→listener paths.
- Layout save/load (JSON) from the UI; plugin state saved/restored via host preset/state.
- OSC: receive external listener/IR position updates and IR lists; send current weights via OSC.
- Per-IR channel selection (for multichannel files), and previewing/choosing channels.

**Requirements**
- macOS (build instructions below assume macOS but CMake + JUCE is cross-platform).
- CMake >= 3.15
- A C++17 capable compiler (Apple Clang on recent Xcode is fine).
- The project includes a local `JUCE/` directory under `IRIS_VST/`, so no external JUCE install is required for this workspace.

**Build (macOS) — quick**

1. From the repo root run:

```bash
cd IRIS_VST
mkdir -p build
cd build
cmake ..
make -j$(sysctl -n hw.logicalcpu)
```

2. Build artifacts for `IRIS3` will be produced under `IRIS_VST/build/IRIS3_artefacts/Release/` and a VST3 bundle at `IRIS_VST/build/IRIS3_artefacts/Release/VST3/IRIS3.vst3`.

Notes:
- If you prefer to use the Projucer or an IDE, open the CMake project or use the JUCE project setup in `IRIS_VST/JUCE`.
- Build errors referencing JUCE APIs indicate a mismatched JUCE module version; ensure the bundled `IRIS_VST/JUCE` is not inadvertently replaced.

**Install**
- macOS VST3: copy `IRIS3.vst3` to `~/Library/Audio/Plug-Ins/VST3/` (user-wide) or `/Library/Audio/Plug-Ins/VST3/` (system-wide).
- For AU/AAX builds (if enabled), use the corresponding generated bundles from the build output.

**User guide — UI and workflow**

IRIS3 editor is split into four main regions:

- Room Map (left, ~60%):
  - Visual 2D space where each IR point and the Listener are shown.
  - Drag points to change their normalized positions (0..1).
  - Listener is a red point; move to change perceived position.
  - Walls (occlusion) are drawn as cyan lines; they attenuate IR contributions when cross paths.

- Control Panel (top-right):
  - `+ IR` — open a file chooser to load one or more IR files (WAV/AIFF/MP3 supported by JUCE formats).
  - `Mix` — global Dry/Wet mix slider.
  - `Load Layout` / `Save Layout` — load or save a room layout (JSON).
  - `+ Wall` — insert a wall segment; walls are editable via the Wall List.
  - `Wall Opacity` — visual opacity; not the same as attenuation (see wall list).
  - `Freeze` — freeze positional updates / weight updates.
  - `Inertia` — smoothing of positional updates (0.0–1.0).
  - `Spread` — controls the Gaussian sigma used to compute neighbor weights.

- IR List (middle-right):
  - Lists loaded IRs (name, color). Select an IR to change its channel or lock/rename/remove.
  - Channel switching calls `reloadIRChannel()` and rebuilds the convolver using the selected channel.

- Wall List (bottom-right):
  - Lists walls with names and attenuation settings.
  - Adjust attenuation to control how much each wall reduces IR contribution (0.0 = full block, 1.0 = transparent).

Typical workflow:
1. `+ IR` → load one or more impulse responses (recommended: short room IRs, mono preferred but multichannel supported).
2. Arrange IR points on the Room Map; place the Listener where you want the mix focus.
3. Add walls to simulate occlusion and adjust attenuation.
4. Tweak `Spread` to widen or narrow spatial blending; `Inertia` to smooth fast movement.
5. Use `Mix` to set the dry/wet balance. Save your layout to JSON for reuse.

**Parameters**

- `w1` / `w2` / `w3` — internal legacy normalized weights for the top 3 neighbors. Range: [0.0, 1.0]. Default: 0.0. (Primarily diagnostic / UI display.)
- `inertia` — smoothing/inertia for position-to-weight updates. Range: [0.0, 1.0]. Default: 0.0.
- `freeze` — toggle to freeze dynamic updates. Type: boolean. Default: false.
- `spread` — controls the spatial sigma used for Gaussian weighting (0.0..1.0). Default: 0.3.
- `mix` — overall dry/wet mix. Range: [0.0, 1.0]. Default: 1.0.
- `wallOpacity` — controls UI opacity of wall visuals. Range: [0.0, 1.0]. Default: 0.8.

Notes: The actual effective wet amount is computed as `mix * clamp(totalWeight, 0, 1)`. The plugin uses smoothed weights internally to prevent zippering.

**OSC integration**

IRIS3 both receives OSC and can send OSC updates. Default ports used by the plugin code:
- OSC Receiver: listens on port `9001` (hardcoded in constructor).
- OSC Sender: sends to `127.0.0.1:9002` by default.

Incoming messages supported:
- `/listener/pos [float x, float y]` — set listener normalized position (0.0–1.0).
- `/ir/pos [int index, float x, float y]` — set IR position by index (non-listener IRs enumerated in order loaded).
- `/ir_list [string name ...]` — populate the plugin's `availableIRs` list (used if an external source enumerates IR files remotely).

Outgoing messages sent from the plugin:
- `/iris/weights s:name f:weight s:name f:weight ...` — periodically sends current nearest neighbors' names and normalized weights (sum normalized by the plugin before sending).

Example usage — sending OSC from Python (script included `test_osc.py`):

```bash
python3 test_osc.py --ip 127.0.0.1 --port 9001
```

The example moves the listener in a circle and moves IR 0 to demonstrate OSC control.

**Layout / Persistence**

- Use `Save Layout` to write a JSON description containing `extent`, `irs` (with resolved file paths), and `walls` to a file you choose.
- Use `Load Layout` to import a previously exported JSON. The loader resolves relative paths relative to the JSON file and will try to reload audio files synchronously.
- Host preset/state save: the plugin saves its internal state (points, walls, file paths, selected channels, and basic IR processing fields) into the host's plugin state via `getStateInformation` / `setStateInformation`.

**Supported audio formats**
- The plugin uses JUCE's format manager with `registerBasicFormats()`; typical supported formats include WAV, AIFF, MP3 (depending on build), and other JUCE-compatible formats. For best results use mono WAV or purpose-prepared IRs.

**Troubleshooting**

- Build errors referencing missing JUCE modules: ensure `IRIS_VST/JUCE` folder is present and intact.
- `IRIS3.vst3` code signing warnings on macOS: after building, macOS may warn about missing resources/signature. If distributing signed bundles, follow Apple code signing and notarization flows.
- High CPU during initial IR load: large IR files are loaded and convolved; consider downsampling or trimming long impulses before importing.
- No audio output: check host plugin routing (plugin is configured for mono input and mono processing then mixed to output). Hosts expecting stereo inputs may need proper bus configuration — the plugin is configured with strict mono main output in the current build.

**Developer notes**
- Main plugin CMake project: `IRIS_VST/CMakeLists.txt` (defines `IRIS_V2` and `IRIS3` targets).
- IRIS3 source lives under `IRIS_VST/Source/V3/` — key files:
  - `PluginProcessor.cpp` / `PluginProcessorV3.h` — audio & OSC logic
  - `PluginEditor.cpp` / `PluginEditorV3.h` — GUI layout
  - `RoomMapComponentV3.*`, `ControlPanelComponentV3.*`, `IRListComponentV3.*`, `WallListComponentV3.*` — UI subcomponents
- Build outputs (macOS VST3): `IRIS_VST/build/IRIS3_artefacts/Release/VST3/IRIS3.vst3`.

**Screenshots**

If you can provide screenshots, add them under `IRIS_VST/docs/screenshots/` (or `docs/screenshots/` at repo root) and I will embed them here. Recommended names:
- `roommap.png` — full UI focused on the Room Map
- `controlpanel.png` — control sliders and buttons
- `irlist.png` — IR list panel
- `walllist.png` — wall list panel and an example wall attenuation

---

**Changelog / Evolution — v1 → v2 → v3 (functional summary)**

Note: v1 sources are not present in this repository. The following summary is based on the available V2 and V3 source code and on common progression of the project as represented in the codebase.

- v1 (prototype):
  - Basic convolution plugin prototype that loaded a single impulse response and applied convolution to the input.
  - Very simple UI.

- v2 (multi-IR spatial model):
  - Introduced multiple IR points and a `Listener` point on a normalized 2D room map.
  - Implemented nearest-neighbor selection and Gaussian-weighted blending between multiple IRs.
  - Added per-IR processing (onset alignment, RMS normalization) to make imported IRs consistent.
  - Added smoothing/hysteresis across weights and basic APVTS parameters (inertia, spread, mix).
  - Began OSC support for remote control/automation and `loadLayoutFromJSON`.

- v3 (interactive, occlusion, layout & UI polish):
  - Added occlusion `Wall` objects that attenuate IRs when a wall intersects the path from source to listener.
  - Added UI subcomponents for Wall management (`WallList`), improved `ControlPanel` (save/load layout, add wall), and an overlay showing active IR gains.
  - Implemented `saveLayoutToJSON` and richer JSON layout semantics (resolving relative paths), and per-IR channel selection and lazy convolution load.
  - Improved mixing logic: dynamic fade based on total weight, improved smoothing, and UI polish (labels, bars, colors).
  - OSC improvements: `/ir_list`, `/listener/pos`, `/ir/pos` incoming messages; `/iris/weights` outgoing messages.

If you want I can expand this evolution section into a full chronological changelog with commit references or file diffs if your repository contains that history.

---

If you'd like, I can now:
- embed screenshots (upload images or point me to files),
- generate a short quickstart video script or GIF instructions for new users, or
- open a PR with the README added and optionally a `docs/` folder containing the suggested screenshot placeholders.

Please tell me if you want screenshots embedded and provide the image files (or say "I'll add screenshots later").
