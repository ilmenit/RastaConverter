# FindSDL2_ttf.cmake
# Try to find SDL2_ttf library
# Once done this will define:
#  SDL2_TTF_FOUND - system has SDL2_ttf
#  SDL2_TTF_INCLUDE_DIRS - the SDL2_ttf include directory
#  SDL2_TTF_LIBRARIES - Link these to use SDL2_ttf

# Look for the header file
find_path(SDL2_TTF_INCLUDE_DIRS NAMES SDL_ttf.h
    PATHS
    $ENV{SDL2_TTF_DIR}/include
    $ENV{SDL2_TTF_DIR}/include/SDL2
    ${SDL2_TTF_DIR}/include
    ${SDL2_TTF_DIR}/include/SDL2
    /usr/local/include/SDL2
    /usr/include/SDL2
    DOC "Path in which the file SDL_ttf.h is located."
)

# Look for the library
find_library(SDL2_TTF_LIBRARIES NAMES SDL2_ttf
    PATHS
    $ENV{SDL2_TTF_DIR}/lib
    $ENV{SDL2_TTF_DIR}/lib/x64
    ${SDL2_TTF_DIR}/lib
    ${SDL2_TTF_DIR}/lib/x64
    /usr/local/lib64
    /usr/local/lib
    /usr/lib64
    /usr/lib
    DOC "Path to SDL2_ttf library."
)

# Handle the QUIETLY and REQUIRED arguments and set SDL2_TTF_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2_ttf 
                                  REQUIRED_VARS SDL2_TTF_LIBRARIES SDL2_TTF_INCLUDE_DIRS)

# Hide these variables in cmake GUIs
mark_as_advanced(SDL2_TTF_INCLUDE_DIRS SDL2_TTF_LIBRARIES)