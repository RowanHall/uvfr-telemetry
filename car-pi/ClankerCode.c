/*
    SX1262 Raspberry Pi TRANSMITTER
    =================================

    Build:
        gcc LoraPi.c -o LoraPi -lpigpio

    Run:
        sudo ./LoraPi


    GPIO:
        CS      = GPIO21
        SCK     = GPIO11
        MOSI    = GPIO10
        MISO    = GPIO9
        BUSY    = GPIO20
        DIO1    = GPIO16
        RESET   = GPIO18

    SPI:
        SPI0
        Mode 0
        8 MHz


    RADIO:
        SX1262
        LoRa
        ~915 MHz
        SF10
        BW 62.5 kHz
        CR 4/5
        LDRO ON
        +22 dBm
*/


#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <pigpio.h>


/* ============================================================
 * GPIO
 * ============================================================ */

#define LORA_CS        21
#define LORA_CLK       11
#define LORA_MOSI      10
#define LORA_MISO      9

#define LORA_BUSY      20
#define LORA_DIO1      16
#define LORA_RST       18

#define LORA_SPI       0


/* ============================================================
 * SPI
 * ============================================================ */

/*
    Start at 8 MHz for reliability.

    The SX1262 can operate at higher SPI speeds, but keeping
    this at 8 MHz removes unnecessary signal-integrity concerns
    while debugging.
*/
#define LORA_SPI_SPEED     8000000


/* ============================================================
 * RADIO CONFIGURATION
 * ============================================================ */

/*
    RF frequency.

    These bytes correspond to approximately 915 MHz for the
    SX1262 PLL frequency calculation.
*/
#define LORA_FREQ_3        0x39
#define LORA_FREQ_2        0x38
#define LORA_FREQ_1        0x00
#define LORA_FREQ_0        0x00


/*
    LoRa modulation:

        SF10
        BW 62.5 kHz
        CR 4/5
        LDRO ON
*/
#define LORA_SF            0x0A
#define LORA_BW            0x03
#define LORA_CR            0x01
#define LORA_LDRO          0x01


/*
    TX power:

        +22 dBm
*/
#define LORA_TX_POWER      0x16
#define LORA_TX_RAMP       0x02


/* ============================================================
 * IRQ FLAGS
 * ============================================================ */

#define IRQ_TX_DONE        0x0001
#define IRQ_RX_DONE        0x0002
#define IRQ_PREAMBLE       0x0004
#define IRQ_SYNCWORD       0x0008
#define IRQ_HEADER         0x0010
#define IRQ_HEADER_ERR     0x0020
#define IRQ_CRC_ERR        0x0040
#define IRQ_CAD_DONE       0x0080
#define IRQ_CAD_DETECT     0x0100
#define IRQ_TIMEOUT        0x0200

#define IRQ_ALL            0x03FF


/* ============================================================
 * TIMEOUTS
 * ============================================================ */

#define BUSY_TIMEOUT_MS        1000

/*
    Hardware reset timing.
*/
#define RESET_DELAY_US         5000
#define RESET_RELEASE_US       10000

/*
    Host-side safety timeout.

    This is intentionally generous for:

        SF10
        BW 62.5 kHz
        19-byte payload
*/
#define TX_HOST_TIMEOUT_MS     15000


/*
    SX1262 SetTx timeout.

    0x000000 would mean no radio timeout.

    Instead we use a finite timeout so a failed TX can recover
    automatically.

    0x20 << 8 = 8192 units of 15.625 us ~= 128 ms.

    Increase this if using substantially slower LoRa settings
    or much larger packets.
*/
#define TX_TIMEOUT_3          0x00
#define TX_TIMEOUT_2          0x20
#define TX_TIMEOUT_1          0x00


/* ============================================================
 * SPI HANDLE
 * ============================================================ */

static int spi_handle = -1;


/* ============================================================
 * WAIT FOR BUSY LOW
 * ============================================================ */

