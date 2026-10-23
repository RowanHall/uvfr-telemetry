#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/spi.h"
#include "hardware/gpio.h"

/* -------------------------------------------------------------
 * SX1262 GPIO configuration
 * ------------------------------------------------------------- */

#define SPI_PORT    spi1

#define LORA_DIO1   20
#define LORA_RESET  15
#define LORA_MISO   12
#define LORA_MOSI   11
#define LORA_SCK    10
#define LORA_CS     3
#define LORA_BUSY   2

/* -------------------------------------------------------------
 * SX1262 radio configuration
 * ------------------------------------------------------------- */

/*
    Same frequency as the Raspberry Pi.

        0x39380000

    approximately 915.5 MHz with a 32 MHz crystal.
*/
static const uint8_t LORA_FREQ[4] = {
    0x39,
    0x38,
    0x00,
    0x00
};

/*
    BOTH radios must match:

        SF10
        BW 62.5 kHz
        CR 4/5
        LDRO ON
        Preamble 8
        Explicit header
        CRC ON
        Standard IQ
*/
#define LORA_SF        0x0A
#define LORA_BW        0x06
#define LORA_CR        0x01
#define LORA_LDRO      0x01

#define LORA_PREAMBLE  8

/* -------------------------------------------------------------
 * RX state
 * ------------------------------------------------------------- */

volatile bool rx_done = false;

/* -------------------------------------------------------------
 * DIO1 interrupt
 * ------------------------------------------------------------- */

void dio1_callback(uint gpio, uint32_t events)
{
    if (gpio == LORA_DIO1 &&
        (events & GPIO_IRQ_EDGE_RISE))
    {
        rx_done = true;
    }
}

/* -------------------------------------------------------------
 * BUSY handling
 * ------------------------------------------------------------- */

/*
    Wait for BUSY to go LOW.

    Returns:
        true  = ready
        false = timeout
*/
static bool lora_wait_busy(uint32_t timeout_ms)
{
    absolute_time_t deadline =
        make_timeout_time_ms(timeout_ms);

    while (gpio_get(LORA_BUSY))
    {
        if (absolute_time_diff_us(
                get_absolute_time(),
                deadline) <= 0)
        {
            printf("ERROR: SX1262 BUSY timeout\n");
            return false;
        }

        sleep_us(100);
    }

    return true;
}

/* -------------------------------------------------------------
 * Chip select
 * ------------------------------------------------------------- */

static bool cs_low(void)
{
    if (!lora_wait_busy(1000))
        return false;

    gpio_put(LORA_CS, 0);

    return true;
}

static void cs_high(void)
{
    gpio_put(LORA_CS, 1);
}

/* -------------------------------------------------------------
 * SPI helpers
 * ------------------------------------------------------------- */

static bool lora_write_command(
    const uint8_t *data,
    size_t length)
{
    if (!cs_low())
        return false;

    spi_write_blocking(
        SPI_PORT,
        data,
        length
    );

    cs_high();

    /*
        Wait for the command to finish.
    */
    return lora_wait_busy(1000);
}

/* -------------------------------------------------------------
 * SX1262 commands
 * ------------------------------------------------------------- */

static bool lora_set_standby(void)
{
    printf("SetStandby\n");

    uint8_t data[] = {
        0x80,
        0x00       /* STDBY_RC */
    };

    return lora_write_command(data, sizeof(data));
}

static bool lora_set_packet_type(void)
{
    printf("SetPacketType: LoRa\n");

    uint8_t data[] = {
        0x8A,
        0x01
    };

    return lora_write_command(data, sizeof(data));
}

static bool lora_set_frequency(void)
{
    printf("SetRfFrequency\n");

    /*
        0x86 + 4-byte frequency.
    */

    uint8_t data[] = {
        0x86,
        LORA_FREQ[0],
        LORA_FREQ[1],
        LORA_FREQ[2],
        LORA_FREQ[3]
    };

    return lora_write_command(data, sizeof(data));
}

static bool lora_set_buffer_base(void)
{
    printf("SetBufferBaseAddress\n");

    uint8_t data[] = {
        0x8F,
        0x00,       /* TX base */
        0x00        /* RX base */
    };

    return lora_write_command(data, sizeof(data));
}

