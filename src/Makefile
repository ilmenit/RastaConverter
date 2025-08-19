CXX			= g++
CXXFLAGS 	= -O3 -Wall -flto -std=c++17 -Wno-deprecated-declarations -Wno-unused-variable `sdl2-config --cflags` -Iapp -Ifrontend/common -Ifrontend/gui -Ifrontend/console -Iplatform/win -Icolor -Irng -Iutils
LDFLAGS 	=
LIBS	 	= -lSDL2 -lSDL2_ttf -lfreeimage -lX11 -lXcursor -lXpm -lXxf86vm -lpthread
SOURCES 	= app/CommandLineParser.cpp app/config.cpp main.cpp core/rasta.cpp color/rgb.cpp core/mt19937int.cpp core/Evaluator.cpp core/TargetPicture.cpp color/Distance.cpp core/Program.cpp frontend/gui/RastaSDL.cpp frontend/console/RastaConsole.cpp rng/prng_xoroshiro.cpp
OBJECTS 	= $(SOURCES:.cpp=.o)
EXECUTABLE	= rastaconv

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS) 
	$(CXX) $(LDFLAGS) $(CXXFLAGS) $(OBJECTS) -o $@ $(LIBS)

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJECTS): CommandLineParser.h  EpochTypes.h main.h color/rgb.h config.h ErrorConstants.h  rasta.h  utils/string_conv.h rng/prng_xoroshiro.h

clean:
	rm -f *~ $(OBJECTS) $(EXECUTABLE)
