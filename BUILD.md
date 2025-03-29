# Building RastaConverter

This document explains how to build RastaConverter from source.

## Prerequisites

- CMake 3.14 or higher
- C++17 compatible compiler
- FreeImage library
- SDL2 and SDL2_ttf libraries (unless building with `-DNO_GUI=ON`)

## Getting the Dependencies

### Windows

#### FreeImage

1. Download the FreeImage library from [https://freeimage.sourceforge.io/](https://freeimage.sourceforge.io/)
2. Extract it to a location of your choice
3. Set `FREEIMAGE_DIR` environment variable to point to the FreeImage directory

#### SDL2 and SDL2_ttf

1. Download SDL2 from [https://www.libsdl.org/download-2.0.php](https://www.libsdl.org/download-2.0.php)
2. Download SDL2_ttf from [https://www.libsdl.org/projects/SDL_ttf/](https://www.libsdl.org/projects/SDL_ttf/)
3. Extract both to locations of your choice
4. Set `SDL2_DIR` and `SDL2_TTF_DIR` environment variables

### Linux

Install the required dependencies using your package manager:

#### Ubuntu/Debian:
```bash
sudo apt-get install libfreeimage-dev libsdl2-dev libsdl2-ttf-dev
```

#### Fedora:
```bash
sudo dnf install freeimage-devel SDL2-devel SDL2_ttf-devel
```

## Building the Project

### Command Line Build

1. Create a build directory:
```bash
mkdir build
cd build
```

2. Run CMake to configure the project:
```bash
cmake ..
```

3. Build the project:
```bash
cmake --build . --config Release
```

### Visual Studio (Windows)

1. Open a command prompt and create a build directory:
```batch
mkdir build
cd build
```

2. Generate Visual Studio solution:
```batch
cmake .. -G "Visual Studio 16 2019" -A x64
```

3. Open the generated solution file `RastaConverter.sln` in Visual Studio

4. Build the solution (F7 or Build → Build Solution)

### Building without GUI

If you want to build without the SDL2 GUI (console mode only):

```bash
cmake .. -DNO_GUI=ON
cmake --build . --config Release
```

## Installation

To install the built executable and required files:

```bash
cmake --install . --prefix /path/to/install
```

On Windows, you might need to copy the required DLLs (FreeImage.dll, SDL2.dll, SDL2_ttf.dll) to the same directory as the executable if they aren't found automatically.

## Troubleshooting

### FreeImage Not Found

If CMake cannot find FreeImage, you can specify its location explicitly:

```bash
cmake .. -DFREEIMAGE_DIR=/path/to/freeimage
```

### SDL2 Not Found

Similarly for SDL2:

```bash
cmake .. -DSDL2_DIR=/path/to/sdl2 -DSDL2_TTF_DIR=/path/to/sdl2_ttf
```

## Project Structure

The project is organized into several directories:

- `assets/` - Runtime assets like fonts
- `cache/` - Caching mechanisms for performance
- `cmake/` - CMake modules for finding dependencies
- `color/` - Color handling and distance calculations
- `execution/` - Program execution simulation
- `io/` - Input/output operations
- `mutation/` - Program mutation logic
- `optimization/` - Optimization algorithms
- `raster/` - Raster program representation
- `ui/` - GUI and console interfaces
- `utils/` - Utility functions

## Required Font

If you're building with GUI support, you'll need the `clacon2.ttf` font in the `assets` directory. This font is used by the SDL interface for text rendering. 

The build system will automatically copy this file to the executable directory.
