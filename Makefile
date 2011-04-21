CFLAGS = -ggdb -Wall -Wextra -pedantic -std=c99 -Wformat-nonliteral -Wformat-security
CFLAGS += -O2 -funroll-loops -fomit-frame-pointer
CFLAGS += -I/usr/include/python2.6 -fPIC
LDFLAGS=-ggdb -lm
OS=$(shell uname -s)
ARCH=$(shell uname -s).$(shell uname -m)
EXE_SUFFIX=
SO_SUFFIX=.so

ifeq ($(OS),Darwin)
CFLAGS += -fast
# Adapt for Fink, Macports or custom library paths
CFLAGS += -I/opt/local/include # -I/sw/include
LDFLAGS += -L/opt/local/lib # -L/sw/lib
endif

all: imageresize$(EXE_SUFFIX) pyirz$(SO_SUFFIX)

imageresize$(EXE_SUFFIX): irz.o
	$(CC) -o $@ $+ $(LDFLAGS) -ljpeg

pyirz$(SO_SUFFIX): pyirz.o
	$(CC) -o $@ -shared $+ $(LDFLAGS) -ljpeg -lpython2.6

irz.o: irz.h
pyirz.o: irz.h

clean:
	-rm -f *.o imageresize$(EXE_SUFFIX) *.so
