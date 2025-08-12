# FindSDL2_ttf.cmake
# Try to find SDL2_ttf library
# Once done this will define:
#  SDL2_TTF_FOUND - system has SDL2_ttf
#  SDL2_TTF_INCLUDE_DIRS - the SDL2_ttf include directory
#  SDL2_TTF_LIBRARIES - Link these to use SDL2_ttf
#  SDL2_TTF_DLL - Path to SDL2_ttf.dll (Windows only)

# Determine architecture
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_SUFFIX "x64")
else()
    set(ARCH_SUFFIX "x86")
endif()

# Look for the header file
if(SDL2_TTF_DIR)
    set(SDL2_TTF_INCLUDE_PATHS
        "${SDL2_TTF_DIR}/include"
        "${SDL2_TTF_DIR}/include/SDL2"
        "${SDL2_TTF_DIR}"
    )
else()
    set(SDL2_TTF_INCLUDE_PATHS
        "/usr/local/include/SDL2"
        "/usr/include/SDL2"
        "/opt/local/include/SDL2"
        "/opt/homebrew/include/SDL2"
    )
endif()

find_path(SDL2_TTF_INCLUDE_DIRS NAMES SDL_ttf.h
    PATHS ${SDL2_TTF_INCLUDE_PATHS}
    PATH_SUFFIXES SDL2
    DOC "Path to SDL_ttf.h"
)

# Look for the library
if(SDL2_TTF_DIR)
    if(WIN32)
        find_library(SDL2_TTF_LIBRARIES NAMES SDL2_ttf
            PATHS
            "${SDL2_TTF_DIR}/lib/${ARCH_SUFFIX}"
            "${SDL2_TTF_DIR}/lib"
            "${SDL2_TTF_DIR}"
            DOC "Path to SDL2_ttf library"
        )
        
        # Find DLL
        find_file(SDL2_TTF_DLL NAMES SDL2_ttf.dll
            PATHS
            "${SDL2_TTF_DIR}/lib/${ARCH_SUFFIX}"
            "${SDL2_TTF_DIR}/lib"
            "${SDL2_TTF_DIR}"
            DOC "Path to SDL2_ttf.dll"
        )
    else()
        find_library(SDL2_TTF_LIBRARIES NAMES SDL2_ttf
            PATHS
            "${SDL2_TTF_DIR}/lib"
            "${SDL2_TTF_DIR}"
            DOC "Path to SDL2_ttf library"
        )
    endif()
else()
    find_library(SDL2_TTF_LIBRARIES NAMES SDL2_ttf
        PATHS
        "/usr/local/lib64"
        "/usr/local/lib"
        "/usr/lib64"
        "/usr/lib"
        "/opt/local/lib"
        "/opt/homebrew/lib"
        DOC "Path to SDL2_ttf library"
    )
endif()

# Handle the QUIETLY and REQUIRED arguments and set SDL2_TTF_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2_ttf 
                                  REQUIRED_VARS SDL2_TTF_LIBRARIES SDL2_TTF_INCLUDE_DIRS)

# Hide these variables in cmake GUIs
mark_as_advanced(SDL2_TTF_INCLUDE_DIRS SDL2_TTF_LIBRARIES SDL2_TTF_DLL)

# Provide an imported target for consistency with config packages
if(SDL2_TTF_LIBRARIES AND SDL2_TTF_INCLUDE_DIRS AND NOT TARGET SDL2_ttf::SDL2_ttf)
    add_library(SDL2_ttf::SDL2_ttf UNKNOWN IMPORTED)
    set_target_properties(SDL2_ttf::SDL2_ttf PROPERTIES
        IMPORTED_LOCATION "${SDL2_TTF_LIBRARIES}"
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_TTF_INCLUDE_DIRS}"
    )
endif()