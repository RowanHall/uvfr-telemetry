#include <stdio.h>
#include <unistd.h>
#include <pigpio.h>

#define LORA_CS 21
#define LORA_CLK 11
#define LORA_MOSI 10
#define LORA_MISO 9
#define LORA_BUSY 20
#define LORA_DIO1 16
#define LORA_DIO4 6
#define LORA_RST 18
#define LORA_SPI 0

static uint8_t LORA_FREQ[5] = {0x86, 0x39, 0x38, 0x00, 0x00};

volatile int tx_status = 0;

void gpio_setup()
{
    gpioInitialise();
    gpioSetMode(LORA_CS, PI_OUTPUT);
    gpioSetMode(LORA_RST, PI_OUTPUT);
    gpioSetMode(LORA_BUSY, PI_INPUT);
    gpioSetMode(LORA_DIO1, PI_INPUT);
    gpioSetMode(LORA_DIO4, PI_INPUT);
    spiOpen(LORA_SPI, 16 * 1000000, 0x100);
}

void cs_low()
{
    gpioWrite(LORA_CS, 0);
}

void cs_high()
{
    gpioWrite(LORA_CS, 1);
}

void lora_set_xosc() // 13.1.15
{
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
    uint8_t data[] = {
        0x8A, // SetPacketType
        type};
    cs_low();
    spiWrite(LORA_SPI, data, 2);
    cs_high();
}

void lora_set_freq()
{
    cs_low();
    spiWrite(LORA_SPI, LORA_FREQ, 5);
    cs_high();
}

void lora_set_pa_config()
{
    uint8_t data[] = {
        0x8C, // SetPacketParams
        0,    // preamble length MSB
        8,    // preamble length LSB (set to 8 for now, can be changed later)
        0,    // 0 for explicit header, 1 for implicit header
        0xFF,
        0, // CRC off
        0, // invert IQ off
        0,
        0,
        0};
    cs_low();
    spiWrite(LORA_SPI, data, 10);
    cs_high();
}

void lora_set_tx_params(int ramp) // lower better? start with 3 I think
{
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
    uint8_t data[] = {
        0x8B, // SetModulationParams
        sf,
        bw,
        cr,
        ldro,
        0,
        0,
        0,
        0};
    cs_low();
    spiWrite(LORA_SPI, data, 9);
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
        0, // CRC off
        0, // invert IQ off
        0,
        0,
        0
    };
    cs_low();
    spiWrite(LORA_SPI, data, 10);
    cs_high();
}

int lora_write_buffer(char *words)
{
    int length = sizeof(words) / sizeof(words[0]);
    if (length > 255)
    {
        return length;
    }
    uint8_t data[] = {
        0x0E,
        0x00};
    cs_low();
    spiWrite(LORA_SPI, data, 2);
    spiWrite(LORA_SPI, words, length);
    cs_high();
    return 0;
}

void lora_set_tx()
{
    uint8_t data[] = {0x83, 0xFF, 0xFF, 0xFF}; // TX no timeout
    cs_low();
    spiWrite(LORA_SPI, data, 4);
    cs_high();
}

void lora_clear_irq()
{
    uint8_t data[] = {
        0x01,      // ClearIrqStatus
        0x00, 0x01 // Clear RxDone
    };
    cs_low();
    spiWrite(LORA_SPI, data, 3);
    cs_high();
}

void lora_map_irq()
{
    uint8_t data[] = {
        0x0D,             // SetDioIrqParams
        0x00, 0b00000001, // IRQ mask
        0x00, 0b00000001, // DIO1 mask
        0x00, 0x00,       // DIO2 mask
        0x00, 0x00        // DIO3 mask
    };
    cs_low();
    spiWrite(LORA_SPI, data, 9);
    cs_high();
}



void lora_init()
{
    lora_set_pkt_type(1);
    lora_set_freq();
    lora_set_pa_config();
    lora_set_tx_params(3);
    lora_set_buffer_base(0x00, 0x00);
    lora_set_modulation_params(10, 6, 1, 0);
    lora_set_packet_params();
    lora_map_irq();
}

void lora_broadcast(char *words)
{
    // what do here? set tx? write buffer? need to read pdf again
    if (!tx_status) //check tx status, if its not done, loop until TX IRQ trips
    {
        while (!gpioRead(LORA_DIO1))
        {
            printf("TX NOT DONE");
        }
        lora_clear_irq();
        tx_status = 1;
    }
    if(lora_write_buffer(words))
    {
        printf("error writing to buffer");
    }
    tx_status = 0;
}

int main()
{
    char send[] = "Testing Testing 123";
    gpio_setup();

    lora_init();

    sleep(1);
    while (1)
    {
        lora_broadcast(send);
        sleep(1);
    }
}