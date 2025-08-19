RastaConverter – Build Guide
===========================

This guide explains how to build RastaConverter on Windows, macOS, and Linux using the provided scripts or CMake directly. It uses a system‑first dependency resolution with fast Release builds by default.

Requirements
------------
- CMake 3.21+
- A C++17 compiler
  - Windows: Visual Studio 2022 (MSVC) or Ninja + cl/clang-cl
  - macOS: Apple Clang (Xcode tools) and optionally Ninja
  - Linux: GCC or Clang and optionally Ninja/Make

Dependencies
------------
- FreeImage
- SDL2
- SDL2_ttf

The build prefers system-installed packages first (find_package). If configs are not present, it falls back to module variables and finally to manual header/library discovery using optional hints from `config.env`.

Installing dependencies
-----------------------
- Ubuntu/Debian
  - `sudo apt install libfreeimage-dev libsdl2-dev libsdl2-ttf-dev`
- Fedora
  - `sudo dnf install FreeImage-devel SDL2-devel SDL2_ttf-devel`
- Arch
  - `sudo pacman -S freeimage sdl2 sdl2_ttf`
- macOS (Homebrew)
  - `brew install freeimage sdl2 sdl2_ttf`
- Windows
  - Recommended: vcpkg with the provided `vcpkg.json`
  - Or vendor SDKs for FreeImage/SDL2/SDL2_ttf and set `FREEIMAGE_DIR`, `SDL2_DIR`, `SDL2_TTF_DIR`

Optional: config.env
--------------------
Create a `config.env` at the project root to provide hints:
```
FREEIMAGE_DIR=d:/libs/FreeImage
SDL2_DIR=d:/libs/SDL2
SDL2_TTF_DIR=d:/libs/SDL2_ttf
```
These values are added to `CMAKE_PREFIX_PATH` and used as fallbacks for manual header/library discovery.

Optional: vcpkg
----------------
A manifest `vcpkg.json` is provided. If you set `VCPKG_ROOT`, wrappers will use the toolchain automatically:
```
cmake --preset x64-release -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake"
```
On macOS/Linux, replace `%VCPKG_ROOT%` with `$VCPKG_ROOT`.

Build with scripts
------------------

Windows (cmd/PowerShell)
```
# GUI Release (default):
build.bat

# Debug:
build.bat debug

# Add console version too:
build.bat nogui

# Clean or clean only:
build.bat CLEAN
build.bat CLEANONLY

# Extra CMake options:
build.bat -DENABLE_UNITY_BUILD=ON -DCOPY_ALL_RUNTIME_DLLS=ON

# Verbose script debug:
set debug_build=1
build.bat

# Optional compiler selection tokens (Windows):
#   msvc | clang | clang-cl | gcc (mingw) | icx
# The script will auto-use Ninja preset for non-MSVC compilers.

# Intel oneAPI icx:
build.bat release x64 icx

# LLVM clang-cl:
build.bat release x64 clang-cl

# GNU clang driver:
build.bat debug x64 clang

# MinGW-w64 gcc (ensure MinGW toolchain and deps are on PATH):
build.bat release x64 gcc
```

macOS/Linux (bash)
```
# Auto-select preset per OS/compiler:
./build.sh

# Debug or add console:
./build.sh '' Debug
./build.sh '' Release nogui

# Clean/CleanOnly:
./build.sh macos-clang Release clean
./build.sh linux-gcc Release cleanonly

# Extra options:
./build.sh linux-clang Release -DENABLE_UNITY_BUILD=ON

# Verbose:
DEBUG_BUILD=1 ./build.sh

# Optional compiler tokens (portable): clang | clang-cl | gcc | mingw | icx
# These add -DCMAKE_{C,CXX}_COMPILER=... to the configure call.

# Intel oneAPI icx example with Ninja preset:
./build.sh linux-clang Release icx

# GCC example:
./build.sh linux-gcc Release gcc
```

PowerShell cross-platform
```
./build.ps1 -Preset x64-release -Config Release -NoGui -Extra -DENABLE_UNITY_BUILD=ON

# Optional: specify compiler (clang | clang-cl | gcc | mingw | icx)
./build.ps1 -Preset ninja-release -Config Release -Compiler icx
```

Build with CMake directly
-------------------------
```
cmake --preset x64-release
cmake --build build/x64-release --config Release
```
Or without presets on Linux/macOS:
```
cmake -S . -B build/linux-clang -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/linux-clang --config Release
```

Release performance options
---------------------------
- Enabled by default (Release/RelWithDebInfo):
  - MSVC: /O2, /Gy,/Gw, /fp:fast (via `ENABLE_MSVC_FAST_MATH=ON`), LTO (`ENABLE_LTO=ON`), AVX2 (`ENABLE_AVX2=ON`)
  - Clang/GCC: -O3 -march=native, `-ffast-math` (`ENABLE_FAST_MATH=ON`), dead-stripping, ThinLTO on Clang when `ENABLE_LTO=ON`
- Legacy Release presets (no AVX2, precise math):
  - `x64-release-legacy`, `ninja-release-legacy`

Troubleshooting tips
--------------------
- Configuration failed:
  - Add hints via `config.env` (FREEIMAGE_DIR, SDL2_DIR, SDL2_TTF_DIR)
  - Install packages (see above)
  - Use vcpkg toolchain with the provided `vcpkg.json`
  - Run `cmake -P check_dependencies.cmake` for a discovery report
- Cannot find SDL2main (Windows):
  - It’s optional; ensure SDL2 import library location is visible (e.g., SDL2/lib/x64)
- AVX2 unsupported on target CPU:
  - Use `*-release-legacy` presets or pass `-DENABLE_AVX2=OFF`
- LTO issues with specific toolchains:
  - Pass `-DENABLE_LTO=OFF`

Install artifacts
-----------------
```
cmake --install build/<preset> --config Release
```
Installs the binary in `bin/` and resources/docs in `share/`.

Notes
-----
- GUI target is always built; the console target `RastaConverter-NO_GUI` is added when `-DBUILD_NO_GUI=ON`.
- Clear build summaries are printed with dependency resolution info.


