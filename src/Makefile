# Console-only build, for when CMake is not available.
#
# This builds what `cmake -DBUILD_NO_GUI=ON` builds: the converter with no
# window, driven entirely by the command line. It deliberately does not build
# the interactive interface - that needs Dear ImGui, which CMake fetches at
# configure time, and a Makefile pretending to build it would break whenever
# that fetch changed. Use CMake for the GUI; see BUILD.md.
#
# The list below mirrors CMake's COMMON_SOURCE_FILES. Adding a file there means
# adding it here - the previous version of this file listed thirteen of the
# twenty-six and had not built a working program for a long time.

CXX      ?= g++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wno-deprecated-declarations -Wno-unused-variable
CPPFLAGS += -DNO_GUI -I. -Iapp -Iapp/thirdparty -Icolor -Icore \
            -Ifrontend/common -Ifrontend/console -Irng -Iutils
LDLIBS   += -lfreeimage -lpthread

EXECUTABLE = rastaconv

SOURCES = \
	app/CommandLineParser.cpp \
	app/config.cpp \
	app/main.cpp \
	color/ColorCorrection.cpp \
	color/Distance.cpp \
	color/rgb.cpp \
	core/Cycles.cpp \
	core/DetailsMask.cpp \
	core/Evaluator.cpp \
	core/OptimizerState.cpp \
	core/Program.cpp \
	core/RastaDual.cpp \
	core/StructuredSolver.cpp \
	core/TargetBuilder.cpp \
	core/TargetPicture.cpp \
	core/VisualObjective.cpp \
	core/dual/RastaDual_DataIO.cpp \
	core/dual/RastaDual_Display.cpp \
	core/dual/RastaDual_InputTargets.cpp \
	core/dual/RastaDual_MainLoop.cpp \
	core/dual/RastaDual_Tables.cpp \
	core/live/ProgressHistory.cpp \
	core/rasta.cpp \
	frontend/console/RastaConsole.cpp \
	rng/prng_xoroshiro.cpp \
	utils/Interrupt.cpp

OBJECTS = $(SOURCES:.cpp=.o)

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS) $(LDFLAGS) $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(EXECUTABLE)

.PHONY: all clean
