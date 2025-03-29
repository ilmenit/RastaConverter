cmake_minimum_required(VERSION 3.10)
project(RastaConverter VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add cmake directory to module path
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Options
option(USE_GUI "Build with GUI support (requires SDL2)" ON)

# Find dependencies
find_package(FreeImage REQUIRED)
find_package(Threads REQUIRED)

if(USE_GUI)
    find_package(SDL2 REQUIRED)
    find_package(SDL2_ttf REQUIRED)
endif()

# Define source files
set(COMMON_SOURCES
    src/main.cpp
    src/CommandLineParser.cpp
    src/config.cpp
    src/color/Distance.cpp
    src/color/rgb.cpp
    src/raster/Program.cpp
    src/execution/Executor.cpp
    src/execution/SpriteManager.cpp
    src/cache/LineCache.cpp
    src/mutation/RasterMutator.cpp
    src/optimization/EvaluationContext.cpp
    src/optimization/DLAS.cpp
    src/mt19937int.cpp
    src/pch.cpp
    src/rasta.cpp
    src/TargetPicture.cpp
)

# GUI-specific sources
if(USE_GUI)
    set(GUI_SOURCES
        src/ui/RastaSDL.cpp
    )
else()
    set(GUI_SOURCES
        src/ui/RastaConsole.cpp
    )
    add_definitions(-DNO_GUI)
endif()

# Create executable
add_executable(rasta ${COMMON_SOURCES} ${GUI_SOURCES})

# Include directories
target_include_directories(rasta PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FREEIMAGE_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(rasta PRIVATE
    ${FREEIMAGE_LIBRARIES}
    Threads::Threads
)

if(USE_GUI)
    target_include_directories(rasta PRIVATE
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
    )
    target_link_libraries(rasta PRIVATE
        ${SDL2_LIBRARIES}
        ${SDL2_TTF_LIBRARIES}
    )
endif()

# Precompiled headers
target_precompile_headers(rasta PRIVATE src/pch.h)

# Install rules
install(TARGETS rasta DESTINATION bin)
install(FILES README.md LICENSE DESTINATION share/doc/rasta)

# Data files
install(DIRECTORY palettes DESTINATION share/rasta)