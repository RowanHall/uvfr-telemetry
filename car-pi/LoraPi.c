/*
gcc LoraPi.c -o LoraPi -lpigpio

sudo ./LoraPi
*/


#include <stdio.h>
#include <string.h>
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

static uint8_t LORA_FREQ[5] = {0x86, 0x34, 0x40, 0x00, 0x00};//{0x86, 0x39, 0x38, 0x00, 0x00};

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
    while(gpioRead(LORA_BUSY)){
        printf("loraBusy\n");
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

void lora_set_packet_params()
{
    printf("lora_set_packet_params\n");
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
    spiWrite(LORA_SPI, data, 7);
    cs_high();
}

int lora_write_buffer(char *words)
{
    printf("lora_write_buffer\n");
    int length = strlen(words);
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
    lora_set_pkt_type(1);
    lora_set_freq();
    lora_set_pa_config();
    lora_set_tx_params(3);
    lora_set_buffer_base(0x00, 0x00);
    lora_set_modulation_params(10, 6, 1, 0);
    lora_set_packet_params();
    lora_map_irq();
    lora_set_sync_word();
}
/*
void lora_broadcast(char *words)
{
    printf("attempting broadcast\n");
    // what do here? set tx? write buffer? need to read pdf again
    if (!tx_status) //check tx status, if its not done, loop until TX IRQ trips
    {
        while (!gpioRead(LORA_DIO1))
        {
            printf("TX NOT DONE.\n");
        }
        lora_clear_irq();
        tx_status = 1;
    }
    printf("TX done\n");
    if(lora_write_buffer(words))
    {
        printf("error writing to buffer\n");
        return;
    }
    lora_set_tx();
    tx_status = 0;
}
*/

void lora_broadcast(char *words)
{
    printf("Attempting broadcast\n");
    lora_get_status();
    // 1. Write the payload to the FIFO buffer FIRST
    if (lora_write_buffer(words))
    {
        printf("Error writing to buffer (too long)\n");
        return;
    }

    // 2. Clear any pending interrupts before starting TX
    lora_clear_irq();

    // 3. Put the radio into TX mode (this starts transmission of the buffer)
    lora_set_tx();

    // 4. Wait for DIO1 to go high, indicating TxDone
    while (!gpioRead(LORA_DIO1))
    {
        // Optional short sleep or print, but busy-waiting works for simple scripts
        usleep(1000); 
    }

    // 5. Clear the TxDone IRQ flag so the pin goes low again
    lora_clear_irq();
    
    printf("TX done\n");
    lora_get_status();
}


int main()
{
    char send[] = "Testing Testing 123";
    gpio_setup();


    gpioWrite(LORA_RST, 0);
    gpioDelay(5000); // 5ms
    gpioWrite(LORA_RST, 1);
    gpioDelay(10000); // 10ms

    sleep(1);

    lora_init();

    sleep(1);
    while (1)
    {
        lora_broadcast(send);
        sleep(1);
    }
}

