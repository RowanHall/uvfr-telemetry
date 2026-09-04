#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "pico/multicore.h" //hearbeat core
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#define SPI_PORT spi1

#define LORA_DIO1 20
#define LORA_RESET 15
#define LORA_MISO 12
#define LORA_MOSI 11
#define LORA_SCK 10
#define LORA_CS 3
#define LORA_BUSY 2

volatile bool rx_done = false;

void dio1_callback(uint gpio, uint32_t events)
{
    if (gpio == LORA_DIO1 && (events & GPIO_IRQ_EDGE_RISE))
    {
        rx_done = true;
    }
}

void gpio_setup(void)
{
    gpio_set_function(LORA_MISO, GPIO_FUNC_SPI);
    gpio_set_function(LORA_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(LORA_SCK, GPIO_FUNC_SPI);

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
    gpio_pull_down(LORA_DIO1);

    gpio_set_irq_enabled_with_callback(
        LORA_DIO1,
        GPIO_IRQ_EDGE_RISE,
        true,
        &dio1_callback);

    spi_init(SPI_PORT, 16 * 1000000);
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


void cs_low(void)
{
    gpio_put(LORA_CS, 0);
}

void cs_high(void)
{
    gpio_put(LORA_CS, 1);
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
        0x0D,             // SetDioIrqParams
        0x00, 0b00000010, // IRQ mask
        0x00, 0b00000010, // DIO1 mask
        0x00, 0x00,       // DIO2 mask
        0x00, 0x00        // DIO3 mask
    };

    cs_low();
    spi_write_blocking(spi1, data, 9);
    cs_high();
}

void lora_set_rx()
{
    uint8_t data[] = {0x82, 0xFF, 0xFF, 0xFF}; // RX continuous

    cs_low();
    spi_write_blocking(spi1, data, 4);
    cs_high();
}

void heartbeat_core(void)
{
    while (true)
    {
        printf("[HEARTBEAT] Pico is alive\n");
        sleep_ms(1000);
    }
}

int main(void)
{
    stdio_init_all();

    multicore_launch_core1(heartbeat_core);

    gpio_setup();

    sleep_ms(2000);

    printf("\nSX1262 receiver\n");
    printf("Frequency: 915 MHz\n");

    // lora_init();
    lora_set_rx();
    lora_set_dio();

    printf("Listening...\n");

    while (1)
    {
        if (rx_done)
        {
            printf("RX_DONE interrupt received!\n");
            rx_done = false;
            printf("Received data: 0x%02X\n", lora_read(0x00));
            lora_clear_irq();
        }
    }
}