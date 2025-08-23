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

Profile-guided optimization (PGO)
---------------------------------
Two minimal, non-intrusive flows are supported:

- Preset-based (recommended, no command flags to remember)
- Ad‑hoc via `-Extra` flags (no repo changes required)

PGO for MSVC (Windows)
======================

Using presets (recommended)
```
# One-time: create profile dir
# PowerShell
New-Item -ItemType Directory -Force pgo\msvc | Out-Null
# cmd.exe
if not exist pgo\msvc mkdir pgo\msvc

# Phase 1 – Generate profile (instrumented build)
cmake --preset x64-pgo-msvc-gen
cmake --build build/x64-pgo-msvc-gen --config Release

# Run representative scenarios to produce .pgc files
# (Use the GUI or the console target if built with -DBUILD_NO_GUI=ON.)
build/x64-pgo-msvc-gen/Release/RastaConverter.exe

# Optional: split profiles between scenarios
# pgosweep

# Phase 3 – Use profile (optimized build)
cmake --preset x64-pgo-msvc-use
cmake --build build/x64-pgo-msvc-use --config Release
```

Details:
- The `.pgd` file lives at `pgo/msvc/Rasta.pgd` (under the repo root).
- Presets set `/GL` for compilation and `/LTCG /GENPROFILE` (phase 1) or `/LTCG /USEPROFILE` (phase 3) at link.
- You can re-run the instrumented executable to collect more `.pgc` files and rebuild the "use" preset again.
- Alternative with wrapper (PowerShell):
```
./build.ps1 -Preset x64-pgo-msvc-gen -Config Release
build/x64-pgo-msvc-gen/Release/RastaConverter.exe
./build.ps1 -Preset x64-pgo-msvc-use -Config Release
```

- Alternative with batch (cmd.exe):
```
rem One-time: create profile dir
if not exist pgo\msvc mkdir pgo\msvc

rem Phase 1 – Generate profile
build.bat release x64 msvc "-DCMAKE_C_FLAGS_RELEASE=/GL" "-DCMAKE_CXX_FLAGS_RELEASE=/GL" "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/LTCG /GENPROFILE PGD=%CD%\pgo\msvc\Rasta.pgd"

rem Run representative scenarios (.pgc files will be created)
build\x64-release\Release\RastaConverter.exe

rem Phase 3 – Use profile
build.bat release x64 msvc "-DCMAKE_C_FLAGS_RELEASE=/GL" "-DCMAKE_CXX_FLAGS_RELEASE=/GL" "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/LTCG /USEPROFILE PGD=%CD%\pgo\msvc\Rasta.pgd"
```

Ad‑hoc flags (no presets)
```
# Generate profile
./build.ps1 -Preset x64-release -Config Release -Compiler msvc -Extra \
  '-DCMAKE_C_FLAGS_RELEASE=/GL' \
  '-DCMAKE_CXX_FLAGS_RELEASE=/GL' \
  '-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/LTCG /GENPROFILE:PGD=${PWD}/pgo/msvc/Rasta.pgd'

# Run scenarios to produce .pgc files
build/x64-release/Release/RastaConverter.exe

# Use profile
./build.ps1 -Preset x64-release -Config Release -Compiler msvc -Extra \
  '-DCMAKE_C_FLAGS_RELEASE=/GL' \
  '-DCMAKE_CXX_FLAGS_RELEASE=/GL' \
  '-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/LTCG /USEPROFILE:PGD=${PWD}/pgo/msvc/Rasta.pgd'
```

Tips for MSVC:
- Use `pgosweep` to end a scenario and start a new `.pgc` during the same process, or `PgoAutoSweep` in code.
- Keep compiler and sources the same between phases.

PGO for Intel oneAPI icx (Windows) – LLVM-style (recommended)
=============================================================

Prerequisites
- Run from an Intel oneAPI Developer Command Prompt (so that `icx` and `llvm-profdata` are on PATH), or run the oneAPI environment script first:
  - `"C:\Program Files (x86)\Intel\oneAPI\setvars.bat" intel64`
- On Windows (Ninja), ensure required runtime DLLs are next to the executable. The build can copy them automatically if you pass `-DCOPY_ALL_RUNTIME_DLLS=ON` or if you maintain a `dlls/` directory at the repo root containing `FreeImage.dll`, `SDL2.dll`, `SDL2_ttf.dll` (copied post-build).
- If you keep a `test.jpg` in the repo root, it will be copied to the run directory as well.

