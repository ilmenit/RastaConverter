cmake_minimum_required(VERSION 3.21)

project(RastaConverter)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable folder structure in IDEs
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Ensure native threads are available for std::thread on POSIX (adds -pthread flags)
find_package(Threads REQUIRED)

# --------------------------------------------------------------------------------------
# Options (speed-oriented defaults for Release builds)
# --------------------------------------------------------------------------------------
option(BUILD_NO_GUI "Build console-only version (no SDL UI)" OFF)
option(NO_GUI "Alias for BUILD_NO_GUI" OFF)
option(ENABLE_LTO "Enable interprocedural optimization (Release/RelWithDebInfo)" ON)
option(ENABLE_FAST_MATH "Enable fast math in Release/RelWithDebInfo (non-MSVC)" ON)
option(ENABLE_DEAD_STRIP "Enable function/data section and linker GC (non-MSVC)" ON)
option(ENABLE_MSVC_FAST_MATH "Use /fp:fast on MSVC/clang-cl in Release/RelWithDebInfo" ON)
option(ENABLE_AVX2 "Enable AVX2 in Release/RelWithDebInfo" OFF)
option(ENABLE_UNITY_BUILD "Enable unity build for faster compiles" OFF)
option(COPY_ALL_RUNTIME_DLLS "Copy all dependent runtime DLLs on Windows" OFF)
option(BUILD_STATIC "Build with static linking for easier distribution" OFF)

if(NO_GUI)
    set(BUILD_NO_GUI ON)
endif()

# --------------------------------------------------------------------------------------
# Read optional config.env (root preferred, then src/) to accept user-provided hints
# Supported keys: FREEIMAGE_DIR, SDL2_DIR, SDL2_TTF_DIR. These will be used as hints
# for find_package via CMAKE_PREFIX_PATH or *_DIR variables.
# --------------------------------------------------------------------------------------
function(read_config_env)
    if(EXISTS "${CMAKE_SOURCE_DIR}/config.env")
        message(STATUS "Reading config.env from project root...")
        set(_CONFIG_ENV_PATH "${CMAKE_SOURCE_DIR}/config.env")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/src/config.env")
        message(STATUS "Reading config.env from src/...")
        set(_CONFIG_ENV_PATH "${CMAKE_SOURCE_DIR}/src/config.env")
    else()
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

read_config_env()

# If user provided *_DIR hints, propagate them to CMake discovery paths
if(FREEIMAGE_DIR)
    # Common config packages respect *_DIR or CMAKE_PREFIX_PATH
    set(CMAKE_PREFIX_PATH "${FREEIMAGE_DIR};${CMAKE_PREFIX_PATH}")
    set(FreeImage_DIR "${FREEIMAGE_DIR}")
endif()
if(SDL2_DIR)
    set(CMAKE_PREFIX_PATH "${SDL2_DIR};${CMAKE_PREFIX_PATH}")
endif()
if(SDL2_TTF_DIR)
    set(CMAKE_PREFIX_PATH "${SDL2_TTF_DIR};${CMAKE_PREFIX_PATH}")
endif()

# Source files (now prefixed with src/)
set(COMMON_SOURCE_FILES
    src/app/CommandLineParser.cpp
    src/app/config.cpp
    src/color/Distance.cpp
    src/core/Evaluator.cpp
    src/app/main.cpp
    src/rng/prng_xoroshiro.cpp
    src/core/Program.cpp
    src/core/Cycles.cpp
    src/core/rasta.cpp
    src/core/RastaDual.cpp
    src/core/dual/RastaDual_Display.cpp
    src/core/dual/RastaDual_Tables.cpp
    src/core/dual/RastaDual_DataIO.cpp
    src/core/dual/RastaDual_MainLoop.cpp
    src/frontend/console/RastaConsole.cpp
    src/color/rgb.cpp
    src/core/TargetPicture.cpp
)

set(GUI_SOURCE_FILES
    src/frontend/gui/RastaSDL.cpp
)

set(ALL_SOURCE_FILES ${COMMON_SOURCE_FILES} ${GUI_SOURCE_FILES})

