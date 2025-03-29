# FindFreeImage.cmake
# Try to find FreeImage library
# Once done this will define:
#  FREEIMAGE_FOUND - system has FreeImage
#  FREEIMAGE_INCLUDE_DIRS - the FreeImage include directory
#  FREEIMAGE_LIBRARIES - Link these to use FreeImage

# Look for the header file
find_path(FREEIMAGE_INCLUDE_DIRS NAMES FreeImage.h
    PATHS
    $ENV{FREEIMAGE_DIR}/include
    $ENV{FREEIMAGE_DIR}
    ${FREEIMAGE_DIR}/include
    ${FREEIMAGE_DIR}
    /usr/local/include
    /usr/include
    /sw/include
    /opt/local/include
    /opt/csw/include
    /opt/include
    DOC "Path in which the file FreeImage.h is located."
)

# Look for the library
find_library(FREEIMAGE_LIBRARIES NAMES FreeImage freeimage
    PATHS
    $ENV{FREEIMAGE_DIR}/lib
    $ENV{FREEIMAGE_DIR}
    ${FREEIMAGE_DIR}/lib
    ${FREEIMAGE_DIR}
    /usr/local/lib64
    /usr/local/lib
    /usr/lib64
    /usr/lib
    /sw/lib
    /opt/local/lib
    /opt/csw/lib
    /opt/lib
    DOC "Path to FreeImage library."
)

# Handle the QUIETLY and REQUIRED arguments and set FREEIMAGE_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FreeImage 
                                  REQUIRED_VARS FREEIMAGE_LIBRARIES FREEIMAGE_INCLUDE_DIRS)

# Hide these variables in cmake GUIs
mark_as_advanced(FREEIMAGE_LIBRARIES FREEIMAGE_INCLUDE_DIRS)
