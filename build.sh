#!/bin/bash

#!/bin/bash

# Script to build RastaConverter on POSIX systems via CMake presets
# Usage: ./build.sh <configure-preset> [Debug|Release] [extra -D options]

# Default values
PRESET=""
CONFIG="Release"
EXTRA_CMAKE_ARGS=()
SELECTED_PRESET=""
SINGLE_CONFIG_GEN=0

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

# Normalize CONFIG case to CMake conventions
case "${CONFIG,,}" in
  debug) CONFIG="Debug" ;;
  release) CONFIG="Release" ;;
  relwithdebinfo) CONFIG="RelWithDebInfo" ;;
  minsizerel) CONFIG="MinSizeRel" ;;
  *) ;; # leave as-is
esac

# Decide on fallback if Ninja isn't available and preset expects it
SELECTED_PRESET="$PRESET"
if ! command -v ninja >/dev/null 2>&1 && ! command -v ninja-build >/dev/null 2>&1; then
  case "$PRESET" in
    linux-gcc|linux-gcc-nogui)
      SELECTED_PRESET="${PRESET/linux-gcc/linux-gcc-make}" ;;
    linux-clang|linux-clang-nogui)
      SELECTED_PRESET="${PRESET/linux-clang/linux-clang-make}" ;;
    macos-clang|macos-clang-nogui)
      SELECTED_PRESET="${PRESET/macos-clang/macos-clang-make}" ;;
    *) ;;
  esac
fi

# Mark single-config generators to set CMAKE_BUILD_TYPE at configure time
case "$SELECTED_PRESET" in
  *-make|*-make-nogui)
    SINGLE_CONFIG_GEN=1 ;;
  win-mingw-gcc|win-mingw-gcc-nogui)
    SINGLE_CONFIG_GEN=1 ;;
  *)
    SINGLE_CONFIG_GEN=0 ;;
esac

echo "Configuring with CMake preset $SELECTED_PRESET ..."
if [ ${#EXTRA_CMAKE_ARGS[@]} -gt 0 ]; then
  echo "Extra CMake args: ${EXTRA_CMAKE_ARGS[*]}"
fi

CONFIGURE_ARGS=("--preset" "$SELECTED_PRESET")
if [ $SINGLE_CONFIG_GEN -eq 1 ]; then
  CONFIGURE_ARGS+=("-DCMAKE_BUILD_TYPE=$CONFIG")
fi
CONFIGURE_ARGS+=("${EXTRA_CMAKE_ARGS[@]}")

if ! cmake "${CONFIGURE_ARGS[@]}"; then
  echo "Configure failed"
  exit 1
fi

BINARY_DIR="out/build/$SELECTED_PRESET"
echo "Building in $BINARY_DIR (config $CONFIG) ..."
cmake --build "$BINARY_DIR" --config "$CONFIG" -j"$(command -v nproc >/dev/null 2>&1 && nproc || sysctl -n hw.ncpu)"

if [ $SINGLE_CONFIG_GEN -eq 1 ]; then
  echo "Build finished. Artifacts under out/build/$SELECTED_PRESET/"
else
  echo "Build finished. Artifacts under out/build/$SELECTED_PRESET/$CONFIG/"
fi
