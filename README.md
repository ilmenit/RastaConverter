Rasta Converter
===============

RastaConverter is a graphics converter from modern computers to old [8bit Atari computers](http://en.wikipedia.org/wiki/Atari_8-bit_family).
The tool uses [SDL3](https://www.libsdl.org/) and [FreeImage](http://freeimage.sourceforge.net/) graphics libraries.

GUI builds open an interactive setup screen and live dashboard: launch without
arguments, or pass `/livegui` alongside other options. See
[Interactive Live UI](#interactive-live-ui) below. Pass `-DENABLE_LIVE_UI=OFF`
to build only the traditional SDL interface; `NO_GUI` builds remain independent
of Dear ImGui. See `BUILD.md`.

Quick start
-----------

1. Build it (see [Building RastaConverter](#building-rastaconverter)) or unpack a release.
2. Run the program with no arguments. The setup screen opens.
3. Drop a picture onto the window, press **Convert**, and watch the dashboard.
4. Press **Stop and save** when it looks good. Everything lands in a folder next
   to your picture, and the **XEX** button in *Recent* turns it into an Atari
   executable.

From the command line, the equivalent is:

```sh
RastaConverter photo.jpg /h=240 /objective=source /threads=8
```

The conversion process is optimization of the [Kernel Program](http://www.atariarchives.org/dere/chapt05.php#H5_7).
It uses most of the Atari graphics capabilities including sprites, midline color changes and sprite multiplication. 

Key capabilities
----------------
- Extremely optimized emulator of subset of [6502 CPU](https://en.wikipedia.org/wiki/MOS_Technology_6502) and [ANTIC](https://en.wikipedia.org/wiki/ANTIC) to simulate execution on real machine.
- Optimization: Late Acceptance Hill Climbing (LAHC) and Diversified Late Acceptance Search (DLAS), with support for reproducible runs, evaluation limits, auto-save and resume. Single-frame checkpoints reconstruct exact scores; dual checkpoints reconstruct the saved A/B programs and establish a fresh conditional baseline.
- Dithering: chess, Floyd–Steinberg, line, line2, 2D, Jarvis, simple, and Knoll; tunable strength and randomness. The legacy `rfloyd` option currently selects a per-pixel quantization path rather than randomized Floyd–Steinberg and ignores the strength/randomness controls.
- Colour distance: Rasta, YUV, RGB Euclidean, CIEDE2000, CIE94 and OKLab, chosen independently for building the target picture (`/predistance`, default CIEDE2000) and for scoring candidates during the search (`/distance`, default Rasta).
- Scoring reference: score against the quantized target picture or against the original source (`/objective=target|source`); the source objective costs no more per evaluation and usually lands closer to the original.
- Dual-frame mode: two alternating frames (A/B) with YUV or RGB blending, optional temporal luma/chroma penalties to reduce flicker, and export of both per-frame and blended outputs.
- Performance: multi-threaded execution with per-thread line caches and configurable cache size.
- Image pipeline: resize filters (box, bilinear, bicubic, bspline, Catmull–Rom, Lanczos3) plus brightness, contrast, gamma, saturation and vibrance adjustments.
- Hardware control: fine-grained control over Atari registers, including enabling/disabling hardware sprites (players/missiles) per scanline.
- Graphics modes: the established ANTIC E bitmap output remains the default;
  experimental ANTIC 4 character-mode output adds a cell-selectable fifth
  playfield colour and complete screen/font/XEX export.
- Details mask: provide a mask image to emphasize selected regions and bring out fine details in the result. Legacy, normalized and refined weighting modes, with strength reaching the classic 4x and 16x emphasis, and an overlay in the GUI showing the effective weight map.
- Interfaces: an interactive setup screen and live dashboard for every run, and a headless console mode for scripting. (The pre-2025 three-thumbnail display remains only in `-DENABLE_LIVE_UI=OFF` builds.)
- Palette selection: choose target palette files via Adobe ACT to match different monitors and CRT settings.
- Cross-platform: CMake-based builds for Windows, macOS and Linux, with scripted Profile Guided Optimization.
- Live editing: pause a run to paint the details mask or repaint the destination picture with the hardware palette, then resume - the search picks up from the edited target without restarting.
- Per-run output: each conversion writes into its own `rc-<image>-NNN` folder by default (`/subfolder=off` to write beside the source image), and the Recent browser can assemble any run into an Atari executable with the bundled MADS.
- Interruptible: Ctrl+C, a kill or a closing terminal stop a run the way the Stop button does - it saves and exits rather than losing the work since the last autosave.
- Extras: scripts and generators to assemble Atari executables.

The converter uses Late Acceptance Hill Climbing (LAHC) and [Diversified Late Acceptance Search](https://doi.org/10.1007/978-3-030-03991-2_29).

ANTIC 4 character mode
----------------------

Choose **ANTIC 4 (text mode)** under *Source and destination* in the Live UI,
or pass `/graphics_mode=antic4` on the command line:

```sh
RastaConverter photo.jpg /graphics_mode=antic4 /h=200 /threads=8
```

Use `/playfield=normal|wide` for either graphics mode. Normal is the default
for both ANTIC E and ANTIC 4:

```sh
RastaConverter photo.jpg /graphics_mode=antic4
RastaConverter photo.jpg /graphics_mode=e /playfield=wide
```

Normal output is 160 Atari colour clocks (320 square preview pixels); wide
output is the central 168 clocks of ANTIC's 48-byte DMA window (336 pixels).
In normal ANTIC 4, P2/P3 and two fifth-player missiles mask the four-clock side
strips. `PRIOR=$1F` makes each overlap resolve to hardware black independently
of COLPF3. Every 4x8 cell has four colours selected from COLBK, COLPF0, COLPF1,
and either COLPF2 or COLPF3.

ANTIC 4 output and wide-playfield output are currently single-frame only.
ANTIC 4 height is rounded to a complete 8-scanline character row and clamped
to 8–240; selecting an incompatible mode in the UI disables dual-frame output.
A save produces the usual `.rp`, `.opt`, `.pmg`, and optimizer-state files
plus `.a4.scr` and `.a4.fnt` for ANTIC 4.
The **XEX** action in Recent automatically selects the bundled ANTIC 4 MADS
template and assembles a runnable executable.

See [ANTIC 4 implementation reference](ANTIC_4_IMPLEMENTATION_SPECIFICATION.md)
for the memory layout, timing contract, limitations, and validation procedure.

Interactive Live UI
-------------------

Launching the GUI build with no arguments opens the setup screen. Choose the
input image at the top - by browsing, by typing a path, or by dropping a file
anywhere on the window - and every conversion option is available beside it in
one grouped, searchable form. "Only changed" lists just what differs from the
defaults, and "Copy command line" produces the exact command line for the
settings on screen, so a GUI experiment can be scripted or shared verbatim.
Application preferences survive restarts: colour theme, font scale, setup
window and splitter sizes, collapsed sections, the "Only changed" view, and
whether new runs use their own subfolder. Conversion settings remain attached
to individual run recipes.

The preview shows what the optimizer will actually aim at, rebuilt as options
change: the resized source, the colour-corrected image, the palette-quantized
image and the final dithered target, with split-wipe and blink comparison
between any two, zoom and pan, and a readout of how many of the 128 palette
entries the target reaches. With a details mask loaded, an overlay shows the
effective weight map - the map the run derives, not the file, which differs
substantially in the normalized and refined modes.

Each conversion writes into its own folder beside the source image,
`rc-<image>-NNN` (`rc-photo-001`, `rc-photo-002`, ...), so runs never overwrite
one another. The "Recent" browser lists previous conversions with their picture,
score and evaluation count, and offers Continue (resume that run in place) or
Reuse (the same settings in a fresh folder).

A card is also the shortest way to the files themselves. XEX assembles the run
into an Atari executable with the bundled MADS - `Generator` for a single-frame
run, `GeneratorDual` for a dual one - writes it into the run's own folder and
hands it to whatever your system opens `.xex` with, normally an emulator. After
that the button just opens the file; Shift-click assembles it again. The folder
name opens the folder in your file manager, the source name opens the image in
your editor, and the thumbnail opens the picture the run produced. Right-click a
card for the same actions in one menu.

During the run, the dashboard shows the convergence chart, plateau state, the
mutation-operator breakdown, dual-frame phase and a recap of the configuration,
with explicit Save, Stop and save, and Abort actions. Finishing a conversion
returns to the setup screen with the settings still in place, so the next
attempt is a tweak away.

Building RastaConverter
-----------------------

RastaConverter requires [CMake](https://cmake.org/) 3.21 or newer and a C++17
compiler. Every build requires FreeImage. The default desktop GUI also requires
SDL3 and SDL3_ttf; a console-only build can omit both SDL libraries. The commands
below create an optimized Release build, and should be run from the project root.

### Linux

Install a compiler, CMake, Ninja, and the development packages for FreeImage,
SDL3, and SDL3_ttf. Use the command for your distribution:

```bash
# Debian 13+ or an Ubuntu release that provides SDL3 development packages
sudo apt update
sudo apt install build-essential cmake ninja-build \
  libfreeimage-dev libsdl3-dev libsdl3-ttf-dev

# Fedora
sudo dnf install gcc-c++ cmake ninja-build \
  freeimage-devel SDL3-devel SDL3_ttf-devel

# Arch Linux, CachyOS, or another Arch-based distribution
sudo pacman -S --needed base-devel cmake ninja freeimage sdl3 sdl3_ttf
```

SDL3 packages are not available in every older Debian/Ubuntu release. On those
systems, use newer distribution packages or supply SDL3 and SDL3_ttf through
vcpkg or `CMAKE_PREFIX_PATH`; SDL2 packages cannot be substituted.

Build, test, and run the GUI with GCC:

```bash
./build.sh linux-gcc Release
ctest --test-dir build/linux-gcc --output-on-failure
./build/linux-gcc/Release/RastaConverter
```

To use Clang, install it and replace `linux-gcc` with `linux-clang`.

### macOS

Install the Apple command-line tools, then install the remaining dependencies
with [Homebrew](https://brew.sh/). The same commands work on Apple Silicon and
Intel Macs; Homebrew and CMake select the appropriate prefix and architecture.

```bash
xcode-select --install
brew install cmake ninja freeimage sdl3 sdl3_ttf
```

Build, test, and run the GUI:

```bash
./build.sh macos-clang Release
ctest --test-dir build/macos-clang --output-on-failure
./build/macos-clang/Release/RastaConverter
```

### Windows

Install these prerequisites:

- Visual Studio 2022 or Build Tools 2022 with the **Desktop development with
  C++** workload
- CMake 3.21 or newer, available on `PATH`
- Git, used only below to obtain vcpkg if it is not already installed
- [vcpkg](https://learn.microsoft.com/vcpkg/get_started/get-started), used by
  the checked-in `vcpkg.json` manifest to build FreeImage, SDL3, and SDL3_ttf

From a **Developer Command Prompt for VS 2022**, bootstrap vcpkg if necessary,
then configure and build. Change `C:\src\vcpkg` if vcpkg is elsewhere.

```bat
git clone https://github.com/microsoft/vcpkg.git C:\src\vcpkg
call C:\src\vcpkg\bootstrap-vcpkg.bat
set VCPKG_ROOT=C:\src\vcpkg

cmake --preset x64-release ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DCOPY_ALL_RUNTIME_DLLS=ON
cmake --build --preset x64-release
ctest --test-dir build\x64-release -C Release --output-on-failure
build\x64-release\Release\RastaConverter.exe
```

If vcpkg is already installed, skip the clone/bootstrap lines and set
`VCPKG_ROOT` to that installation. `COPY_ALL_RUNTIME_DLLS` places the required
dependency DLLs beside the executable.

### Console-only and custom dependency locations

The console-only target is useful on headless hosts and requires FreeImage but
not SDL3 or SDL3_ttf:

```bash
# Linux (use macos-clang instead on macOS)
./build.sh linux-gcc Release nogui
./build/linux-gcc-nogui/Release-NO_GUI/RastaConverter-NO_GUI
```

On Windows, add `-DBUILD_NO_GUI=ON` to the CMake configure command. The target
is written to `build\x64-release\Release-NO_GUI\RastaConverter-NO_GUI.exe`.

If libraries are installed outside the package manager's normal prefix, create
`config.env` in the project root with `FREEIMAGE_DIR`, `SDL3_DIR`, and
`SDL3_TTF_DIR` entries, or pass their prefixes through `CMAKE_PREFIX_PATH`.
Run `cmake -P check_dependencies.cmake` to see which headers CMake can find.

See [BUILD.md](BUILD.md) for Debug builds, alternate compilers, legacy-CPU
builds, direct CMake usage, installation, and profile-guided optimization.

Cleaning builds
---------------

CMake provides the usual `clean` target on every supported platform. It removes
compiled outputs while retaining the configured build tree for a quick rebuild:

```bash
# Linux
cmake --build build/linux-gcc --target clean

# macOS
cmake --build build/macos-clang --target clean
```

From a Developer Command Prompt on Windows:

```bat
cmake --build build\x64-release --config Release --target clean
```

For a completely fresh build, `CLEANONLY` removes the selected build directory
and stops without configuring or rebuilding it:

```bash
# Linux
./build.sh linux-gcc Release cleanonly

# macOS
./build.sh macos-clang Release cleanonly
```

```bat
rem Windows cmd.exe
build.bat release x64 CLEANONLY
```

```powershell
# Windows PowerShell
./build.ps1 -Preset x64-release -Config Release -CleanOnly
```

Add `nogui` to the POSIX/batch wrapper command, or `-NoGui` in PowerShell, to
clean the separate console-only build tree instead.

Screenshots
-----------
![Rasta Converter setup screen](https://github.com/ilmenit/RastaConverter/raw/master/examples/live-ui-setup.png "Rasta Converter setup screen")
![Rasta Converter live dashboard](https://github.com/ilmenit/RastaConverter/raw/master/examples/live-ui-dashboard.png "Rasta Converter live dashboard")
![Rasta Converter recent runs](https://github.com/ilmenit/RastaConverter/raw/master/examples/live-ui-results.png "Rasta Converter recent runs")

Examples
--------
![Example1](http://github.com/ilmenit/RastaConverter/raw/master/examples/ilmenit-autumn-new-output.png)
![Example2](http://github.com/ilmenit/RastaConverter/raw/master/examples/ilmenit-snow_woods.xex-output.png)
![Example3](http://github.com/ilmenit/RastaConverter/raw/master/examples/ilmenit-fairey_wood.xex-output.png)
![Example4](http://github.com/ilmenit/RastaConverter/raw/master/examples/ilmenit-landscape.xex-output.png)

Atari executables for those and many other pictures can be downloaded [here](https://github.com/ilmenit/RastaConverter/blob/master/examples/atari-executables.zip?raw=true).

Development and optimization work
---------------------------------

Command-line reference for every option, including the details-mask, dithering
and dual-frame controls, is in [help.txt](help.txt); the release history is in
[ChangeLog.md](ChangeLog.md).
