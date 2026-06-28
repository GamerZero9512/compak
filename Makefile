CC ?= cc
FLAGS ?=

LIBS := -lcurl -larchive -lz -llzma -llz4 -lxml2 -lssl -lcrypto -lbz2 -lparson -lpsl
SAFE ?= -Wall -Wextra -pedantic

.PHONY: all debug clean solo install debuginstall

all: compak

compak: src/compak.c include/*.h lib/libarchive.a lib/libparson.a
	$(CC) src/compak.c -Iinclude -Llib $(SAFE) $(FLAGS) $(LIBS) -O2 -o compak

debug: src/compak.c include/*.h lib/libarchive.a lib/libparson.a
	$(CC) -fno-omit-frame-pointer src/compak.c -Iinclude -Llib $(SAFE) -Werror $(FLAGS) $(LIBS) -O0 -g3 -fsanitize=address,undefined,leak -o compak

clean:
	rm -f compak

install: compak
	./compak -p .
	sudo ./compak -i compak.tar.xz
	rm compak.tar.xz