set(HEADER_FILES
    src/app/CommandLineParser.h
    src/app/config.h
    src/color/Distance.h
    src/core/Evaluator.h
    src/frontend/common/gui.h
    src/core/InsnSequenceCache.h
    src/core/LinearAllocator.h
    src/core/LineCache.h
    src/core/Program.h
    src/frontend/console/RastaConsole.h
    src/frontend/gui/RastaSDL.h
    src/core/RasterInstruction.h
    src/core/RegisterState.h
    src/rng/prng_xoroshiro.h
    src/core/rasta.h
    src/color/rgb.h
    src/utils/string_conv.h
    src/core/TargetPicture.h
)

# Targets
if(BUILD_NO_GUI)
    # Console-only build (no SDL2/GUI)
    add_executable(RastaConverter-NO_GUI ${COMMON_SOURCE_FILES} ${HEADER_FILES})
    if(WIN32)
        target_sources(RastaConverter-NO_GUI PRIVATE src/platform/win/ConsoleCtrlWin.cpp)
        target_include_directories(RastaConverter-NO_GUI PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/win)
    endif()
    message(STATUS "Building console-only version (NO_GUI=ON)")
else()
    # GUI build (default)
    add_executable(RastaConverter ${COMMON_SOURCE_FILES} ${GUI_SOURCE_FILES} ${HEADER_FILES})
    if(WIN32)
        target_sources(RastaConverter PRIVATE src/platform/win/ConsoleCtrlWin.cpp)
        target_include_directories(RastaConverter PRIVATE ${CMAKE_SOURCE_DIR}/src/platform/win)
    endif()
    message(STATUS "Building GUI version only (use -DBUILD_NO_GUI=ON or -DNO_GUI=ON for console-only)")
endif()

# Include directories
if(NOT BUILD_NO_GUI)
    target_include_directories(RastaConverter PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/color
        ${CMAKE_SOURCE_DIR}/src/rng
        ${CMAKE_SOURCE_DIR}/src/utils
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/app
        ${CMAKE_SOURCE_DIR}/src/app/thirdparty
        ${CMAKE_SOURCE_DIR}/src/frontend/common
        ${CMAKE_SOURCE_DIR}/src/frontend/gui
        ${CMAKE_SOURCE_DIR}/src/frontend/console
    )
endif()
if(BUILD_NO_GUI)
    target_include_directories(RastaConverter-NO_GUI PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/color
        ${CMAKE_SOURCE_DIR}/src/rng
        ${CMAKE_SOURCE_DIR}/src/utils
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/app
        ${CMAKE_SOURCE_DIR}/src/app/thirdparty
        ${CMAKE_SOURCE_DIR}/src/frontend/common
        ${CMAKE_SOURCE_DIR}/src/frontend/console
    )
endif()

# --------------------------------------------------------------------------------------
# Dependencies: prefer system/packaged configs via find_package, with helpful messages.
# We accept *_DIR hints or CMAKE_PREFIX_PATH from config.env/user env.
# --------------------------------------------------------------------------------------

# FreeImage
set(RESOLVED_FREEIMAGE_LIBS "")
set(RESOLVED_FREEIMAGE_INCLUDES "")
find_package(FreeImage CONFIG QUIET)
if(NOT TARGET FreeImage::FreeImage)
    find_package(FreeImage QUIET)
endif()
if(TARGET FreeImage::FreeImage)
    set(RESOLVED_FREEIMAGE_LIBS FreeImage::FreeImage)
    message(STATUS "FreeImage: using imported target FreeImage::FreeImage")
elseif(DEFINED FREEIMAGE_LIBRARIES OR DEFINED FreeImage_LIBRARIES)
    if(DEFINED FREEIMAGE_LIBRARIES)
        set(RESOLVED_FREEIMAGE_LIBS ${FREEIMAGE_LIBRARIES})
        set(RESOLVED_FREEIMAGE_INCLUDES ${FREEIMAGE_INCLUDE_DIRS})
    else()
        set(RESOLVED_FREEIMAGE_LIBS ${FreeImage_LIBRARIES})
        set(RESOLVED_FREEIMAGE_INCLUDES ${FreeImage_INCLUDE_DIRS})
    endif()
    message(STATUS "FreeImage: using libraries from Find module variables")
