cmake_minimum_required(VERSION 3.21)

project(RastaConverter)

include(CTest)
include(FetchContent)

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
option(ENABLE_NATIVE_ARCH "Tune Release builds for the building machine's CPU" OFF)
set(RC_ARCH_BASELINE "x86-64-v2" CACHE STRING
    "Instruction-set baseline for portable x86-64 Release builds")
option(ENABLE_UNITY_BUILD "Enable unity build for faster compiles" OFF)
option(ENABLE_LINE_LRU_DIAGNOSTICS "Restore retired line-order bookkeeping for regression diagnosis" OFF)
option(COPY_ALL_RUNTIME_DLLS "Copy all dependent runtime DLLs on Windows" OFF)
option(BUILD_STATIC "Build with static linking for easier distribution" OFF)
option(FETCH_SDL3 "Always fetch SDL3 from source (skip find_package)" OFF)
option(FETCH_SDL3_TTF "Always fetch SDL3_ttf from source (skip find_package)" OFF)
option(ENABLE_LIVE_UI "Build the Dear ImGui live setup and dashboard for GUI targets" ON)

if(NO_GUI)
    set(BUILD_NO_GUI ON)
endif()

# The C runtime every target links, decided in one place and before any target
# is created. Per-target /MT is not enough: our ImGui library and everything
# vcpkg builds would keep the dynamic default, and mixing the two fails to link
# with unresolved __imp_ symbols.
if(MSVC AND BUILD_STATIC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# What CPU the Release build targets.
#
# This used to be -march=native unconditionally, which is right for a binary you
# build for yourself and wrong for one you hand to anyone else: it emits
# whatever the build machine supports, so a release built on a recent desktop
# faults with an illegal instruction on an older one. Portable by default,
# native on request.
if(ENABLE_NATIVE_ARCH)
    set(RC_ARCH_FLAG "-march=native")
    message(STATUS "Architecture: native (this machine only - not for releases)")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(RC_ARCH_FLAG "-march=${RC_ARCH_BASELINE}")
    message(STATUS "Architecture: ${RC_ARCH_BASELINE} (portable)")
else()
    set(RC_ARCH_FLAG "")
    message(STATUS "Architecture: compiler default")
endif()

# Console-only builds never need GUI implementation dependencies. FORCE is
# intentional: it also makes switching an existing build tree to NO_GUI safe.
if(BUILD_NO_GUI AND ENABLE_LIVE_UI)
    message(STATUS "BUILD_NO_GUI=ON: disabling ENABLE_LIVE_UI")
    set(ENABLE_LIVE_UI OFF CACHE BOOL
        "Build the Dear ImGui live setup and dashboard for GUI targets" FORCE)
endif()

# --------------------------------------------------------------------------------------
# Read optional config.env (root preferred, then src/) to accept user-provided hints
# Supported keys: FREEIMAGE_DIR, SDL3_DIR, SDL3_TTF_DIR. These will be used as hints
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
if(SDL3_DIR)
    set(CMAKE_PREFIX_PATH "${SDL3_DIR};${CMAKE_PREFIX_PATH}")
endif()
if(SDL3_TTF_DIR)
    set(CMAKE_PREFIX_PATH "${SDL3_TTF_DIR};${CMAKE_PREFIX_PATH}")
endif()

# Source files (now prefixed with src/)
set(COMMON_SOURCE_FILES
    src/app/CommandLineParser.cpp
    src/app/config.cpp
    src/color/Distance.cpp
    src/color/ColorCorrection.cpp
    src/core/Evaluator.cpp
    src/core/DetailsMask.cpp
    src/core/OptimizerState.cpp
    src/core/VisualObjective.cpp
    src/core/live/ProgressHistory.cpp
    src/app/main.cpp
    src/utils/Interrupt.cpp
    src/utils/Utf8Path.cpp
    src/utils/FreeImageIO.cpp
    src/rng/prng_xoroshiro.cpp
    src/core/Program.cpp
    src/core/Cycles.cpp
    src/core/StructuredSolver.cpp
    src/core/rasta.cpp
    src/core/RastaDual.cpp
    src/core/dual/RastaDual_Display.cpp
    src/core/dual/RastaDual_Tables.cpp
    src/core/dual/RastaDual_DataIO.cpp
    src/core/dual/RastaDual_InputTargets.cpp
    src/core/dual/RastaDual_MainLoop.cpp
    src/frontend/console/RastaConsole.cpp
    src/color/rgb.cpp
    src/core/TargetPicture.cpp
    src/core/TargetBuilder.cpp
)

set(GUI_SOURCE_FILES
    src/frontend/gui/RastaSDL.cpp
    src/frontend/gui/WindowIconHelper.cpp
)

if(ENABLE_LIVE_UI AND NOT BUILD_NO_GUI)
    list(APPEND GUI_SOURCE_FILES
        src/frontend/gui/live_ui/LiveUI.cpp
        src/frontend/gui/live_ui/LiveTheme.cpp
        src/frontend/gui/live_ui/FileDialog.cpp
        src/frontend/gui/live_ui/ConfigModel.cpp
        src/frontend/gui/live_ui/TargetPreview.cpp
        src/frontend/gui/live_ui/ImageViewer.cpp
        src/frontend/gui/live_ui/SetupScreen.cpp
        src/frontend/gui/live_ui/SetupSections.cpp
        src/frontend/gui/live_ui/Dashboard.cpp
        src/frontend/gui/live_ui/PaintEditor.cpp
        src/frontend/gui/live_ui/PaintEditorPanels.cpp
        src/frontend/gui/live_ui/RecentRuns.cpp
        src/frontend/gui/live_ui/RecentGallery.cpp
        src/frontend/gui/live_ui/Desktop.cpp
        src/frontend/gui/live_ui/XexBuild.cpp
    )
endif()

if(APPLE)
    list(APPEND GUI_SOURCE_FILES src/frontend/gui/WindowIconHelper_Mac.cpp)
elseif(WIN32)
    list(APPEND GUI_SOURCE_FILES src/frontend/gui/WindowIconHelper_Win32.cpp)
endif()

set(ALL_SOURCE_FILES ${COMMON_SOURCE_FILES} ${GUI_SOURCE_FILES})

if(BUILD_TESTING)
    add_executable(OptimizerStateTests
        tests/OptimizerStateTests.cpp
        src/core/OptimizerState.cpp
    )
    target_include_directories(OptimizerStateTests PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
    add_test(NAME OptimizerStateTests COMMAND OptimizerStateTests)

    add_executable(LineCacheTests
        tests/LineCacheTests.cpp
    )
    target_include_directories(LineCacheTests PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/color
    )
    add_test(NAME LineCacheTests COMMAND LineCacheTests)

    add_executable(TimingModelTests
        tests/TimingModelTests.cpp
        src/core/Cycles.cpp
        src/core/Program.cpp
    )
    target_include_directories(TimingModelTests PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/color
    )
    add_test(NAME TimingModelTests COMMAND TimingModelTests)

    add_executable(StructuredSolverTests
        tests/StructuredSolverTests.cpp
        src/core/StructuredSolver.cpp
		src/core/VisualObjective.cpp
        src/core/Cycles.cpp
        src/core/Program.cpp
    )
    target_include_directories(StructuredSolverTests PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/color
    )
    add_test(NAME StructuredSolverTests COMMAND StructuredSolverTests)

    add_executable(VisualObjectiveTests
        tests/VisualObjectiveTests.cpp
        src/core/VisualObjective.cpp
    )
    target_include_directories(VisualObjectiveTests PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/color
    )
    add_test(NAME VisualObjectiveTests COMMAND VisualObjectiveTests)

endif()

set(HEADER_FILES
    src/app/CommandLineParser.h
    src/app/config.h
    src/color/Distance.h
    src/color/ColorCorrection.h
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
    src/core/StructuredSolver.h
    src/rng/prng_xoroshiro.h
    src/core/rasta.h
    src/color/rgb.h
    src/utils/string_conv.h
    src/core/TargetPicture.h
    src/core/VisualObjective.h
    src/core/DetailsMask.h
    src/core/live/LiveTunables.h
    src/core/live/ProgressHistory.h
)

# Targets
if(BUILD_NO_GUI)
    # Console-only build (no SDL3/GUI)
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

if(ENABLE_LINE_LRU_DIAGNOSTICS)
    if(TARGET RastaConverter)
        target_compile_definitions(RastaConverter PRIVATE RASTA_TRACK_LINE_LRU=1)
    endif()
    if(TARGET RastaConverter-NO_GUI)
        target_compile_definitions(RastaConverter-NO_GUI PRIVATE RASTA_TRACK_LINE_LRU=1)
    endif()
    message(STATUS "Retired line-order diagnostics: ENABLED")
else()
    message(STATUS "Retired line-order diagnostics: disabled")
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

if(BUILD_TESTING)
    add_executable(CommandLineParserTests
        tests/CommandLineParserTests.cpp
        src/app/CommandLineParser.cpp
    )
    target_include_directories(CommandLineParserTests PRIVATE ${CMAKE_SOURCE_DIR}/src/app)
    add_test(NAME CommandLineParserTests COMMAND CommandLineParserTests)

    add_executable(DetailsMaskTests
        tests/DetailsMaskTests.cpp
        src/core/DetailsMask.cpp
        src/utils/FreeImageIO.cpp
        src/utils/Utf8Path.cpp
    )
    target_include_directories(DetailsMaskTests PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/color
        ${CMAKE_SOURCE_DIR}/src/utils
        ${RESOLVED_FREEIMAGE_INCLUDES}
    )
    target_link_libraries(DetailsMaskTests PRIVATE ${RESOLVED_FREEIMAGE_LIBS})
    add_test(NAME DetailsMaskTests COMMAND DetailsMaskTests
        ${CMAKE_CURRENT_BINARY_DIR}/details-mask-test.png)
endif()

# SDL3 / SDL3_ttf (required only for GUI builds)
#
# Dependency resolution strategy (matches the sibling AltirraSDL project):
# prefer a system/vcpkg-installed copy for faster configure/build; if none is
# found, automatically fetch and build the missing piece from source so a GUI
# build works out-of-the-box without the user having to hunt down and install
# a matching devel package (e.g. distros that ship SDL3 but not SDL3_ttf).
# Force a from-source build of either with -DFETCH_SDL3=ON / -DFETCH_SDL3_TTF=ON.
set(RESOLVED_SDL_LIBS "")
set(RESOLVED_SDL_INCLUDES "")
set(SDL3_WAS_FETCHED FALSE)
set(SDL3_TTF_WAS_FETCHED FALSE)
if(NOT BUILD_NO_GUI)
    if(FETCH_SDL3)
        message(STATUS "FETCH_SDL3=ON - skipping find_package(SDL3)")
    else()
        find_package(SDL3 3.2 CONFIG QUIET)
    endif()

    if(TARGET SDL3::SDL3)
        list(APPEND RESOLVED_SDL_LIBS SDL3::SDL3)
        message(STATUS "SDL3: using imported target")
    else()
        find_path(SDL3_INCLUDE_DIR SDL3/SDL.h HINTS ${SDL3_DIR} ${SDL3_DIR}/include PATHS /usr/include /usr/local/include /opt/homebrew/include)
        find_library(SDL3_LIBRARY NAMES SDL3 HINTS ${SDL3_DIR} ${SDL3_DIR}/lib ${SDL3_DIR}/lib/x64 ${SDL3_DIR}/lib/x86 PATHS /usr/lib /usr/local/lib /opt/homebrew/lib)
        if(NOT FETCH_SDL3 AND SDL3_LIBRARY AND SDL3_INCLUDE_DIR)
            list(APPEND RESOLVED_SDL_LIBS ${SDL3_LIBRARY})
            list(APPEND RESOLVED_SDL_INCLUDES ${SDL3_INCLUDE_DIR})
            message(STATUS "SDL3: using fallback library path: ${SDL3_LIBRARY}")
        else()
            set(_RASTA_SDL3_VERSION "release-3.2.30")
            message(STATUS "No usable system SDL3 found - fetching SDL3 ${_RASTA_SDL3_VERSION} from source")
            message(STATUS "  (to avoid this, install SDL3 and set SDL3_DIR or CMAKE_PREFIX_PATH)")

            set(SDL_SHARED ON  CACHE BOOL "Build SDL3 shared library" FORCE)
            set(SDL_STATIC OFF CACHE BOOL "Build SDL3 static library" FORCE)
            set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL3 test library" FORCE)
            set(SDL_TESTS         OFF CACHE BOOL "Build SDL3 tests" FORCE)
            set(SDL_EXAMPLES      OFF CACHE BOOL "Build SDL3 examples" FORCE)
            set(SDL_DISABLE_INSTALL ON CACHE BOOL "Skip SDL3 install rules" FORCE)

            FetchContent_Declare(
                sdl3
                GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                GIT_TAG        ${_RASTA_SDL3_VERSION}
                GIT_SHALLOW    TRUE
            )
            FetchContent_MakeAvailable(sdl3)

            if(NOT TARGET SDL3::SDL3)
                message(FATAL_ERROR "SDL3 FetchContent completed but SDL3::SDL3 target not found.")
            endif()
            list(APPEND RESOLVED_SDL_LIBS SDL3::SDL3)
            set(SDL3_WAS_FETCHED TRUE)
            message(STATUS "SDL3 ${_RASTA_SDL3_VERSION} ready (built from source)")
        endif()
    endif()

    # If SDL3 itself came from FetchContent, a system SDL3_ttf would link
    # against a different SDL3 than the one we're building against - fetch
    # SDL3_ttf from source too in that case (mirrors SDL3_image's handling
    # in AltirraSDL's CMakeLists.txt for the same reason).
    if(FETCH_SDL3_TTF OR SDL3_WAS_FETCHED)
        message(STATUS "Skipping find_package(SDL3_ttf) (FETCH_SDL3_TTF, or SDL3 was fetched)")
    else()
        find_package(SDL3_ttf CONFIG QUIET)
    endif()

    if(TARGET SDL3_ttf::SDL3_ttf)
        list(APPEND RESOLVED_SDL_LIBS SDL3_ttf::SDL3_ttf)
        message(STATUS "SDL3_ttf: using imported target")
    else()
        find_path(SDL3_TTF_INCLUDE_DIR SDL3_ttf/SDL_ttf.h HINTS ${SDL3_TTF_DIR} ${SDL3_TTF_DIR}/include PATHS /usr/include /usr/local/include /opt/homebrew/include)
        find_library(SDL3_TTF_LIBRARY NAMES SDL3_ttf HINTS ${SDL3_TTF_DIR} ${SDL3_TTF_DIR}/lib ${SDL3_TTF_DIR}/lib/x64 ${SDL3_DIR}/lib ${SDL3_DIR}/lib/x64 PATHS /usr/lib /usr/local/lib /opt/homebrew/lib)
        if(NOT FETCH_SDL3_TTF AND NOT SDL3_WAS_FETCHED AND SDL3_TTF_LIBRARY AND SDL3_TTF_INCLUDE_DIR)
            list(APPEND RESOLVED_SDL_LIBS ${SDL3_TTF_LIBRARY})
            list(APPEND RESOLVED_SDL_INCLUDES ${SDL3_TTF_INCLUDE_DIR})
            message(STATUS "SDL3_ttf: using fallback library path: ${SDL3_TTF_LIBRARY}")
        else()
            set(_RASTA_SDL3_TTF_VERSION "release-3.2.2")
            message(STATUS "No usable system SDL3_ttf found - fetching SDL3_ttf ${_RASTA_SDL3_TTF_VERSION} from source")
            message(STATUS "  (to avoid this, install SDL3_ttf and set SDL3_TTF_DIR or CMAKE_PREFIX_PATH)")

            # Vendored FreeType only: skip harfbuzz (complex text shaping) and
            # plutosvg (color emoji) - RastaConverter only renders plain stats
            # text, and this keeps the from-source build fast with no extra
            # system devel packages required.
            set(SDLTTF_VENDORED ON  CACHE BOOL "" FORCE)
            set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
            set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
            set(SDLTTF_SAMPLES  OFF CACHE BOOL "" FORCE)
            set(SDLTTF_INSTALL  OFF CACHE BOOL "" FORCE)

            FetchContent_Declare(
                sdl3_ttf
                GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
                GIT_TAG        ${_RASTA_SDL3_TTF_VERSION}
                GIT_SHALLOW    TRUE
            )
            FetchContent_MakeAvailable(sdl3_ttf)

            if(NOT TARGET SDL3_ttf::SDL3_ttf)
                message(FATAL_ERROR "SDL3_ttf FetchContent completed but SDL3_ttf::SDL3_ttf target not found.")
            endif()
            list(APPEND RESOLVED_SDL_LIBS SDL3_ttf::SDL3_ttf)
            set(SDL3_TTF_WAS_FETCHED TRUE)
            message(STATUS "SDL3_ttf ${_RASTA_SDL3_TTF_VERSION} ready (built from source)")
        endif()
    endif()
endif()

if(ENABLE_LIVE_UI AND NOT BUILD_NO_GUI)
    set(_RASTA_IMGUI_VERSION "v1.91.9b")
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        ${_RASTA_IMGUI_VERSION}
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(imgui)
    add_library(RastaImGui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
    )
    add_library(ImGui::ImGui ALIAS RastaImGui)
    target_include_directories(RastaImGui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_include_directories(RastaImGui PRIVATE ${RESOLVED_SDL_INCLUDES})
    target_link_libraries(RastaImGui PUBLIC ${RESOLVED_SDL_LIBS})
    if(NOT TARGET ImGui::ImGui)
        message(FATAL_ERROR "Dear ImGui fetch completed but ImGui::ImGui target was not created.")
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
        if(MSVC)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:msvcrt")
        endif()
        if(NOT BUILD_NO_GUI)
            target_compile_definitions(RastaConverter PRIVATE FREEIMAGE_LIB)
        endif()
        if(BUILD_NO_GUI)
            target_compile_definitions(RastaConverter-NO_GUI PRIVATE FREEIMAGE_LIB)
        endif()
    else()
        # Linux/macOS static linking
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
        # Note: Full static linking on Linux requires static versions of all dependencies
        message(STATUS "Static linking enabled - ensure static versions of FreeImage and SDL3 are available")
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
    if(CMAKE_SYSTEM_PROCESSOR)
        set(ARCH_DIR "${CMAKE_SYSTEM_PROCESSOR}")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ARCH_DIR "unknown-64-bit")
    else()
        set(ARCH_DIR "unknown-32-bit")
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
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/O2;/Ob2;/Oi;/Ot;/Gy;/Gw;$<IF:$<BOOL:${ENABLE_MSVC_FAST_MATH}>,/fp:fast,>>
            $<$<AND:$<BOOL:${ENABLE_AVX2}>,$<BOOL:${IS_X86_64}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:/arch:AVX2>
        )
        target_link_options(RastaConverter PRIVATE $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/OPT:REF;/OPT:ICF>)
    endif()
    if(BUILD_NO_GUI)
        target_compile_options(RastaConverter-NO_GUI PRIVATE
            /W3 /wd4819
            $<$<CONFIG:Debug>:/MDd;/Od;/RTC1>
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/O2;/Ob2;/Oi;/Ot;/Gy;/Gw;$<IF:$<BOOL:${ENABLE_MSVC_FAST_MATH}>,/fp:fast,>>
            $<$<AND:$<BOOL:${ENABLE_AVX2}>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>:/arch:AVX2>
        )
        target_link_options(RastaConverter-NO_GUI PRIVATE $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/OPT:REF;/OPT:ICF>)
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(NOT BUILD_NO_GUI)
        target_compile_options(RastaConverter PRIVATE
            $<$<CONFIG:Debug>:-Wall -Wextra -Wpedantic>
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O3 ${RC_ARCH_FLAG}>
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
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O3 ${RC_ARCH_FLAG}>
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
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O3 ${RC_ARCH_FLAG}>
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
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O3 ${RC_ARCH_FLAG}>
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
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O3 ${RC_ARCH_FLAG}>
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
            $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O3 ${RC_ARCH_FLAG}>
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
        $<$<PLATFORM_ID:Windows>:Dbghelp;shell32>
    )
    if(ENABLE_LIVE_UI)
        target_compile_definitions(RastaConverter PRIVATE RASTA_ENABLE_LIVE_UI=1)
        target_link_libraries(RastaConverter PRIVATE ImGui::ImGui)
    endif()

    if(APPLE)
        # Needed for Objective-C runtime and CoreGraphics APIs used in WindowIconHelper
        target_link_libraries(RastaConverter PRIVATE
            "-framework AppKit"
            "-framework CoreGraphics"
        )
    endif()
endif()
if(BUILD_NO_GUI)
    target_link_libraries(RastaConverter-NO_GUI PRIVATE
        ${RESOLVED_FREEIMAGE_LIBS}
        Threads::Threads
        $<$<PLATFORM_ID:Windows>:Dbghelp;shell32>
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
# Runtime resources live beside the executable, matching the release zip and
# the paths used by the program. A split bin/share install left the installed
# GUI without its font and made palettes and XEX generators undiscoverable.
install(DIRECTORY Palettes DESTINATION bin)
install(DIRECTORY Generator GeneratorDual DESTINATION bin
    USE_SOURCE_PERMISSIONS)
if(NOT BUILD_NO_GUI)
    install(FILES src/clacon2.ttf DESTINATION bin)
endif()

# Summary
message(STATUS "=== BUILD CONFIGURATION SUMMARY ===")
if(NOT BUILD_NO_GUI)
    message(STATUS "Main target: RastaConverter (GUI enabled)")
    if(ENABLE_LIVE_UI)
        message(STATUS "Live UI: ENABLED (opens with no arguments or /livegui)")
    else()
        message(STATUS "Live UI: disabled (ENABLE_LIVE_UI=OFF)")
    endif()
    message(STATUS "Console target: DISABLED (use -DBUILD_NO_GUI=ON to enable console-only)")
else()
    message(STATUS "Main target: RastaConverter-NO_GUI (console only)")
    message(STATUS "Live UI: not applicable (console-only)")
endif()
message(STATUS "Target architecture: ${ARCH_DIR}")
message(STATUS "Dependencies:")
message(STATUS "  ✓ FreeImage: ${RESOLVED_FREEIMAGE_LIBS}")
if(NOT BUILD_NO_GUI)
    if(SDL3_WAS_FETCHED)
        message(STATUS "  ✓ SDL3: fetched from source")
    else()
        message(STATUS "  ✓ SDL3: via imported target or fallback path")
    endif()
    if(SDL3_TTF_WAS_FETCHED)
        message(STATUS "  ✓ SDL3_ttf: fetched from source")
    else()
        message(STATUS "  ✓ SDL3_ttf: via imported target or fallback path")
    endif()
else()
    message(STATUS "  – SDL3/SDL3_ttf: not required (NO_GUI)")
endif()
if(ENABLE_AVX2 AND IS_X86_64)
    set(AVX2_STATUS "ON (active for x86_64 target)")
elseif(ENABLE_AVX2)
    set(AVX2_STATUS "ON (configured, inactive for non-x86_64 target)")
else()
    set(AVX2_STATUS "OFF")
endif()
message(STATUS "Optimizations (Release/RelWithDebInfo): LTO=${ENABLE_LTO}, FAST_MATH=${ENABLE_FAST_MATH}/MSVC_FAST=${ENABLE_MSVC_FAST_MATH}, DEAD_STRIP=${ENABLE_DEAD_STRIP}, AVX2=${AVX2_STATUS}")
message(STATUS "====================================")
