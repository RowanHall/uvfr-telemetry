# make        -> builds build/LoraPi
# make run    -> builds and runs it (needs sudo for pigpio)
# make clean  -> deletes the build folder

CFLAGS = -Wall -Wno-pointer-sign -pthread
LIBS = -lpigpio -lrt

build/LoraPi: LoraPi.c
	mkdir -p build
	gcc $(CFLAGS) -o $@ $< $(LIBS)

run: build/LoraPi
	sudo ./build/LoraPi

clean:
	rm -rf build

.PHONY: run clean