static int lora_wait_busy(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;


    while (gpioRead(LORA_BUSY))
    {
        usleep(1000);

        elapsed++;


        if (elapsed >= timeout_ms)
        {
            fprintf(
                stderr,
                "ERROR: SX1262 BUSY timeout\n"
            );

            return -1;
        }
    }


    return 0;
}


/* ============================================================
 * CHIP SELECT
 * ============================================================ */

static int cs_low(void)
{
    /*
        The SX1262 must not be accessed while BUSY is high.
    */

    if (lora_wait_busy(BUSY_TIMEOUT_MS) != 0)
        return -1;


    gpioWrite(
        LORA_CS,
        0
    );


    return 0;
}


static void cs_high(void)
{
    gpioWrite(
        LORA_CS,
        1
    );
}


/* ============================================================
 * WRITE COMMAND
 * ============================================================ */

static int lora_write_command(
    const uint8_t *data,
    unsigned length)
{
    int result;


    if (cs_low() != 0)
        return -1;


    result = spiWrite(
        spi_handle,
        (char *)data,
        length
    );


    cs_high();


    if (result < 0)
    {
        fprintf(
            stderr,
            "ERROR: spiWrite failed: %d\n",
            result
        );

        return -1;
    }


    /*
        Wait for the SX1262 to finish processing the command.
    */

    if (lora_wait_busy(BUSY_TIMEOUT_MS) != 0)
        return -1;


    return 0;
}


/* ============================================================
 * GET STATUS
 * ============================================================ */

static int lora_get_status(
    uint8_t *status)
{
    uint8_t tx[2] = {
        0xC0,
        0x00
    };


    uint8_t rx[2] = {
        0x00,
        0x00
    };


    if (cs_low() != 0)
        return -1;


    int result = spiXfer(
        spi_handle,
        (char *)tx,
        (char *)rx,
        2
    );


    cs_high();


    if (result < 0)
    {
        fprintf(
            stderr,
            "ERROR: GetStatus SPI failed: %d\n",
            result
        );

        return -1;
    }


    /*
        GetStatus response:

            rx[0] = status response during opcode
            rx[1] = actual status byte
    */

    *status = rx[1];


    return 0;
}


/* ============================================================
 * PRINT STATUS
 * ============================================================ */

static void lora_print_status(
    uint8_t status)
{
    uint8_t chip_mode =
        (status >> 4) & 0x07;


    uint8_t command_status =
        (status >> 1) & 0x07;


    printf(
        "STATUS = 0x%02X, chipMode=%u, commandStatus=%u\n",
        status,
        chip_mode,
        command_status
    );


    switch (chip_mode)
    {
        case 0:
            printf(
                "  Chip mode: SLEEP\n"
            );
            break;

        case 1:
            printf(
                "  Chip mode: STANDBY_RC\n"
            );
            break;

        case 2:
            printf(
                "  Chip mode: STANDBY_XOSC\n"
            );
            break;

        case 3:
            printf(
                "  Chip mode: FS\n"
            );
            break;

        case 4:
            printf(
                "  Chip mode: RX\n"
            );
            break;

        case 5:
            printf(
                "  Chip mode: RX_DC\n"
            );
            break;

        case 6:
            printf(
                "  Chip mode: TX\n"
            );
            break;

        case 7:
            printf(
                "  Chip mode: CAD\n"
            );
            break;

        default:
            printf(
                "  Chip mode: UNKNOWN\n"
            );
            break;
    }
}


/* ============================================================
 * GET IRQ STATUS
 *
 * IMPORTANT:
 *
 * SX1262 response:
 *
 *     MOSI:
 *         0x12 0x00 0x00 0x00
 *
 *     MISO:
 *         RFU STATUS IRQ_MSB IRQ_LSB
 *
 * Therefore:
 *
 *     rx[0] = RFU
 *     rx[1] = STATUS
 *     rx[2] = IRQ MSB
 *     rx[3] = IRQ LSB
 *
 * The original problem was caused by using rx[1] and rx[2].
 * ============================================================ */

