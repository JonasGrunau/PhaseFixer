# PhaseFucker

## Purpose
Automatic phase alignment plugin (VST3 / AU, JUCE 8). Inserted on the track to move, with the reference on the sidechain. ALIGN analyses the last seconds of both and writes shift (sub-sample, ±50 ms), polarity and an optional allpass into normal parameters. The editor shows both waveforms over each other (scope) and phase per frequency band.

## Key Files
| File | Description |
|------|-------------|
| `CMakeLists.txt` | Targets: `phasefucker_core` (static lib, no JUCE), `PhaseFucker` plugin, `phasefucker_snapshot`, `phasefucker_tests`, `phasefucker_align`. Options `PHASEFUCKER_BUILD_PLUGIN`, `PHASEFUCKER_BUILD_TESTS`, `PHASEFUCKER_COPY_PLUGIN` (off in CI), cache var `PHASEFUCKER_VERSION` (release tag sets it). UI sources are listed once in `PHASEFUCKER_UI_SOURCES` and used by the plugin and the snapshot tool |
| `src/core/align/Analyzer.cpp` | The detection: ML-GCC candidates, sum-scored selection, two-pass refinement, confidence, allpass fit |
| `src/core/align/Aligner.cpp` | Real-time chain (sinc delay with crossfaded heads, polarity ramp, allpass) and `applyCorrection` for offline use |
| `src/core/align/CrossSpectrum.cpp` | Averaged S_ab/S_aa/S_bb; band phases and summed-energy gains for any `Correction` |
| `src/core/align/Capture.cpp` | Lock-free ring of mono reference/track plus per-block energies; audio thread writes, worker and editor read |
| `src/plugin/PluginProcessor.cpp` | Buses (main + "Reference" sidechain), parameters, align worker thread, auto lookahead/latency, state |
| `src/plugin/PluginEditor.cpp` | 880x620 sheet scaled as a whole; header, scope, phase strip, controls, status line |
| `.github/workflows/release.yml` | `v*` tag: macOS universal build + tests + pluginval + sign/notarize (when secrets set) + pkg/zip, Windows VST3 + tests + Inno Setup, GitHub release |

## For AI Agents

### Working In This Directory
- Core loop (seconds): `cmake -B build-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DPHASEFUCKER_BUILD_PLUGIN=OFF && cmake --build build-core && ./build-core/phasefucker_tests`
- Full plugin: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build` (installs into `~/Library/Audio/Plug-Ins`). A local JUCE: `-DFETCHCONTENT_SOURCE_DIR_JUCE=...`.
- Never include JUCE from `src/core`. New `.cpp` files go into `CMakeLists.txt` by hand.
- Audio thread: no allocation or locks in `processBlock` / `Aligner::process` / `Capture::push`. The capture is reallocated only in `prepareToPlay`, under `captureLock`; readers go through `withCapture`.
- Analysis results become parameter changes on the message thread (`applyResult`), so they are undoable and automatable.
- JUCE 8 API: text widths via `theme::textWidth` (no `Font::getStringWidthFloat`); `juce::String (x, 0)` prints all decimals, use `theme::fixed`.
- libc++ `std::polar` returns NaN for a negative magnitude: write `sign * std::polar (1.0, phase)`.

### Testing Requirements
- After core changes: `./build-core/phasefucker_tests` must pass (detection accuracy, polarity, colouring, bleed, reflections, allpass fit, silence, sample rates, click-free changes).
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
