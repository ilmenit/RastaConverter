CXX			= g++
CXXFLAGS 	= -O3 -Wall -flto -std=c++17 -Wno-deprecated-declarations -Wno-unused-variable `sdl2-config --cflags` -Iapp -Ifrontend/common -Ifrontend/gui -Ifrontend/console -Iplatform -Iplatform/win -Icolor -Irng -Iutils -Icore
LDFLAGS 	=
LIBS	 	= -lSDL2 -lSDL2_ttf -lfreeimage -lX11 -lXcursor -lXpm -lXxf86vm -lpthread
SOURCES 	= app/CommandLineParser.cpp app/config.cpp app/main.cpp core/rasta.cpp color/rgb.cpp core/Evaluator.cpp core/TargetPicture.cpp color/Distance.cpp core/Program.cpp frontend/gui/RastaSDL.cpp frontend/console/RastaConsole.cpp rng/prng_xoroshiro.cpp
OBJECTS 	= $(SOURCES:.cpp=.o)
EXECUTABLE	= rastaconv

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS) 
	$(CXX) $(LDFLAGS) $(CXXFLAGS) $(OBJECTS) -o $@ $(LIBS)

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *~ $(OBJECTS) $(EXECUTABLE)