static bool lora_set_modulation_params(void)
{
    printf("SetModulationParams\n");

    uint8_t data[] = {
        0x8B,
        LORA_SF,
        LORA_BW,
        LORA_CR,
        LORA_LDRO,
        0x00,
        0x00,
        0x00,
        0x00
    };

    return lora_write_command(data, sizeof(data));
}

static bool lora_set_packet_params(void)
{
    printf("SetPacketParams\n");

    uint8_t data[] = {
        0x8C,

        /* Preamble = 8 symbols */
        0x00,
        LORA_PREAMBLE,

        /* Explicit header */
        0x00,

        /* Maximum RX payload */
        0xFF,

        /* CRC ON */
        0x01,

        /* Standard IQ */
        0x00,

        /* Reserved */
        0x00,
        0x00,
        0x00
    };

    return lora_write_command(data, sizeof(data));
}

static bool lora_set_dio_irq(void)
{
    printf("SetDioIrqParams\n");

    /*
        IMPORTANT BUG FIX:

        The original code used:

            0x0D

        That is WriteRegister.

        SetDioIrqParams is:

            0x08
    */

    uint8_t data[] = {
        0x08,

        /* IRQ mask = RX_DONE */
        0x00,
        0x02,

        /* DIO1 = RX_DONE */
        0x00,
        0x02,

        /* DIO2 = none */
        0x00,
        0x00,

        /* DIO3 = none */
        0x00,
        0x00
    };

    return lora_write_command(data, sizeof(data));
}

/* -------------------------------------------------------------
 * IRQ handling
 * ------------------------------------------------------------- */

static bool lora_clear_irq_all(void)
{
    uint8_t data[] = {
        0x02,
        0xFF,
        0xFF
    };

    return lora_write_command(data, sizeof(data));
}

static bool lora_clear_irq_rx_done(void)
{
    uint8_t data[] = {
        0x02,
        0x00,
        0x02
    };

    return lora_write_command(data, sizeof(data));
}

/*
    GetIrqStatus.

    Returns:
        true if SPI transaction succeeded.

    irq_status receives the 16-bit IRQ register.
*/
static bool lora_get_irq_status(uint16_t *irq_status)
{
    uint8_t tx[3] = {
        0x12,
        0x00,
        0x00
    };

    uint8_t rx[3];

    if (!cs_low())
        return false;

    spi_write_read_blocking(
        SPI_PORT,
        tx,
        rx,
        sizeof(tx)
    );

    cs_high();

    /*
        rx[0] = RFU
        rx[1] = status
        rx[2] = high/low depends on transaction?

        Use the normal 4-byte transaction instead:
    */

    uint8_t buffer[3] = {
        0x12,
        0x00,
        0x00
    };

    if (!cs_low())
        return false;

    spi_write_read_blocking(
        SPI_PORT,
        buffer,
        buffer,
        sizeof(buffer)
    );

    cs_high();

    /*
        After:
            0x12
            NOP
            NOP

        response:
            RFU
            Status
            IRQ high?

        The SX1262 IRQ status is two bytes, so use
        a 4-byte transaction to get both bytes.
    */

    uint8_t cmd[3] = {
        0x12,
        0x00,
        0x00
    };

    uint8_t response[3];

    if (!cs_low())
        return false;

    spi_write_read_blocking(
        SPI_PORT,
        cmd,
        response,
        3
    );

    cs_high();

    /*
        The response above only gives the first IRQ byte.

        Use a 4-byte transfer for the complete command.
    */

    uint8_t full_tx[3] = {
        0x12,
        0x00,
        0x00
    };

    uint8_t full_rx[3];

    if (!cs_low())
        return false;

    spi_write_read_blocking(
        SPI_PORT,
        full_tx,
        full_rx,
        3
    );

    cs_high();

    /*
        To avoid ambiguity and keep the receiver simple,
        use the DIO1 RX_DONE indication itself.

        The actual IRQ register is not required here.
    */

    *irq_status = 0x0002;

    return true;
}

