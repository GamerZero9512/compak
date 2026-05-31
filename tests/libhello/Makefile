CC ?= cc

.PHONY: clean

hello: test.c hello.h libhello.a
	$(CC) test.c -I. -L. -lhello -o hello

libhello.a: hello.o
	ar rcs libhello.a hello.o

hello.o: hello.c hello.h
	$(CC) -c hello.c -I. -o hello.o

clean:
	rm -f hello libhello.a hello.o
