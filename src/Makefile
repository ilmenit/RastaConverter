CXX			= g++
CXXFLAGS 	= -O3 -Wall -flto -Wno-deprecated-declarations -Wno-unused-variable
LDFLAGS 	=
LIBS	 	= -lalleg -lfreeimage -lX11 -lXcursor -lXpm -lXxf86vm -lpthread
SOURCES 	= CommandLineParser.cpp  config.cpp  main.cpp  rasta.cpp  rgb.cpp mt19937int.cpp Evaluator.cpp TargetPicture.cpp Distance.cpp Program.cpp
OBJECTS 	= $(SOURCES:.cpp=.o)
EXECUTABLE	= rastaconv

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS) 
	$(CXX) $(LDFLAGS) $(CXXFLAGS) $(OBJECTS) -o $@ $(LIBS)

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJECTS): CommandLineParser.h  EpochTypes.h main.h rgb.h config.h ErrorConstants.h  rasta.h  string_conv.h

clean:
	rm -f *~ $(OBJECTS) $(EXECUTABLE)
