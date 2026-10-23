/*
make        (builds build/LoraPi)

make run    (or: sudo ./build/LoraPi)
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pigpio.h>
#include "../protocol/telemetry.h"

#define LORA_CS 21
#define LORA_CLK 11
#define LORA_MOSI 10
#define LORA_MISO 9
#define LORA_BUSY 20
#define LORA_DIO1 16
#define LORA_DIO4 6
#define LORA_RST 18
#define LORA_SPI 0

// Set to 1 if the module uses a TCXO powered from DIO3 (most E22 / Core1262 style modules).
// If GetDeviceErrors reports XOSC_START_ERR, flip this.
#define LORA_USE_TCXO 0
#define LORA_TCXO_VOLTAGE 0x02 // 0x02 = 1.8V (see 13.3.6)

// Set to 1 if the module's antenna switch is driven by DIO2 (Waveshare Core1262 etc). Harmless if DIO2 is unconnected.
#define LORA_DIO2_RF_SWITCH 0

#define LORA_TX_TIMEOUT_US 2000000 // 2s, a short packet at SF10/500kHz takes ~50ms

#define LORA_BUSY_TIMEOUT_US 100000 // 100ms, normal commands take a few us

static uint8_t LORA_FREQ[5] = {0x86, 0x39, 0x38, 0x00, 0x00}; // 915.5 MHz (US 902-928 band)

volatile int tx_status = 1;

void gpio_setup()
{
    gpioInitialise();
    gpioSetMode(LORA_CS, PI_OUTPUT);
    gpioSetMode(LORA_RST, PI_OUTPUT);
    gpioSetMode(LORA_BUSY, PI_INPUT);
    gpioSetMode(LORA_DIO1, PI_INPUT);
    gpioSetMode(LORA_DIO4, PI_INPUT);
    spiOpen(LORA_SPI, 8 * 1000000, 0x000);
}

void lora_check_busy()
{
    uint32_t start = gpioTick();
    while (gpioRead(LORA_BUSY))
    {
        if (gpioTick() - start > LORA_BUSY_TIMEOUT_US)
        {
            // the last function name printed is the command that never finished
            printf("ERROR: BUSY stuck high for %dms, check wiring (CS/NSS on GPIO%d?) and TCXO setting\n",
                   LORA_BUSY_TIMEOUT_US / 1000, LORA_CS);
            gpioTerminate();
            exit(1);
        }
    }
}

void cs_low()
{
    lora_check_busy();
    gpioWrite(LORA_CS, 0);
}

void cs_high()
{
    gpioWrite(LORA_CS, 1);
}

void lora_set_standby()
{
    printf("lora_set_standby\n");
    uint8_t data[] = { 0x80, 0x00 }; // 0x00 = STDBY_RC mode
    cs_low();
    spiWrite(LORA_SPI, data, 2);
    cs_high();
}

void lora_set_xosc() // 13.1.15
{
    printf("lora_set_xosc\n");
    uint8_t data[] = {
        0x93,
        0x30,
    };
    cs_low();
    spiWrite(LORA_SPI, data, 2);
    cs_high();
}

void lora_set_pkt_type(int type) // 0 for GFSK, 1 for LoRa
{
    printf("lora_set_pkt_type\n");
    uint8_t data[] = {
        0x8A, // SetPacketType
        type};
    cs_low();
    spiWrite(LORA_SPI, data, 2);
    cs_high();
}

void lora_set_freq()
{
    printf("lora_set_freq\n");
    cs_low();
    spiWrite(LORA_SPI, LORA_FREQ, 5);
    cs_high();
}

void lora_set_pa_config() //check 13.1.14.1
{
    printf("lora_set_pa_config\n");
    uint8_t data[] = {
        0x95, // SetPacketParams
        0x04,
        0x07,
        0x00,
        0x01
        };
    cs_low();
    spiWrite(LORA_SPI, data, 5);
    cs_high();
}

void lora_set_sync_word()
{
    uint8_t data[] = {
        0x0D,
        0x07,
        0x40,
        0x14,
        0x24
    };
    cs_low();
    spiWrite(LORA_SPI, data, 5);
    cs_high();
}

void lora_set_tx_params(int ramp) // lower better? start with 3 I think
{
    printf("lora_set_tx_params\n");
    uint8_t data[] = {
        0x8E,
        0x16,
        ramp};
    cs_low();
    spiWrite(LORA_SPI, data, 3);
    cs_high();
}

void lora_set_buffer_base(uint8_t tx_base, uint8_t rx_base)
{
    printf("lora_set_buffer_base\n");
    uint8_t data[] = {
        0x8F, // SetBufferBaseAddress
        tx_base,
        rx_base};
    cs_low();
    spiWrite(LORA_SPI, data, 3);
    cs_high();
}

void lora_set_modulation_params(int sf, int bw, int cr, int ldro)
{
    printf("lora_set_modulation_params\n");
    uint8_t data[] = {
        0x8B, // SetModulationParams
        sf,
        bw,
        cr,
        ldro};
    cs_low();
    spiWrite(LORA_SPI, data, 5);
    cs_high();
}

void lora_set_packet_params(uint8_t length)
{
    printf("lora_set_packet_params\n");
    uint8_t data[] = {
        0x8C, // SetPacketParams
        0, // preamble length MSB
        8, // preamble length LSB (set to 8 for now, can be changed later)
        0, //0 for explicit header, 1 for implicit header
        length, // payload length
        1, // CRC 0 off 1 on
        0 // invert IQ off
    };
    cs_low();
    spiWrite(LORA_SPI, data, 7);
    cs_high();
}

// Write `len` raw bytes into the SX1262 FIFO at offset 0. Binary-safe: unlike
// the old strlen-based version, this handles payloads that contain zero bytes
// (a packed struct almost always does), so it must be given an explicit length.
int lora_write_buffer(const uint8_t *buf, uint8_t len)
{
    printf("lora_write_buffer (%u bytes)\n", len);
    uint8_t cmd[] = {
        0x0E, // WriteBuffer
        0x00}; // offset 0
    cs_low();
    spiWrite(LORA_SPI, (char *)cmd, 2);
    spiWrite(LORA_SPI, (char *)buf, len);
    cs_high();
    return 0;
}

void lora_set_tx()
{
    printf("lora_set_tx\n");
    uint8_t data[] = {0x83, 0xFF, 0xFF, 0xFF}; // TX no timeout
    cs_low();
    spiWrite(LORA_SPI, data, 4);
    cs_high();
}

void lora_clear_irq()
{
    printf("lora_clear_irq\n");
    uint8_t data[] = {
        0x02,      // ClearIrqStatus
        0x00, 0x01 // Clear TxDone
    };
    cs_low();
    spiWrite(LORA_SPI, data, 3);
    cs_high();
}

void lora_map_irq()
{
    printf("lora_mp_irq\n");
    uint8_t data[] = {
        0x08,             // SetDioIrqParams
        0x00, 0b00000001, // IRQ mask (TX Done)
        0x00, 0b00000001, // DIO1 mask (TX Done)
        0x00, 0x00,       // DIO2 mask
        0x00, 0x00        // DIO3 mask
    };
    cs_low();
    spiWrite(LORA_SPI, data, 9);
    cs_high();
}

void lora_set_tcxo() // 13.3.6, must be sent in STDBY_RC before anything uses the oscillator
{
    printf("lora_set_tcxo\n");
    uint8_t data[] = {
        0x97, // SetDIO3AsTCXOCtrl
        LORA_TCXO_VOLTAGE,
        0x00, 0x01, 0x40 // startup delay, 320 * 15.625us = 5ms
    };
    cs_low();
    spiWrite(LORA_SPI, data, 5);
    cs_high();
}

void lora_calibrate() // 13.1.12, recalibrate after the clock source changes
{
    printf("lora_calibrate\n");
    uint8_t data[] = {
        0x89, // Calibrate
        0x7F  // all blocks
    };
    cs_low();
    spiWrite(LORA_SPI, data, 2);
    cs_high();
}

void lora_clear_device_errors()
{
    uint8_t data[] = {0x07, 0x00, 0x00}; // ClearDeviceErrors
    cs_low();
    spiWrite(LORA_SPI, data, 3);
    cs_high();
}

void lora_get_device_errors() // 13.6.1
{
    char tx_buf[4] = {0x17, 0x00, 0x00, 0x00}; // GetDeviceErrors
    char rx_buf[4] = {0};

    cs_low();
    spiXfer(LORA_SPI, tx_buf, rx_buf, 4);
    cs_high();

    uint16_t err = ((uint8_t)rx_buf[2] << 8) | (uint8_t)rx_buf[3];
    printf("SX1262 Device errors: 0x%04X\n", err);
    if (err & (1 << 0)) printf("  RC64K_CALIB_ERR\n");
    if (err & (1 << 1)) printf("  RC13M_CALIB_ERR\n");
    if (err & (1 << 2)) printf("  PLL_CALIB_ERR\n");
    if (err & (1 << 3)) printf("  ADC_CALIB_ERR\n");
    if (err & (1 << 4)) printf("  IMG_CALIB_ERR\n");
    if (err & (1 << 5)) printf("  XOSC_START_ERR (try flipping LORA_USE_TCXO)\n");
    if (err & (1 << 6)) printf("  PLL_LOCK_ERR\n");
    if (err & (1 << 8)) printf("  PA_RAMP_ERR\n");
}

void lora_set_dio2_rf_switch() // 13.3.5
{
    printf("lora_set_dio2_rf_switch\n");
    uint8_t data[] = {0x9D, 0x01}; // SetDIO2AsRfSwitchCtrl
    cs_low();
    spiWrite(LORA_SPI, data, 2);
    cs_high();
}

uint16_t lora_get_irq_status() // 13.3.3
{
    char tx_buf[4] = {0x12, 0x00, 0x00, 0x00}; // GetIrqStatus
    char rx_buf[4] = {0};

    cs_low();
    spiXfer(LORA_SPI, tx_buf, rx_buf, 4);
    cs_high();

    return ((uint8_t)rx_buf[2] << 8) | (uint8_t)rx_buf[3];
}

void lora_get_status()
{
    // Use arrays of size 2
    char tx_buf[2] = {0xC0, 0x00}; // 0xC0 = GetStatus command, 0x00 = dummy byte to clock out response
    char rx_buf[2] = {0};

    cs_low();
    spiXfer(LORA_SPI, tx_buf, rx_buf, 2);
    cs_high();

    // rx_buf[1] holds the actual status byte returned by the SX1262
    printf("SX1262 Status: 0x%02X\n", (unsigned char)rx_buf[1]);
}

void lora_init()
{
    printf("lora_init\n");
    lora_set_standby();
    lora_clear_device_errors();
#if LORA_USE_TCXO
    lora_set_tcxo();
#endif
    lora_calibrate();
    lora_get_device_errors();
    lora_get_status();
    lora_set_pkt_type(1);
    lora_set_freq();
    lora_set_pa_config();
    lora_set_tx_params(3);
    lora_set_buffer_base(0x00, 0x00);
    lora_set_modulation_params(10, 6, 1, 0);
    lora_set_packet_params(0xFF);
    lora_map_irq();
#if LORA_DIO2_RF_SWITCH
    lora_set_dio2_rf_switch();
#endif
    lora_set_sync_word();
}
void lora_broadcast(const uint8_t *buf, uint8_t len)
{
    printf("Attempting broadcast (%u bytes)\n", len);
    lora_get_status();
    // 1. Write the payload to the FIFO buffer FIRST
    lora_write_buffer(buf, len);
    lora_set_packet_params(len); // send exactly len bytes, not the whole 255 byte buffer

    // 2. Clear any pending interrupts before starting TX
    lora_clear_irq();

    // 3. Put the radio into TX mode (this starts transmission of the buffer)
    lora_set_tx();

    // 4. Wait for TxDone, either on the DIO1 pin or by asking the chip over SPI
    uint32_t start = gpioTick();
    while (!gpioRead(LORA_DIO1))
    {
        if (lora_get_irq_status() & 0x0001)
        {
            // DIO1 usually rises a moment later; only a wiring problem if this happens every time
            printf("note: TxDone seen over SPI before DIO1 (GPIO%d) went high\n", LORA_DIO1);
            break;
        }
        if (gpioTick() - start > LORA_TX_TIMEOUT_US)
        {
            printf("ERROR: TX never finished. IRQ flags: 0x%04X\n", lora_get_irq_status());
            lora_get_status();
            lora_get_device_errors();
            printf("If status shows STBY_RC and errors are 0 the chip probably reset (power brownout?)\n");
            return;
        }
        usleep(1000);
    }

    // 5. Clear the TxDone IRQ flag so the pin goes low again
    lora_clear_irq();
    
    printf("TX done\n");
    lora_get_status();
}


int main()
{
    gpio_setup();


    gpioWrite(LORA_RST, 0);
    gpioDelay(5000); // 5ms
    gpioWrite(LORA_RST, 1);
    gpioDelay(10000); // 10ms
    gpioWrite(LORA_CS, 1); // idle high so the chip doesn't see a half command

    sleep(1);

    lora_init();

    sleep(1);

    // STEP B: send a real telem_frame_t with SYNTHETIC values so the whole
    // pipeline (frame -> LoRa -> Pico -> laptop -> Grafana) can be built and
    // watched before any CAN data exists. Step D replaces these with SocketCAN
    // reads; nothing else in the pipeline has to change.
    telem_frame_t frame = {0};
    frame.type = TELEM_TYPE_DATA;
    uint16_t seq = 0;

    while (1)
    {
        // Sweep each channel through its range so the dashboard shows movement.
        frame.seq = seq;
        frame.timestamp_ms = gpioTick() / 1000u; // pigpio tick is microseconds
        frame.pack_voltage  = 3000 + (seq % 1000);            // 300.0-400.0 V (x10)
        frame.pack_current  = (int16_t)((seq * 7) % 2000) - 500; // -50.0..+150.0 A (x10)
        frame.soc_pct       = 50 + (seq % 50);                // 50-99 %
        frame.cell_temp_max = 250 + (seq % 400);              // 25.0-65.0 C (x10)
        frame.cell_volt_min = 3200 + (seq % 900);             // 3200-4100 mV
        frame.motor_rpm     = (int16_t)((seq * 37) % 6000);   // 0-6000 rpm
        frame.motor_temp    = 300 + (seq % 500);              // 30.0-80.0 C (x10)
        frame.inverter_temp = 350 + (seq % 300);              // 35.0-65.0 C (x10)
        frame.torque_actual = (int16_t)((seq * 3) % 1500);    // 0-150.0 Nm (x10)
        frame.throttle_pct  = seq % 101;                      // 0-100 %
        frame.brake_pct     = (seq * 2) % 101;                // 0-100 %
        frame.speed         = (seq * 5) % 1200;               // 0-120.0 km/h (x10)
        frame.flags         = 0;

        telem_finalize(&frame); // stamps version + CRC
        lora_broadcast((const uint8_t *)&frame, sizeof(frame));

        seq++;
        sleep(1);
    }
}

