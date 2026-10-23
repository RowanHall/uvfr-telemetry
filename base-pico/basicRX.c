#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <strings.h>


//RacecaR1 password to pi debug wifi

//Password to SSH: uvfr@10.1.0.154 (Ethernet) (PassWord) - have to config ethernet settings on pc side (ipv4 BS)
//SSH uvfr@10.2.0.154 (wifi) (PassWord)
//NEW SSH WITH TAILSCALE FROM ANYWHERE (machine must be added to account - message rowan039028 on Discord)
//ssh uvfr@100.80.224.72 (message rowan039028 for passowrd)

//filepath in Pi /home/uvfr/Documents/uvfr-telemetry/car-pi

#include "pico/multicore.h" //hearbeat core
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "../protocol/telemetry.h" // shared wire protocol (telem_frame_t, CRC)

static const uint8_t LORA_FREQ[5] = {0x86, 0x39, 0x38, 0x00, 0x00}; // 915.5 MHz, must match the Pi

#define SPI_PORT spi1

#define LORA_DIO1 20
#define LORA_RESET 15
#define LORA_MISO 12
#define LORA_MOSI 11
#define LORA_SCK 10
#define LORA_CS 3
#define LORA_BUSY 2

// TCXO settings tried in order at boot until the radio actually enters RX (see 13.3.6)
#define LORA_NO_TCXO 0xFF
static const uint8_t TCXO_OPTIONS[] = {LORA_NO_TCXO, 0x02 /* 1.8V */, 0x07 /* 3.3V */};

volatile bool rx_done = false;

void dio1_callback(uint gpio, uint32_t events)
{
    if (gpio == LORA_DIO1 && (events & GPIO_IRQ_EDGE_RISE))
    {
        rx_done = true;
    }
}

