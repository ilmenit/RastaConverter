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
AUTO_VCPKG="${AUTO_VCPKG:-0}"
DISABLE_VCPKG="${DISABLE_VCPKG:-0}"
NONINTERACTIVE="${NONINTERACTIVE:-0}"

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

# Prepare for potential vcpkg fallback later (do not enable by default)
if [ -f "vcpkg.json" ] && [ "$DISABLE_VCPKG" != "1" ]; then
  # Preserve previous behavior: if VCPKG_ROOT is already set and valid, use it automatically
  if [ -n "${VCPKG_ROOT:-}" ] && [ -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]; then
    USE_VCPKG=1
  elif [ "$AUTO_VCPKG" = "1" ]; then
    # If AUTO_VCPKG=1, attempt to prepare VCPKG_ROOT now; otherwise defer until fallback
    if [ -z "${VCPKG_ROOT:-}" ]; then
      if [ -d ".vcpkg" ]; then
        export VCPKG_ROOT="$(pwd)/.vcpkg"
      else
        if command -v git >/dev/null 2>&1; then
          echo "Preparing local vcpkg under .vcpkg ..."
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
    fi
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

echo "Detecting CMake version ..."
cmake_ver_str="$(cmake --version | head -n1 | sed -E 's/[^0-9]*([0-9]+)\.([0-9]+).*/\1 \2/')"
cmake_major="${cmake_ver_str%% *}"
cmake_minor="${cmake_ver_str##* }"

supports_presets=0
if [ "$cmake_major" -gt 3 ] || { [ "$cmake_major" -eq 3 ] && [ "$cmake_minor" -ge 21 ]; }; then
  supports_presets=1
fi

run_configure_with_presets() {
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
    # macOS triplet selection when using vcpkg
    if [ "$(uname -s)" = "Darwin" ]; then
      arch="$(uname -m)"
      if [ "$arch" = "arm64" ]; then
        CONFIGURE_ARGS+=("-DVCPKG_TARGET_TRIPLET=arm64-osx")
      else
        CONFIGURE_ARGS+=("-DVCPKG_TARGET_TRIPLET=x64-osx")
      fi
    fi
    CONFIGURE_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" "-DVCPKG_FEATURE_FLAGS=manifests")
  fi
  CONFIGURE_ARGS+=("${EXTRA_CMAKE_ARGS[@]}")
  cmake "${CONFIGURE_ARGS[@]}"
  # Check for successful configure by verifying CMakeCache.txt exists
  [ -f "$BINARY_DIR/CMakeCache.txt" ]
}

# Map preset to generator for -S/-B fallback
map_preset_to_generator() {
  case "$SELECTED_PRESET" in
    linux-gcc|linux-gcc-nogui|linux-gcc-make|linux-gcc-make-nogui)
      GEN="Unix Makefiles" ;;
    linux-clang|linux-clang-nogui|linux-clang-make|linux-clang-make-nogui)
      GEN="Unix Makefiles" ;;
    macos-clang|macos-clang-nogui|macos-clang-make|macos-clang-make-nogui)
      GEN="Unix Makefiles" ;;
    *) GEN="Unix Makefiles" ;;
  esac
}

run_configure_with_sB() {
  map_preset_to_generator
  echo "Presets unsupported (CMake $cmake_major.$cmake_minor). Falling back to -S/-B with generator: $GEN"
  BINARY_DIR="out/build/$SELECTED_PRESET"
  CONFIGURE_ARGS=("-S" "." "-B" "$BINARY_DIR" "-G" "$GEN")
  if [ $SINGLE_CONFIG_GEN -eq 1 ]; then
    CONFIGURE_ARGS+=("-DCMAKE_BUILD_TYPE=$CONFIG")
  fi
  # Compiler hints from preset
  case "$SELECTED_PRESET" in
    linux-gcc* ) CONFIGURE_ARGS+=("-DCMAKE_C_COMPILER=gcc" "-DCMAKE_CXX_COMPILER=g++") ;;
    linux-clang* ) CONFIGURE_ARGS+=("-DCMAKE_C_COMPILER=clang" "-DCMAKE_CXX_COMPILER=clang++") ;;
    macos-clang* ) CONFIGURE_ARGS+=("-DCMAKE_C_COMPILER=clang" "-DCMAKE_CXX_COMPILER=clang++") ;;
  esac
  case "$SELECTED_PRESET" in
    *-nogui) CONFIGURE_ARGS+=("-DNO_GUI=ON") ;;
    *) : ;;
  esac
  if [ $USE_VCPKG -eq 1 ]; then
    export VCPKG_ROOT
    if [ "$(uname -s)" = "Darwin" ]; then
      arch="$(uname -m)"
      if [ "$arch" = "arm64" ]; then
        CONFIGURE_ARGS+=("-DVCPKG_TARGET_TRIPLET=arm64-osx")
      else
        CONFIGURE_ARGS+=("-DVCPKG_TARGET_TRIPLET=x64-osx")
      fi
    fi
    CONFIGURE_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" "-DVCPKG_FEATURE_FLAGS=manifests")
  fi
  CONFIGURE_ARGS+=("${EXTRA_CMAKE_ARGS[@]}")
  cmake "${CONFIGURE_ARGS[@]}"
  # Check for successful configure by verifying CMakeCache.txt exists
  [ -f "$BINARY_DIR/CMakeCache.txt" ]
}

