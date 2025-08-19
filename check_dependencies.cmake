# CMake script to check if all dependencies can be found
# Usage: cmake -P check_dependencies.cmake

message(STATUS "Checking RastaConverter dependencies...")

# Function to read config.env file (root first, then src/)
function(read_config_env)
    if(EXISTS "${CMAKE_SOURCE_DIR}/config.env")
        message(STATUS "Reading config.env from project root...")
        set(_CONFIG_ENV_PATH "${CMAKE_SOURCE_DIR}/config.env")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/src/config.env")
        message(STATUS "Reading config.env from src/...")
        set(_CONFIG_ENV_PATH "${CMAKE_SOURCE_DIR}/src/config.env")
    else()
        message(STATUS "config.env file not found; continuing with system defaults and package managers")
        return()
    endif()

    file(STRINGS "${_CONFIG_ENV_PATH}" CONFIG_LINES)
    foreach(LINE ${CONFIG_LINES})
        if(LINE MATCHES "^[^#].*=.*")
            string(REGEX REPLACE "([^=]+)=(.*)" "\\1;\\2" KEY_VALUE ${LINE})
            list(GET KEY_VALUE 0 KEY)
            list(GET KEY_VALUE 1 VALUE)
            string(REGEX REPLACE "^\"(.*)\"$" "\\1" VALUE ${VALUE})
            string(REGEX REPLACE "^'(.*)'$" "\\1" VALUE ${VALUE})
            set(${KEY} ${VALUE} PARENT_SCOPE)
            message(STATUS "  ${KEY} = ${VALUE}")
        endif()
    endforeach()
endfunction()

# Read environment configuration
read_config_env()

# Propagate *_DIR hints into prefix path for discovery if provided
if(FREEIMAGE_DIR)
    set(CMAKE_PREFIX_PATH "${FREEIMAGE_DIR};${CMAKE_PREFIX_PATH}")
endif()
if(SDL2_DIR)
    set(CMAKE_PREFIX_PATH "${SDL2_DIR};${CMAKE_PREFIX_PATH}")
endif()
if(SDL2_TTF_DIR)
    set(CMAKE_PREFIX_PATH "${SDL2_TTF_DIR};${CMAKE_PREFIX_PATH}")
endif()

message(STATUS "\nDiscovery hints (optional):")
if(FREEIMAGE_DIR)
    message(STATUS "  FREEIMAGE_DIR = ${FREEIMAGE_DIR}")
endif()
if(SDL2_DIR)
    message(STATUS "  SDL2_DIR = ${SDL2_DIR}")
endif()
if(SDL2_TTF_DIR)
    message(STATUS "  SDL2_TTF_DIR = ${SDL2_TTF_DIR}")
endif()

# Check for required tools
find_program(CMAKE_EXE cmake)
if(CMAKE_EXE)
    message(STATUS "\n✓ CMake found: ${CMAKE_EXE}")
else()
    message(FATAL_ERROR "\n✗ CMake not found")
endif()

# Check for MSVC compiler
find_program(CL_EXE cl)
if(CL_EXE)
    message(STATUS "✓ MSVC compiler found: ${CL_EXE}")
else()
    message(WARNING "✗ MSVC compiler (cl.exe) not found in PATH")
endif()

message(STATUS "\nChecking include directories...")

# Check FreeImage using config.env path
set(FREEIMAGE_HEADER "${FREEIMAGE_DIR}/FreeImage.h")
if(EXISTS "${FREEIMAGE_HEADER}")
    message(STATUS "✓ FreeImage header found: ${FREEIMAGE_HEADER}")
else()
    message(WARNING "✗ FreeImage header not found at: ${FREEIMAGE_HEADER}")
    message(STATUS "  Check FREEIMAGE_DIR in config.env")
endif()

# Check SDL2 locations using config.env paths
message(STATUS "\nChecking SDL2 locations...")
set(SDL2_LOCATIONS
    "${SDL2_DIR}/include/SDL.h"
    "${SDL2_DIR}/include/SDL2/SDL.h"
    "${CMAKE_SOURCE_DIR}/src/packages/sdl2.nuget.2.30.2/build/native/include/SDL.h"
    "$ENV{VCPKG_ROOT}/installed/x64-windows/include/SDL2/SDL.h"
    "$ENV{VCPKG_ROOT}/installed/x86-windows/include/SDL2/SDL.h"
    "C:/vcpkg/installed/x64-windows/include/SDL2/SDL.h"
)
set(SDL2_FOUND FALSE)
foreach(SDL2_PATH ${SDL2_LOCATIONS})
    if(EXISTS "${SDL2_PATH}")
        message(STATUS "✓ SDL2 header found: ${SDL2_PATH}")
        set(SDL2_FOUND TRUE)
        break()
    endif()
