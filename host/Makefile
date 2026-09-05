CC ?= cc
CFLAGS ?= -O2 -Wall -Wno-unused-function -DLSB_FIRST -I../core -I../core/z80 -I../main/roms
SRCS = harness.c ../core/galaga.c ../core/galaga_video.c ../core/galaga_sound.c ../core/z80/Z80.c
harness: $(SRCS) ../core/*.h ../core/z80/*.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) -lm
clean:
	rm -f harness