static int lora_get_irq_status(
    uint16_t *irq)
{
    uint8_t tx[4] = {
        0x12,
        0x00,
        0x00,
        0x00
    };


    uint8_t rx[4] = {
        0x00,
        0x00,
        0x00,
        0x00
    };


    if (cs_low() != 0)
        return -1;


    int result = spiXfer(
        spi_handle,
        (char *)tx,
        (char *)rx,
        4
    );


    cs_high();


    if (result < 0)
    {
        fprintf(
            stderr,
            "ERROR: GetIrqStatus SPI failed: %d\n",
            result
        );

        return -1;
    }


    /*
        CORRECT IRQ BYTE POSITIONS.
    */

    *irq =
        ((uint16_t)rx[2] << 8) |
        (uint16_t)rx[3];


    return 0;
}


/* ============================================================
 * PRINT IRQ
 * ============================================================ */

static void lora_print_irq(
    uint16_t irq)
{
    printf(
        "IRQ = 0x%04X",
        irq
    );


    if (irq & IRQ_TX_DONE)
        printf(" TX_DONE");

    if (irq & IRQ_RX_DONE)
        printf(" RX_DONE");

    if (irq & IRQ_PREAMBLE)
        printf(" PREAMBLE");

    if (irq & IRQ_SYNCWORD)
        printf(" SYNCWORD");

    if (irq & IRQ_HEADER)
        printf(" HEADER");

    if (irq & IRQ_HEADER_ERR)
        printf(" HEADER_ERROR");

    if (irq & IRQ_CRC_ERR)
        printf(" CRC_ERROR");

    if (irq & IRQ_CAD_DONE)
        printf(" CAD_DONE");

    if (irq & IRQ_CAD_DETECT)
        printf(" CAD_DETECTED");

    if (irq & IRQ_TIMEOUT)
        printf(" TIMEOUT");


    printf("\n");
}


/* ============================================================
 * CLEAR IRQ
 * ============================================================ */

static int lora_clear_irq(
    uint16_t mask)
{
    uint8_t data[] = {
        0x02,

        (uint8_t)(mask >> 8),
        (uint8_t)(mask & 0xFF)
    };


    return lora_write_command(
        data,
        sizeof(data)
    );
}


/* ============================================================
 * STANDBY
 * ============================================================ */

static int lora_set_standby(void)
{
    printf(
        "SetStandby\n"
    );


    uint8_t data[] = {
        0x80,
        0x00
    };


    return lora_write_command(
        data,
        sizeof(data)
    );
}


/* ============================================================
 * PACKET TYPE
 * ============================================================ */

static int lora_set_packet_type(void)
{
    printf(
        "SetPacketType: LoRa\n"
    );


    uint8_t data[] = {
        0x8A,
        0x01
    };


    return lora_write_command(
        data,
        sizeof(data)
    );
}


/* ============================================================
 * RF FREQUENCY
 * ============================================================ */

static int lora_set_frequency(void)
{
    printf(
        "SetRfFrequency\n"
    );


    uint8_t data[] = {
        0x86,

        LORA_FREQ_3,
        LORA_FREQ_2,
        LORA_FREQ_1,
        LORA_FREQ_0
    };


    return lora_write_command(
        data,
        sizeof(data)
    );
}


/* ============================================================
 * PA CONFIG
 * ============================================================ */

static int lora_set_pa_config(void)
{
    printf(
        "SetPaConfig\n"
    );


    uint8_t data[] = {
        0x95,

        0x04,
        0x07,
        0x00,
        0x01
    };


    return lora_write_command(
        data,
        sizeof(data)
    );
}


/* ============================================================
 * TX PARAMS
 * ============================================================ */

static int lora_set_tx_params(void)
{
    printf(
        "SetTxParams\n"
    );


    uint8_t data[] = {
        0x8E,

        LORA_TX_POWER,
        LORA_TX_RAMP
    };


    return lora_write_command(
        data,
        sizeof(data)
    );
}


/* ============================================================
 * BUFFER BASE ADDRESS
 * ============================================================ */

