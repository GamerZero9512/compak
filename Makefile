CC ?= cc

DEBUG ?= undefined
ifneq ($(DEBUG),undefined)
	FLAGS ?= -Werror -fno-omit-frame-pointer -O0 -g3 -fsanitize=address,undefined,leak
else
	FLAGS ?= -O2
endif

LIBS := -lcurl -larchive -lz -llzma -llz4 -lxml2 -lssl -lcrypto -lbz2 -lparson -lpsl
SAFE ?= -Wall -Wextra -pedantic

.PHONY: all clean install

all: compak

compak: src/compak.c include/*.h lib/libarchive.a lib/libparson.a lib/libcurl.a
	$(CC) src/compak.c -Iinclude -Llib $(SAFE) $(LIBS) $(FLAGS) -o compak

clean:
	rm -f compak lib/libparson.a lib/parson.o lib/libcurl.a lib/libarchive.a

install: compak
	./compak -p .
	sudo ./compak -i compak.tar.xz
	rm compak.tar.xz

# LIBS

lib/parson.o: src/parson.c include/parson.h
	$(CC) -c src/parson.c -Iinclude $(SAFE) $(FLAGS) -o lib/parson.o

lib/libparson.a: lib/parson.o
	ar rcs lib/libparson.a lib/parson.o

.ONESHELL:
lib/libcurl.a: src/curl/configure # I can't be bothered to put EVERY libcurl source item in here
	@echo "========== COMPILING LIBCURL =========="
	cd src/curl
	sh ./configure --with-openssl
	make
	cd ../..
	cp src/curl/lib/.libs/libcurl.a lib/libcurl.a

lib/libarchive.a: src/libarchive/configure
	@echo "========== COMPILING LIBARCHIVE =========="
	cd src/libarchive
	sh ./configure --disable-maintainer-mode
	make
	cd ../..
	cp src/libarchive/.libs/libarchive.a lib/libarchive.a