else()
    # Fallback: try locating header/library via common paths and FREEIMAGE_DIR hint
    find_path(FREEIMAGE_INCLUDE_DIR FreeImage.h
        HINTS ${FREEIMAGE_DIR} ${FREEIMAGE_DIR}/include
        PATHS /usr/include /usr/local/include /opt/homebrew/include
    )
    find_library(FREEIMAGE_LIBRARY NAMES FreeImage freeimage
        HINTS ${FREEIMAGE_DIR} ${FREEIMAGE_DIR}/lib ${FREEIMAGE_DIR}/Dist ${FREEIMAGE_DIR}/Dist/x64 ${FREEIMAGE_DIR}/Dist/x86
        PATHS /usr/lib /usr/local/lib /opt/homebrew/lib
    )
    if(FREEIMAGE_LIBRARY)
        set(RESOLVED_FREEIMAGE_LIBS ${FREEIMAGE_LIBRARY})
        if(FREEIMAGE_INCLUDE_DIR)
            set(RESOLVED_FREEIMAGE_INCLUDES ${FREEIMAGE_INCLUDE_DIR})
        endif()
        message(STATUS "FreeImage: using fallback library path: ${FREEIMAGE_LIBRARY}")
    else()
        message(FATAL_ERROR "FreeImage not found. Install system package (Ubuntu: libfreeimage-dev, macOS: brew install freeimage) or provide via vcpkg or set FREEIMAGE_DIR.")
    endif()
endif()

