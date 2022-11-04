.PHONY: clean

VPATH = src

# CC = gcc-8
CFLAGS += -O3 -g3 -Wall -Wno-unused-but-set-variable -Wno-unknown-pragmas -fverbose-asm
LDLIBS += -L/usr/lib/x86_64-linux-gnu -lgsl -lgslcblas -lm

more-drama: more-drama.o stats.o pool.o map.o perf.o access_pattern.o reverse.o types.o

clean:
	-rm -f more-drama *.o