endforeach()
if(NOT SDL2_FOUND)
    message(WARNING "✗ SDL2 header (SDL.h) not found in any of these locations:")
    foreach(SDL2_PATH ${SDL2_LOCATIONS})
        message(STATUS "  ${SDL2_PATH}")
    endforeach()
    message(STATUS "  Check SDL2_DIR in config.env")
endif()

# Check SDL2_ttf locations using config.env paths
set(SDL2_TTF_LOCATIONS
    "${SDL2_TTF_DIR}/include/SDL_ttf.h"
    "${SDL2_TTF_DIR}/include/SDL2/SDL_ttf.h"
    "${SDL2_DIR}/include/SDL_ttf.h"
    "${SDL2_DIR}/include/SDL2/SDL_ttf.h"
    "${CMAKE_SOURCE_DIR}/src/packages/sdl2_ttf.nuget.2.22.0/build/native/include/SDL_ttf.h"
    "$ENV{VCPKG_ROOT}/installed/x64-windows/include/SDL2/SDL_ttf.h"
    "$ENV{VCPKG_ROOT}/installed/x86-windows/include/SDL2/SDL_ttf.h"
    "C:/vcpkg/installed/x64-windows/include/SDL2/SDL_ttf.h"
)
set(SDL2_TTF_FOUND FALSE)
foreach(SDL2_TTF_PATH ${SDL2_TTF_LOCATIONS})
    if(EXISTS "${SDL2_TTF_PATH}")
        message(STATUS "✓ SDL2_ttf header found: ${SDL2_TTF_PATH}")
        set(SDL2_TTF_FOUND TRUE)
        break()
    endif()
endforeach()
if(NOT SDL2_TTF_FOUND)
    message(WARNING "✗ SDL2_ttf header (SDL_ttf.h) not found in any of these locations:")
    foreach(SDL2_TTF_PATH ${SDL2_TTF_LOCATIONS})
        message(STATUS "  ${SDL2_TTF_PATH}")
    endforeach()
    message(STATUS "  Check SDL2_TTF_DIR in config.env")
endif()

message(STATUS "\nChecking library discovery via find_package...")
set(ALL_FOUND TRUE)

find_package(FreeImage QUIET)
if(FreeImage_FOUND)
    message(STATUS "✓ FreeImage found via find_package")
else()
    set(ALL_FOUND FALSE)
    message(WARNING "✗ FreeImage not found via find_package")
endif()

find_package(SDL2 QUIET)
if(SDL2_FOUND)
    message(STATUS "✓ SDL2 found via find_package")
else()
    set(ALL_FOUND FALSE)
    message(WARNING "✗ SDL2 not found via find_package")
endif()

find_package(SDL2_ttf QUIET)
if(SDL2_TTF_FOUND)
    message(STATUS "✓ SDL2_ttf found via find_package")
else()
    set(ALL_FOUND FALSE)
    message(WARNING "✗ SDL2_ttf not found via find_package")
endif()

message(STATUS "\nDependency check complete!")

if(ALL_FOUND)
    message(STATUS "\n🎉 All dependencies found! You should be able to build successfully.")
else()
    message(STATUS "\nSome dependencies are missing.")
    message(STATUS "Hints:")
    message(STATUS "  - Provide hints in config.env (FREEIMAGE_DIR/SDL2_DIR/SDL2_TTF_DIR) or via CMAKE_PREFIX_PATH")
    message(STATUS "  - Install via your system package manager:")
    message(STATUS "      Ubuntu: sudo apt install libfreeimage-dev libsdl2-dev libsdl2-ttf-dev")
    message(STATUS "      macOS:  brew install freeimage sdl2 sdl2_ttf")
    message(STATUS "      Windows: use vcpkg (add toolchain) or vendor SDKs")
endif()


