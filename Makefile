#

CFLAGS=-Wall -O2 -g -std=c99 -Iinclude
LDFLAGS=lib/libdvdread.a -ldl -lssl -ljansson -s
PREFIX?=/usr/local

BINS=udf_fingerprint udf_extract

%: %.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

all:	lib/libdvdread.a $(BINS)

lib/libdvdread.a:
	sh -c 'cd libdvdread-4.2.0.plus && ./autogen.sh && ./configure --prefix=${PWD} --enable-static && make && make install'

install:	all
	strip -s $(BINS)
	mkdir -p "$(PREFIX)/bin/"
	cp $(BINS) "$(PREFIX)/bin/"

clean:
	rm -f *.o $(BINS)
