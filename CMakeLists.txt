cmake_minimum_required(VERSION 3.14)
project(RastaConverter VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define options
option(NO_GUI "Build without GUI" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# Add custom module path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

# Find dependencies
find_package(FreeImage REQUIRED)
if(NOT NO_GUI)
    find_package(SDL2 REQUIRED)
    find_package(SDL2_ttf REQUIRED)
endif()

# Group source files by directory with correct src/ prefix
set(CORE_SOURCES
    src/main.cpp
    src/main.h
    src/RastaConverter.cpp
    src/RastaConverter.h
    src/config.cpp
    src/config.h
    src/string_conv.h
    src/mt19937int.cpp
    src/mt19937int.h
    src/port.h
    src/TargetPicture.cpp
    src/TargetPicture.h
    src/OnOffMap.h
    src/gui.h
    src/pch.cpp
    src/pch.h
    src/CommandLineParser.cpp
    src/CommandLineParser.h
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
    src/optimization/DLAS.cpp
    src/optimization/DLAS.h
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
    )
    add_definitions(-DNO_GUI)
else()
    set(UI_SOURCES 
        src/ui/RastaSDL.cpp
        src/ui/RastaSDL.h
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
)

# Add executable
add_executable(rasta ${SOURCES})

# Set PCH if available
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.16)
    target_precompile_headers(rasta PRIVATE src/pch.h)
endif()

# Include directories
target_include_directories(rasta PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FREEIMAGE_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(rasta PRIVATE ${FREEIMAGE_LIBRARIES})

# Add SDL dependencies if GUI is enabled
if(NOT NO_GUI)
    target_include_directories(rasta PRIVATE
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
    )
    target_link_libraries(rasta PRIVATE
        ${SDL2_LIBRARIES}
        ${SDL2_TTF_LIBRARIES}
    )
endif()

# Platform-specific settings
if(WIN32)
    # Add Windows-specific libraries
    target_link_libraries(rasta PRIVATE ws2_32)

    # Copy DLLs on Windows
    if(FREEIMAGE_DLL)
        add_custom_command(TARGET rasta POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FREEIMAGE_DLL}"
            $<TARGET_FILE_DIR:rasta>
        )
    endif()
    
    if(NOT NO_GUI)
        if(SDL2_DLL)
            add_custom_command(TARGET rasta POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SDL2_DLL}"
                $<TARGET_FILE_DIR:rasta>
            )
        endif()
        
        if(SDL2_TTF_DLL)
            add_custom_command(TARGET rasta POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SDL2_TTF_DLL}"
                $<TARGET_FILE_DIR:rasta>
            )
        endif()
    endif()
endif()

# Copy font file
if(NOT NO_GUI)
    add_custom_command(TARGET rasta POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/clacon2.ttf"
        $<TARGET_FILE_DIR:rasta>
    )
endif()

# Installation rules
install(TARGETS rasta DESTINATION bin)
install(FILES README.md LICENSE DESTINATION share/doc/rasta)
install(DIRECTORY palettes DESTINATION share/rasta)
if(NOT NO_GUI)
    install(FILES assets/clacon2.ttf DESTINATION bin)
endif()