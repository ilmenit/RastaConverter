# FindFreeImage.cmake
# Try to find FreeImage library
# Once done this will define:
#  FREEIMAGE_FOUND - system has FreeImage
#  FREEIMAGE_INCLUDE_DIRS - the FreeImage include directory
#  FREEIMAGE_LIBRARIES - Link these to use FreeImage
#  FREEIMAGE_DLL - DLL path for runtime deployment (Windows only)

# Debug mode - uncomment for troubleshooting
# set(CMAKE_FIND_DEBUG_MODE TRUE)

# Look for the header file
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # 64-bit architecture
    set(ARCH_FOLDER "x64")
else()
    # 32-bit architecture
    set(ARCH_FOLDER "x32")
endif()

message(STATUS "Architecture detected: ${ARCH_FOLDER}")
message(STATUS "FREEIMAGE_DIR set to: ${FREEIMAGE_DIR}")

# Check if FREEIMAGE_DIR is specified
if(FREEIMAGE_DIR)
    # Direct explicit paths for FreeImage
    set(FREEIMAGE_INCLUDE_DIRS "${FREEIMAGE_DIR}/Dist/${ARCH_FOLDER}")
    set(FREEIMAGE_LIBRARIES "${FREEIMAGE_DIR}/Dist/${ARCH_FOLDER}/FreeImage.lib")
    if(WIN32)
        set(FREEIMAGE_DLL "${FREEIMAGE_DIR}/Dist/${ARCH_FOLDER}/FreeImage.dll")
    endif()
    
    # Verify files exist
    if(NOT EXISTS "${FREEIMAGE_INCLUDE_DIRS}/FreeImage.h")
        message(FATAL_ERROR "FreeImage.h not found at expected location: ${FREEIMAGE_INCLUDE_DIRS}/FreeImage.h")
    endif()
    
    if(NOT EXISTS "${FREEIMAGE_LIBRARIES}")
        message(FATAL_ERROR "FreeImage.lib not found at expected location: ${FREEIMAGE_LIBRARIES}")
    endif()
    
    if(WIN32 AND NOT EXISTS "${FREEIMAGE_DLL}")
        message(WARNING "FreeImage.dll not found at expected location: ${FREEIMAGE_DLL}")
    endif()
    
    message(STATUS "FreeImage include: ${FREEIMAGE_INCLUDE_DIRS}")
    message(STATUS "FreeImage library: ${FREEIMAGE_LIBRARIES}")
    if(WIN32)
        message(STATUS "FreeImage DLL: ${FREEIMAGE_DLL}")
    endif()
else()
    # Use find_path and find_library for traditional search
    find_path(FREEIMAGE_INCLUDE_DIRS NAMES FreeImage.h
        PATHS
        "/usr/local/include"
        "/usr/include"
        "/opt/local/include"
        DOC "Path to FreeImage.h"
    )

    find_library(FREEIMAGE_LIBRARIES NAMES FreeImage freeimage
        PATHS
        "/usr/local/lib64"
        "/usr/local/lib"
        "/usr/lib64"
        "/usr/lib"
        "/opt/local/lib"
        DOC "Path to FreeImage library"
    )
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FreeImage 
                                  REQUIRED_VARS FREEIMAGE_LIBRARIES FREEIMAGE_INCLUDE_DIRS)

mark_as_advanced(FREEIMAGE_LIBRARIES FREEIMAGE_INCLUDE_DIRS FREEIMAGE_DLL)