#!/bin/bash
# Configure and build the plugin.
#
#   ./build.sh            # Release: AU, VST3 and Standalone; the plugins are
#                         # copied to ~/Library/Audio/Plug-Ins after building
#   ./build.sh Debug      # Debug build of the same
#   ./build.sh xcode      # generate an Xcode project in build-xcode/ instead
#
# CMake and Ninja come from .tools/, a project-local Python environment, so
# nothing has to be installed system-wide. It is created on first run.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

if [ ! -x .tools/bin/cmake ] || [ ! -x .tools/bin/ninja ]; then
  echo "setting up .tools/ (CMake + Ninja from PyPI)..."
  python3 -m venv .tools
  .tools/bin/pip install --quiet --upgrade pip
  .tools/bin/pip install --quiet cmake ninja
fi
export PATH="$PWD/.tools/bin:$PATH"

MODE="${1:-Release}"

if [ "$MODE" = "xcode" ]; then
  cmake -S . -B build-xcode -G Xcode
  echo "open build-xcode/AudioToMidi.xcodeproj"
  exit 0
fi

case "$MODE" in
  Release|Debug) ;;
  *) echo "usage: ./build.sh [Release|Debug|xcode]" >&2; exit 2 ;;
esac

BUILD_DIR="build/$MODE"
cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$MODE"
cmake --build "$BUILD_DIR"

echo
echo "built: $BUILD_DIR/AudioToMidi_artefacts/$MODE/"
