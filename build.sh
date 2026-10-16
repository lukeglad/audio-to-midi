#!/bin/bash
# Configure and build the plugin.
#
#   ./build.sh            # Release: AU, VST3 and Standalone, left in build/
#                         # (to try them in a host: ./package.sh --skip-notarize)
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

# The preview's soundfont (external/GeneralUser-GS/README.md): fetched once
# into the shared AudioFlower folder when missing, pinned to a commit and
# checked against its digest. The installer puts it there for users.
SF_DIR="$HOME/Library/Application Support/AudioFlower/soundfonts"
SF_FILE="$SF_DIR/GeneralUser-GS.sf2"
SF_URL="https://github.com/mrbumpy409/GeneralUser-GS/raw/97049183643d5fc5a9322a69c5b09efb667c6c3a/GeneralUser-GS.sf2"
SF_SHA256="9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe"
if [ ! -f "$SF_FILE" ]; then
  echo "fetching GeneralUser GS (32 MB) into $SF_DIR..."
  mkdir -p "$SF_DIR"
  curl -fsSL -o "$SF_FILE.part" "$SF_URL"
  if [ "$(shasum -a 256 "$SF_FILE.part" | cut -d' ' -f1)" != "$SF_SHA256" ]; then
    echo "GeneralUser GS: digest mismatch, not installed" >&2
    rm -f "$SF_FILE.part"
    exit 1
  fi
  mv "$SF_FILE.part" "$SF_FILE"
fi

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
