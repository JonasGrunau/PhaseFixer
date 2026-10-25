# PhaseFucker

## Purpose
Automatic phase alignment plugin (VST3 / AU / Standalone, JUCE 8), working like Voxengo PHA-979: the track is never moved in time; every frequency is turned by one angle with a linear-phase Hilbert FIR (fixed ~48 ms latency, host-compensated), plus a polarity flip. Inserted on the track to fix, with the reference on the sidechain. ALIGN analyses the last seconds of both and writes phase (−180…180°) and polarity into normal parameters, and reports the time offset (the "time anchor") as advice only. Per hit (`perhit` parameter), ALIGN instead learns a turn per reference hit into a phase map on the host's timeline (kick + bass), played back with the transport; the phase knob is then an offset. REC PASS records one pass and renders it offline into a 24-bit WAV to drag into the DAW. The editor shows both waveforms over each other (scope) and phase per frequency band, or the phase map.

## Key Files
| File | Description |
|------|-------------|
| `CMakeLists.txt` | Targets: `phasefucker_core` (static lib, no JUCE), `PhaseFucker` plugin, `phasefucker_snapshot`, `phasefucker_tests`, `phasefucker_align`. Options `PHASEFUCKER_BUILD_PLUGIN`, `PHASEFUCKER_BUILD_TESTS`, `PHASEFUCKER_COPY_PLUGIN` (off in CI), cache var `PHASEFUCKER_VERSION` (release tag sets it). UI sources are listed once in `PHASEFUCKER_UI_SOURCES` and used by the plugin and the snapshot tool |
| `src/core/align/Analyzer.cpp` | The detection: per reference hit (low onsets), the turn scored through the real Hilbert filter in 10 ms analytic pieces (sum envelope in dB, pieces weighted by how much both play) on weight (<150 Hz), punch (150–500 Hz attack) and tone bands; frame-based −arg Σ W·S_ab for material without hits; confidence; ML-GCC time anchor |
| `src/core/align/PhaseMap.cpp` | Per-hit turns on the timeline (ppq): hold, raised-cosine glide in the second half of each gap, round the host's loop (its last hit glides into its first), merging loop passes, keeping hand edits; `timelineBefore` steps back across a loop wrap |
| `src/core/align/Rotator.cpp` | Real-time chain: `cos φ·x − sin φ·H{x}` with a partitioned FFT convolution (stereo packed into one complex FFT), glides along the arc; `hilbertOffline` / `rotateOffline` for the scope, `renderOffline` for export (same chain, no latency, loop-aware) |
| `src/core/dsp/Hilbert.cpp` | Linear-phase FIR Hilbert design (Kaiser β 6, ~43 ms half length) |
| `src/core/align/CrossSpectrum.cpp` | Averaged S_ab/S_aa/S_bb; band phases and summed-energy gains for any `Correction` |
| `src/core/align/Capture.cpp` | Lock-free ring of mono reference/track plus per-block energies and host timeline (ppq; a jump inside a block is kept too: a loop wrapping between two host blocks, or inside one, worked out from the loop points); audio thread writes, worker and editor read |
| `src/plugin/PluginProcessor.cpp` | Buses (main + "Reference" sidechain), parameters (`phase`, `invert`, `compare`, `perhit`), align worker thread (also renders exports), phase map (SpinLock, audio thread try-locks), export recorder, fixed latency, state (incl. map) |
| `src/plugin/PluginEditor.cpp` | 880x620 sheet scaled as a whole; header, scope, phase strip, controls, time anchor, status line. Repaints only what changed |
| `tools/Demo.cpp` | Demo host (`./start.sh`): plugin + built-in kick + rolling bass scenarios or two WAVs through the default output, reference on the sidechain, SUM/TRACK/REFERENCE monitor, looping `TempoPlayHead` transport. Samples from a local `demo-kit.txt` (`PF_DEMO_KIT`, `tools/DemoKit.h`; gitignored, never commit sample-pack audio or its paths), else synthesized (`tools/PsyLoop.h`) |
| `.github/workflows/release.yml` | `v*` tag: macOS universal build + tests + pluginval + sign/notarize (when secrets set) + pkg/zip, Windows VST3 + tests + Inno Setup, GitHub release |

## For AI Agents

