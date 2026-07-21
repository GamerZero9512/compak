CC ?= cc

DEBUG ?= undefined
ifneq ($(DEBUG),undefined)
	FLAGS ?= -Werror -fno-omit-frame-pointer -O0 -g3 -fsanitize=address,undefined,leak
else
	FLAGS ?= -O2
endif

LIBS := -lcurl -larchive -lz -llzma -llz4 -lxml2 -lssl -lcrypto -lbz2 -lparson -lpsl -lzstd -lacl -lidn2 -lbrotlidec -lbrotlicommon -lnghttp2 -lldap -llber
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

install-fast: compak
	sudo sh -c "mkdir -p /usr/local /usr/local/bin /var/lib/compak /var/lib/compak/compak /usr/local/share /usr/local/share/man /usr/local/share/man/man1 && cp ./compak /usr/local/bin/compak && cp ./compak.json /var/lib/compak/compak/compak.json && cp ./docs/compak.1 /usr/local/share/man/man1/compak.1"

# Create the lib/ directory
lib:
	mkdir -p lib

# LIBS

ifneq ($(PARSON),no)
lib/parson.o: lib src/parson/parson.c include/parson.h
	$(CC) -c src/parson/parson.c -Iinclude $(SAFE) $(FLAGS) -o lib/parson.o

lib/libparson.a: lib lib/parson.o
	ar rcs lib/libparson.a lib/parson.o
endif

.ONESHELL:

ifneq ($(CURL),no)
lib/libcurl.a: lib
	@echo "================== COMPILING CURL =================="
	cd src/curl
	sh ./configure --with-openssl --enable-static
	$(MAKE) clean
	$(MAKE)
	cd ../..
	cp src/curl/lib/.libs/libcurl.a lib/libcurl.a
endif

ifneq ($(LIBARCHIVE),no)
lib/libarchive.a: lib
	@echo "=============== COMPILING LIBARCHIVE ==============="
	cd src/libarchive
	sh ./configure --disable-maintainer-mode --enable-static
	$(MAKE) clean
	$(MAKE)
	cd ../..
	cp src/libarchive/.libs/libarchive.a lib/libarchive.a
endif

clean-all: clean
	cd src/curl
	if [ -e Makefile ]; then $(MAKE) clean; fi
	cd ../libarchive
	if [ -e Makefile ]; then $(MAKE) clean; fi
	cd ../..