Using presets (recommended)
```
# One-time: create profile dir
# PowerShell
New-Item -ItemType Directory -Force pgo\icx | Out-Null
# cmd.exe
if not exist pgo\icx mkdir pgo\icx

# Phase 1 – Generate profile (instrumented build)
cmake --preset ninja-pgo-icx-gen
cmake --build build/ninja-pgo-icx-gen --config Release

# Run representative scenarios to produce .profraw files (note Release subdir)
set LLVM_PROFILE_FILE=pgo\icx\rasta-%p.profraw  &  build/ninja-pgo-icx-gen/Release/RastaConverter.exe

# Merge raw profiles into a single .profdata (requires llvm-profdata in PATH; installed with oneAPI LLVM tools)
llvm-profdata merge -output=pgo/icx/merged.profdata pgo/icx/*.profraw

# Phase 3 – Use profile (optimized build)
cmake --preset ninja-pgo-icx-use
cmake --build build/ninja-pgo-icx-use --config Release
```

Details:
- Phase 1 uses `-fprofile-generate` to emit `.profraw` files at runtime; `LLVM_PROFILE_FILE` controls naming/location.
- The `merged.profdata` is consumed by `-fprofile-use=<path>` in the "use" preset.
- Alternative with wrapper (PowerShell):
```
./build.ps1 -Preset ninja-pgo-icx-gen -Config Release -Extra -DCOPY_ALL_RUNTIME_DLLS=ON
$env:LLVM_PROFILE_FILE = "pgo/icx/rasta-%p.profraw"
build/ninja-pgo-icx-gen/Release/RastaConverter.exe
llvm-profdata merge -output=pgo/icx/merged.profdata pgo/icx/*.profraw
./build.ps1 -Preset ninja-pgo-icx-use -Config Release
```

- Alternative with batch (cmd.exe):
```
if not exist pgo\icx mkdir pgo\icx

rem Phase 1 – Generate profile (Ninja preset auto-selected for non-MSVC compilers)
build.bat release x64 icx "-DCMAKE_C_FLAGS_RELEASE=-fprofile-instr-generate" "-DCMAKE_CXX_FLAGS_RELEASE=-fprofile-instr-generate" "-DCOPY_ALL_RUNTIME_DLLS=ON"

rem Run representative scenarios (.profraw files will be created)
set LLVM_PROFILE_FILE=%CD%\pgo\icx\rasta-%%p.profraw & build\ninja-release\Release\RastaConverter.exe

rem Merge profiles (ensure llvm-profdata is on PATH)
llvm-profdata merge -output=pgo/icx/merged.profdata pgo/icx/*.profraw

rem Phase 3 – Use profile
build.bat release x64 icx "-DCMAKE_C_FLAGS_RELEASE=-fprofile-instr-use=%CD%\pgo\icx\merged.profdata" "-DCMAKE_CXX_FLAGS_RELEASE=-fprofile-instr-use=%CD%\pgo\icx\merged.profdata"
```

Fully automated script (Windows)
--------------------------------
Use the provided automation to run the entire flow:
```
# From an Intel oneAPI Developer Command Prompt
build-pgo.bat                 # uses test.jpg from repo root by default
build-pgo.bat examples\test.jpg  # or specify a custom input image
```
It will build instrumented, run multiple scenarios (writing distinct .profraw files), merge to `pgo\icx\merged.profdata`, then build the optimized binary.

Ad‑hoc flags (no presets)
```
# Generate profile (configure with -fprofile-instr-generate)
./build.ps1 -Preset ninja-release -Config Release -Compiler icx -Extra \
  '-DCMAKE_C_FLAGS_RELEASE=-fprofile-instr-generate' \
  '-DCMAKE_CXX_FLAGS_RELEASE=-fprofile-instr-generate'

# Run scenarios (set LLVM_PROFILE_FILE to control .profraw location)
$env:LLVM_PROFILE_FILE = "pgo/icx/rasta-%p.profraw"
build/ninja-release/Release/RastaConverter.exe

# Merge
llvm-profdata merge -output=pgo/icx/merged.profdata pgo/icx/*.profraw

# Use profile (configure with -fprofile-instr-use)
./build.ps1 -Preset ninja-release -Config Release -Compiler icx -Extra \
  '-DCMAKE_C_FLAGS_RELEASE=-fprofile-instr-use=${PWD}/pgo/icx/merged.profdata' \
  '-DCMAKE_CXX_FLAGS_RELEASE=-fprofile-instr-use=${PWD}/pgo/icx/merged.profdata'
```

General guidance
- Prefer training the console binary for automation: add `-Extra -DBUILD_NO_GUI=ON` at configure time, then run `RastaConverter-NO_GUI` with realistic CLI options.
- Store profiles under `pgo/` in the repo root for easy cleanup and repeatability.
- Do not mix compilers or change major compile options between phases.


