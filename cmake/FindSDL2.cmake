# FindSDL2.cmake
# Try to find SDL2 library
# Once done this will define:
#  SDL2_FOUND - system has SDL2
#  SDL2_INCLUDE_DIRS - the SDL2 include directory
#  SDL2_LIBRARIES - Link these to use SDL2
#  SDL2_DLL - Path to SDL2.dll (Windows only)

# Determine architecture
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_SUFFIX "x64")
else()
    set(ARCH_SUFFIX "x86")
endif()

# Look for the header file
if(SDL2_DIR)
    set(SDL2_INCLUDE_PATHS
        "${SDL2_DIR}/include"
        "${SDL2_DIR}/include/SDL2"
        "${SDL2_DIR}"
    )
else()
    set(SDL2_INCLUDE_PATHS
        "/usr/local/include/SDL2"
        "/usr/include/SDL2"
    )
endif()

find_path(SDL2_INCLUDE_DIRS NAMES SDL.h
    PATHS ${SDL2_INCLUDE_PATHS}
    PATH_SUFFIXES SDL2
    DOC "Path to SDL.h"
)

# Look for the library
if(SDL2_DIR)
    if(WIN32)
        find_library(SDL2_LIBRARY NAMES SDL2
            PATHS
            "${SDL2_DIR}/lib/${ARCH_SUFFIX}"
            "${SDL2_DIR}/lib"
            "${SDL2_DIR}"
            DOC "Path to SDL2 library"
        )
        
        find_library(SDL2MAIN_LIBRARY NAMES SDL2main
            PATHS
            "${SDL2_DIR}/lib/${ARCH_SUFFIX}"
            "${SDL2_DIR}/lib"
            "${SDL2_DIR}"
            DOC "Path to SDL2main library"
        )
        
        # Find DLL
        find_file(SDL2_DLL NAMES SDL2.dll
            PATHS
            "${SDL2_DIR}/lib/${ARCH_SUFFIX}"
            "${SDL2_DIR}/lib"
            "${SDL2_DIR}"
            DOC "Path to SDL2.dll"
        )
    else()
        find_library(SDL2_LIBRARY NAMES SDL2
            PATHS
            "${SDL2_DIR}/lib"
            "${SDL2_DIR}"
            DOC "Path to SDL2 library"
        )
    endif()
else()
    find_library(SDL2_LIBRARY NAMES SDL2
        PATHS
        "/usr/local/lib64"
        "/usr/local/lib"
        "/usr/lib64"
        "/usr/lib"
        DOC "Path to SDL2 library"
    )
    
    find_library(SDL2MAIN_LIBRARY NAMES SDL2main
        PATHS
        "/usr/local/lib64"
        "/usr/local/lib"
        "/usr/lib64"
        "/usr/lib"
        DOC "Path to SDL2main library"
    )
endif()

# Set up the library variable
set(SDL2_LIBRARIES ${SDL2_LIBRARY})
if(SDL2MAIN_LIBRARY)
    list(APPEND SDL2_LIBRARIES ${SDL2MAIN_LIBRARY})
endif()

if(WIN32)
    # Additional libraries needed on Windows
    list(APPEND SDL2_LIBRARIES version.lib imm32.lib winmm.lib)
endif()

# Handle the QUIETLY and REQUIRED arguments and set SDL2_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2 
                                  REQUIRED_VARS SDL2_LIBRARIES SDL2_INCLUDE_DIRS)

# Hide these variables in cmake GUIs
mark_as_advanced(SDL2_INCLUDE_DIRS SDL2_LIBRARY SDL2MAIN_LIBRARY SDL2_DLL)