### Working In This Directory
- Core loop (seconds): `cmake -B build-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DPHASEFUCKER_BUILD_PLUGIN=OFF && cmake --build build-core && ./build-core/phasefucker_tests`
- Full plugin: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build` (installs into `~/Library/Audio/Plug-Ins`). A local JUCE: `-DFETCHCONTENT_SOURCE_DIR_JUCE=...`.
- Never include JUCE from `src/core`. New `.cpp` files go into `CMakeLists.txt` by hand.
- Audio thread: no allocation or locks in `processBlock` / `Rotator::process` / `Capture::push`. The capture is reallocated only in `prepareToPlay`, under `captureLock`; readers go through `withCapture`. The map is read with a try-lock (a miss holds the last turn); export buffers are sized on the message thread while the export is idle, and stage changes are atomic compare-exchanges.
- Map timing: a block's output is `latency` samples late, so `turnFor` looks up the map at the block's ppq minus the latency plus one glide (`timelineBefore`: right after a loop wraps, that is the end of the last pass), passing the host's loop to `angleAt`. The scope and the map lane pass the loop too.
- Scope window: `Scope::windowStart()` is `scopePanMs` minus the default lead-in, clamped within the grab (the bar length). Sideways scroll moves it (that delta already follows the OS direction: no `isReversed`); vertical scroll and pinch zoom around the pointer. `panned` renders it.
- Loop wraps: some hosts end a block at the loop's end, others run through it and report only where each block starts. `readTransport` folds the expected position round the loop (a wrap is no jump) and sets `Transport::wrapAt`; the capture (`setTimeline` with the loop, `samplesToLoopEnd`) and the export recorder cut at that sample. `PFSNAP_SPLIT=0` runs the snapshot host through the loop's end; its export must match the split one.
- The map is learned on a copy and swapped in under `mapLock`; edits go by ppq (`editMapPoint`), and every change calls `mapChanged()`, which marks the session dirty (VST3; AU ignores it).
- Analysis results become parameter changes on the message thread (`applyResult`), so they are undoable and automatable.
- JUCE 8 API: text widths via `theme::textWidth` (no `Font::getStringWidthFloat`); `juce::String (x, 0)` prints all decimals, use `theme::fixed`.
- libc++ `std::polar` returns NaN for a negative magnitude: write `sign * std::polar (1.0, phase)`.
- UI performance: every control is opaque; nothing repaints on the timer unless its content changed. Busy plots (scope, phase strip) draw through `theme::paintOffscreen` (JUCE software renderer into an image, then one blit; 2-3x faster than CoreGraphics for them). Dense traces are one filled outline per signal, never a stroked zigzag or a rectangle per column. Check with `phasefucker_snapshot <dir> bench`.

### Testing Requirements
- After core changes: `./build-core/phasefucker_tests` must pass (Hilbert flatness, rotation accuracy and latency, click-free arc glides, offline render = real time, detected angle within 1°, per-hit angles within 3°, polarity, colouring + bleed, delayed kick within 0.5 dB of the best low end, time anchor, silence, sample rates, phase map transitions, loop wrap and merging, capture timeline incl. a wrap inside a capture block).
- After UI changes: build `phasefucker_snapshot`, render `<dir>` and read the PNGs (2x; `PFSNAP_SCALE=1` for 1x). `PFSNAP_PHASE=<deg>` sets the phase knob (per hit: the offset); `PFSNAP_SPLIT=0` runs host blocks through the loop's end; `longmap` is a 96-bar map. `phasefucker_snapshot <dir> gestures` drives the knob, readout and scope gestures through their handlers and exits 1 on a FAIL.
- Before a release: `auval -v aufx PhFk Jgru` and pluginval at strictness 5 on both formats.

### Common Patterns
- Namespaces: `pf` (core), `pf::dsp` (primitives), `theme` (UI tokens and helpers). Plugin/UI classes are global.
- JUCE-style formatting: space before call parens `foo (x)`, 4-space indent, braces on their own line.
- Knob gestures (the house rule): double-click or Alt/Cmd-click resets to the default; only a double-click on the number types a value (`Dial::mouseDoubleClick`: the number's hit box is its text width + 8 px, at least 24 px, on the value line). The phase dial hides its number, so a double-click on it only resets, and its big readout takes the typing double-click (`Sheet::mouseDoubleClick`, the field opens over the readout).
- Look: the SDK's night sheet: sheet `#131211`, plots in the well `#0C0B0A`, bone ink `#ECE7DB`, one accent orange `#FF8505` (the old vermilion's direction, lifted for the dark: 7.7:1 on the sheet, OKLab ΔE ≥ 0.2 from the sibling plugins' accents). Every colour is a role of `theme::palette` in `src/ui/Theme.h` (`sheet`, `well`, `raised`, `ink`…`accentWash`); never a hex value or `brighter()`/`darker()` at a call site. The light `paper` variant is kept there and swaps in with one line. No gradients, shadows or glow; corner radius 2 px; IBM Plex Sans Condensed labels, IBM Plex Mono numbers.

## Dependencies
- JUCE 8.0.15 (AGPLv3), Catch2 v3.7.1, C++20, CMake ≥ 3.24, macOS deployment target 11.0
- IBM Plex fonts (OFL), bundled in `resources/fonts`

## Manual Notes

Notes under this heading are written by humans and preserved on regeneration.