# SDL2 / SDL2_ttf (required only for GUI builds)
set(RESOLVED_SDL_LIBS "")
set(RESOLVED_SDL_INCLUDES "")
if(NOT BUILD_NO_GUI)
    find_package(SDL2 CONFIG QUIET)
    find_package(SDL2_ttf CONFIG QUIET)
    if(TARGET SDL2::SDL2)
        list(APPEND RESOLVED_SDL_LIBS SDL2::SDL2)
        if(TARGET SDL2::SDL2main)
            list(APPEND RESOLVED_SDL_LIBS SDL2::SDL2main)
        endif()
        message(STATUS "SDL2: using imported targets")
    else()
        find_package(SDL2 QUIET)
        if(SDL2_FOUND AND DEFINED SDL2_LIBRARIES)
            list(APPEND RESOLVED_SDL_LIBS ${SDL2_LIBRARIES})
            set(RESOLVED_SDL_INCLUDES ${SDL2_INCLUDE_DIRS})
            message(STATUS "SDL2: using Find module variables")
        else()
            # Fallback: try locating headers and library manually using SDL2_DIR hint and common paths
            find_path(SDL2_INCLUDE_DIR SDL.h
                HINTS ${SDL2_DIR} ${SDL2_DIR}/include ${SDL2_DIR}/include/SDL2
                PATH_SUFFIXES SDL2
                PATHS /usr/include /usr/local/include /opt/homebrew/include
            )
            find_library(SDL2_LIBRARY NAMES SDL2 SDL2-2.0
                HINTS ${SDL2_DIR} ${SDL2_DIR}/lib ${SDL2_DIR}/lib/x64 ${SDL2_DIR}/lib/x86
                PATHS /usr/lib /usr/local/lib /opt/homebrew/lib
            )
            # SDL2main is optional; try to find it on Windows
            if(WIN32)
                find_library(SDL2MAIN_LIBRARY NAMES SDL2main
                    HINTS ${SDL2_DIR} ${SDL2_DIR}/lib ${SDL2_DIR}/lib/x64 ${SDL2_DIR}/lib/x86
                    PATHS /usr/lib /usr/local/lib
                )
            endif()
            if(SDL2_LIBRARY)
                list(APPEND RESOLVED_SDL_LIBS ${SDL2_LIBRARY})
                if(SDL2MAIN_LIBRARY)
                    list(APPEND RESOLVED_SDL_LIBS ${SDL2MAIN_LIBRARY})
                endif()
                if(SDL2_INCLUDE_DIR)
                    list(APPEND RESOLVED_SDL_INCLUDES ${SDL2_INCLUDE_DIR})
                endif()
                message(STATUS "SDL2: using fallback library path: ${SDL2_LIBRARY}")
            else()
                message(FATAL_ERROR "SDL2 not found. Install system package (Ubuntu: libsdl2-dev, macOS: brew install sdl2) or provide via vcpkg or set SDL2_DIR.")
            endif()
        endif()
    endif()

    if(TARGET SDL2_ttf::SDL2_ttf)
        list(APPEND RESOLVED_SDL_LIBS SDL2_ttf::SDL2_ttf)
        message(STATUS "SDL2_ttf: using imported target")
    else()
        find_package(SDL2_ttf QUIET)
        if(SDL2_TTF_FOUND AND DEFINED SDL2_TTF_LIBRARIES)
            list(APPEND RESOLVED_SDL_LIBS ${SDL2_TTF_LIBRARIES})
            list(APPEND RESOLVED_SDL_INCLUDES ${SDL2_TTF_INCLUDE_DIRS})
            message(STATUS "SDL2_ttf: using Find module variables")
        else()
            # Fallback for SDL2_ttf
            find_path(SDL2_TTF_INCLUDE_DIR SDL_ttf.h
                HINTS ${SDL2_TTF_DIR} ${SDL2_TTF_DIR}/include ${SDL2_TTF_DIR}/include/SDL2
                PATH_SUFFIXES SDL2
                PATHS /usr/include /usr/local/include /opt/homebrew/include
            )
            find_library(SDL2_TTF_LIBRARY NAMES SDL2_ttf
                HINTS ${SDL2_TTF_DIR} ${SDL2_TTF_DIR}/lib ${SDL2_TTF_DIR}/lib/x64 ${SDL2_TTF_DIR}/lib/x86 ${SDL2_DIR}/lib ${SDL2_DIR}/lib/x64 ${SDL2_DIR}/lib/x86
                PATHS /usr/lib /usr/local/lib /opt/homebrew/lib
            )
            if(SDL2_TTF_LIBRARY)
                list(APPEND RESOLVED_SDL_LIBS ${SDL2_TTF_LIBRARY})
                if(SDL2_TTF_INCLUDE_DIR)
                    list(APPEND RESOLVED_SDL_INCLUDES ${SDL2_TTF_INCLUDE_DIR})
                endif()
                message(STATUS "SDL2_ttf: using fallback library path: ${SDL2_TTF_LIBRARY}")
            else()
                message(FATAL_ERROR "SDL2_ttf not found. Install system package (Ubuntu: libsdl2-ttf-dev, macOS: brew install sdl2_ttf) or provide via vcpkg or set SDL2_TTF_DIR.")
            endif()
        endif()
    endif()
endif()

# Static linking configuration
if(BUILD_STATIC)
    # Force static linking for all libraries
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib)
    set(BUILD_SHARED_LIBS OFF)
    
    # Add static linking flags
    if(WIN32)
        # Windows static linking
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:msvcrt")
        target_compile_definitions(RastaConverter PRIVATE FREEIMAGE_LIB)
        if(BUILD_NO_GUI)
            target_compile_definitions(RastaConverter-NO_GUI PRIVATE FREEIMAGE_LIB)
        endif()
    else()
        # Linux/macOS static linking
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
        # Note: Full static linking on Linux requires static versions of all dependencies
        message(STATUS "Static linking enabled - ensure static versions of FreeImage and SDL2 are available")
    endif()
    
    message(STATUS "Static linking enabled")
else()
    message(STATUS "Dynamic linking (default)")
endif()

