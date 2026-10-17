#!/usr/bin/env bash
# Builds and starts the standalone app. Inputs 1-2 are the track, 3-4 the reference.
# Extra arguments go to the CMake configure step, e.g. ./start.sh -DFETCHCONTENT_SOURCE_DIR_JUCE=~/JUCE
set -euo pipefail
cd "$(dirname "$0")"

if [[ ! -f build/build.ninja || $# -gt 0 ]]; then
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "$@"
fi
cmake --build build --target PhaseFucker_Standalone

exec build/PhaseFucker_artefacts/Release/Standalone/PhaseFucker.app/Contents/MacOS/PhaseFucker
