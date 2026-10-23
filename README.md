# uvfr-telemetry

Wireless telemetry link for the **UVic Formula Racing (UVFR)** car. A LoRa radio
carries data from the car to a base station at trackside: the Raspberry Pi in the
car transmits, and a Raspberry Pi Pico W at the base station receives and prints
each packet over USB serial to a laptop.

```
  car-pi (Raspberry Pi 3B+ + SX1262)              base-pico (Pico W + SX1262)
    LoraPi.c  ── TX ──►  ((( 915.5 MHz LoRa )))  ──► basicRX.c  ── USB serial ──► laptop
```

Both ends use a **Semtech SX1262** radio driven over SPI. The link is one-way
(car → base) and currently sends a fixed test string; real telemetry gets dropped
in where that string is built. **The two ends must agree on every radio parameter**
(frequency, modulation, sync word, CRC) or nothing is received — see
[Radio link parameters](#radio-link-parameters-must-match).

## Repository layout

| Path | What it is |
|------|------------|
| [`car-pi/`](car-pi/) | **Transmitter.** C program for the Raspberry Pi in the car, using the [pigpio](https://github.com/joan2937/pigpio) library. Builds with `make`. See [`car-pi/README.md`](car-pi/README.md). |
| [`base-pico/`](base-pico/) | **Receiver.** Firmware for a Raspberry Pi Pico W, built with the Pico SDK (CMake). Listens continuously and prints packets over USB serial. See [`base-pico/README.md`](base-pico/README.md). |
| [`protocol/`](protocol/) | Reserved for a shared packet/telemetry format definition (empty for now). |
| [`docs/`](docs/) | Reference material — the [SX1261/2 datasheet](docs/DS_SX1261-2_V1.2.pdf). Section numbers in code comments (e.g. `13.3.6`) refer to it. |
| [`tools/`](tools/) | Supporting config, e.g. an OpenOCD SWD file for debugging the Pico. |

Each end has its own README with the full wiring table, build steps, expected
output, and troubleshooting. Start there once you know which board you're working
on. This file is the map; those are the manuals.

## Quick start

**Base station (Pico W receiver):**

```sh
cd base-pico
mkdir -p build && cd build
cmake .. && make          # produces build/Pico-Reciever.uf2
# hold BOOTSEL, plug in the Pico, copy the .uf2 onto RPI-RP2
screen /dev/tty.usbmodem* 115200   # watch output (macOS; Linux: /dev/ttyACM0)
```

**Car (Raspberry Pi transmitter):**

```sh
cd car-pi
make run                  # builds build/LoraPi and runs it (pigpio needs sudo)
```

A healthy link prints `Attempting broadcast` → `TX done` on the car once a second,
and `RX_DONE` / `Received N bytes: ...` on the base station.

> ⚠️ **Always attach a 915 MHz antenna before transmitting.** Running the SX1262
> power amplifier without an antenna can damage the radio.

## Radio link parameters (must match)

Set identically in `car-pi/LoraPi.c` and `base-pico/basicRX.c`. If the heartbeat
runs but no packets arrive, these are the first thing to check.

| Parameter | Value |
|-----------|-------|
| Frequency | 915.5 MHz (US 902–928 MHz band) |
| Spreading factor | SF10 |
| Bandwidth | 500 kHz |
| Coding rate | 4/5 |
| Preamble | 8 symbols |
| Header | explicit |
| CRC | on |
| Sync word | `0x1424` (private network) |
| TX power | +22 dBm, 80 µs ramp |

One asymmetry worth knowing: the two SX1262 modules differ in their clock source.
The car's module uses a plain crystal (`LORA_USE_TCXO 0`); the base station's
firmware auto-probes `{ none, 1.8 V, 3.3 V }` TCXO options at boot and keeps the
first that brings the radio into RX. A wrong TCXO setting shows up as
`XOSC_START_ERR` in the device errors.

## Status

Working end-to-end over the air with a fixed test payload. Next steps: define a
real telemetry packet format (the `protocol/` folder is the home for it) and feed
live car data into the transmitter.