# Windows-specific compile definitions
if(WIN32)
    if(NOT BUILD_NO_GUI)
        target_compile_definitions(RastaConverter PRIVATE WIN32 _WINDOWS)
        set_target_properties(RastaConverter PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()
    if(BUILD_NO_GUI)
        target_compile_definitions(RastaConverter-NO_GUI PRIVATE WIN32 _WINDOWS NO_GUI)
        set_target_properties(RastaConverter-NO_GUI PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ARCH_DIR "x64")
    else()
        set(ARCH_DIR "x86")
    endif()
else()
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ARCH_DIR "x86_64")
    else()
        set(ARCH_DIR "x86")
    endif()
endif()

# Determine if we are targeting an x86_64 architecture (used to guard AVX2)
set(IS_X86_64 FALSE)
if(CMAKE_SYSTEM_PROCESSOR)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _proc)
    if(_proc MATCHES "x86_64|amd64|x64")
        set(IS_X86_64 TRUE)
    endif()
endif()

# Config-specific defines
if(NOT BUILD_NO_GUI)
    target_compile_definitions(RastaConverter PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG;_SCL_SECURE=0>
        $<$<CONFIG:RelWithDebInfo>:NDEBUG;_SCL_SECURE=0>
        $<$<CONFIG:MinSizeRel>:NDEBUG;_SCL_SECURE=0>
    )
endif()
if(BUILD_NO_GUI)
    target_compile_definitions(RastaConverter-NO_GUI PRIVATE
        NO_GUI
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG;_SCL_SECURE=0>
        $<$<CONFIG:RelWithDebInfo>:NDEBUG;_SCL_SECURE=0>
        $<$<CONFIG:MinSizeRel>:NDEBUG;_SCL_SECURE=0>
    )
endif()

