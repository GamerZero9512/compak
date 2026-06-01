CC ?= cc
FLAGS ?=

LIBS := -lcurl -larchive -lz -llzma -llz4 -lxml2 -lssl -lcrypto -lbz2 -lparson -lpsl
SAFE ?= -Wall -Wextra -pedantic

.PHONY: all debug clean solo install

all: compak

compak: src/compak.c include/*.h lib/libarchive.a lib/libparson.a
	$(CC) src/compak.c -Iinclude -Llib $(SAFE) $(FLAGS) $(LIBS) -O2 -o compak

debug: src/compak.c
	$(CC) -fno-omit-frame-pointer src/compak.c -Iinclude -Llib $(SAFE) $(FLAGS) $(LIBS) -O0 -g3 -fsanitize=address,undefined,leak -o compak

clean:
	rm -f compak lib/libparson.a parson.o

lib/libparson.a: parson.o
	ar rcs lib/libparson.a parson.o

parson.o: src/parson.c include/parson.h
	$(CC) -c src/parson.c -Iinclude $(SAFE) -o parson.o

solo: compak
	rm -f parson.o lib/libparson.a

install: compak
	./compak -p .
	sudo ./compak -i compak.tar.xz
	rm compak.tar.xz