static int lora_set_buffer_base(void)
{
    printf(
        "SetBufferBaseAddress\n"
    );


    uint8_t data[] = {
        0x8F,

        0x00,
        0x00
    };


    return lora_write_command(
        data,
        sizeof(data)
    );
}


/* ============================================================
 * MODULATION PARAMETERS
 * ============================================================ */

static int lora_set_modulation_params(void)
{
    printf(
        "SetModulationParams\n"
    );


    uint8_t data[] = {
        0x8B,

        /*
            Spreading Factor
        */
        LORA_SF,

        /*
            Bandwidth
        */
        LORA_BW,

        /*
            Coding Rate
        */
        LORA_CR,

        /*
            Low Data Rate Optimize
        */
        LORA_LDRO,

        /*
            Reserved
        */
        0x00,
        0x00,
        0x00,
        0x00
    };


    return lora_write_command(
        data,
        sizeof(data)
    );
}


/* ============================================================
 * PACKET PARAMETERS
 * ============================================================ */

static int lora_set_packet_params(
    uint8_t payload_length)
{
    printf(
        "SetPacketParams: payload length = %u\n",
        payload_length
    );


    uint8_t data[] = {
        0x8C,

        /*
            Preamble = 8 symbols
        */
        0x00,
        0x08,

        /*
            Explicit header
        */
        0x00,

        /*
            Actual payload length
        */
        payload_length,

        /*
            CRC enabled
        */
        0x01,

        /*
            Normal IQ
        */
        0x00
    };


    return lora_write_command(
        data,
        sizeof(data)
    );
}


/* ============================================================
 * DIO IRQ CONFIGURATION
 * ============================================================ */

static int lora_map_irq(void)
{
    printf(
        "SetDioIrqParams: TX_DONE -> DIO1\n"
    );


    /*
        Opcode:

            0x08

        IRQ mask:

            TX_DONE

        DIO1:

            TX_DONE

        DIO2:

            none

        DIO3:

            none
    */

    uint8_t data[] = {
        0x08,

        /*
            IRQ mask
        */
        0x00,
        0x01,

        /*
            DIO1 mask
        */
        0x00,
        0x01,

        /*
            DIO2 mask
        */
        0x00,
        0x00,

        /*
            DIO3 mask
        */
        0x00,
        0x00
    };


    return lora_write_command(
        data,
        sizeof(data)
    );
}


/* ============================================================
 * WRITE BUFFER
 * ============================================================ */

static int lora_write_buffer(
    const char *text,
    uint8_t *payload_length)
{
    size_t length =
        strlen(text);


    if (length == 0)
    {
        fprintf(
            stderr,
            "ERROR: payload is empty\n"
        );

        return -1;
    }


    if (length > 255)
    {
        fprintf(
            stderr,
            "ERROR: payload too long: %zu bytes\n",
            length
        );

        return -1;
    }


    printf(
        "Writing %zu byte payload\n",
        length
    );


    /*
        Return the payload length to the caller.
    */

    *payload_length =
        (uint8_t)length;


    /*
        WriteBuffer:

            opcode
            offset
            payload
    */

    uint8_t buffer[257];


    buffer[0] = 0x0E;


    /*
        TX buffer offset = 0.
    */

    buffer[1] = 0x00;


    memcpy(
        &buffer[2],
        text,
        length
    );


    if (cs_low() != 0)
        return -1;


    int result = spiWrite(
        spi_handle,
        (char *)buffer,
        (unsigned)(length + 2)
    );


    cs_high();


    if (result < 0)
    {
        fprintf(
            stderr,
            "ERROR: WriteBuffer failed: %d\n",
            result
        );

        return -1;
    }


    /*
        Wait for WriteBuffer command completion.
    */

    if (lora_wait_busy(BUSY_TIMEOUT_MS) != 0)
        return -1;


    return 0;
}


/* ============================================================
 * SET TX
 * ============================================================ */

