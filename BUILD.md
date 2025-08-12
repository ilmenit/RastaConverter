# Building RastaConverter

RastaConverter builds on Windows, Linux and macOS using CMake. You can use either:
- CMake directly with Presets 
- The provided helper scripts: `build.bat` (Windows) and `build.sh` (Linux/macOS)

The project supports:
- GUI and No-GUI variants
- Debug and Release configurations
- Multiple compilers (MSVC, clang-cl, GCC/Clang)

## Prerequisites

### Windows
- Visual Studio 2022 (Desktop C++ workload) or Build Tools
- CMake 3.21+
- Ninja (recommended; used by presets)
- Dependencies: FreeImage; SDL2 + SDL2_ttf for GUI builds

### Linux
- GCC or Clang
- CMake 3.21+
- Ninja (recommended; used by presets)
- Dependencies: `freeimage`, `sdl2`, `sdl2-ttf` (package names vary by distro)

### macOS
- Xcode command line tools (Apple Clang)
- CMake 3.21+
- Ninja (recommended; used by presets)
- Dependencies via Homebrew: `freeimage`, `sdl2`, `sdl2_ttf`

## Quick start (Presets)

List available presets:

```
cmake --list-presets
```

On Windows you can choose between Visual Studio and Ninja:

```
# Visual Studio generator (no Ninja required)
cmake --preset win-msvc
cmake --build out/build/win-msvc --config Release

# Ninja Multi-Config (requires Ninja)
cmake --preset win-msvc-ninja
cmake --build out/build/win-msvc-ninja --config Release
```

Typical builds:

```
# Windows (clang-cl)
cmake --preset win-clangcl
cmake --build out/build/win-clangcl --config Release

# Linux (GCC)
cmake --preset linux-gcc
cmake --build out/build/linux-gcc --config Release

# macOS (Apple Clang)
cmake --preset macos-clang
cmake --build out/build/macos-clang --config Release

# No-GUI variants (presets with -nogui)
cmake --preset linux-gcc-nogui
cmake --build out/build/linux-gcc-nogui --config Release
```

Artifacts are placed in `out/build/<preset>/<config>/`.

## Quick start (helper scripts)

The scripts are thin wrappers around presets and accept extra `-D` options:

```
# Windows
build.bat win-clangcl Release -DTHREAD_DEBUG=ON -DUI_DEBUG=ON
build.bat win-msvc Release CLEAN   # removes old cache if generator mismatch

# Linux/macOS
./build.sh linux-gcc Release -DNO_GUI=ON
```

Run without arguments to see usage and the preset list.

## Passing compile-time options (logging, GUI)

You can toggle features at configure time with `-D...=ON/OFF` (works with both presets and scripts):

- `NO_GUI` (OFF by default): build console-only version
- `THREAD_DEBUG`: verbose optimization/control thread logs
- `UI_DEBUG`: SDL UI event and heartbeat logs
- `SUPPRESS_IMPROVEMENT_LOGS`: hides frequent “New best solution” logs
- `IGNORE_SDL_QUIT`: ignore SDL_QUIT and ESC (diagnose unexpected exits)

Examples:

```
cmake --preset linux-clang -DTHREAD_DEBUG=ON -DUI_DEBUG=ON
cmake --build out/build/linux-clang --config Debug

build.bat win-msvc Debug -DNO_GUI=ON
```

## Dependency management options

### Option A (recommended): vcpkg manifest mode

If you use vcpkg, set `VCPKG_ROOT` and pick a `*-vcpkg` preset. Dependencies are auto-installed in the correct ABI for your compiler.

```
cmake --preset win-msvc-vcpkg
cmake --build out/build/win-msvc-vcpkg --config Release
```

### Option B: System packages

Install dev packages via your package manager.

Debian/Ubuntu:
```bash
sudo apt update
sudo apt install build-essential cmake ninja-build libfreeimage-dev libsdl2-dev libsdl2-ttf-dev
```

Fedora/RHEL:
```bash
sudo dnf install gcc-c++ cmake ninja-build freeimage-devel SDL2-devel SDL2_ttf-devel
```

macOS (Homebrew):
```bash
brew install cmake ninja freeimage sdl2 sdl2_ttf
```

### Option C: Manual paths

If you have local installs, point CMake to them:

