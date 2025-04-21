
make:
	g++ -g -c Wad.cpp -o Wad.o && g++ -g -c file_utils.cpp -o file_utils.o 
	ar cr libWad.a Wad.o file_utils.o

test:
	g++ -g -Wall test_main.cpp file_utils.cpp Wad.cpp -o test_runner