static int lora_set_tx(void)
{
    printf(
        "SetTx\n"
    );


    /*
        SetTx timeout.

        Finite timeout instead of 0x000000.

        This allows the SX1262 itself to recover if something
        prevents the packet from completing.
    */

    uint8_t data[] = {
        0x83,

        TX_TIMEOUT_3,
        TX_TIMEOUT_2,
        TX_TIMEOUT_1
    };


    return lora_write_command(
        data,
        sizeof(data)
    );
}


/* ============================================================
 * WAIT FOR TX DONE
 * ============================================================ */

static int lora_wait_tx_done(void)
{
    printf(
        "Waiting for TX_DONE...\n"
    );


    uint32_t elapsed = 0;


    while (elapsed < TX_HOST_TIMEOUT_MS)
    {
        uint16_t irq = 0;


        /*
            Read IRQ status.
        */

        if (lora_get_irq_status(&irq) != 0)
            return -1;


        /*
            TX completed successfully.
        */

        if (irq & IRQ_TX_DONE)
        {
            printf(
                "TX_DONE detected.\n"
            );

            lora_print_irq(
                irq
            );

            return 0;
        }


        /*
            Genuine radio timeout.
        */

        if (irq & IRQ_TIMEOUT)
        {
            printf(
                "RADIO TIMEOUT detected.\n"
            );

            lora_print_irq(
                irq
            );


            /*
                Print radio state for diagnosis.
            */

            uint8_t status;


            if (lora_get_status(&status) == 0)
            {
                printf(
                    "Radio status after timeout:\n"
                );

                lora_print_status(
                    status
                );
            }


            return -1;
        }


        /*
            Ignore unrelated IRQ flags while waiting for TX.

            In particular, CAD_DETECTED, CAD_DONE, RX_DONE,
            etc. are not TX completion events.

            We don't repeatedly clear them here because doing
            so is unnecessary and can make the TX debugging
            output extremely noisy.
        */

        usleep(1000);

        elapsed++;
    }


    /*
        Host-side timeout.
    */

    printf(
        "ERROR: host-side TX timeout after %u ms\n",
        TX_HOST_TIMEOUT_MS
    );


    /*
        Print final IRQ state.
    */

    uint16_t irq = 0;


    if (lora_get_irq_status(&irq) == 0)
    {
        printf(
            "Final radio IRQ:\n"
        );

        lora_print_irq(
            irq
        );
    }


    /*
        Print final radio status.
    */

    uint8_t status;


    if (lora_get_status(&status) == 0)
    {
        printf(
            "Final radio status:\n"
        );

        lora_print_status(
            status
        );
    }


    return -1;
}


/* ============================================================
 * BROADCAST
 * ============================================================ */

