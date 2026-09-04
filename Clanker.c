#include <stdio.h>
#include <string.h>

#include "pico/multicore.h" //hearbeat core

#include "pico/stdlib.h"
#include "hardware/spi.h"

#define SPI_PORT spi1

#define LORA_DIO1   20
#define LORA_RESET  15
#define LORA_MISO   12
#define LORA_MOSI   11
#define LORA_SCK    10
#define LORA_CS      3
#define LORA_BUSY    2

#define LORA_FREQ 915000000UL

void heartbeat_core(void)
{
    while (true)
    {
        printf("[HEARTBEAT] Pico is alive\n");
        sleep_ms(1000);
    }
}


// -----------------------------------------------------------------------------
// GPIO
// -----------------------------------------------------------------------------

static void lora_cs(bool state)
{
    gpio_put(LORA_CS, state ? 1 : 0);
}

static void lora_reset(void)
{
    gpio_put(LORA_RESET, 0);
    sleep_ms(10);

    gpio_put(LORA_RESET, 1);
    sleep_ms(20);
}

static void lora_wait_busy(void)
{
    while (gpio_get(LORA_BUSY))
        tight_loop_contents();
}

// -----------------------------------------------------------------------------
// SPI
// -----------------------------------------------------------------------------

static void lora_write_command(uint8_t command,
                                const uint8_t *data,
                                size_t length)
{
    lora_wait_busy();

    lora_cs(false);

    spi_write_blocking(SPI_PORT, &command, 1);

    if (length > 0)
        spi_write_blocking(SPI_PORT, data, length);

    lora_cs(true);

    lora_wait_busy();
}

static void lora_read_command(uint8_t command,
                               uint8_t *data,
                               size_t length)
{
    uint8_t dummy = 0;

    lora_wait_busy();

    lora_cs(false);

    spi_write_blocking(SPI_PORT, &command, 1);
    spi_write_blocking(SPI_PORT, &dummy, 1);

    if (length > 0)
        spi_read_blocking(SPI_PORT, 0, data, length);

    lora_cs(true);
}

// -----------------------------------------------------------------------------
// SX1262
// -----------------------------------------------------------------------------

static void lora_set_standby(void)
{
    uint8_t data[] = { 0x00 }; // STDBY_RC

    lora_write_command(0x80, data, 1);
}

static void lora_set_packet_type(void)
{
    uint8_t data[] = { 0x01 }; // LoRa

    lora_write_command(0x8A, data, 1);
}

static void lora_set_frequency(uint32_t frequency)
{
    uint32_t frf =
        ((uint64_t)frequency << 25) / 32000000ULL;

    uint8_t data[4];

    data[0] = (frf >> 24) & 0xFF;
    data[1] = (frf >> 16) & 0xFF;
    data[2] = (frf >> 8) & 0xFF;
    data[3] = frf & 0xFF;

    lora_write_command(0x86, data, 4);
}

static void lora_set_modulation(void)
{
    // SF7, BW125, CR4/5, LDRO off

    uint8_t data[] = {
        0x07,
        0x04,
        0x01,
        0x00
    };

    lora_write_command(0x8B, data, 4);
}

static void lora_set_packet_params(void)
{
    // Preamble = 8
    // Variable-length header
    // CRC enabled
    // Standard IQ

    uint8_t data[] = {
        0x00, 0x08,   // Preamble length
        0x00,         // Variable-length header
        0xFF,         // Maximum payload length
        0x01,         // CRC enabled
        0x00          // Standard IQ
    };

    lora_write_command(0x8C, data, 6);
}

static void lora_set_buffer_base(void)
{
    uint8_t data[] = {
        0x00,    // TX base
        0x00     // RX base
    };

    lora_write_command(0x8F, data, 2);
}

static void lora_set_rx(void)
{
    // RX continuous

    uint8_t data[] = {
        0xFF,
        0xFF,
        0xFF
    };

    lora_write_command(0x82, data, 3);
}

