GCCFLAGS= -ansi -Wall -Wunused -pedantic -ggdb
LINKERFLAGS=-lpthread -lm

all:  Ritardatore.exe

Util.o: Util.c 
	gcc -c ${GCCFLAGS}  Util.c

Ritardatore.o:	Ritardatore.c Util.h
	gcc -c ${GCCFLAGS} Ritardatore.c

Ritardatore.exe:	Ritardatore.o Util.o
	gcc -o Ritardatore.exe ${GCCFLAGS} ${LINKERFLAGS} Util.o Ritardatore.o 

clean:	
	rm -f core* *.stackdump
	rm -f *.exe
	rm -f Ritardatore.o Ritardatore.exe
	rm -f Util.o




