# car-pi — LoRa transmitter

Runs on the Raspberry Pi in the car. Drives an SX1262 LoRa radio over SPI and
transmits a telemetry frame at 10 Hz for the base station Pico
([`../base-pico`](../base-pico)) to receive.

The payload is the shared `telem_frame_t` from
[`../protocol/telemetry.h`](../protocol/telemetry.h). `main()` in `LoraPi.c`
currently fills it with synthetic sweeping values; step D replaces those with real
CAN reads (see the top-level README).

## Hardware

- Raspberry Pi 3 Model B+ (Raspberry Pi OS / Debian 13 trixie)
- SX1262 LoRa module (crystal oscillator, **not** TCXO — see [Settings](#settings))
- 915 MHz antenna — **always attach it before transmitting**, running the PA
  without an antenna can damage the radio

### Wiring

| SX1262 pin | Pi GPIO | Pi physical pin |
|------------|---------|-----------------|
| NSS / CS   | GPIO21  | 40 |
| SCK        | GPIO11  | 23 |
| MOSI       | GPIO10  | 19 |
| MISO       | GPIO9   | 21 |
| BUSY       | GPIO20  | 38 |
| DIO1       | GPIO16  | 36 |
| DIO4       | GPIO6   | 31 (not used yet) |
| NRESET     | GPIO18  | 12 |
| 3.3V       | 3V3     | 1 or 17 |
| GND        | GND     | 6 (any GND) |

CS is toggled manually on GPIO21, not the hardware CE0 pin.

## Setup (once per Pi)

The code uses the [pigpio](https://github.com/joan2937/pigpio) C library.
Debian trixie no longer packages it (`apt install pigpio` fails), so build it
from source:

```sh
git clone https://github.com/joan2937/pigpio.git
cd pigpio
make
sudo make install
```

Also make sure SPI is enabled: `dtparam=spi=on` in `/boot/firmware/config.txt`
(or `sudo raspi-config` → Interface Options → SPI), then reboot.

## Build and run

From this folder:

```sh
make          # builds build/LoraPi
make run      # builds and runs it (pigpio needs sudo)
make clean    # deletes build/
```

Or run it directly with `sudo ./build/LoraPi`. Stop it with **Ctrl+C**.

### Expected output

```
lora_init
lora_set_standby
lora_calibrate
...
Attempting broadcast (32 bytes)
lora_write_buffer (32 bytes)
lora_set_packet_params
lora_set_tx
TX done
```

`Attempting broadcast (32 bytes)` → `TX done` repeats ~10×/second.

## Settings

Switches at the top of `LoraPi.c`:

| Setting | Current | What it does |
|---------|---------|--------------|
| `LORA_USE_TCXO` | `0` | Set to `1` only for modules with a TCXO powered from DIO3. Wrong setting → `XOSC_START_ERR` and TX never finishes. |
| `LORA_TCXO_VOLTAGE` | `0x02` (1.8V) | TCXO supply voltage, only used if the above is `1`. |
| `LORA_DIO2_RF_SWITCH` | `0` | Lets DIO2 drive the antenna switch. Turning it on locked up our module. |
| `LORA_FREQ` | 915.5 MHz | Bytes are `0x86` + `freq_Hz × 2^25 / 32 MHz`. Keep inside the US 902–928 MHz band. |

### Radio parameters

These **must match the Pico** (`base-pico/basicRX.c`) or nothing gets through:

| Parameter | Value |
|-----------|-------|
| Frequency | 915.5 MHz |
| Spreading factor | SF10 |
| Bandwidth | 500 kHz (`0x06`) |
| Coding rate | 4/5 |
| Preamble | 8 symbols |
| Header | explicit |
| CRC | on |
| Sync word | `0x1424` (private network) |
| TX power | +22 dBm, 80 µs ramp |

## Troubleshooting

| Output | Likely cause |
|--------|--------------|
| `ERROR: BUSY stuck high` | Radio not responding — check wiring (CS on GPIO21?), power, or a bad setting. The function name printed just before is the command that hung. |
| `ERROR: TX never finished` + `XOSC_START_ERR` | Wrong `LORA_USE_TCXO` setting — flip it. |
| `ERROR: TX never finished`, no errors | Radio probably reset mid-TX (power brownout at +22 dBm). Try a stronger 3.3V supply. |
| `note: TxDone seen over SPI before DIO1` | Harmless timing race. Only a DIO1 wiring problem if it happens every time. |
| `gpioInitialise failed` / permission errors | Run with `sudo`. |
| `TX done` but the Pico hears nothing | Check the Pico's startup output says `mode RX`, antennas, and that radio parameters match. |

Section numbers in code comments (e.g. `13.3.6`) refer to the SX1261/2 datasheet
in [`../docs`](../docs/DS_SX1261-2_V1.2.pdf).
