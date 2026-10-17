#!/usr/bin/env bash
# Builds and opens the demo host: PhaseFucker in a window with audio playing
# through it, the reference on its sidechain, out of your speakers. No DAW.
#   ./start.sh                        built-in kick + rolling bass scenarios (pick one in the window)
#   ./start.sh ref.wav track.wav      your files: the reference, then the track to fix
# The built-in scenarios use real samples when demo-kit.txt lies next to this
# script (see demo-kit.example.txt; it stays out of git), or PF_DEMO_KIT names
# a kit file; otherwise a synthesized loop.
# CMake options for the first configure go in PF_CMAKE_ARGS, e.g.
#   PF_CMAKE_ARGS=-DFETCHCONTENT_SOURCE_DIR_JUCE=~/JUCE ./start.sh
set -euo pipefail

# File arguments are relative to where you are, not to the repo.
files=()
for f in "$@"; do
    [[ -f "$f" ]] || { echo "No such file: $f" >&2; exit 1; }
    files+=("$(cd "$(dirname "$f")" && pwd)/$(basename "$f")")
done

cd "$(dirname "$0")"
if [[ -z "${PF_DEMO_KIT:-}" && -f demo-kit.txt ]]; then
    export PF_DEMO_KIT="$PWD/demo-kit.txt"
fi
if [[ ! -f build/build.ninja ]]; then
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ${PF_CMAKE_ARGS:-}
fi
cmake --build build --target phasefucker_demo

exec "build/phasefucker_demo_artefacts/Release/PhaseFucker Demo.app/Contents/MacOS/PhaseFucker Demo" ${files[@]+"${files[@]}"}
