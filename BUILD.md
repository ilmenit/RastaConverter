RastaConverter – Build Guide
===========================

This guide explains how to build RastaConverter on Windows, macOS, and Linux using the provided scripts or CMake directly. It uses a system‑first dependency resolution with fast Release builds by default.

Live UI
-------
GUI builds include the Dear ImGui setup screen and dashboard by default:

```sh
cmake -S . -B build
cmake --build build --config Release
```

Launch the binary without arguments to open interactive setup. `/livegui` (or
`--livegui`) opens it when other arguments are present. Every run displays the
dashboard, including runs started entirely from the command line - `/livegui`
decides whether the setup screen appears first, not how a conversion is shown.

Pass `-DENABLE_LIVE_UI=OFF` (or `noliveui` to a build wrapper) to omit Dear
ImGui; those builds fall back to the pre-2025 display of three thumbnails with
statistics beneath them. Console-only builds disable the interface entirely and
do not fetch or compile GUI dependencies.

Which build do you want?
------------------------

| You want | Configure with | Needs |
|---|---|---|
| The normal program: setup screen, dashboard, editor | *(defaults)* | FreeImage, SDL3, SDL3_ttf; Dear ImGui is fetched automatically |
| A headless binary for scripts, servers or CI | `-DBUILD_NO_GUI=ON` | FreeImage only |
| The old three-thumbnail display, no Dear ImGui | `-DENABLE_LIVE_UI=OFF` | FreeImage, SDL3, SDL3_ttf |

If CMake is unavailable, `src/Makefile` builds the headless target directly
(`cd src && make`). It cannot build the interactive interface, which needs Dear
ImGui fetched at configure time - use CMake for that.

Requirements
------------
- CMake 3.21+
- A C++17 compiler
  - Windows: Visual Studio 2022 (MSVC) or Ninja + cl/clang-cl
  - macOS: Apple Clang (Xcode tools) and optionally Ninja
  - Linux: GCC or Clang and optionally Ninja/Make
- Optional: Ninja build system (for faster builds and PGO automation)
  - Windows: Download from https://github.com/ninja-build/ninja/releases
  - macOS: `brew install ninja`
  - Linux: `sudo apt install ninja-build` (Ubuntu) or `sudo dnf install ninja-build` (Fedora)

Dependencies
------------
- FreeImage
- SDL3
- SDL3_ttf
- Dear ImGui (Live UI only; fetched and built automatically by CMake, nothing to install)

The build prefers system-installed packages first (find_package). If configs are not present, it falls back to module variables and finally to manual header/library discovery using optional hints from `config.env`.

If SDL3 and/or SDL3_ttf still cannot be found after all of that (a common case: a distro ships an SDL3 package but not SDL3_ttf, or vice versa), the GUI build automatically fetches and builds the missing piece from source via CMake `FetchContent` — no manual install is required to get a working GUI build. The fetched SDL3_ttf build vendors its own FreeType (harfbuzz/plutosvg disabled, since RastaConverter only renders plain stats text), so it needs no extra system devel packages either. This only kicks in for whichever piece is actually missing — e.g. if SDL3 is found on the system but SDL3_ttf is not, only SDL3_ttf is fetched. Force a from-source build of either regardless of what's installed with `-DFETCH_SDL3=ON` / `-DFETCH_SDL3_TTF=ON` (useful if `find_package` picks up a broken or mismatched install). The console-only (`nogui`/`BUILD_NO_GUI=ON`) build needs neither SDL3 nor SDL3_ttf and never fetches anything.

Installing dependencies
-----------------------
- Ubuntu/Debian
  - `sudo apt install build-essential cmake ninja-build libfreeimage-dev libsdl3-dev libsdl3-ttf-dev`
  - Older releases have no SDL3 packages. Install FreeImage alone and let CMake
    fetch SDL3 and SDL3_ttf from source - the build does this automatically for
    whichever piece is missing.
- Fedora
  - `sudo dnf install gcc-c++ cmake ninja-build freeimage-devel SDL3-devel SDL3_ttf-devel`
- Arch
  - `sudo pacman -S freeimage sdl3 sdl3_ttf`
- macOS (Homebrew)
  - `brew install freeimage sdl3 sdl3_ttf`