static int lora_broadcast(
    const char *text)
{
    printf("\n");

    printf(
        "========================================\n"
    );

    printf(
        "Attempting broadcast: \"%s\"\n",
        text
    );

    printf(
        "========================================\n"
    );


    /*
        Always start from standby.
    */

    if (lora_set_standby() != 0)
        return -1;


    /*
        Clear every pending IRQ.
    */

    printf(
        "ClearIrqStatus: ALL\n"
    );


    if (lora_clear_irq(IRQ_ALL) != 0)
        return -1;


    /*
        Verify IRQs are clear.
    */

    uint16_t irq = 0;


    if (lora_get_irq_status(&irq) != 0)
        return -1;


    if (irq != 0)
    {
        printf(
            "WARNING: IRQs still set before TX:\n"
        );

        lora_print_irq(
            irq
        );


        /*
            Try once more.
        */

        if (lora_clear_irq(IRQ_ALL) != 0)
            return -1;


        if (lora_get_irq_status(&irq) != 0)
            return -1;


        if (irq != 0)
        {
            printf(
                "ERROR: IRQs remain set after clearing:\n"
            );

            lora_print_irq(
                irq
            );

            return -1;
        }
    }


    /*
        Write payload.
    */

    uint8_t payload_length = 0;


    if (lora_write_buffer(
            text,
            &payload_length) != 0)
    {
        return -1;
    }


    /*
        Configure packet length for this packet.
    */

    if (lora_set_packet_params(
            payload_length) != 0)
    {
        return -1;
    }


    /*
        Make sure no IRQ appeared during configuration.
    */

    if (lora_get_irq_status(&irq) != 0)
        return -1;


    if (irq != 0)
    {
        printf(
            "Clearing IRQs before TX:\n"
        );

        lora_print_irq(
            irq
        );


        if (lora_clear_irq(IRQ_ALL) != 0)
            return -1;
    }


    /*
        Start TX.
    */

    if (lora_set_tx() != 0)
        return -1;


    /*
        Do NOT rely on an immediate GetStatus here.

        The SX1262 can still be transitioning between states
        immediately after SetTx.

        Wait for BUSY to return low and then inspect status.
    */

    if (lora_wait_busy(BUSY_TIMEOUT_MS) != 0)
    {
        fprintf(
            stderr,
            "ERROR: BUSY remained high after SetTx\n"
        );

        return -1;
    }


    /*
        Print status after the SetTx command has completed.
    */

    uint8_t status;


    if (lora_get_status(&status) == 0)
    {
        printf(
            "Radio status after SetTx:\n"
        );

        lora_print_status(
            status
        );
    }


    /*
        Wait for TX_DONE.
    */

    if (lora_wait_tx_done() != 0)
    {
        fprintf(
            stderr,
            "TX failed.\n"
        );


        /*
            Recover the radio.
        */

        lora_set_standby();

        lora_clear_irq(
            IRQ_ALL
        );


        return -1;
    }


    /*
        Clear TX_DONE.
    */

    printf(
        "ClearIrqStatus: TX_DONE\n"
    );


    if (lora_clear_irq(
            IRQ_TX_DONE) != 0)
    {
        return -1;
    }


    /*
        Verify the IRQ register is clean.
    */

    if (lora_get_irq_status(&irq) != 0)
        return -1;


    if (irq != 0)
    {
        printf(
            "Remaining IRQs after TX:\n"
        );

        lora_print_irq(
            irq
        );


        /*
            Clean up anything remaining.
        */

        if (lora_clear_irq(IRQ_ALL) != 0)
            return -1;
    }


    printf(
        "TX done\n"
    );


    return 0;
}


/* ============================================================
 * GPIO / SPI SETUP
 * ============================================================ */

static int gpio_setup(void)
{
    printf(
        "Initializing GPIO/SPI...\n"
    );


    /*
        Initialize pigpio.
    */

    if (gpioInitialise() < 0)
    {
        fprintf(
            stderr,
            "ERROR: gpioInitialise failed\n"
        );

        return -1;
    }


    /*
        Manual chip select.
    */

    gpioSetMode(
        LORA_CS,
        PI_OUTPUT
    );


    gpioWrite(
        LORA_CS,
        1
    );


    /*
        RESET.
    */

    gpioSetMode(
        LORA_RST,
        PI_OUTPUT
    );


    gpioWrite(
        LORA_RST,
        1
    );


    /*
        BUSY.
    */

    gpioSetMode(
        LORA_BUSY,
        PI_INPUT
    );


    /*
        DIO1.

        TX_DONE is mapped here.

        We don't need to poll it because the IRQ register is
        sufficient for this simple transmitter, but keeping the
        pin configured is useful for hardware diagnostics.
    */

    gpioSetMode(
        LORA_DIO1,
        PI_INPUT
    );


    gpioSetPullUpDown(
        LORA_DIO1,
        PI_PUD_DOWN
    );


    /*
        SPI:

            SPI0
            8 MHz
            Mode 0

        GPIO21 is the actual SX1262 CS, so the pigpio CE0
        chip-select is not being used by the radio.
    */

    spi_handle = spiOpen(
        LORA_SPI,
        LORA_SPI_SPEED,
        0
    );


    if (spi_handle < 0)
    {
        fprintf(
            stderr,
            "ERROR: spiOpen failed: %d\n",
            spi_handle
        );

        gpioTerminate();

        return -1;
    }


    printf(
        "SPI initialized at %d Hz, mode 0\n",
        LORA_SPI_SPEED
    );


    return 0;
}


