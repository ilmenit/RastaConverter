# Profile-Guided Optimization (PGO) Build Guide

This document explains how to use the PGO build scripts for RastaConverter to create optimized binaries.

## Overview

Profile-Guided Optimization (PGO) is a compiler optimization technique that uses runtime profiling data to improve performance. The process involves:

1. **Generate Phase**: Build an instrumented binary that collects profiling data
2. **Profile Phase**: Run the instrumented binary with representative workloads
3. **Optimize Phase**: Build an optimized binary using the collected profile data

## Available Scripts

### Windows (`build-pgo.bat`)
- Uses Intel ICX compiler with LLVM PGO
- Requires Intel oneAPI Developer Command Prompt
- Supports multiple training scenarios

### Linux (`build-pgo.sh`)
- Supports GCC, Clang, and Intel ICX compilers
- Auto-detects available compiler
- Supports both GCC PGO and LLVM PGO

## Linux PGO Build Usage

### Basic Usage

```bash
# Auto-detect compiler and use test.jpg
./build-pgo.sh

# Use specific compiler
./build-pgo.sh --compiler=gcc
./build-pgo.sh --compiler=clang
./build-pgo.sh --compiler=icx

# Use custom test image
./build-pgo.sh examples/test.jpg
./build-pgo.sh --compiler=gcc examples/test.jpg
```

### Prerequisites

#### For GCC PGO:
- GCC compiler
- CMake 3.21+
- Ninja build system

#### For Clang/ICX PGO:
- Clang or Intel ICX compiler
- LLVM tools (llvm-profdata)
- CMake 3.21+
- Ninja build system

#### System Dependencies:
```bash
# Ubuntu/Debian
sudo apt install build-essential cmake ninja-build
sudo apt install llvm  # For Clang PGO

# For Intel ICX
# Install Intel oneAPI Base Toolkit
```

### Training Scenarios

The script runs multiple training scenarios to collect comprehensive profile data:

1. **Base scenario**: Single-threaded with 1M evaluations
2. **Multi-threaded (4 threads)**: 4-threaded with 1M evaluations  
3. **Multi-threaded (8 threads)**: 8-threaded with 1M evaluations
4. **Dual mode**: Single-threaded dual mode with 1M evaluations
5. **Dual mode + 4 threads**: 4-threaded dual mode with 1M evaluations
6. **Dual mode + 8 threads**: 8-threaded dual mode with 1M evaluations

### Output

After successful completion, you'll find:

- **Optimized binary**: `build/{preset}-use/Release/RastaConverter`
- **Profile data**: 
  - GCC: `build/{preset}-gen/Release/*.gcda`, `build/{preset}-gen/Release/*.gcno`
  - Clang/ICX: `pgo/{compiler}/merged.profdata`

### Troubleshooting

#### Common Issues

1. **Compiler not found**:
   ```bash
   # Install missing compiler
   sudo apt install gcc g++  # For GCC
   sudo apt install clang    # For Clang
   ```

2. **llvm-profdata not found**:
   ```bash
   sudo apt install llvm
   ```

3. **CMake preset not found**:
   - Ensure `CMakePresets.json` contains the required PGO presets
   - Run `cmake --list-presets` to verify

4. **Test image not found**:
   - Provide a valid image path as argument
   - Ensure the image exists in the repository root or run directory

#### Performance Tips

1. **Use representative test images**: The quality of PGO optimization depends on how well the training scenarios match real usage
2. **Run on target hardware**: For best results, run PGO builds on the same hardware where the optimized binary will be used
3. **Multiple training runs**: Consider running the script multiple times with different test images for more comprehensive profiling

### Example Workflow

```bash
# 1. Ensure dependencies are installed
sudo apt install build-essential cmake ninja-build llvm

# 2. Run PGO build with auto-detected compiler
./build-pgo.sh

# 3. Use the optimized binary
./build/linux-gcc-pgo-use/Release/RastaConverter test.jpg

# 4. Compare performance with non-PGO build
./build.sh linux-gcc
./build/linux-gcc/Release/RastaConverter test.jpg
```

### Advanced Usage

#### Custom Training Scenarios

You can modify the script to add custom training scenarios by editing the `run_scenario` calls in the script.

#### Profile Data Reuse

- GCC: Profile data is automatically reused from the generation phase
- Clang/ICX: Profile data is merged into `merged.profdata` and can be reused

#### Clean Builds

To start fresh:
```bash
rm -rf build/linux-*-pgo-*
rm -rf pgo/
./build-pgo.sh
```