```
cmake -S . -B build \
  -DFREEIMAGE_DIR=/path/to/freeimage \
  -DSDL2_DIR=/path/to/sdl2 \
  -DSDL2_TTF_DIR=/path/to/sdl2_ttf
cmake --build build --config Release
```

## Choosing a compiler

- Windows:
  - `win-msvc`: MSVC (cl.exe)
  - `win-clangcl`: LLVM clang-cl with MSVC STL/CRT
  - `win-mingw-gcc`: MinGW-w64 GCC (ensure MinGW is on PATH or use MSYS2 MinGW shell)
- Linux: `linux-gcc`, `linux-clang`
- macOS: `macos-clang`

Tip: For performance experiments on CPU-heavy code, try `win-clangcl` (thin LTO) or run PGO on your critical workloads.

## Release optimizations

- IPO/LTO is enabled by default where supported. Disable with `-DENABLE_LTO=OFF` if needed.
- Fast-math for GCC/Clang is ON by default in Release/RelWithDebInfo. Disable with `-DENABLE_FAST_MATH=OFF`.
- Dead code/data elimination is ON by default: function/data sections + linker GC. Disable with `-DENABLE_DEAD_STRIP=OFF`.
- MSVC/clang-cl fast-math is OFF by default (use `-DENABLE_MSVC_FAST_MATH=ON` if acceptable for your workload).
- Optional AVX2 for MSVC/clang-cl: `-DENABLE_AVX2=ON` (ensure target CPUs support it).
- PGO workflow: `-DPGO_MODE=GENERATE` to record, run representative workloads, then `-DPGO_MODE=USE` (and `-DPGO_PROFILE_PATH=` for Clang).

### Fast and safe defaults

These are enabled by default and safe for correctness:
- LTO/IPO (where supported)
- Dead-stripping (function/data sections + linker GC)
- O2/O3 equivalents and inlining per-compiler

### Optional toggles (opt-in)

Use these to squeeze more performance when acceptable for your use-case:
- MSVC/clang-cl fast-math: `-DENABLE_MSVC_FAST_MATH=ON`
- AVX2 (Windows MSVC/clang-cl): `-DENABLE_AVX2=ON` (only if all target CPUs support AVX2)
- Unity build: `-DENABLE_UNITY_BUILD=ON` (can improve inlining and compile time)
- lld linker (Linux): `-DENABLE_LLD_LINKER=ON` (faster links)
- PGO: see workflow above

### Max performance recipes

- Windows (MSVC), fast-math + AVX2:
  ```
  build.bat win-msvc Release -DENABLE_MSVC_FAST_MATH=ON -DENABLE_AVX2=ON -DENABLE_UNITY_BUILD=ON
  ```

- Windows (clang-cl), ThinLTO + fast-math + AVX2:
  ```
  build.bat win-clangcl Release -DENABLE_MSVC_FAST_MATH=ON -DENABLE_AVX2=ON -DENABLE_UNITY_BUILD=ON
  ```

- Linux (Clang), ThinLTO + dead strip + lld:
  ```
  cmake --preset linux-clang -DENABLE_LLD_LINKER=ON -DENABLE_UNITY_BUILD=ON
  cmake --build out/build/linux-clang --config Release
  ```

- PGO (Linux GCC) example:
  ```
  cmake --preset linux-gcc -DPGO_MODE=GENERATE
  cmake --build out/build/linux-gcc --config Release
  # run representative workloads here to generate .gcda profiles
  cmake --preset linux-gcc -DPGO_MODE=USE
  cmake --build out/build/linux-gcc --config Release
  ```

## Troubleshooting

- List presets:
  ```
  cmake --list-presets
  ```
- CMake can’t find libraries:
  - Prefer vcpkg presets (set `VCPKG_ROOT`)
  - Or provide `FREEIMAGE_DIR`, `SDL2_DIR`, `SDL2_TTF_DIR`
- Windows runtime DLLs:
  - DLLs are copied to the output dir automatically when discoverable
- MinGW on Windows:
  - Run from the MSYS2 MinGW 64-bit shell or ensure `C:/msys64/mingw64/bin` is on PATH before configuring

## Where is the binary?

After a successful build, the binary is placed under:

```
out/build/<preset>/<config>/
```

Windows executable: `rasta.exe`

Linux/macOS executable: `rasta`
