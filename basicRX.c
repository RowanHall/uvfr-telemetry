#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <strings.h>

#include "pico/multicore.h" //hearbeat core
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

static const uint8_t LORA_FREQ[5] = {0x86, 0x39, 0x38, 0x00, 0x00};

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

void gpio_setup()
{
    gpio_set_function(LORA_MISO, GPIO_FUNC_SPI);
    gpio_set_function(LORA_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(LORA_SCK, GPIO_FUNC_SPI);

    gpio_init(LORA_CS);
    gpio_set_dir(LORA_CS, GPIO_OUT);
    gpio_put(LORA_CS, 0);

    gpio_init(LORA_RESET);
    gpio_set_dir(LORA_RESET, GPIO_OUT);
    gpio_put(LORA_RESET, 0);

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

void lora_check_busy()
{
    while(gpio_get(LORA_BUSY)){
        printf("Lora Busy\n");
    }
}

void cs_low()
{
    lora_check_busy();
    gpio_put(LORA_CS, 0);
}

void cs_high()
{
    gpio_put(LORA_CS, 1);
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

void lora_set_freq()
{
    cs_low();
    spi_write_blocking(spi1, LORA_FREQ, 5);
    cs_high();
}

void lora_set_rx()
{
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

void lora_read_packet(uint8_t length, uint8_t offset) // reads the last received packet, returns it as a string
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

    printf("Received %u bytes: ", length);

    for (uint8_t i = 0; i < length; i++)
    {
        printf("%02X ", data[i]);
    }

    printf("\n");
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
        ldro,
        0,
        0,
        0,
        0 
    };
    cs_low();
    spi_write_blocking(spi1, data, 9);
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
        1, // CRC off
        0, // invert IQ off
        0,
        0,
        0
    };
    cs_low();
    spi_write_blocking(spi1, data, 10);
    cs_high();
}


void lora_init()
{
    //setstdby Skipped since first boot
    //setpackettype
    lora_set_packet_type(1); // LoRa
    //SetRfFrequency
    lora_set_freq();
    //setbufferBase
    lora_set_buffer_base(0x00, 0x00); // tx base 0, rx base 0
    //SetModulationParams
    lora_set_modulation_params(10, 6, 1, 0);
    //SetPacketParams
    lora_set_packet_params();
    //SetDIO
    lora_set_dio();
    //WriteReg set sync word? Skipped, already private
    //SetRX
    lora_set_rx();
}

int main(void)
{
    stdio_init_all();

    multicore_launch_core1(heartbeat_core);

    gpio_setup();


    gpio_put(LORA_RESET, 0);
    sleep_ms(5);          // Hold reset low for 5ms
    gpio_put(LORA_RESET, 1); // Bring it back high
    sleep_ms(10);


    sleep_ms(2000);

    uint8_t rx_status_reader[] =
        {
            0x00, // status   0
            0x00, // length   1
            0x00  // offset   2
        };

    lora_init();

    printf("Listening...\n");

    int loop = 0;
    while (1)
    {
        loop++;
        if(loop>10000000){
            printf("Awaiting RX\n");
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