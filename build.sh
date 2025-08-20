#!/usr/bin/env bash

set -euo pipefail

# RastaConverter cross-platform build wrapper (POSIX)
# Usage: ./build.sh [<preset>] [Debug|Release|RelWithDebInfo|MinSizeRel] [nogui] [clean|cleanonly] [extra -D options]

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
extra=( )
compiler=""

if [[ $# -gt 0 ]]; then preset="$1"; shift; fi
if [[ $# -gt 0 ]]; then config="$1"; shift; fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    nogui) build_no_gui=1 ; shift ;;
    clean) clean=1 ; shift ;;
    cleanonly) clean=1 ; cleanonly=1 ; shift ;;
    msvc|clang|clang-cl|gcc|mingw|icx) compiler="$1" ; shift ;;
    *) extra+=("$1") ; shift ;;
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
cfg=("--preset" "$preset")
[[ $build_no_gui -eq 1 ]] && cfg+=("-DBUILD_NO_GUI=ON")

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

echo "[info] Configuring (preset=$preset, config=$config, nogui=$build_no_gui${compiler:+, compiler=$compiler}) ..."
set +e
cmake -S . "${cfg[@]}"
status=$?
set -e
if [[ $status -ne 0 ]]; then
  echo "[error] Configuration failed." >&2
  echo "[hint] Try one of the following:" >&2
  echo "  - Provide paths in config.env: FREEIMAGE_DIR, SDL2_DIR, SDL2_TTF_DIR" >&2
  echo "  - OR install system packages:" >&2
  echo "      Ubuntu:   sudo apt install libfreeimage-dev libsdl2-dev libsdl2-ttf-dev" >&2
  echo "                sudo apt install ninja-build build-essential cmake" >&2
  echo "      macOS:    brew install freeimage sdl2 sdl2_ttf" >&2
  echo "      Windows:  use vcpkg or vendor SDKs" >&2
  echo "  - With vcpkg: set VCPKG_ROOT then pass toolchain, e.g.:" >&2
  echo "      cmake --preset $preset -DCMAKE_TOOLCHAIN_FILE=\"$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake\"" >&2
  echo "  - You can run: cmake -P check_dependencies.cmake   to see discovery hints" >&2
  exit 1
fi

if [[ $cleanonly -eq 1 ]]; then
  echo "[info] CLEANONLY requested, exiting after configure."
  exit 0
fi

echo "[info] Building ..."
cores=1
if command -v nproc >/dev/null 2>&1; then
  cores=$(nproc)
elif [[ "$(uname -s)" == "Darwin" ]]; then
  cores=$(sysctl -n hw.ncpu)
fi
cmake --build "$binary_dir" --config "$config" --parallel "$cores"

if [[ -d "$binary_dir/$config" ]]; then
  echo "[success] Artifacts: $binary_dir/$config/"
else
  echo "[success] Artifacts: $binary_dir/"
fi


