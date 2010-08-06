CFLAGS = -ggdb -Wall -Wextra -pedantic -std=c99
CFLAGS += -O2 -funroll-loops -fomit-frame-pointer
LDFLAGS=-ggdb -lm
OS=$(shell uname -s)
ARCH=$(shell uname -s).$(shell uname -m)

ifeq ($(OS),Darwin)
CFLAGS += -arch i386 -I/sw/include -fast
LDFLAGS += -arch i386 -L/sw/lib
endif

all: imageresize.$(ARCH)

imageresize.$(ARCH): irz.o
	$(CC) -o $@ $+ $(LDFLAGS) -ljpeg

clean:
	rm *.o imageresize.$(ARCH)
