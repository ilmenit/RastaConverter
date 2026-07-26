#!/usr/bin/env bash

set -euo pipefail

# RastaConverter cross-platform build wrapper (POSIX)
# Usage: ./build.sh [<preset>] [Debug|Release|RelWithDebInfo|MinSizeRel]
#                   [nogui] [liveui|noliveui] [clean|cleanonly] [extra -D options]

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$script_dir"

if [[ "${DEBUG_BUILD:-${debug_build:-0}}" == "1" ]]; then
  set -x
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "[error] CMake not found. Please install CMake >= 3.21." >&2
  exit 1
fi

preset=""
config="Release"
clean=0
cleanonly=0
build_no_gui=0
live_ui=1
extra=( )
compiler=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    Debug|debug) config="Debug" ; shift ;;
    Release|release) config="Release" ; shift ;;
    RelWithDebInfo|relwithdebinfo) config="RelWithDebInfo" ; shift ;;
    MinSizeRel|minsizerel) config="MinSizeRel" ; shift ;;
    nogui) build_no_gui=1 ; live_ui=0 ; shift ;;
    liveui) live_ui=1 ; shift ;;
    noliveui) live_ui=0 ; shift ;;
    clean) clean=1 ; shift ;;
    cleanonly) clean=1 ; cleanonly=1 ; shift ;;
    msvc|clang|clang-cl|gcc|mingw|icx) compiler="$1" ; shift ;;
    -D*|--fresh) extra+=("$1") ; shift ;;
    *)
      if [[ -z "$preset" ]]; then preset="$1"; else extra+=("$1"); fi
      shift ;;
  esac
done

# Auto-select a sensible default preset per OS if none provided
if [[ -z "$preset" ]]; then
  uname_s="$(uname -s 2>/dev/null || echo unknown)"
  case "$uname_s" in
    Linux)
      if command -v clang >/dev/null 2>&1; then preset="linux-clang"; else preset="linux-gcc"; fi ;;
    Darwin)
      preset="macos-clang" ;;
    *)
      echo "[error] Unknown platform; specify a preset (see: cmake --list-presets)." >&2
      exit 1 ;;
  esac
fi

# Normalize config casing (portable, works with older bash)
config_lower="$(printf '%s' "$config" | tr '[:upper:]' '[:lower:]')"
case "$config_lower" in
  debug) config=Debug ;;
  release) config=Release ;;
  relwithdebinfo) config=RelWithDebInfo ;;
  minsizerel) config=MinSizeRel ;;
esac

binary_dir="build/${preset}"
[[ $build_no_gui -eq 1 ]] && binary_dir="${binary_dir}-nogui"
cfg=("--preset" "$preset" "-B" "$binary_dir")
cfg+=("-DCMAKE_BUILD_TYPE=$config")
if [[ $build_no_gui -eq 1 ]]; then
  cfg+=("-DBUILD_NO_GUI=ON")
else
  cfg+=("-DBUILD_NO_GUI=OFF")
fi
if [[ $live_ui -eq 1 ]]; then
  cfg+=("-DENABLE_LIVE_UI=ON")
else
  cfg+=("-DENABLE_LIVE_UI=OFF")
fi

# Map compiler token to CMake CC/CXX
case "$compiler" in
  clang)
    cfg+=("-DCMAKE_C_COMPILER=clang" "-DCMAKE_CXX_COMPILER=clang++") ;;
  clang-cl)
    cfg+=("-DCMAKE_C_COMPILER=clang-cl" "-DCMAKE_CXX_COMPILER=clang-cl") ;;
  gcc|mingw)
    cfg+=("-DCMAKE_C_COMPILER=gcc" "-DCMAKE_CXX_COMPILER=g++") ;;
  icx)
    cfg+=("-DCMAKE_C_COMPILER=icx" "-DCMAKE_CXX_COMPILER=icx") ;;
esac

# If VCPKG_ROOT provided, add toolchain (optional, not default)
if [[ -n "${VCPKG_ROOT:-}" && -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]]; then
  cfg+=("-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" "-DVCPKG_FEATURE_FLAGS=manifests")
fi

if [[ ${#extra[@]} -gt 0 ]]; then
  cfg+=("${extra[@]}")
fi

if [[ $clean -eq 1 && -d "$binary_dir" ]]; then
  echo "[info] CLEAN: removing $binary_dir"
  rm -rf "$binary_dir"
fi

if [[ $cleanonly -eq 1 ]]; then
  echo "[success] CLEANONLY: $binary_dir has been removed."
  exit 0
fi

echo "[info] Configuring (preset=$preset, config=$config, nogui=$build_no_gui, liveui=$live_ui${compiler:+, compiler=$compiler}) ..."
if [[ -n "$compiler" ]]; then
    echo "[info] Compiler: $compiler"
else
    echo "[info] Compiler: auto-detected from preset"
fi
if [[ ${#extra[@]} -gt 0 ]]; then
    echo "[info] Extra CMake args: ${extra[*]}"
fi
set +e
cmake -S . "${cfg[@]}"
status=$?
set -e
if [[ $status -ne 0 ]]; then
  echo "[error] Configuration failed." >&2
  echo "[hint] SDL3/SDL3_ttf are auto-fetched from source if no usable system copy is" >&2
  echo "       found, so this is likely a different problem (e.g. FreeImage missing, or" >&2
  echo "       no network/git access for the SDL3/SDL3_ttf FetchContent fallback)." >&2
  echo "  Try one of the following:" >&2
  echo "  - Provide paths in config.env: FREEIMAGE_DIR, SDL3_DIR, SDL3_TTF_DIR" >&2
  echo "  - OR install system packages:" >&2
  echo "      Ubuntu:   install FreeImage, SDL3, and SDL3_ttf development packages" >&2
  echo "                sudo apt install ninja-build build-essential cmake" >&2
  echo "      macOS:    brew install freeimage sdl3 sdl3_ttf" >&2
  echo "      Windows:  use vcpkg or vendor SDKs" >&2
  echo "  - With vcpkg: set VCPKG_ROOT then pass toolchain, e.g.:" >&2
  echo "      cmake --preset $preset -DCMAKE_TOOLCHAIN_FILE=\"\${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake\"" >&2
  echo "  - You can run: cmake -P check_dependencies.cmake   to see discovery hints" >&2
  exit 1
fi

echo "[info] Detected compiler information:"
cmake -LA -N "$binary_dir" 2>/dev/null | grep -i "CMAKE_C_COMPILER\|CMAKE_CXX_COMPILER\|CMAKE_BUILD_TYPE\|ENABLE_" | head -10

echo "[info] Building ..."
cores=1
if command -v nproc >/dev/null 2>&1; then
  cores=$(nproc)
elif [[ "$(uname -s)" == "Darwin" ]]; then
  cores=$(sysctl -n hw.ncpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
fi
cmake --build "$binary_dir" --config "$config" --parallel "$cores"

if [[ -d "$binary_dir/$config" ]]; then
  echo "[success] Artifacts: $binary_dir/$config/"
else
  echo "[success] Artifacts: $binary_dir/"
fi