# Compiler options (speed-oriented defaults for Release)
if(MSVC)
    if(NOT BUILD_NO_GUI)
        target_compile_options(RastaConverter PRIVATE
            /W3 /wd4819
            $<$<CONFIG:Debug>:/MDd;/Od;/RTC1>
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/MT;/O2;/Ob2;/Oi;/Ot;/Gy;/Gw;$<IF:$<BOOL:${ENABLE_MSVC_FAST_MATH}>,/fp:fast,>>
            $<$<AND:$<BOOL:${ENABLE_AVX2}>,$<BOOL:${IS_X86_64}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:/arch:AVX2>
        )
        target_link_options(RastaConverter PRIVATE $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/OPT:REF;/OPT:ICF>)
    endif()
    if(BUILD_NO_GUI)
        target_compile_options(RastaConverter-NO_GUI PRIVATE
            /W3 /wd4819
            $<$<CONFIG:Debug>:/MDd;/Od;/RTC1>
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/MT;/O2;/Ob2;/Oi;/Ot;/Gy;/Gw;$<IF:$<BOOL:${ENABLE_MSVC_FAST_MATH}>,/fp:fast,>>
            $<$<AND:$<BOOL:${ENABLE_AVX2}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:/arch:AVX2>
        )
        target_link_options(RastaConverter-NO_GUI PRIVATE $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/OPT:REF;/OPT:ICF>)
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(NOT BUILD_NO_GUI)
        target_compile_options(RastaConverter PRIVATE
            $<$<CONFIG:Debug>:-Wall -Wextra -Wpedantic>
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O3 -march=native>
            $<$<AND:$<BOOL:${ENABLE_FAST_MATH}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-ffast-math>
            $<$<AND:$<BOOL:${ENABLE_DEAD_STRIP}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-ffunction-sections -fdata-sections>
            $<$<AND:$<BOOL:${ENABLE_AVX2}>,$<BOOL:${IS_X86_64}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-mavx2 -mfma>
        )
        if(ENABLE_DEAD_STRIP)
            if(APPLE)
                target_link_options(RastaConverter PRIVATE $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-Wl,-dead_strip>)
            else()
                target_link_options(RastaConverter PRIVATE $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-Wl,--gc-sections -Wl,-O1>)
            endif()
        endif()
    endif()
    if(BUILD_NO_GUI)
        target_compile_options(RastaConverter-NO_GUI PRIVATE
            $<$<CONFIG:Debug>:-Wall -Wextra -Wpedantic>
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O3 -march=native>
            $<$<AND:$<BOOL:${ENABLE_FAST_MATH}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-ffast-math>
            $<$<AND:$<BOOL:${ENABLE_DEAD_STRIP}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-ffunction-sections -fdata-sections>
            $<$<AND:$<BOOL:${ENABLE_AVX2}>,$<BOOL:${IS_X86_64}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-mavx2 -mfma>
        )
        if(ENABLE_DEAD_STRIP)
            if(APPLE)
                target_link_options(RastaConverter-NO_GUI PRIVATE $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-Wl,-dead_strip>)
            else()
                target_link_options(RastaConverter-NO_GUI PRIVATE $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-Wl,--gc-sections -Wl,-O1>)
            endif()
        endif()
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
    if(NOT BUILD_NO_GUI)
        target_compile_options(RastaConverter PRIVATE
            $<$<CONFIG:Debug>:-Wall -Wextra -Wpedantic>
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O3 -march=native>
            $<$<AND:$<BOOL:${ENABLE_FAST_MATH}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-ffast-math>
            $<$<AND:$<BOOL:${ENABLE_DEAD_STRIP}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-ffunction-sections -fdata-sections>
            $<$<AND:$<BOOL:${ENABLE_AVX2}>,$<BOOL:${IS_X86_64}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-mavx2 -mfma>
        )
        if(NOT WIN32)
            if(ENABLE_DEAD_STRIP)
                target_link_options(RastaConverter PRIVATE $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-Wl,--gc-sections -Wl,-O1>)
            endif()
        endif()
    endif()
    if(BUILD_NO_GUI)
        target_compile_options(RastaConverter-NO_GUI PRIVATE
            $<$<CONFIG:Debug>:-Wall -Wextra -Wpedantic>
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O3 -march=native>
            $<$<AND:$<BOOL:${ENABLE_FAST_MATH}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-ffast-math>
            $<$<AND:$<BOOL:${ENABLE_DEAD_STRIP}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-ffunction-sections -fdata-sections>
            $<$<AND:$<BOOL:${ENABLE_AVX2}>,$<BOOL:${IS_X86_64}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-mavx2 -mfma>
        )
        if(NOT WIN32)
            if(ENABLE_DEAD_STRIP)
                target_link_options(RastaConverter-NO_GUI PRIVATE $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-Wl,--gc-sections -Wl,-O1>)
            endif()
        endif()
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(NOT BUILD_NO_GUI)
        target_compile_options(RastaConverter PRIVATE
            $<$<CONFIG:Debug>:-Wall -Wextra -Wpedantic>
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O3 -march=native>
            $<$<AND:$<BOOL:${ENABLE_FAST_MATH}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-ffast-math>
            $<$<AND:$<BOOL:${ENABLE_DEAD_STRIP}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-ffunction-sections -fdata-sections>
            $<$<AND:$<BOOL:${ENABLE_AVX2}>,$<BOOL:${IS_X86_64}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-mavx2 -mfma>
        )
        if(ENABLE_DEAD_STRIP)
            target_link_options(RastaConverter PRIVATE $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-Wl,--gc-sections -Wl,-O1>)
        endif()
    endif()
    if(BUILD_NO_GUI)
        target_compile_options(RastaConverter-NO_GUI PRIVATE
            $<$<CONFIG:Debug>:-Wall -Wextra -Wpedantic>
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O3 -march=native>
            $<$<AND:$<BOOL:${ENABLE_FAST_MATH}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-ffast-math>
            $<$<AND:$<BOOL:${ENABLE_DEAD_STRIP}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-ffunction-sections -fdata-sections>
            $<$<AND:$<BOOL:${ENABLE_AVX2}>,$<BOOL:${IS_X86_64}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:-mavx2 -mfma>
        )
        if(ENABLE_DEAD_STRIP)
            target_link_options(RastaConverter-NO_GUI PRIVATE $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-Wl,--gc-sections -Wl,-O1>)
        endif()
    endif()
endif()