static uint16_t lora_get_irq_status(void)
{
    uint8_t data[2];

    lora_read_command(0x12, data, 2);

    return ((uint16_t)data[0] << 8) | data[1];
}

static void lora_clear_irq(uint16_t irq)
{
    uint8_t data[] = {
        (uint8_t)(irq >> 8),
        (uint8_t)(irq & 0xFF)
    };

    lora_write_command(0x02, data, 2);
}

static void lora_get_rx_buffer(uint8_t *payload,
                               uint8_t length,
                               uint8_t offset)
{
    uint8_t command = 0x1E;
    uint8_t dummy = 0x00;

    lora_wait_busy();

    lora_cs(false);

    // ReadBuffer command
    spi_write_blocking(SPI_PORT, &command, 1);

    // Buffer offset
    spi_write_blocking(SPI_PORT, &offset, 1);

    // Dummy byte
    spi_write_blocking(SPI_PORT, &dummy, 1);

    // Read payload
    spi_read_blocking(SPI_PORT, 0, payload, length);

    lora_cs(true);
}

static void lora_get_rx_status(uint8_t *length,
                               uint8_t *offset)
{
    uint8_t data[2];

    lora_read_command(0x13, data, 2);

    *length = data[0];
    *offset = data[1];
}

// -----------------------------------------------------------------------------
// Initialize
// -----------------------------------------------------------------------------

static void lora_init(void)
{
    spi_init(SPI_PORT, 8000000);

    gpio_set_function(LORA_MISO, GPIO_FUNC_SPI);
    gpio_set_function(LORA_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(LORA_SCK,  GPIO_FUNC_SPI);

    gpio_init(LORA_CS);
    gpio_set_dir(LORA_CS, GPIO_OUT);
    gpio_put(LORA_CS, 1);

    gpio_init(LORA_RESET);
    gpio_set_dir(LORA_RESET, GPIO_OUT);
    gpio_put(LORA_RESET, 1);

    gpio_init(LORA_BUSY);
    gpio_set_dir(LORA_BUSY, GPIO_IN);

    gpio_init(LORA_DIO1);
    gpio_set_dir(LORA_DIO1, GPIO_IN);

    lora_reset();

    lora_wait_busy();

    lora_set_standby();
    lora_set_packet_type();
    lora_set_frequency(LORA_FREQ);
    lora_set_modulation();
    lora_set_packet_params();
    lora_set_buffer_base();

    // Enable RX_DONE, CRC_ERROR, and TIMEOUT
    uint8_t irq[] = {
        0x02, 0x42,   // IRQ mask: RX_DONE + CRC_ERROR + TIMEOUT
        0x02, 0x42,   // DIO1 mask
        0x00, 0x00,   // DIO2
        0x00, 0x00    // DIO3
    };

    lora_write_command(0x08, irq, 8);

    lora_clear_irq(0xFFFF);
    lora_set_rx();
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

int main(void)
{
    stdio_init_all();
    
    multicore_launch_core1(heartbeat_core);
    


    sleep_ms(2000);

    printf("\nSX1262 receiver\n");
    printf("Frequency: 915 MHz\n");

    lora_init();

    printf("Listening...\n");

    while (true)
    {
        uint16_t irq = lora_get_irq_status();

        if (irq != 0)
        {
            printf("IRQ: 0x%04X\n", irq);
        }

        if (irq & 0x0040)
        {
            printf("CRC error\n");
        }

        if (irq & 0x0200)
        {
            printf("RX timeout\n");
        }

        if (irq & 0x0002)
        {
            uint8_t length;
            uint8_t offset;

            lora_get_rx_status(&length, &offset);

            printf("RX_DONE: length=%u offset=%u\n", length, offset);

            uint8_t buffer[256];

            if (length > 0)
            {
                lora_get_rx_buffer(buffer, length, offset);
                buffer[length] = '\0';

                printf("Received: %.*s\n", length, buffer);
            }

            lora_clear_irq(0xFFFF);
            lora_set_rx();
        }

        sleep_ms(10);
    }
}