void gpio_setup()
{
    gpio_set_function(LORA_MISO, GPIO_FUNC_SPI);
    gpio_set_function(LORA_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(LORA_SCK, GPIO_FUNC_SPI);

    gpio_init(LORA_CS);
    gpio_set_dir(LORA_CS, GPIO_OUT);
    gpio_put(LORA_CS, 1); // idle high, low here makes the radio see a half command

    gpio_init(LORA_RESET);
    gpio_set_dir(LORA_RESET, GPIO_OUT);
    gpio_put(LORA_RESET, 0);

    gpio_init(LORA_BUSY);
    gpio_set_dir(LORA_BUSY, GPIO_IN);
    gpio_pull_down(LORA_BUSY);

    gpio_init(LORA_DIO1);
    gpio_set_dir(LORA_DIO1, GPIO_IN);
    gpio_pull_down(LORA_DIO1);

    gpio_set_irq_enabled_with_callback(
        LORA_DIO1,
        GPIO_IRQ_EDGE_RISE,
        true,
        &dio1_callback);

    spi_init(SPI_PORT, 16 * 1000000);
}

void lora_check_busy()
{
    while(gpio_get(LORA_BUSY)){
        printf("Lora Busy\n");
    }
}

void cs_low()
{
    lora_check_busy();
    sleep_us(10);
    gpio_put(LORA_CS, 0);
}

void cs_high()
{
    gpio_put(LORA_CS, 1);
}

void lora_set_standby()
{
    uint8_t data[] = { 0x80, 0x00 }; // 0x00 = STDBY_RC mode
    cs_low();
    spi_write_blocking(SPI_PORT, data, 2);
    cs_high();
}

void lora_clear_irq()
{
    uint8_t data[] = {
        0x02,      // ClearIrqStatus
        0x00, 0x02 // Clear RxDone
    };

    cs_low();
    spi_write_blocking(spi1, data, 3);
    cs_high();
}

uint8_t lora_read(uint8_t write)
{ // reads a single byte from the SX1262, while sending a byte "write" to the SX1262
    uint8_t data;
    spi_read_blocking(spi1, write, &data, 1);
    return data;
}

void lora_set_dio() // table 8-4, sets DIO1 to RX_DONE, DIO2 to none, DIO3 to none
{
    uint8_t data[] = {
        0x08,             // SetDioIrqParams
        0x00, 0b00000010, // IRQ mask
        0x00, 0b00000010, // DIO1 mask
        0x00, 0x00,       // DIO2 mask
        0x00, 0x00        // DIO3 mask
    };

    cs_low();
    spi_write_blocking(spi1, data, 9);
    cs_high();
}

void lora_set_freq()
{
    cs_low();
    spi_write_blocking(spi1, LORA_FREQ, 5);
    cs_high();
}

void lora_set_rx()
{
    lora_set_standby();
    sleep_ms(1);


    uint8_t data[] = {0x82, 0xFF, 0xFF, 0xFF}; // RX continuous
    cs_low();
    spi_write_blocking(spi1, data, 4);
    cs_high();
}

void lora_get_rx_status(uint8_t *pass) // takes a pointer to a 4 byte array, fills it with the length and offset of the last received packet
{
    uint8_t data = 0x13;
    cs_low();
    spi_write_blocking(spi1, &data, 1);
    spi_read_blocking(spi1, 0x00, pass, 3);
    cs_high();
}

// Reads the last received packet and, if it is a valid telem_frame_t, emits a
// machine-readable CSV line for the laptop dashboard reader (step C). Lines are
// prefixed "DATA," so the Python reader can ignore heartbeat/debug chatter.
// CSV column order (matches the printf below):
//   DATA,seq,ms,packV,packI,soc,cellTmax,cellVmin,rpm,motT,invT,torque,thr,brk,speed,flags
void lora_read_packet(uint8_t length, uint8_t offset)
{
    uint8_t ask[3];
    ask[0] = 0x1E; // ReadBuffer command
    ask[1] = offset;
    ask[2] = 0x00; // dummy byte

    uint8_t data[256] = {0}; // buffer to hold the received packet

    cs_low();
    spi_write_blocking(spi1, ask, 3);
    spi_read_blocking(spi1, 0x00, data, length);
    cs_high();

    if (length != sizeof(telem_frame_t))
    {
        printf("RX %u bytes, not a telem frame (expected %u)\n",
               length, (unsigned)sizeof(telem_frame_t));
        return;
    }

    telem_frame_t *f = (telem_frame_t *)data;
    if (!telem_valid(f))
    {
        printf("RX telem frame FAILED validation (version/CRC) - corrupt or wrong protocol version\n");
        return;
    }

    // Packet-loss tracking via the sequence number.
    static bool have_last = false;
    static uint16_t last_seq = 0;
    if (have_last)
    {
        uint16_t gap = (uint16_t)(f->seq - last_seq - 1);
        if (gap)
            printf("WARN: %u dropped frame(s) before seq %u\n", gap, f->seq);
    }
    last_seq = f->seq;
    have_last = true;

    printf("DATA,%u,%lu,%u,%d,%u,%d,%u,%d,%d,%d,%d,%u,%u,%u,%u\n",
           f->seq, (unsigned long)f->timestamp_ms,
           f->pack_voltage, f->pack_current, f->soc_pct,
           f->cell_temp_max, f->cell_volt_min,
           f->motor_rpm, f->motor_temp, f->inverter_temp, f->torque_actual,
           f->throttle_pct, f->brake_pct, f->speed, f->flags);
}

void heartbeat_core(void)
{
    while (true)
    {
        printf("[HEARTBEAT] Pico is alive\n");
        sleep_ms(1000);
    }
}

void lora_set_packet_type(int type){   // 0 for GFSK, 1 for LoRa
    uint8_t data[] = {
        0x8A, // SetPacketType
        type  
    };
    cs_low();
    spi_write_blocking(spi1, data, 2);
    cs_high();
}

void lora_set_buffer_base(uint8_t tx_base, uint8_t rx_base)
{
    uint8_t data[] = {
        0x8F, // SetBufferBaseAddress
        tx_base,
        rx_base};

    cs_low();
    spi_write_blocking(spi1, data, 3);
    cs_high();
}

/*
I think this needs to be the same between the PI and the Pico
sf can be 5, 6, 7, 8, 9, 10, 11, or 12, lower the faster, higher the more range
bf found on table 13-48, try between 4-6, 4 is more range, 6 is faster
cr can be 1-4, error correction, 1 is fastest, 4 is slowest but most reliable
LowDataRateOptimize - read 6.1.1.4, 0 off 1 on
*/
void lora_set_modulation_params(int sf, int bw, int cr, int ldro)
{
    uint8_t data[] = {
        0x8B, // SetModulationParams
        sf,
        bw,
        cr,
        ldro 
    };
    cs_low();
    spi_write_blocking(spi1, data, 5);
    cs_high();
}

void lora_set_packet_params()
{
    uint8_t data[] = {
        0x8C, // SetPacketParams
        0, // preamble length MSB
        8, // preamble length LSB (set to 8 for now, can be changed later)
        0, //0 for explicit header, 1 for implicit header
        0xFF,
        1, // CRC 0 off 1 on
        0 // invert IQ off
    };
    cs_low();
    spi_write_blocking(spi1, data, 7);
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
    spi_write_blocking(spi1, data, 5);
    cs_high();
}

uint8_t lora_get_status()
{
    uint8_t status_cmd = 0xC0; // GetStatus command
    uint8_t chip_status = 0;

    cs_low();
    spi_write_blocking(spi1, &status_cmd, 1);
    spi_read_blocking(spi1, 0x00, &chip_status, 1);
    cs_high();

    // table 13-76: bits 6:4 chip mode, bits 3:1 command status
    static const char *modes[] = {"?", "?", "STBY_RC", "STBY_XOSC", "FS", "RX", "TX", "?"};
    static const char *cmds[] = {"ok", "?", "data available", "timeout", "processing error", "failed to execute", "tx done", "?"};
    printf("Status 0x%02X: mode %s, last command %s\n",
           chip_status, modes[(chip_status >> 4) & 7], cmds[(chip_status >> 1) & 7]);
    return chip_status;
}

void lora_reset()
{
    gpio_put(LORA_RESET, 0);
    sleep_ms(5);
    gpio_put(LORA_RESET, 1);
    sleep_ms(10);
}

void lora_set_tcxo(uint8_t voltage) // 13.3.6, must be sent in STDBY_RC before anything uses the oscillator
{
    uint8_t data[] = {
        0x97, // SetDIO3AsTCXOCtrl
        voltage,
        0x00, 0x01, 0x40 // startup delay, 320 * 15.625us = 5ms
    };
    cs_low();
    spi_write_blocking(spi1, data, 5);
    cs_high();
}

void lora_calibrate() // 13.1.12, recalibrate after the clock source changes
{
    uint8_t data[] = {0x89, 0x7F}; // Calibrate all blocks
    cs_low();
    spi_write_blocking(spi1, data, 2);
    cs_high();
}

void lora_clear_device_errors()
{
    uint8_t data[] = {0x07, 0x00, 0x00}; // ClearDeviceErrors
    cs_low();
    spi_write_blocking(spi1, data, 3);
    cs_high();
}

void lora_get_device_errors() // 13.6.1
{
    uint8_t cmd = 0x17; // GetDeviceErrors
    uint8_t rx[3] = {0}; // status, OpError MSB, OpError LSB
    cs_low();
    spi_write_blocking(spi1, &cmd, 1);
    spi_read_blocking(spi1, 0x00, rx, 3);
    cs_high();

    uint16_t err = (rx[1] << 8) | rx[2];
    printf("Device errors: 0x%04X\n", err);
    if (err & (1 << 5)) printf("  XOSC_START_ERR (oscillator didn't start)\n");
    if (err & (1 << 6)) printf("  PLL_LOCK_ERR\n");
    if (err & (1 << 2)) printf("  PLL_CALIB_ERR\n");
}

bool lora_init(uint8_t tcxo) // returns true if the radio ends up listening
{
    //setstdby Skipped since first boot
    lora_set_standby();
    lora_get_status();
    lora_clear_device_errors();
    if (tcxo != LORA_NO_TCXO)
    {
        lora_set_tcxo(tcxo);
    }
    lora_calibrate();
    //setpackettype
    lora_set_packet_type(1); // LoRa
    lora_get_status();
    //SetRfFrequency
    lora_set_freq();
    lora_get_status();
    //setbufferBase
    lora_set_buffer_base(0x00, 0x00); // tx base 0, rx base 0
    lora_get_status();
    //SetModulationParams
    lora_set_modulation_params(10, 6, 1, 0);
    lora_get_status();
    //SetPacketParams
    lora_set_packet_params();
    lora_get_status();
    //SetDIO
    lora_set_dio();
    lora_get_status();
    //WriteReg set sync word? Skipped, already private
    lora_set_sync_word();
    lora_get_status();
    //SetRX
    sleep_ms(100);
    lora_set_rx();
    sleep_ms(10);
    lora_get_device_errors();
    return ((lora_get_status() >> 4) & 7) == 5; // mode 5 = RX
}

int main(void)
{
    stdio_init_all();

    multicore_launch_core1(heartbeat_core);

    gpio_setup();


    sleep_ms(5000); // time to open the serial monitor

    uint8_t rx_status_reader[] =
        {
            0x00, // status   0
            0x00, // length   1
            0x00  // offset   2
        };

    bool listening = false;
    for (int i = 0; i < sizeof(TCXO_OPTIONS) && !listening; i++)
    {
        if (TCXO_OPTIONS[i] == LORA_NO_TCXO)
            printf("\n--- Trying without TCXO ---\n");
        else
            printf("\n--- Trying TCXO setting 0x%02X ---\n", TCXO_OPTIONS[i]);

        lora_reset(); // clears any TCXO setting from the last attempt
        listening = lora_init(TCXO_OPTIONS[i]);
    }

    if (listening)
        printf("Listening...\n");
    else
        printf("ERROR: radio never entered RX with any TCXO setting, check module wiring/power\n");

    int loop = 0;
    while (1)
    {
        loop++;
        if(loop>16000000){
            printf("Awaiting RX\n");
            printf("%d", gpio_get(LORA_DIO1));
            printf("\n");
            lora_get_status();
            //lora_set_rx();
            loop = 0;
        }
        if (rx_done)
        {
            printf("RX_DONE interrupt received!\n");
            rx_done = false;
            // find length
            lora_get_rx_status(rx_status_reader);
            // read packet
            lora_read_packet(rx_status_reader[1], rx_status_reader[2]);
            // clear flags
            lora_clear_irq();
            loop = 0;
        }
    }
}
