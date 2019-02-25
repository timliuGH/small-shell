smallsh: smallsh.o
	gcc -o smallsh smallsh.o

smallsh.o: smallsh.c
	gcc -c -Wall -g smallsh.c

clean:
	rm smallsh smallsh.o