/* ============================================================
 * RESET
 * ============================================================ */

static int lora_reset(void)
{
    printf(
        "Resetting SX1262...\n"
    );


    /*
        Assert reset.
    */

    gpioWrite(
        LORA_RST,
        0
    );


    gpioDelay(
        RESET_DELAY_US
    );


    /*
        Release reset.
    */

    gpioWrite(
        LORA_RST,
        1
    );


    gpioDelay(
        RESET_RELEASE_US
    );


    /*
        Allow the radio to initialize.
    */

    sleep(1);


    /*
        BUSY should eventually become low.
    */

    return lora_wait_busy(
        BUSY_TIMEOUT_MS
    );
}


/* ============================================================
 * RADIO INITIALIZATION
 * ============================================================ */

static int lora_init(void)
{
    printf(
        "\nInitializing SX1262...\n"
    );


    /*
        Standby.
    */

    if (lora_set_standby() != 0)
        return -1;


    /*
        LoRa packet type.
    */

    if (lora_set_packet_type() != 0)
        return -1;


    /*
        RF frequency.
    */

    if (lora_set_frequency() != 0)
        return -1;


    /*
        PA configuration.
    */

    if (lora_set_pa_config() != 0)
        return -1;


    /*
        TX power and ramp.
    */

    if (lora_set_tx_params() != 0)
        return -1;


    /*
        Buffer base address.
    */

    if (lora_set_buffer_base() != 0)
        return -1;


    /*
        LoRa modulation parameters.
    */

    if (lora_set_modulation_params() != 0)
        return -1;


    /*
        Initial packet parameters.

        Actual payload length is set before each TX.
    */

    if (lora_set_packet_params(0) != 0)
        return -1;


    /*
        TX_DONE -> DIO1.
    */

    if (lora_map_irq() != 0)
        return -1;


    /*
        Clear all pending IRQs.
    */

    if (lora_clear_irq(IRQ_ALL) != 0)
        return -1;


    /*
        Print initial radio status.
    */

    uint8_t status;


    if (lora_get_status(&status) == 0)
    {
        printf(
            "Initial radio status:\n"
        );

        lora_print_status(
            status
        );
    }


    /*
        Print initial IRQ state.
    */

    uint16_t irq;


    if (lora_get_irq_status(&irq) == 0)
    {
        printf(
            "Initial IRQ state:\n"
        );

        lora_print_irq(
            irq
        );
    }


    printf(
        "SX1262 initialization complete.\n"
    );


    return 0;
}


/* ============================================================
 * MAIN
 * ============================================================ */

int main(void)
{
    const char send[] =
        "Testing Testing 123";


    printf("\n");

    printf(
        "========================================\n"
    );

    printf(
        "SX1262 Raspberry Pi TRANSMITTER\n"
    );

    printf(
        "========================================\n"
    );


    /*
        GPIO and SPI initialization.
    */

    if (gpio_setup() != 0)
        return 1;


    /*
        Hardware reset.
    */

    if (lora_reset() != 0)
    {
        fprintf(
            stderr,
            "ERROR: SX1262 reset failed\n"
        );


        spiClose(
            spi_handle
        );

        gpioTerminate();


        return 1;
    }


    /*
        Radio initialization.
    */

    if (lora_init() != 0)
    {
        fprintf(
            stderr,
            "ERROR: SX1262 initialization failed\n"
        );


        spiClose(
            spi_handle
        );

        gpioTerminate();


        return 1;
    }


    printf("\n");

    printf(
        "Radio ready.\n"
    );

    printf(
        "Starting transmission loop...\n"
    );


    /*
        Continuous transmission loop.
    */

    while (1)
    {
        if (lora_broadcast(send) != 0)
        {
            fprintf(
                stderr,
                "TX failed. Retrying after 1 second...\n"
            );
        }

        else
        {
            printf(
                "Transmission successful.\n"
            );
        }


        sleep(1);
    }


    /*
        Normally unreachable.
    */

    spiClose(
        spi_handle
    );

    gpioTerminate();


    return 0;
}
