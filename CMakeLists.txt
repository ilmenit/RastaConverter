cmake_minimum_required(VERSION 3.19)
project(RastaConverter VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define options
option(NO_GUI "Build without GUI" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(THREAD_DEBUG "Enable detailed thread/synchronization debug logs" OFF)
option(SUPPRESS_IMPROVEMENT_LOGS "Suppress 'New best solution' improvement logging" OFF)
option(UI_DEBUG "Enable SDL UI event debug logging" OFF)
option(IGNORE_SDL_QUIT "Ignore SDL_QUIT and ESC (for diagnosing unexpected exits)" OFF)
option(ENABLE_LTO "Enable interprocedural optimization (IPO/LTO) for Release builds" ON)
option(ENABLE_FAST_MATH "Enable fast-math on Release builds for GCC/Clang" ON)
option(ENABLE_DEAD_STRIP "Enable dead code/data elimination in Release (function/data sections + linker GC)" ON)
option(ENABLE_MSVC_FAST_MATH "Use /fp:fast for MSVC/clang-cl in Release" OFF)
option(ENABLE_AVX2 "Enable AVX2 instructions where supported (MSVC only; use at your own risk)" OFF)
option(ENABLE_LLD_LINKER "Use lld linker for faster links (Clang/GCC on ELF); optional" OFF)
option(ENABLE_UNITY_BUILD "Enable unity build for the executable (can improve inlining and compile times)" OFF)
option(COPY_ALL_RUNTIME_DLLS "Copy all dependent runtime DLLs on Windows (may include many transitive DLLs via vcpkg)" OFF)

# Profile-Guided Optimization (PGO)
set(PGO_MODE "OFF" CACHE STRING "PGO mode: OFF, GENERATE, or USE")
set_property(CACHE PGO_MODE PROPERTY STRINGS OFF GENERATE USE)
set(PGO_PROFILE_PATH "" CACHE PATH "Path to PGO profile data directory or file (toolchain-specific)")

# Enable IPO/LTO using CMake property where supported
if(ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_msg)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    else()
        message(STATUS "IPO/LTO not supported: ${ipo_msg}")
    endif()
endif()

# Add custom module path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

# Define cache variables for dependency paths if not already set
if(NOT DEFINED FREEIMAGE_DIR AND DEFINED ENV{FREEIMAGE_DIR})
    set(FREEIMAGE_DIR $ENV{FREEIMAGE_DIR} CACHE PATH "Path to FreeImage library")
endif()

if(NOT DEFINED SDL2_DIR AND DEFINED ENV{SDL2_DIR})
    set(SDL2_DIR $ENV{SDL2_DIR} CACHE PATH "Path to SDL2 library")
endif()

if(NOT DEFINED SDL2_TTF_DIR AND DEFINED ENV{SDL2_TTF_DIR})
    set(SDL2_TTF_DIR $ENV{SDL2_TTF_DIR} CACHE PATH "Path to SDL2_ttf library")
endif()

# Find dependencies: prefer config packages (vcpkg/homebrew/conan), then fall back to custom Find modules
# FreeImage
find_package(FreeImage CONFIG QUIET)
if(NOT TARGET FreeImage::FreeImage)
    find_package(FreeImage REQUIRED)
endif()

# SDL2 and SDL2_ttf
if(NOT NO_GUI)
    find_package(SDL2 CONFIG QUIET)
    find_package(SDL2_ttf CONFIG QUIET)
    if(NOT TARGET SDL2::SDL2)
        find_package(SDL2 REQUIRED)
    endif()
    if(NOT TARGET SDL2_ttf::SDL2_ttf)
        find_package(SDL2_ttf REQUIRED)
    endif()
endif()

# Group source files by directory with correct src/ prefix
set(CORE_SOURCES
    src/main.cpp
    src/main.h
    src/app/RastaConverter.h
    src/app/RastaConverter.ProcessInit.cpp
    src/app/RastaConverter.Init.cpp
    src/app/RastaConverter.MainLoop.cpp
    src/app/RastaConverter.UI.cpp
    src/app/RastaConverter.IO.cpp
    src/app/RastaConverter.OnOff.cpp
    src/app/RastaConverter.Save.cpp
    src/common/pch.cpp
    src/common/pch.h
)

set(COLOR_SOURCES
    src/color/Distance.cpp
    src/color/Distance.h
    src/color/rgb.cpp
    src/color/rgb.h
)

set(RASTER_SOURCES
    src/raster/Program.cpp
    src/raster/Program.h
    src/raster/RegisterState.h
    src/raster/RasterInstruction.h
    src/raster/RasterProgramGenerator.cpp
    src/raster/RasterProgramGenerator.h
    src/raster/OnOffMap.h
)

set(IO_SOURCES
    src/io/ImageProcessor.cpp
    src/io/ImageProcessor.h
    src/io/OutputManager.cpp
    src/io/OutputManager.h
)

set(OPTIMIZATION_SOURCES
    src/optimization/EvaluationContext.cpp
    src/optimization/EvaluationContext.h
    src/optimization/OptimizationController.cpp
    src/optimization/OptimizationController.h
    src/optimization/AcceptancePolicy.cpp
    src/optimization/AcceptancePolicy.h
    src/optimization/CoreEvaluator.cpp
    src/optimization/CoreEvaluator.h
    src/optimization/OptimizationRunner.cpp
    src/optimization/OptimizationRunner.h
    src/optimization/DLAS.cpp
    src/optimization/DLAS.h
    src/optimization/LAHC.cpp
    src/optimization/LAHC.h
    src/optimization/Optimizer.h
)

set(EXECUTION_SOURCES
    src/execution/Executor.cpp
    src/execution/Executor.h
    src/execution/SpriteManager.cpp
    src/execution/SpriteManager.h
)

set(CACHE_SOURCES
    src/cache/LineCache.cpp
    src/cache/LineCache.h
    src/cache/InsnSequenceCache.h
)

set(UTILS_SOURCES
    src/utils/LinearAllocator.h
    src/utils/RandomUtils.h
    src/utils/port.h
    src/utils/string_conv.h
    src/utils/mt19937int.h
    src/utils/mt19937int.cpp
)

set(MUTATION_SOURCES
    src/mutation/Mutator.h
    src/mutation/RasterMutator.cpp
    src/mutation/RasterMutator.h
)

# Choose UI implementation
if(NO_GUI)
    set(UI_SOURCES 
        src/ui/RastaConsole.cpp
        src/ui/RastaConsole.h
        src/ui/gui.h
    )
    add_definitions(-DNO_GUI)
else()
    set(UI_SOURCES 
        src/ui/RastaSDL.cpp
        src/ui/RastaSDL.h
        src/ui/gui.h
    )
endif()

# Collect all sources
set(SOURCES
    ${CORE_SOURCES}
    ${COLOR_SOURCES}
    ${RASTER_SOURCES}
    ${IO_SOURCES}
    ${OPTIMIZATION_SOURCES}
    ${EXECUTION_SOURCES}
    ${CACHE_SOURCES}
    ${UTILS_SOURCES}
    ${MUTATION_SOURCES}
    ${UI_SOURCES}
    src/target/TargetPicture.cpp
    src/target/TargetPicture.h
    src/config/config.cpp
    src/config/config.h
    src/config/CommandLineParser.cpp
    src/config/CommandLineParser.h
)

# Add executable
add_executable(rasta ${SOURCES})

# Set PCH if available (disabled for Windows 7 compatibility to avoid PCH conflicts)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.16 AND NOT WIN32)
    target_precompile_headers(rasta PRIVATE src/common/pch.h)
endif()

# Optional unity build (merges sources to improve inlining and compile time)
if(ENABLE_UNITY_BUILD)
    set_target_properties(rasta PROPERTIES UNITY_BUILD ON)
endif()

# Include directories always include project src
target_include_directories(rasta PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
)

# Link FreeImage using imported target if available, otherwise variables from Find module
if(TARGET FreeImage::FreeImage)
    target_link_libraries(rasta PRIVATE FreeImage::FreeImage)
else()
    target_include_directories(rasta PRIVATE ${FREEIMAGE_INCLUDE_DIRS})
    target_link_libraries(rasta PRIVATE ${FREEIMAGE_LIBRARIES})
endif()

# Compile-time debug controls
if(THREAD_DEBUG)
    target_compile_definitions(rasta PRIVATE THREAD_DEBUG=1)
endif()
if(SUPPRESS_IMPROVEMENT_LOGS)
    target_compile_definitions(rasta PRIVATE SUPPRESS_IMPROVEMENT_LOGS=1)
endif()
if(UI_DEBUG)
    target_compile_definitions(rasta PRIVATE UI_DEBUG=1)
endif()
if(IGNORE_SDL_QUIT)
    target_compile_definitions(rasta PRIVATE IGNORE_SDL_QUIT=1)
endif()

# Add SDL dependencies if GUI is enabled
if(NOT NO_GUI)
    if(TARGET SDL2::SDL2)
        target_link_libraries(rasta PRIVATE SDL2::SDL2)
        if(TARGET SDL2::SDL2main)
            target_link_libraries(rasta PRIVATE SDL2::SDL2main)
        endif()
    else()
        target_include_directories(rasta PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(rasta PRIVATE ${SDL2_LIBRARIES})
    endif()

    if(TARGET SDL2_ttf::SDL2_ttf)
        target_link_libraries(rasta PRIVATE SDL2_ttf::SDL2_ttf)
    else()
        target_include_directories(rasta PRIVATE ${SDL2_TTF_INCLUDE_DIRS})
        target_link_libraries(rasta PRIVATE ${SDL2_TTF_LIBRARIES})
    endif()
endif()

# Compiler warnings and optimization options applied per-compiler via target properties
if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # MSVC cl.exe
    target_compile_options(rasta PRIVATE
        /W4 /permissive-
        $<$<CONFIG:Release,RelWithDebInfo>:/O2 /Ob2 /Oi /Ot /Gy /Gw>
        $<$<AND:$<BOOL:${ENABLE_MSVC_FAST_MATH}>,$<CONFIG:Release,RelWithDebInfo>>:/fp:fast>
        $<$<AND:$<BOOL:${ENABLE_AVX2}>,$<CONFIG:Release,RelWithDebInfo>>:/arch:AVX2>
    )
    target_link_options(rasta PRIVATE
        $<$<CONFIG:Release,RelWithDebInfo>:/OPT:REF /OPT:ICF>
    )
elseif(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # clang-cl on Windows (MSVC frontend)
    target_compile_options(rasta PRIVATE
        /W4
        $<$<CONFIG:Release,RelWithDebInfo>:/O2 /Ob2 /Oi /Ot /Gy>
        $<$<AND:$<BOOL:${ENABLE_MSVC_FAST_MATH}>,$<CONFIG:Release,RelWithDebInfo>>:/fp:fast>
        $<$<AND:$<BOOL:${ENABLE_AVX2}>,$<CONFIG:Release,RelWithDebInfo>>:/arch:AVX2>
    )
    target_link_options(rasta PRIVATE
        $<$<CONFIG:Release,RelWithDebInfo>:/OPT:REF /OPT:ICF>
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Clang on Unix-like systems
    target_compile_options(rasta PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Release,RelWithDebInfo>:-O3 -march=native>
        $<$<AND:$<BOOL:${ENABLE_FAST_MATH}>,$<CONFIG:Release,RelWithDebInfo>>:-ffast-math>
        $<$<AND:$<BOOL:${ENABLE_DEAD_STRIP}>,$<CONFIG:Release,RelWithDebInfo>>:-ffunction-sections -fdata-sections>
    )
    if(ENABLE_LTO)
        # Prefer ThinLTO for faster links with near-equal perf
        target_link_options(rasta PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-flto=thin>)
    endif()
    if(ENABLE_DEAD_STRIP)
        if(APPLE)
            target_link_options(rasta PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-Wl,-dead_strip>)
        else()
            target_link_options(rasta PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-Wl,--gc-sections -Wl,-O1>)
        endif()
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(rasta PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Release,RelWithDebInfo>:-O3 -march=native>
        $<$<AND:$<BOOL:${ENABLE_FAST_MATH}>,$<CONFIG:Release,RelWithDebInfo>>:-ffast-math>
        $<$<AND:$<BOOL:${ENABLE_DEAD_STRIP}>,$<CONFIG:Release,RelWithDebInfo>>:-ffunction-sections -fdata-sections>
    )
    if(ENABLE_DEAD_STRIP)
        target_link_options(rasta PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-Wl,--gc-sections -Wl,-O1>)
    endif()
endif()

# PGO flags
if(PGO_MODE STREQUAL "GENERATE")
    if(MSVC)
        # Instrumented binary for MSVC PGO (requires running workload and re-link with USE)
        target_link_options(rasta PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:/LTCG:PGINSTRUMENT>)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(rasta PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-fprofile-generate>)
        target_link_options(rasta PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-fprofile-generate>)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(rasta PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-fprofile-instr-generate>)
        target_link_options(rasta PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-fprofile-instr-generate>)
    endif()
elseif(PGO_MODE STREQUAL "USE")
    if(MSVC)
        target_link_options(rasta PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:/LTCG:PGOPTIMIZE>)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(rasta PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-fprofile-use -fprofile-correction>)
        target_link_options(rasta PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-fprofile-use>)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        if(PGO_PROFILE_PATH)
            target_compile_options(rasta PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-fprofile-instr-use=${PGO_PROFILE_PATH}>)
            target_link_options(rasta PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-fprofile-instr-use=${PGO_PROFILE_PATH}>)
        else()
            message(WARNING "PGO_MODE=USE set but PGO_PROFILE_PATH not provided for Clang")
        endif()
    endif()
endif()

# Linker selection (lld) for faster links on ELF platforms
if(ENABLE_LLD_LINKER AND NOT WIN32 AND NOT APPLE)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(rasta PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-fuse-ld=lld>)
    endif()
endif()

# Platform-specific settings
if(WIN32)
    # Use Windows executable name 'RastaConverter.exe'
    set_target_properties(rasta PROPERTIES OUTPUT_NAME RastaConverter)

    # Windows 7 compatibility: apply flags only to the actual source compilation
    # This prevents the use of Windows APIs introduced after Windows 7
    if(MSVC)
        # Set Windows 7 compatibility for source files only (not CMake internal files)
        target_compile_definitions(rasta PRIVATE _WIN32_WINNT=0x0601)
        target_link_options(rasta PRIVATE "/SUBSYSTEM:CONSOLE,6.01")
    endif()

    # Add Windows-specific libraries
    target_link_libraries(rasta PRIVATE ws2_32 dbghelp)

    # Copy primary runtime DLLs on Windows: FreeImage, SDL2, SDL2_ttf
    # Prefer explicit variables from Find modules, otherwise fall back to imported targets when available
    if(FREEIMAGE_DLL)
        add_custom_command(TARGET rasta POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FREEIMAGE_DLL}"
            $<TARGET_FILE_DIR:rasta>
        )
    elseif(TARGET FreeImage::FreeImage)
        # Attempt to copy the imported DLL if the library is shared
        get_target_property(_fi_type FreeImage::FreeImage TYPE)
        if(_fi_type STREQUAL "SHARED_LIBRARY")
            add_custom_command(TARGET rasta POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:FreeImage::FreeImage>
                $<TARGET_FILE_DIR:rasta>
            )
        endif()
    endif()

    if(NOT NO_GUI)
        if(SDL2_DLL)
            add_custom_command(TARGET rasta POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SDL2_DLL}"
                $<TARGET_FILE_DIR:rasta>
            )
        elseif(TARGET SDL2::SDL2)
            get_target_property(_sdl2_type SDL2::SDL2 TYPE)
            if(_sdl2_type STREQUAL "SHARED_LIBRARY")
                add_custom_command(TARGET rasta POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:SDL2::SDL2>
                    $<TARGET_FILE_DIR:rasta>
                )
            endif()
        endif()
        
        if(SDL2_TTF_DLL)
            add_custom_command(TARGET rasta POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SDL2_TTF_DLL}"
                $<TARGET_FILE_DIR:rasta>
            )
        elseif(TARGET SDL2_ttf::SDL2_ttf)
            get_target_property(_sdlttf_type SDL2_ttf::SDL2_ttf TYPE)
            if(_sdlttf_type STREQUAL "SHARED_LIBRARY")
                add_custom_command(TARGET rasta POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:SDL2_ttf::SDL2_ttf>
                    $<TARGET_FILE_DIR:rasta>
                )
            endif()
        endif()
    endif()

    # Optionally copy all runtime DLLs discovered via target dependencies (can be noisy with vcpkg)
    # Requires CMake >= 3.21 for $<TARGET_RUNTIME_DLLS:...>
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
        if(COPY_ALL_RUNTIME_DLLS)
            add_custom_command(TARGET rasta POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:rasta>
                    $<TARGET_FILE_DIR:rasta>
                COMMAND_EXPAND_LISTS
            )
        endif()
    endif()
else()
    # Linux-specific settings
    find_package(Threads REQUIRED)
    target_link_libraries(rasta PRIVATE Threads::Threads)
endif()

# Copy font file
if(NOT NO_GUI)
    add_custom_command(TARGET rasta POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/clacon2.ttf"
        $<TARGET_FILE_DIR:rasta>
    )
endif()

# Copy runtime resources
add_custom_command(TARGET rasta POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/Palettes"
    "$<TARGET_FILE_DIR:rasta>/Palettes"
)
add_custom_command(TARGET rasta POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/Generator"
    "$<TARGET_FILE_DIR:rasta>/Generator"
)
add_custom_command(TARGET rasta POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/help.txt"
    "$<TARGET_FILE_DIR:rasta>/help.txt"
)

# Installation rules
install(TARGETS rasta DESTINATION bin)
install(FILES README.md ChangeLog.md help.txt DESTINATION share/doc/rasta)
install(DIRECTORY Palettes DESTINATION share/rasta)
if(NOT NO_GUI)
    install(FILES assets/clacon2.ttf DESTINATION bin)
endif()