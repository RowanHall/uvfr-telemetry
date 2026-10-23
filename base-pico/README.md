# base-pico — LoRa receiver (base station)

Firmware for a **Raspberry Pi Pico W** driving a **Semtech SX1262** LoRa radio. It is
the *base station* / receiver half of the UVFR telemetry link: it listens
continuously for packets transmitted by the car-side radio and prints each one
over USB serial.

```
  car-pi (Raspberry Pi + SX1262)                base-pico (Pico W + SX1262)
   LoraPi.c  ── TX ──►  ((( 915.5 MHz LoRa )))  ──► basicRX.c  ── USB serial ──► laptop
```

The transmitter lives in [`../car-pi/LoraPi.c`](../car-pi/LoraPi.c). **The two ends
must agree on every radio parameter** (frequency, modulation, sync word, CRC) or
nothing is received — see [Radio configuration](#radio-configuration) below. The
SX1262 datasheet referenced throughout the source (section numbers like `13.3.6`)
is in [`../docs/DS_SX1261-2_V1.2.pdf`](../docs/DS_SX1261-2_V1.2.pdf).

## Hardware / wiring

The radio is connected to **SPI1** on the Pico. Pins are defined at the top of
`basicRX.c`:

| Signal       | Pico GPIO | Notes                                              |
|--------------|-----------|----------------------------------------------------|
| SCK          | 10        | SPI1 clock                                         |
| MOSI         | 11        | SPI1 TX                                            |
| MISO         | 12        | SPI1 RX                                            |
| CS / NSS     | 3         | Chip select, idle **high**                         |
| BUSY         | 2         | Input — polled before every command                |
| RESET        | 15        | Active low                                         |
| DIO1         | 20        | RX-done interrupt (rising edge)                    |

SPI runs at 16 MHz. CS is held high at idle so the chip never sees a half-clocked
command.

## Radio configuration

These are set in `lora_init()` and **must match the transmitter** in `car-pi`:

| Parameter      | Value                | Where                          |
|----------------|----------------------|--------------------------------|
| Frequency      | 915.5 MHz            | `LORA_FREQ` (`0x86393800...`)  |
| Packet type    | LoRa                 | `lora_set_packet_type(1)`      |
| Spreading factor | SF10               | `lora_set_modulation_params`   |
| Bandwidth      | 500 kHz (index `6`)  | `lora_set_modulation_params`   |
| Coding rate    | 4/5 (index `1`)      | `lora_set_modulation_params`   |
| Low-data-rate opt | off (`0`)         | `lora_set_modulation_params`   |
| Preamble       | 8 symbols            | `lora_set_packet_params`       |
| Header         | explicit             | `lora_set_packet_params`       |
| CRC            | on                   | `lora_set_packet_params`       |
| Sync word      | `0x1424` (private)   | `lora_set_sync_word`           |

`lora_set_modulation_params(sf, bw, cr, ldro)` takes the raw SX1262 register
indices, not Hz — see the datasheet tables (and the comment above the function)
for the full value maps. Lower SF / higher bandwidth = faster but shorter range.

## How the firmware works

**Boot (`main`)**
1. `stdio_init_all()` brings up USB serial.
2. Core 1 is launched running `heartbeat_core()`, which prints
   `[HEARTBEAT] Pico is alive` once a second so you can tell the board is running
   even when no packets arrive.
3. `gpio_setup()` configures SPI1, the control pins, and registers the DIO1
   rising-edge interrupt (`dio1_callback`).
4. A 5-second delay gives you time to open the serial monitor.

**TCXO auto-probing** — some SX1262 modules use a plain crystal, others a TCXO
powered from the chip's DIO3 at 1.8 V or 3.3 V. Guessing wrong means the
oscillator never starts and the radio never receives. So `main` loops over
`TCXO_OPTIONS = { none, 1.8V, 3.3V }`, doing a full `lora_reset()` +
`lora_init()` for each, and stops at the first setting that leaves the chip in RX
mode. `lora_init()` returns `true` when `GetStatus` reports chip mode `5` (RX).
If none work it prints a wiring/power error.

**Receive loop** — after setup the radio sits in RX-continuous mode
(`lora_set_rx()` → `0x82 FFFFFF`). The main `while(1)` loop:
- Polls the `rx_done` flag set by the DIO1 interrupt. On a packet it calls
  `lora_get_rx_status()` to read the payload length + buffer offset,
  `lora_read_packet()` to read the FIFO (printed as both hex and ASCII), then
  `lora_clear_irq()` to re-arm.
- Every ~16M idle iterations it prints `Awaiting RX`, the DIO1 pin level, and a
  decoded status line as a liveness/diagnostic check.

**Status decoding** — `lora_get_status()` prints the SX1262 chip mode
(STBY_RC, FS, RX, TX…) and last-command result in human-readable form, and
`lora_get_device_errors()` decodes oscillator / PLL calibration failures. These
are the first things to read when the link is quiet.

## Build & flash

Built with the **Raspberry Pi Pico SDK 2.3.0** (CMake), targeting board
`pico_w`. USB serial (stdio) is enabled; UART stdio is disabled. The project is
set up for the Raspberry Pi Pico VS Code extension, which supplies the SDK paths.

With the VS Code extension: open the `base-pico` folder and use *Compile* /
*Run* (configures the kit and SDK automatically).

From the command line (SDK installed, `PICO_SDK_PATH` set):

```sh
cd base-pico
mkdir -p build && cd build
cmake ..
make
```

This produces `build/Pico-Reciever.uf2`. To flash: hold **BOOTSEL** while
plugging in the Pico, then copy the `.uf2` onto the `RPI-RP2` drive (or
`picotool load Pico-Reciever.uf2`).

## Watching output

The firmware prints over **USB serial**. Open the port after flashing:

```sh
# macOS
screen /dev/tty.usbmodem* 115200
# Linux
screen /dev/ttyACM0 115200
```

Expected on a healthy link:

```
[HEARTBEAT] Pico is alive
--- Trying without TCXO ---
Status 0x2A: mode RX, last command data available
Listening...
RX_DONE interrupt received!
Received 19 bytes: 54 65 73 74 ... Testing Testing 123
```

## Troubleshooting

- **`Lora Busy` prints forever** — the radio holds BUSY high: check CS/MISO/MOSI/
  SCK wiring and power.
- **"radio never entered RX with any TCXO setting"** — module not responding on
  SPI, or wiring/power fault. Confirm the pin table above matches your board.
- **Heartbeat prints but no packets** — the ends disagree on a radio parameter.
  Re-check every row of [Radio configuration](#radio-configuration) against
  `car-pi/LoraPi.c` (frequency, SF/BW/CR, sync word, CRC).
- **`XOSC_START_ERR` in device errors** — TCXO voltage mismatch; the auto-probe
  normally handles this, but verify your module's TCXO requirement.

> Note: the project/binary is named `Pico-Reciever` (original spelling kept so the
> CMake target and build artifacts stay consistent).
