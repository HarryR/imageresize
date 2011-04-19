CFLAGS = -ggdb -Wall -Wextra -pedantic -std=c99
CFLAGS += -O2 -funroll-loops -fomit-frame-pointer
CFLAGS += -I/usr/include/python2.6
LDFLAGS=-ggdb -lm
OS=$(shell uname -s)
ARCH=$(shell uname -s).$(shell uname -m)

ifeq ($(OS),Darwin)
CFLAGS += -I/opt/local/include -fast
LDFLAGS += -L/opt/local/lib
endif

all: imageresize.$(ARCH) pyirz.so

imageresize.$(ARCH): irz.o
	$(CC) -o $@ $+ $(LDFLAGS) -ljpeg

pyirz.so: pyirz.o
	$(CC) -o $@ -shared $+ $(LDFLAGS) -ljpeg -lpython2.6

clean:
	rm *.o imageresize.$(ARCH) *.so
