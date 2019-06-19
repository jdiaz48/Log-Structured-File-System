all: LFS

LFS: LFS.cpp
	g++ -Wall -g -o LFS LFS.cpp

LFS.o: LFS.cpp
	g++ -c LFS.cpp

clean :
	rm -f *.o LFS