- Windows
  - Recommended: vcpkg with the provided `vcpkg.json`
  - Or vendor SDKs for FreeImage/SDL3/SDL3_ttf and set `FREEIMAGE_DIR`, `SDL3_DIR`, `SDL3_TTF_DIR`

Optional: config.env
--------------------
Create a `config.env` at the project root to provide hints:
```
FREEIMAGE_DIR=d:/libs/FreeImage
SDL3_DIR=d:/libs/SDL3
SDL3_TTF_DIR=d:/libs/SDL3_ttf
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

# Console-only (no SDL3/GUI):
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

# Debug or console-only:
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
  - Add hints via `config.env` (FREEIMAGE_DIR, SDL3_DIR, SDL3_TTF_DIR)
  - Install packages (see above)
  - Use vcpkg toolchain with the provided `vcpkg.json`
  - Run `cmake -P check_dependencies.cmake` for a discovery report
- Cannot find SDL3 (Windows):
  - Ensure the SDL3 import library location is visible (for example `SDL3/lib/x64`).
- Cannot find SDL3_ttf on Linux/macOS (e.g. distro ships SDL3 but not SDL3_ttf devel packages):
  - No action needed — the GUI build now auto-fetches and builds SDL3_ttf from source the first time (adds a few minutes to a clean build; cached afterward). Pass `-DFETCH_SDL3_TTF=ON` to force this even if a system copy exists, or install the system package to skip the from-source build entirely.
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
- With `-DBUILD_NO_GUI=ON` (or `nogui` in wrappers), the build is console-only: target `RastaConverter-NO_GUI` is produced and SDL3/SDL3_ttf discovery/linking is skipped.
- Without NO_GUI the default GUI target `RastaConverter` is built and SDL3/SDL3_ttf are required.
- The wrappers keep GUI and console configurations in separate sibling trees.
  Production pairs are `build/linux-gcc{,-nogui}` on Linux,
  `build/macos-clang{,-nogui}` on macOS, and
  `build/x64-release{,-nogui}` on Windows. Platform-specific trees should be
  generated on their corresponding host rather than kept as empty placeholders.
- Clear build summaries are printed with dependency resolution info.

Generate Atari executables
--------------------------
After a conversion has produced the generator inputs, assemble the single- or
dual-frame XEX with the platform wrapper:

```bash
# macOS arm64 or Linux x86-64
Generator/build.sh
GeneratorDual/build.sh
```

On Windows, run `Generator\build.bat` or `GeneratorDual\build.bat`. The Unix
`mads` launchers select separate bundled macOS arm64 and Linux x86-64 binaries;
the batch files invoke the bundled 32-bit Windows `mads.exe` by absolute local
path. Unsupported host architectures fail explicitly. See
`Generator/README.md` for binary provenance and checksums.

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
- On Windows (Ninja), ensure required runtime DLLs are next to the executable. Prefer automatic copying with `-DCOPY_ALL_RUNTIME_DLLS=ON`. A repo-root `dlls/` directory can also stage host-validated current `FreeImage.dll`, `SDL3.dll`, and `SDL3_ttf.dll` files for post-build copying. The obsolete bundled DLLs were intentionally removed and must not be treated as available until replacements are validated on Windows.
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
- Phase 3 (optimized build) includes additional performance optimizations:
  - **LTO**: `-flto` (Ninja) or `-Qipo` (Visual Studio) for Link Time Optimization
  - **CPU Tuning**: `-xHost` (Ninja) or `-QxHost` (Visual Studio) for optimal CPU-specific optimizations
  - **Optimizer parameters**: history length (`/s`) is a search-quality parameter, not a compiler or thread-count scaling rule. Values must be selected from benchmark and visual-review evidence; the example run used for PGO training is not a quality recommendation.
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
The script automatically detects if Ninja is available in PATH:
- If Ninja is found: uses `ninja-pgo-icx-gen` and `ninja-pgo-icx-use` presets
- If Ninja is not found: falls back to `x64-pgo-icx-gen` and `x64-pgo-icx-use` presets (Visual Studio)

It will build instrumented, run multiple scenarios (500K-4M evaluations and LAHC solutions scaled by thread count, writing distinct .profraw files), merge to `pgo\icx\merged.profdata`, then build the optimized binary with LTO and CPU tuning enabled.

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
