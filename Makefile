CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic

.PHONY: all clean

all: wadsrv-bin

wadsrv-bin: wadsvr/main.cpp libWad/Wad.cpp libWad/Wad.h libWad/WadStructure.h libWad/file_utils.cpp libWad/file_utils.h
	$(CXX) $(CXXFLAGS) -IlibWad wadsvr/main.cpp libWad/Wad.cpp libWad/file_utils.cpp -o wadsrv-bin

clean:
	rm -f wadsrv-bin