BINARY_DIR="out/build/$SELECTED_PRESET"

if [ $supports_presets -eq 1 ]; then
  if ! run_configure_with_presets; then
    echo "Initial configure failed."
    configure_failed=1
  else
    configure_failed=0
  fi
else
  if ! run_configure_with_sB; then
    echo "Initial configure failed."
    configure_failed=1
  else
    configure_failed=0
  fi
fi

if [ $configure_failed -ne 0 ]; then
  if [ -f "vcpkg.json" ] && [ "$DISABLE_VCPKG" != "1" ]; then
    # Ask user for vcpkg fallback if interactive and not already using vcpkg
    if [ $USE_VCPKG -eq 0 ]; then
      if [ "$AUTO_VCPKG" = "1" ] || [ "$NONINTERACTIVE" = "1" ]; then
        try_vcpkg="y"
      else
        echo "Dependencies may be missing (FreeImage/SDL2/SDL2_ttf)."
        read -r -p "Attempt vcpkg fallback (local .vcpkg checkout)? [Y/n] " try_vcpkg
      fi
      case "${try_vcpkg,,}" in
        n|no) echo "Skipping vcpkg fallback. Aborting."
              exit 1 ;;
        *)
          # prepare VCPKG_ROOT if needed
          if [ -z "${VCPKG_ROOT:-}" ]; then
            if command -v git >/dev/null 2>&1; then
              echo "Bootstrapping local vcpkg under .vcpkg ..."
              git clone --depth 1 https://github.com/microsoft/vcpkg.git .vcpkg || true
              if [ -x ./.vcpkg/bootstrap-vcpkg.sh ]; then
                (cd .vcpkg && ./bootstrap-vcpkg.sh -disableMetrics)
                export VCPKG_ROOT="$(pwd)/.vcpkg"
              fi
            fi
          fi
          if [ -z "${VCPKG_ROOT:-}" ]; then
            echo "Failed to prepare vcpkg. Please set VCPKG_ROOT or install deps manually."
            exit 1
          fi
          USE_VCPKG=1
          echo "Re-configuring with vcpkg toolchain ..."
          if [ $supports_presets -eq 1 ]; then
            run_configure_with_presets || { echo "Configure with vcpkg failed."; exit 1; }
          else
            run_configure_with_sB || { echo "Configure with vcpkg failed."; exit 1; }
          fi
          ;;
      esac
    else
      echo "Configure failed even with vcpkg. Please inspect errors above."
      exit 1
    fi
  else
    echo "Configure failed and vcpkg fallback is disabled or manifest missing."
    echo "Hints: install system packages (Ubuntu: libfreeimage-dev libsdl2-dev libsdl2-ttf-dev; macOS: brew install freeimage sdl2 sdl2_ttf) or pass -D*_DIR paths."
    exit 1
  fi
fi

echo "Building in $BINARY_DIR (config $CONFIG) ..."
cmake --build "$BINARY_DIR" --config "$CONFIG" --parallel "$(command -v nproc >/dev/null 2>&1 && nproc || sysctl -n hw.ncpu)"

if [ $SINGLE_CONFIG_GEN -eq 1 ]; then
  echo "Build finished. Artifacts under out/build/$SELECTED_PRESET/"
else
  echo "Build finished. Artifacts under out/build/$SELECTED_PRESET/$CONFIG/"
fi