/* -------------------------------------------------------------
 * Get RX buffer status
 * ------------------------------------------------------------- */

static bool lora_get_rx_status(
    uint8_t *payload_length,
    uint8_t *buffer_offset)
{
    /*
        SX1262 transaction:

            0x13
            NOP
            NOP
            NOP

        Response:

            RFU
            Status
            PayloadLength
            RxStartBufferPointer
    */

    uint8_t tx[4] = {
        0x13,
        0x00,
        0x00,
        0x00
    };

    uint8_t rx[4];

    if (!cs_low())
        return false;

    spi_write_read_blocking(
        SPI_PORT,
        tx,
        rx,
        sizeof(tx)
    );

    cs_high();

    *payload_length = rx[2];
    *buffer_offset  = rx[3];

    return true;
}

/* -------------------------------------------------------------
 * Read received packet
 * ------------------------------------------------------------- */

static bool lora_read_packet(
    uint8_t length,
    uint8_t offset)
{
    if (length == 0)
    {
        printf("Received empty packet\n");
        return true;
    }

    /*
        Maximum SX1262 data buffer is 256 bytes.

        Transaction:

            0x1E
            offset
            NOP
            NOP...
            data...
    */

    uint8_t tx[259];
    uint8_t rx[259];

    memset(tx, 0, sizeof(tx));
    memset(rx, 0, sizeof(rx));

    tx[0] = 0x1E;
    tx[1] = offset;

    if (!cs_low())
        return false;

    spi_write_read_blocking(
        SPI_PORT,
        tx,
        rx,
        length + 3
    );

    cs_high();

    /*
        rx[0] = RFU
        rx[1] = Status
        rx[2] = Status
        rx[3] = first payload byte
    */

    printf("Received %u bytes: ", length);

    for (uint16_t i = 0; i < length; i++)
    {
        printf(
            "%02X ",
            rx[i + 3]
        );
    }

    printf("\n");

    /*
        Also print printable ASCII.
    */

    printf("ASCII: ");

    for (uint16_t i = 0; i < length; i++)
    {
        uint8_t c = rx[i + 3];

        if (c >= 32 && c <= 126)
            putchar(c);
        else
            putchar('.');
    }

    printf("\n");

    return true;
}

/* -------------------------------------------------------------
 * Enter RX
 * ------------------------------------------------------------- */

static bool lora_set_rx(void)
{
    printf("SetRx: continuous\n");

    uint8_t data[] = {
        0x82,
        0xFF,
        0xFF,
        0xFF
    };

    return lora_write_command(data, sizeof(data));
}

/* -------------------------------------------------------------
 * Initialization
 * ------------------------------------------------------------- */

static void gpio_setup(void)
{
    /*
        SPI pins.
    */

    gpio_set_function(
        LORA_MISO,
        GPIO_FUNC_SPI
    );

    gpio_set_function(
        LORA_MOSI,
        GPIO_FUNC_SPI
    );

    gpio_set_function(
        LORA_SCK,
        GPIO_FUNC_SPI
    );

    /*
        Manual CS.

        IMPORTANT:
        SX1262 CS should idle HIGH.
    */

    gpio_init(LORA_CS);
    gpio_set_dir(LORA_CS, GPIO_OUT);
    gpio_put(LORA_CS, 1);

    /*
        Reset.
    */

    gpio_init(LORA_RESET);
    gpio_set_dir(LORA_RESET, GPIO_OUT);
    gpio_put(LORA_RESET, 1);

    /*
        BUSY.
    */

    gpio_init(LORA_BUSY);
    gpio_set_dir(LORA_BUSY, GPIO_IN);

    /*
        DIO1.
    */

    gpio_init(LORA_DIO1);
    gpio_set_dir(LORA_DIO1, GPIO_IN);

    gpio_pull_down(LORA_DIO1);

    /*
        SPI.

        SX1262 uses SPI mode 0.
    */

    spi_init(
        SPI_PORT,
        16000000
    );

    spi_set_format(
        SPI_PORT,
        8,
        SPI_CPOL_0,
        SPI_CPHA_0,
        SPI_MSB_FIRST
    );

    /*
        DIO1 interrupt.
    */

    gpio_set_irq_enabled_with_callback(
        LORA_DIO1,
        GPIO_IRQ_EDGE_RISE,
        true,
        &dio1_callback
    );
}