if(ENABLE_UNITY_BUILD)
    if(NOT BUILD_NO_GUI)
        set_target_properties(RastaConverter PROPERTIES UNITY_BUILD ON)
    endif()
    if(BUILD_NO_GUI)
        set_target_properties(RastaConverter-NO_GUI PROPERTIES UNITY_BUILD ON)
    endif()
endif()

# Enable interprocedural optimization (LTO) via CMake IPO if supported
if(ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
    if(ipo_supported)
        if(NOT BUILD_NO_GUI)
            set_property(TARGET RastaConverter PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
            set_property(TARGET RastaConverter PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
        endif()
        if(BUILD_NO_GUI)
            set_property(TARGET RastaConverter-NO_GUI PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
            set_property(TARGET RastaConverter-NO_GUI PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
        endif()
    else()
        message(WARNING "IPO/LTO not supported by this toolchain: ${ipo_output}")
    endif()
endif()

# Apply include dirs if using module variables (imported targets carry includes themselves)
if(RESOLVED_FREEIMAGE_INCLUDES)
    if(NOT BUILD_NO_GUI)
        target_include_directories(RastaConverter PRIVATE ${RESOLVED_FREEIMAGE_INCLUDES})
    endif()
    if(BUILD_NO_GUI)
        target_include_directories(RastaConverter-NO_GUI PRIVATE ${RESOLVED_FREEIMAGE_INCLUDES})
    endif()
endif()
if(RESOLVED_SDL_INCLUDES)
    if(NOT BUILD_NO_GUI)
        target_include_directories(RastaConverter PRIVATE ${RESOLVED_SDL_INCLUDES})
    endif()
    if(BUILD_NO_GUI)
        target_include_directories(RastaConverter-NO_GUI PRIVATE ${RESOLVED_SDL_INCLUDES})
    endif()
endif()

# Link libraries
if(NOT BUILD_NO_GUI)
    target_link_libraries(RastaConverter PRIVATE
        ${RESOLVED_FREEIMAGE_LIBS}
        ${RESOLVED_SDL_LIBS}
        Threads::Threads
        $<$<PLATFORM_ID:Windows>:Dbghelp>
    )
endif()
if(BUILD_NO_GUI)
    target_link_libraries(RastaConverter-NO_GUI PRIVATE
        ${RESOLVED_FREEIMAGE_LIBS}
        Threads::Threads
        $<$<PLATFORM_ID:Windows>:Dbghelp>
    )
endif()

# Output directories (under preset build dir)
if(NOT BUILD_NO_GUI)
    set_target_properties(RastaConverter PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo"
    )
endif()
if(BUILD_NO_GUI)
    set_target_properties(RastaConverter-NO_GUI PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug-NO_GUI"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release-NO_GUI"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/RelWithDebInfo-NO_GUI"
    )
endif()

# Post-build: copy resources (Palettes, Generator, and docs) next to the executables
if(NOT BUILD_NO_GUI)
    set(RC_RUNTIME_COPY_DIR "$<TARGET_FILE_DIR:RastaConverter>")
    add_custom_command(TARGET RastaConverter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Palettes" "${RC_RUNTIME_COPY_DIR}/Palettes"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Generator" "${RC_RUNTIME_COPY_DIR}/Generator"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/GeneratorDual" "${RC_RUNTIME_COPY_DIR}/GeneratorDual"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/src/clacon2.ttf" "${RC_RUNTIME_COPY_DIR}/clacon2.ttf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/ChangeLog.md" "${RC_RUNTIME_COPY_DIR}/ChangeLog.md"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/help.txt" "${RC_RUNTIME_COPY_DIR}/help.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/README.md" "${RC_RUNTIME_COPY_DIR}/README.md"
    )
    if(EXISTS "${CMAKE_SOURCE_DIR}/dlls")
        add_custom_command(TARGET RastaConverter POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/dlls" "${RC_RUNTIME_COPY_DIR}"
        )
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/test.jpg")
        add_custom_command(TARGET RastaConverter POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/test.jpg" "${RC_RUNTIME_COPY_DIR}/test.jpg"
        )
    endif()
endif()
if(BUILD_NO_GUI)
    set(RC_RUNTIME_COPY_DIR_NO_GUI "$<TARGET_FILE_DIR:RastaConverter-NO_GUI>")
    add_custom_command(TARGET RastaConverter-NO_GUI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Palettes" "${RC_RUNTIME_COPY_DIR_NO_GUI}/Palettes"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Generator" "${RC_RUNTIME_COPY_DIR_NO_GUI}/Generator"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/GeneratorDual" "${RC_RUNTIME_COPY_DIR_NO_GUI}/GeneratorDual"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/ChangeLog.md" "${RC_RUNTIME_COPY_DIR_NO_GUI}/ChangeLog.md"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/help.txt" "${RC_RUNTIME_COPY_DIR_NO_GUI}/help.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/README.md" "${RC_RUNTIME_COPY_DIR_NO_GUI}/README.md"
    )
    if(EXISTS "${CMAKE_SOURCE_DIR}/dlls")
        add_custom_command(TARGET RastaConverter-NO_GUI POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/dlls" "${RC_RUNTIME_COPY_DIR_NO_GUI}"
        )
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/test.jpg")
        add_custom_command(TARGET RastaConverter-NO_GUI POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/test.jpg" "${RC_RUNTIME_COPY_DIR_NO_GUI}/test.jpg"
        )
    endif()
endif()

# Optionally copy all runtime DLLs for convenience on Windows (can be noisy under vcpkg)
if(WIN32 AND COPY_ALL_RUNTIME_DLLS AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
    if(NOT BUILD_NO_GUI)
        add_custom_command(TARGET RastaConverter POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:RastaConverter>
                $<TARGET_FILE_DIR:RastaConverter>
            COMMAND_EXPAND_LISTS
        )
    endif()
    if(BUILD_NO_GUI)
        add_custom_command(TARGET RastaConverter-NO_GUI POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:RastaConverter-NO_GUI>
                $<TARGET_FILE_DIR:RastaConverter-NO_GUI>
            COMMAND_EXPAND_LISTS
        )
    endif()
endif()

# Source groups
source_group("Source Files" FILES ${ALL_SOURCE_FILES})
source_group("Header Files" FILES ${HEADER_FILES})

# Install rules (basic)
if(NOT BUILD_NO_GUI)
    install(TARGETS RastaConverter RUNTIME DESTINATION bin)
else()
    install(TARGETS RastaConverter-NO_GUI RUNTIME DESTINATION bin)
endif()
install(FILES README.md ChangeLog.md help.txt DESTINATION share/doc/rasta)
install(DIRECTORY Palettes DESTINATION share/rasta)

# Summary
message(STATUS "=== BUILD CONFIGURATION SUMMARY ===")
if(NOT BUILD_NO_GUI)
    message(STATUS "Main target: RastaConverter (GUI enabled)")
    message(STATUS "Console target: DISABLED (use -DBUILD_NO_GUI=ON to enable console-only)")
else()
    message(STATUS "Main target: RastaConverter-NO_GUI (console only)")
endif()
message(STATUS "Architecture: ${ARCH_DIR}")
message(STATUS "Dependencies:")
message(STATUS "  ✓ FreeImage: ${RESOLVED_FREEIMAGE_LIBS}")
if(NOT BUILD_NO_GUI)
    message(STATUS "  ✓ SDL2/SDL2main: (if present) via imported target or module")
    message(STATUS "  ✓ SDL2_ttf: via imported target or module")
else()
    message(STATUS "  – SDL2/SDL2_ttf: not required (NO_GUI)")
endif()
message(STATUS "Optimizations (Release/RelWithDebInfo): LTO=${ENABLE_LTO}, FAST_MATH=${ENABLE_FAST_MATH}/MSVC_FAST=${ENABLE_MSVC_FAST_MATH}, DEAD_STRIP=${ENABLE_DEAD_STRIP}, AVX2=${ENABLE_AVX2}")
message(STATUS "====================================")


