#!/bin/bash

#!/bin/bash

# Script to build RastaConverter on POSIX systems via CMake presets
# Usage: ./build.sh <configure-preset> [Debug|Release] [extra -D options]

# Default values
PRESET=""
CONFIG="Release"
EXTRA_CMAKE_ARGS=()

if [ -n "${1:-}" ]; then PRESET="$1"; shift; fi
if [ -n "${1:-}" ]; then CONFIG="$1"; shift; fi
if [ $# -gt 0 ]; then EXTRA_CMAKE_ARGS=("$@"); fi

command -v cmake >/dev/null 2>&1 || { echo "CMake is required but not installed. Aborting."; exit 1; }

if [ -z "$PRESET" ]; then
  echo "Usage: ./build.sh <configure-preset> [Debug|Release] [extra -D options]"
  echo "Examples:"
  echo "  ./build.sh linux-clang Release -DTHREAD_DEBUG=ON -DUI_DEBUG=ON"
  echo "  ./build.sh macos-clang Debug -DNO_GUI=ON"
  echo
  echo "Available presets:"
  cmake --list-presets
  exit 1
fi

echo "Configuring with CMake preset $PRESET ..."
if [ ${#EXTRA_CMAKE_ARGS[@]} -gt 0 ]; then
  echo "Extra CMake args: ${EXTRA_CMAKE_ARGS[*]}"
fi
cmake --preset "$PRESET" "${EXTRA_CMAKE_ARGS[@]}" || { echo "Configure failed"; exit 1; }

BINARY_DIR="out/build/$PRESET"
echo "Building in $BINARY_DIR (config $CONFIG) ..."
cmake --build "$BINARY_DIR" --config "$CONFIG" -j"$(command -v nproc >/dev/null 2>&1 && nproc || sysctl -n hw.ncpu)"

echo "Build finished. Artifacts under out/build/$PRESET/$CONFIG/"