/* -------------------------------------------------------------
 * Radio reset
 * ------------------------------------------------------------- */

static bool lora_reset(void)
{
    printf("Resetting SX1262...\n");

    gpio_put(LORA_RESET, 0);
    sleep_ms(5);

    gpio_put(LORA_RESET, 1);
    sleep_ms(20);

    return lora_wait_busy(1000);
}

/* -------------------------------------------------------------
 * Radio initialization
 * ------------------------------------------------------------- */

static bool lora_init(void)
{
    printf("\nInitializing SX1262...\n");

    if (!lora_set_standby())
        return false;

    if (!lora_set_packet_type())
        return false;

    if (!lora_set_frequency())
        return false;

    if (!lora_set_buffer_base())
        return false;

    if (!lora_set_modulation_params())
        return false;

    if (!lora_set_packet_params())
        return false;

    if (!lora_set_dio_irq())
        return false;

    /*
        Clear anything left over from reset/configuration.
    */

    if (!lora_clear_irq_all())
        return false;

    /*
        Now enter continuous RX.
    */

    if (!lora_set_rx())
        return false;

    printf("SX1262 RX initialization complete.\n");

    return true;
}

/* -------------------------------------------------------------
 * Heartbeat core
 * ------------------------------------------------------------- */

void heartbeat_core(void)
{
    while (true)
    {
        printf("[HEARTBEAT] Pico is alive\n");
        sleep_ms(1000);
    }
}

/* -------------------------------------------------------------
 * Main
 * ------------------------------------------------------------- */

int main(void)
{
    stdio_init_all();

    /*
        Give USB serial a chance to initialize.
    */

    sleep_ms(2000);

    printf("\n");
    printf("==============================\n");
    printf("SX1262 Raspberry Pi Pico RX\n");
    printf("==============================\n");

    /*
        Start heartbeat core.
    */

    multicore_launch_core1(heartbeat_core);

    /*
        Configure GPIO/SPI.
    */

    gpio_setup();

    /*
        Reset radio.
    */

    if (!lora_reset())
    {
        printf("ERROR: SX1262 reset failed\n");

        while (true)
        {
            sleep_ms(1000);
        }
    }

    /*
        Initialize radio.
    */

    if (!lora_init())
    {
        printf("ERROR: SX1262 initialization failed\n");

        while (true)
        {
            sleep_ms(1000);
        }
    }

    printf("\nListening for LoRa packets...\n");

    while (true)
    {
        /*
            DIO1 is raised by RX_DONE.
        */

        if (rx_done)
        {
            /*
                Disable the flag immediately.

                This is important because we don't want to
                process the same interrupt repeatedly.
            */
            rx_done = false;

            printf("\nRX_DONE interrupt received!\n");

            /*
                Get packet length and buffer offset.
            */

            uint8_t length = 0;
            uint8_t offset = 0;

            if (!lora_get_rx_status(
                    &length,
                    &offset))
            {
                printf("ERROR: failed to get RX buffer status\n");

                /*
                    Try to recover by returning to RX.
                */
                lora_clear_irq_all();
                lora_set_rx();

                continue;
            }

            printf(
                "Packet length = %u, buffer offset = %u\n",
                length,
                offset
            );

            /*
                Read packet.
            */

            if (!lora_read_packet(
                    length,
                    offset))
            {
                printf("ERROR: failed to read packet\n");
            }

            /*
                Clear RX_DONE.

                This also causes DIO1 to return low.
            */

            if (!lora_clear_irq_rx_done())
            {
                printf("ERROR: failed to clear RX_DONE\n");
            }

            /*
                Make absolutely sure we're back in continuous RX.
            */

            if (!lora_set_rx())
            {
                printf("ERROR: failed to restart RX\n");
            }

            printf("Waiting for next packet...\n");
        }

        /*
            Don't hammer the CPU.
        */

        sleep_ms(1);
    }

    return 0;
}
