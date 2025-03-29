# FindSDL2.cmake
# Try to find SDL2 library
# Once done this will define:
#  SDL2_FOUND - system has SDL2
#  SDL2_INCLUDE_DIRS - the SDL2 include directory
#  SDL2_LIBRARIES - Link these to use SDL2

# Look for the header file
find_path(SDL2_INCLUDE_DIRS NAMES SDL.h
    PATHS
    $ENV{SDL2_DIR}/include
    $ENV{SDL2_DIR}/include/SDL2
    ${SDL2_DIR}/include
    ${SDL2_DIR}/include/SDL2
    /usr/local/include/SDL2
    /usr/include/SDL2
    DOC "Path in which the file SDL.h is located."
)

# Look for the library
find_library(SDL2_LIBRARY NAMES SDL2
    PATHS
    $ENV{SDL2_DIR}/lib
    $ENV{SDL2_DIR}/lib/x64
    ${SDL2_DIR}/lib
    ${SDL2_DIR}/lib/x64
    /usr/local/lib64
    /usr/local/lib
    /usr/lib64
    /usr/lib
    DOC "Path to SDL2 library."
)

find_library(SDL2MAIN_LIBRARY NAMES SDL2main
    PATHS
    $ENV{SDL2_DIR}/lib
    $ENV{SDL2_DIR}/lib/x64
    ${SDL2_DIR}/lib
    ${SDL2_DIR}/lib/x64
    /usr/local/lib64
    /usr/local/lib
    /usr/lib64
    /usr/lib
    DOC "Path to SDL2main library."
)

# Set up the library variable
set(SDL2_LIBRARIES ${SDL2_LIBRARY})
if(SDL2MAIN_LIBRARY)
    list(APPEND SDL2_LIBRARIES ${SDL2MAIN_LIBRARY})
endif()

# Handle the QUIETLY and REQUIRED arguments and set SDL2_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2 
                                  REQUIRED_VARS SDL2_LIBRARIES SDL2_INCLUDE_DIRS)

# Hide these variables in cmake GUIs
mark_as_advanced(SDL2_INCLUDE_DIRS SDL2_LIBRARY SDL2MAIN_LIBRARY)