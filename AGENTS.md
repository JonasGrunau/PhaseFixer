# PhaseFucker

## Purpose
Automatic phase alignment plugin (VST3 / AU / Standalone, JUCE 8), working like Voxengo PHA-979: the track is never moved in time; every frequency is turned by one angle with a linear-phase Hilbert FIR (fixed ~48 ms latency, host-compensated), plus a polarity flip. Inserted on the track to fix, with the reference on the sidechain. ALIGN analyses the last seconds of both and writes phase (−180…180°) and polarity into normal parameters, and reports the time offset (the "time anchor") as advice only. The editor shows both waveforms over each other (scope) and phase per frequency band.

## Key Files
| File | Description |
|------|-------------|
| `CMakeLists.txt` | Targets: `phasefucker_core` (static lib, no JUCE), `PhaseFucker` plugin, `phasefucker_snapshot`, `phasefucker_tests`, `phasefucker_align`. Options `PHASEFUCKER_BUILD_PLUGIN`, `PHASEFUCKER_BUILD_TESTS`, `PHASEFUCKER_COPY_PLUGIN` (off in CI), cache var `PHASEFUCKER_VERSION` (release tag sets it). UI sources are listed once in `PHASEFUCKER_UI_SOURCES` and used by the plugin and the snapshot tool |
| `src/core/align/Analyzer.cpp` | The detection: closed-form best rotation (−arg of the weighted cross-spectrum sum), confidence, ML-GCC time anchor |
| `src/core/align/Rotator.cpp` | Real-time chain: `cos φ·x − sin φ·H{x}` with a partitioned FFT convolution (stereo packed into one complex FFT), click-free glides; `hilbertOffline` / `rotateOffline` for the scope |
| `src/core/dsp/Hilbert.cpp` | Linear-phase FIR Hilbert design (Kaiser β 6, ~43 ms half length) |
| `src/core/align/CrossSpectrum.cpp` | Averaged S_ab/S_aa/S_bb; band phases and summed-energy gains for any `Correction` |
| `src/core/align/Capture.cpp` | Lock-free ring of mono reference/track plus per-block energies; audio thread writes, worker and editor read |
| `src/plugin/PluginProcessor.cpp` | Buses (main + "Reference" sidechain), parameters (`phase`, `invert`, `compare`), align worker thread, fixed latency, state |
| `src/plugin/PluginEditor.cpp` | 880x620 sheet scaled as a whole; header, scope, phase strip, controls, time anchor, status line. Repaints only what changed |
| `.github/workflows/release.yml` | `v*` tag: macOS universal build + tests + pluginval + sign/notarize (when secrets set) + pkg/zip, Windows VST3 + tests + Inno Setup, GitHub release |

## For AI Agents

### Working In This Directory
- Core loop (seconds): `cmake -B build-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DPHASEFUCKER_BUILD_PLUGIN=OFF && cmake --build build-core && ./build-core/phasefucker_tests`
- Full plugin: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build` (installs into `~/Library/Audio/Plug-Ins`). A local JUCE: `-DFETCHCONTENT_SOURCE_DIR_JUCE=...`.
- Never include JUCE from `src/core`. New `.cpp` files go into `CMakeLists.txt` by hand.
- Audio thread: no allocation or locks in `processBlock` / `Rotator::process` / `Capture::push`. The capture is reallocated only in `prepareToPlay`, under `captureLock`; readers go through `withCapture`.
- Analysis results become parameter changes on the message thread (`applyResult`), so they are undoable and automatable.
- JUCE 8 API: text widths via `theme::textWidth` (no `Font::getStringWidthFloat`); `juce::String (x, 0)` prints all decimals, use `theme::fixed`.
- libc++ `std::polar` returns NaN for a negative magnitude: write `sign * std::polar (1.0, phase)`.
- UI performance: every control is opaque; nothing repaints on the timer unless its content changed. Busy plots (scope, phase strip) draw through `theme::paintOffscreen` (JUCE software renderer into an image, then one blit; 2-3x faster than CoreGraphics for them). Dense traces are one filled outline per signal, never a stroked zigzag or a rectangle per column. Check with `phasefucker_snapshot <dir> bench`.

### Testing Requirements
- After core changes: `./build-core/phasefucker_tests` must pass (Hilbert flatness, rotation accuracy and latency, click-free changes, detected angle within 1°, polarity, colouring + bleed, time anchor, silence, sample rates).
- After UI changes: build `phasefucker_snapshot`, render `<dir>` and read the PNGs.
- Before a release: `auval -v aufx PhFk Jgru` and pluginval at strictness 5 on both formats.

### Common Patterns
- Namespaces: `pf` (core), `pf::dsp` (primitives), `theme` (UI tokens and helpers). Plugin/UI classes are global.
- JUCE-style formatting: space before call parens `foo (x)`, 4-space indent, braces on their own line.
- Look: paper `#EEEAE0`, ink `#141414`, one accent vermilion `#E2401C`; no gradients, shadows or glow; corner radius 2 px; IBM Plex Sans Condensed labels, IBM Plex Mono numbers.

## Dependencies
- JUCE 8.0.15 (AGPLv3), Catch2 v3.7.1, C++20, CMake ≥ 3.24, macOS deployment target 11.0
- IBM Plex fonts (OFL), bundled in `resources/fonts`

## Manual Notes

Notes under this heading are written by humans and preserved on regeneration.
