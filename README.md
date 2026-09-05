# SWARMFIGHTER

Namco's 1981 *Galaga*, running on a Fiesta medal.

Like [PELLETINO](https://github.com/aedile/PELLETINO) (Pac-Man) and
[TRENCHRUNNER](https://github.com/aedile/TRENCHRUNNER) (Star Wars), this is an
emulator of the original arcade board running the original ROM code on a
Waveshare ESP32-C6-LCD-1.69, the $20 module the San Antonio Fiesta medal is
built around. Hold it upright like Pac-Man, tilt to move, press the button to
fire.

## The hardware being emulated

Galaga's board is three Z80s at 3.072 MHz sharing one memory space. The first
runs the game, the second handles the attack patterns and sprites, the third
runs the sound. A Namco 06XX chip connects the CPUs to two custom
microcontrollers: the 51XX, which counts coins and credits and reads the
joystick, and the 54XX, which makes the explosion noises. The 05XX generates
the scrolling starfield from a 16-bit shift register, and the video board
draws a 36x28 tile layer and 64 sprites over it. Music and effects come from
the three-voice Namco wavetable generator, the same one Pac-Man has.

All of it is emulated here in plain C. The two custom MCUs have internal ROMs
that aren't part of the standard ROM set, so they are modelled at the protocol
level, the way MAME did before those ROMs were dumped. The 54XX explosion
sound is a filtered-noise approximation tuned by ear.

## What you need

* A Waveshare ESP32-C6-LCD-1.69 with the IMU.
* The MAME `galaga` ROM set. **The ROMs are not included.** The set is:

  | File | Size | What it is |
  |---|---|---|
  | `gg1_1b.3p`, `gg1_2b.3m`, `gg1_3.2m`, `gg1_4b.2l` | 4 KB each | CPU 1 program |
  | `gg1_5b.3f` | 4 KB | CPU 2 program |
  | `gg1_7b.2c` | 4 KB | CPU 3 program |
  | `gg1_9.4l` | 4 KB | tiles |
  | `gg1_11.4d`, `gg1_10.4f` | 4 KB each | sprites |
  | `prom-5.5n` | 32 bytes | palette |
  | `prom-4.2n`, `prom-3.1c` | 256 bytes each | tile and sprite color lookup |
  | `prom-1.1d` | 256 bytes | sound waveforms |

  The converter checks every file's CRC.
* Docker (or a native ESP-IDF 5.3), Python 3, and `esptool`.

## Building and flashing

```
python3 tools/convert_roms.py galaga            # ROMs in ./galaga, writes main/roms/galaga_roms.h
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 idf.py -B build_docker build
python3 -m esptool --chip esp32c6 --port /dev/cu.usbmodem101 -b 460800 write_flash @build_docker/flash_args
```

## Playing

* **Tilt** left and right to move the fighter.
* **BOOT button** fires.
* **PWR button**, short press: inserts a coin and presses start half a second
  later. Long press: power off.

DIP switches are set in `main/main.cpp` (`ga_set_dips`): easy, three fighters,
bonus at 20K and 70K, demo sounds on.

## Running it on your computer

```
cd host && make
./harness out 40 --every 1 --wav out/audio.wav --script "20:coin=1,20.2:coin=0,21:start=1,21.2:start=0,23:fire=1,23.2:fire=0"
python3 ppm2png.py out/*.ppm
```

The harness runs the board on a Mac or Linux machine, saves portrait frames
and audio, and reports each CPU's idle percentage. Script keys are `coin`,
`start`, `fire`, `left`, `right`.

## How it fits in 160 MHz

Three Z80s at 3 MHz is more than the ESP32-C6 can brute-force, but the second
and third CPUs spend most of their time in a two-instruction wait loop for the
next interrupt; the emulator recognises those loops and skips to the interrupt.
Memory is reached through 256-byte page tables. The starfield generator's
shift register has a fixed 65,535-state cycle, so instead of stepping it for
every pixel the renderer keeps a table of the 256 positions in that cycle
where a star can appear and just looks up which ones fall in the current
frame; the output is byte-identical to the per-pixel version, verified on the
host. Rendering runs in its own task so the 15 ms panel transfer overlaps with
emulation. The result is every frame drawn at the game's own 60.6 Hz with room
to spare.

## Credits

Nicola Salmoria and the MAME team documented this board; the 05XX starfield
description and the 51XX/54XX protocol models come from their work. Marat
Fayzullin wrote the Z80 core. The drivers come from PELLETINO. Galaga is by
Namco, 1981, designed by Shigeru Yokoyama.
