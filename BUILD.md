# Building RastaConverter

RastaConverter supports multiple build configurations:
- GUI and No-GUI versions
- x64 and x86 architectures
- Debug and Release builds

## Prerequisites

### Windows
1. Visual Studio 2022 with C++ workload
2. CMake 3.14 or higher
3. FreeImage library
4. SDL2 and SDL2_ttf libraries (for GUI version)

### Linux
1. GCC or Clang compiler
2. CMake 3.14 or higher
3. FreeImage development packages
4. SDL2 and SDL2_ttf development packages (for GUI version)

## Environment Setup

Before building, you need to set up your environment variables for library paths.

### Windows
1. Run `setup-env.bat` script:
   ```
   setup-env.bat
   ```
2. The script will prompt you for paths to FreeImage, SDL2, and SDL2_ttf libraries.
3. It will save these paths to environment variables and a `config.env` file.

### Linux
1. Run `setup-env.sh` script:
   ```
   chmod +x setup-env.sh
   ./setup-env.sh
   ```
2. The script will try to detect libraries using pkg-config and prompt for paths.
3. It can add the variables to your `.bashrc` file for persistence.

## Building on Windows

### Using Visual Studio
1. First run the environment setup as described above.
2. Open the project folder in Visual Studio.
3. Visual Studio should automatically detect the CMake project.
4. Select your desired configuration from the dropdown in the toolbar:
   - x64-Debug-GUI: 64-bit debug build with GUI
   - x64-Debug-NoGUI: 64-bit debug build without GUI
   - x64-Release-GUI: 64-bit optimized release build with GUI
   - x64-Release-NoGUI: 64-bit optimized release build without GUI
   - x86-Debug-GUI: 32-bit debug build with GUI
   - x86-Debug-NoGUI: 32-bit debug build without GUI
   - x86-Release-GUI: 32-bit optimized release build with GUI
   - x86-Release-NoGUI: 32-bit optimized release build without GUI
   - x64-Clang-Debug-GUI: 64-bit debug build with GUI using Clang
   - x64-Clang-Release-GUI: 64-bit release build with GUI using Clang
   - x64-Clang-Debug-NoGUI: 64-bit debug build without GUI using Clang
   - x64-Clang-Release-NoGUI: 64-bit release build without GUI using Clang
5. Click on Build → Build All to compile the project.

### Using Command Prompt
Use the provided build.bat script:

```
build.bat [DEBUG|RELEASE] [GUI|NOGUI] [x64|x86]
```

Examples:
```
build.bat RELEASE GUI x64     # Build 64-bit Release with GUI
build.bat DEBUG NOGUI x86     # Build 32-bit Debug without GUI
```

## Building on Linux

Use the provided build.sh script:

```
./build.sh [DEBUG|RELEASE] [GUI|NOGUI] [x64|x86]
```

Examples:
```
./build.sh RELEASE GUI x64    # Build 64-bit Release with GUI
./build.sh DEBUG NOGUI x86    # Build 32-bit Debug without GUI
```

### Installing Dependencies on Ubuntu/Debian

```bash
sudo apt update
sudo apt install build-essential cmake libfreeimage-dev
# For GUI version
sudo apt install libsdl2-dev libsdl2-ttf-dev
```

### Installing Dependencies on Fedora/RHEL

```bash
sudo dnf install gcc-c++ cmake freeimage-devel
# For GUI version
sudo dnf install SDL2-devel SDL2_ttf-devel
```

## Customizing Library Paths

There are several ways to set the paths to your libraries:

1. **Environment Variables**: The simplest approach is to set these environment variables:
   ```
   FREEIMAGE_DIR=/path/to/freeimage
   SDL2_DIR=/path/to/sdl2
   SDL2_TTF_DIR=/path/to/sdl2_ttf
   ```

2. **Setup Scripts**: Use the provided setup scripts:
   - Windows: `setup-env.bat`
   - Linux: `setup-env.sh`

3. **config.env File**: The build scripts will read from a `config.env` file in the project root:
   ```
   FREEIMAGE_DIR=/path/to/freeimage
   SDL2_DIR=/path/to/sdl2
   SDL2_TTF_DIR=/path/to/sdl2_ttf
   ```

4. **Interactive Mode**: If no environment variables or config file exists, the build scripts will prompt you for the paths.

5. **CMake Cache**: For permanent settings, you can set these as CMake cache variables:
   ```bash
   cmake -DFREEIMAGE_DIR=/path/to/freeimage -DSDL2_DIR=/path/to/sdl2 -DSDL2_TTF_DIR=/path/to/sdl2_ttf ..
   ```

## Release Build Optimizations

The Release build configurations include the following optimizations based on compiler:

### Windows (MSVC)
- /O2: Maximize speed
- /Ob2: Inline function expansion
- /Oi: Generate intrinsic functions
- /Ot: Favor fast code
- /GL: Whole program optimization
- /LTCG: Link-time code generation

### Windows (Clang-cl)
- /O2: Maximize speed
- /Ob2: Inline function expansion
- /Oi: Generate intrinsic functions

### Linux (GCC)
- -O3: Maximum optimization
- -march=native: Optimize for the local machine
- -ffast-math: Enable fast floating point optimizations
- -flto: Link-time optimization

### Linux (Clang)
- -O3: Maximum optimization
- -march=native: Optimize for the local machine
- -ffast-math: Enable fast floating point optimizations

These optimizations ensure the best performance for the release builds while being compatible with each compiler's supported flags.

## Optional debug logging categories

By default, console logging is minimal. You can opt-in to verbose logs by defining these CMake options:

- THREAD_DEBUG: Enables detailed logs from optimization/control threads and executor.
- UI_DEBUG: Enables SDL UI event and heartbeat logs.

Example:

```bash
cmake -B build -S . -DTHREAD_DEBUG=ON -DUI_DEBUG=ON
cmake --build build --config Release
```

Or in Visual Studio CMake Settings, add to CMake command arguments:

```
-DTHREAD_DEBUG=ON -DUI_DEBUG=ON
```

## Troubleshooting

### CMake can't find the libraries
- Make sure the environment variables are set correctly
- Try using absolute paths instead of relative paths
- Check if the library files exist in the specified directories
- Use the interactive mode of the build scripts to verify paths

### Missing DLL errors on Windows
- The build system tries to copy DLLs automatically
- Make sure the DLLs are in the expected locations within the library directories
- Try manually copying the DLLs to the output directory

### Build errors on Linux
- Make sure you have the required development packages installed
- Use the system package manager to install missing dependencies
- Check if you need to specify additional include/library paths

### Compiler-specific issues
- Different compilers may need different flags
- The build system tries to handle common compilers automatically
- If using a specialized compiler setup, you may need to customize the CMake flags
