# uvfr-telemetry

Wireless telemetry link for the UVFR car
(mvp for UV26 but goal of UV27 and next step, make work for uv25).
LoRa on pi carries live data from car to laptop with pico which
feeds to influxDB+Grafana dashboard.

The Pi transmits (TX) via lora
Pico receives (RX) and forwards each frame over USB serial
Python reader on laptop parses it into InfluxDB, and Grafana displays it.

```
  CAN bus          car-pi (RPi + SX1262)           base-pico (Pico W + SX1262)         laptop
 (UV26, EV)  ─►  LoraPi.c ─ TX ─►  ((( 915.5 MHz LoRa )))  ─►  basicRX.c ─ USB serial ─►  telemetry_reader.py
  [step D]         packs a                                       validates CRC/seq,          │
                   telem_frame_t                                 prints "DATA," CSV          ▼
                                                                                        InfluxDB ─► Grafana
```

Both radio ends use a **Semtech SX1262** driven over SPI. The link is one-way
(car → base). **The two ends must agree on every radio parameter** (frequency,
modulation, sync word, CRC) or nothing is received — see
[Radio link parameters](#radio-link-parameters-must-match).

## Status

The full pipeline works end-to-end **with synthetic data**: the car transmits a
real telemetry frame at 10 Hz, the base station validates and forwards it, and the
laptop dashboard shows live gauges and graphs. The remaining piece is **step D** —
replacing the synthetic values with real CAN-bus reads on the car. See
[Roadmap](#roadmap).

## Repository layout

| Path | What it is |
|------|------------|
| [`car-pi/`](car-pi/) | **Transmitter.** C program for the Raspberry Pi in the car, using [pigpio](https://github.com/joan2937/pigpio). Builds `telem_frame_t`s and sends them over the SX1262 at 10 Hz. Builds with `make`. See [`car-pi/README.md`](car-pi/README.md). |
| [`base-pico/`](base-pico/) | **Receiver.** Pico W firmware (Pico SDK / CMake). Listens continuously, validates each frame (version + CRC), tracks dropped packets by sequence number, and prints a `DATA,` CSV line over USB serial. See [`base-pico/README.md`](base-pico/README.md). |
| [`protocol/`](protocol/) | [`telemetry.h`](protocol/telemetry.h) — the **shared wire protocol**. One packed `telem_frame_t` struct included by *both* radio ends (and mirrored by the laptop reader), plus the radio config and a CRC-16. Single source of truth for the frame layout. |
| [`dashboard/`](dashboard/) | **Laptop side.** `telemetry_reader.py` (pyserial → InfluxDB) and a Docker Compose stack that auto-provisions InfluxDB + Grafana with a prebuilt dashboard. See [`dashboard/README.md`](dashboard/README.md). |
| [`docs/`](docs/) | Reference material — the [SX1261/2 datasheet](docs/DS_SX1261-2_V1.2.pdf). Section numbers in code comments (e.g. `13.3.6`) refer to it. |
| [`tools/`](tools/) | Supporting config, e.g. an OpenOCD SWD file for debugging the Pico. |

Each end has its own README with the full wiring table, build steps, expected
output, and troubleshooting. This file is the map; those are the manuals.

## The telemetry frame

`protocol/telemetry.h` defines a single packed 32-byte `telem_frame_t`:

- **Header:** `version` (guards against layout drift — bump `TELEM_VERSION` on any
  change), `type`, `seq` (for drop detection), `timestamp_ms`.
- **Channels:** EV data as scaled integers (no floats): pack voltage/current, SoC,
  cell temp/voltage, motor rpm/temp, inverter temp, torque, throttle, brake, speed,
  flags. Each field's unit and scale is in the header comment.
- **Integrity:** trailing CRC-16/CCITT; `telem_finalize()` stamps it, `telem_valid()`
  checks it.

Because the header is portable C, it compiles unchanged on the Pi, the Pico, and
(for a future car-side MCU) an STM32. The laptop reader keeps a matching field
table — change one, change the other.

## Quick start

**1. Base station (Pico W receiver):**

```sh
cd base-pico
mkdir -p build && cd build
cmake .. && make          # produces build/Pico-Reciever.uf2
# hold BOOTSEL, plug in the Pico, copy the .uf2 onto RPI-RP2
```

**2. Car (Raspberry Pi transmitter):**

```sh
cd car-pi
make run                  # builds build/LoraPi and runs it (pigpio needs sudo)
```

The car prints `Attempting broadcast (32 bytes)` → `TX done` ~10×/second; the base
station prints `RX_DONE` and a `DATA,...` line per frame.

**3. Laptop dashboard:**

```sh
cd dashboard
docker compose up -d      # InfluxDB + Grafana, dashboard auto-provisioned
# confirm frames parse (no DB needed):
python3 telemetry_reader.py --port /dev/cu.usbmodem1101 --stdout
# then stream into InfluxDB:
python3 telemetry_reader.py --port /dev/cu.usbmodem1101 \
    --influx-url http://localhost:8086 --influx-token uvfr-dev-token-change-me \
    --influx-org uvfr --influx-bucket telemetry
```

Open Grafana at <http://localhost:3000> — the **UVFR Telemetry** dashboard fills in
live. Full setup (and the InfluxDB v2-vs-v3 gotcha) is in [`dashboard/README.md`](dashboard/README.md).

> ⚠️ **Always attach a 915 MHz antenna before transmitting.** Running the SX1262
> power amplifier without an antenna can damage the radio.
>
> Only one program can own the serial port at a time — close any serial monitor
> before running the reader.

## Radio link parameters (must match)

Centralized in `protocol/telemetry.h` and set identically in `car-pi/LoraPi.c` and
`base-pico/basicRX.c`. If the heartbeat runs but no packets arrive, check these first.

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

At SF10/BW500 a 32-byte frame is ~40–50 ms on air, so 10 Hz sits around 40–50%
airtime — comfortable. Going much faster needs a lower spreading factor (trading
range for speed).

One asymmetry worth knowing: the two SX1262 modules differ in clock source. The
car's module uses a plain crystal (`LORA_USE_TCXO 0`); the base station auto-probes
`{ none, 1.8 V, 3.3 V }` TCXO options at boot and keeps the first that reaches RX.
A wrong TCXO setting shows up as `XOSC_START_ERR` in the device errors.

## Roadmap

- **Step D — real CAN data (next, needs the car).** UV26 is an EV with two CAN
  buses (CAN1 powertrain @ 500 k, CAN2 sensors @ 250 k). On the Pi, bring up
  SocketCAN (`can0`) via the Waveshare RS485 CAN HAT, then replace the synthetic
  values in `LoraPi.c` with decoded CAN signals — starting with the Energy Meter
  (`0x100`) and Bamocar inverter (`0x181`). The rest of the pipeline is unchanged.
- **Full-rate logging** — the radio link is a lossy subset; the complete record
  should be logged on the car (SD card, or the existing DAQ system).
- **Car-side MCU** — the Pi is ideal for prototyping but heavy for the car. A
  future migration to an STM32 (native CAN) or RP2040 (reuses the Pico LoRa code)
  keeps `protocol/telemetry.h` and the SX1262 command logic; only the SPI/CAN glue
  changes.
