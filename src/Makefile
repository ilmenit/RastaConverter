CXX			= g++
CXXFLAGS 	= -c -O3 -std=c++0x 
LDFLAGS 	=
LIBS	 	= -lalleg -lfreeimage -lX11 -lXcursor -lXpm -lXxf86vm -lpthread
SOURCES 	= CommandLineParser.cpp  config.cpp  main.cpp  rasta.cpp  rgb.cpp
OBJECTS 	= $(SOURCES:.cpp=.o)
EXECUTABLE	= rastaconv

all: $(EXECUTABLE)
		
$(EXECUTABLE): $(OBJECTS) 
	$(CXX) $(LDFLAGS) $(OBJECTS) -o $@ $(LIBS)

.cpp.o:
	$(CXX) $(CXXFLAGS) $< -o $@

$(OBJECTS): CommandLineParser.h  EpochTypes.h main.h rgb.h config.h ErrorConstants.h  rasta.h  string_conv.h

clean:
	rm -f *~ $(OBJECTS) $(EXECUTABLE)
