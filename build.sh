#!/bin/bash

# Script to build RastaConverter on Linux systems
# Usage: ./build.sh [DEBUG|RELEASE] [GUI|NOGUI] [x64|x86]

# Default values
BUILD_TYPE="Release"
GUI_OPTION=""
ARCH=""

# Parse command line arguments
for arg in "$@"
do
    case $arg in
        DEBUG)
        BUILD_TYPE="Debug"
        ;;
        RELEASE)
        BUILD_TYPE="Release"
        ;;
        GUI)
        GUI_OPTION=""
        ;;
        NOGUI)
        GUI_OPTION="-DNO_GUI=ON"
        ;;
        x64)
        ARCH="-DCMAKE_CXX_FLAGS=-m64"
        ;;
        x86)
        ARCH="-DCMAKE_CXX_FLAGS=-m32"
        ;;
    esac
done

# Check for required dependencies
echo "Checking for dependencies..."
command -v cmake >/dev/null 2>&1 || { echo "CMake is required but not installed. Aborting."; exit 1; }

# Load or create configuration
CONFIG_FILE="config.env"
if [ -f "$CONFIG_FILE" ]; then
    source "$CONFIG_FILE"
fi

# Ask for library paths if not set
if [ -z "$FREEIMAGE_DIR" ]; then
    # Try to find FreeImage using pkg-config
    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists freeimage; then
        FREEIMAGE_DIR=$(pkg-config --variable=prefix freeimage)
    else
        echo "FREEIMAGE_DIR environment variable not set."
        read -p "Enter path to FreeImage library (leave empty for system location): " FREEIMAGE_DIR
    fi
    echo "FREEIMAGE_DIR=\"$FREEIMAGE_DIR\"" > "$CONFIG_FILE"
fi

if [ -z "$SDL2_DIR" ] && [ -z "$GUI_OPTION" ]; then
    # Try to find SDL2 using pkg-config
    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2; then
        SDL2_DIR=$(pkg-config --variable=prefix sdl2)
    else
        echo "SDL2_DIR environment variable not set."
        read -p "Enter path to SDL2 library (leave empty for system location): " SDL2_DIR
    fi
    echo "SDL2_DIR=\"$SDL2_DIR\"" >> "$CONFIG_FILE"
fi

if [ -z "$SDL2_TTF_DIR" ] && [ -z "$GUI_OPTION" ]; then
    # Try to find SDL2_ttf using pkg-config
    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists SDL2_ttf; then
        SDL2_TTF_DIR=$(pkg-config --variable=prefix SDL2_ttf)
    else
        echo "SDL2_TTF_DIR environment variable not set."
        read -p "Enter path to SDL2_ttf library (leave empty for system location): " SDL2_TTF_DIR
    fi
    echo "SDL2_TTF_DIR=\"$SDL2_TTF_DIR\"" >> "$CONFIG_FILE"
fi

# Create build directory
BUILD_DIR="build-${BUILD_TYPE}-${GUI_OPTION:4:5}-${ARCH:19:3}"
if [ -z "$GUI_OPTION" ]; then
    BUILD_DIR="build-${BUILD_TYPE}-GUI-${ARCH:19:3}"
fi
if [ -z "$ARCH" ]; then
    BUILD_DIR="${BUILD_DIR%?}x64"
fi

echo "Creating build directory: $BUILD_DIR"
mkdir -p $BUILD_DIR
cd $BUILD_DIR

# Configure and build
echo "Configuring with CMake..."
CMAKE_ARGS="-DCMAKE_BUILD_TYPE=$BUILD_TYPE $GUI_OPTION $ARCH"

# Add library paths if they were set
if [ -n "$FREEIMAGE_DIR" ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DFREEIMAGE_DIR=$FREEIMAGE_DIR"
fi

if [ -n "$SDL2_DIR" ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DSDL2_DIR=$SDL2_DIR"
fi

if [ -n "$SDL2_TTF_DIR" ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DSDL2_TTF_DIR=$SDL2_TTF_DIR"
fi

echo "Running: cmake .. $CMAKE_ARGS"
cmake .. $CMAKE_ARGS

echo "Building..."
cmake --build . -j$(nproc)

# Check if build was successful
if [ $? -eq 0 ]; then
    echo "Build successful! Executable is located at: $BUILD_DIR/rasta"
else
    echo "Build failed!"
    exit 1
fi
