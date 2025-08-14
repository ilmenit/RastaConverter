#!/bin/bash

#!/bin/bash

# Script to build RastaConverter on POSIX systems via CMake presets
# Usage: ./build.sh [<configure-preset>] [Debug|Release] [extra -D options]

# Default values
PRESET=""
CONFIG="Release"
EXTRA_CMAKE_ARGS=()
SELECTED_PRESET=""
SINGLE_CONFIG_GEN=0
USE_VCPKG=0

if [ -n "${1:-}" ]; then PRESET="$1"; shift; fi
if [ -n "${1:-}" ]; then CONFIG="$1"; shift; fi
if [ $# -gt 0 ]; then EXTRA_CMAKE_ARGS=("$@"); fi

command -v cmake >/dev/null 2>&1 || { echo "CMake is required but not installed. Aborting."; exit 1; }

# Auto-select a sensible default preset if none provided
if [ -z "$PRESET" ]; then
  uname_s="$(uname -s 2>/dev/null || echo unknown)"
  case "$uname_s" in
    Linux)
      if command -v clang >/dev/null 2>&1; then
        PRESET="linux-clang"
      else
        PRESET="linux-gcc"
      fi
      ;;
    Darwin)
      PRESET="macos-clang"
      ;;
    *)
      echo "Unknown platform for auto-preset; please pass a preset."
      echo "Available presets:"
      cmake --list-presets
      exit 1
      ;;
  esac
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

# Auto-enable vcpkg manifest mode if vcpkg.json is present and not disabled
if [ -f "vcpkg.json" ] && [ "${DISABLE_VCPKG:-0}" != "1" ]; then
  # Ensure VCPKG_ROOT is set; attempt local bootstrap if missing
  if [ -z "${VCPKG_ROOT:-}" ]; then
    if [ -d ".vcpkg" ]; then
      export VCPKG_ROOT="$(pwd)/.vcpkg"
    else
      if command -v git >/dev/null 2>&1; then
        echo "Bootstrapping local vcpkg under .vcpkg ..."
        git clone --depth 1 https://github.com/microsoft/vcpkg.git .vcpkg || true
        if [ -x ./.vcpkg/bootstrap-vcpkg.sh ]; then
          (cd .vcpkg && ./bootstrap-vcpkg.sh -disableMetrics)
          export VCPKG_ROOT="$(pwd)/.vcpkg"
        fi
      fi
    fi
  fi
  if [ -n "${VCPKG_ROOT:-}" ] && [ -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]; then
    USE_VCPKG=1
  else
    echo "vcpkg manifest detected but VCPKG_ROOT not available; proceeding without vcpkg. Set DISABLE_VCPKG=1 to silence this."
  fi
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
if [ $USE_VCPKG -eq 1 ]; then
  export VCPKG_ROOT
  CONFIGURE_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" "-DVCPKG_FEATURE_FLAGS=manifests")
fi
CONFIGURE_ARGS+=("${EXTRA_CMAKE_ARGS[@]}")

if ! cmake "${CONFIGURE_ARGS[@]}"; then
  echo "Configure failed"
  exit 1
fi

BINARY_DIR="out/build/$SELECTED_PRESET"
echo "Building in $BINARY_DIR (config $CONFIG) ..."
cmake --build "$BINARY_DIR" --config "$CONFIG" --parallel "$(command -v nproc >/dev/null 2>&1 && nproc || sysctl -n hw.ncpu)"

if [ $SINGLE_CONFIG_GEN -eq 1 ]; then
  echo "Build finished. Artifacts under out/build/$SELECTED_PRESET/"
else
  echo "Build finished. Artifacts under out/build/$SELECTED_PRESET/$CONFIG/"
fi
