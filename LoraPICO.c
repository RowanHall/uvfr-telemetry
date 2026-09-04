#include "LoraPICO.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"


// ============================================================
// Internal helpers
// ============================================================

static void lora_select(void)
{
    gpio_put(LORA_CS, 0);
}

static void lora_deselect(void)
{
    gpio_put(LORA_CS, 1);
}


// ============================================================
// Wait for SX126x BUSY to go LOW
// ============================================================

void lora_wait_busy(void)
{
    while (gpio_get(LORA_BUSY)) {
        tight_loop_contents();
    }
}


// ============================================================
// Reset SX126x
// ============================================================

void lora_reset(void)
{
    gpio_put(LORA_RESET, 0);
    sleep_ms(10);

    gpio_put(LORA_RESET, 1);
    sleep_ms(10);

    lora_wait_busy();
}


// ============================================================
// Write SX126x command
// ============================================================

void lora_write_command(
    uint8_t command,
    const uint8_t *data,
    size_t length)
{
    lora_wait_busy();

    lora_select();

    spi_write_blocking(
        LORA_SPI_PORT,
        &command,
        1
    );

    if (data != NULL && length > 0) {
        spi_write_blocking(
            LORA_SPI_PORT,
            data,
            length
        );
    }

    lora_deselect();

    lora_wait_busy();
}


// ============================================================
// Read SX126x command
// ============================================================

void lora_read_command(
    uint8_t command,
    uint8_t *data,
    size_t length)
{
    lora_wait_busy();

    lora_select();

    spi_write_blocking(
        LORA_SPI_PORT,
        &command,
        1
    );

    // SX126x provides a status byte after the command.
    uint8_t status;
    spi_read_blocking(
        LORA_SPI_PORT,
        0x00,
        &status,
        1
    );

    if (data != NULL && length > 0) {
        spi_read_blocking(
            LORA_SPI_PORT,
            0x00,
            data,
            length
        );
    }

    lora_deselect();

    lora_wait_busy();
}


// ============================================================
// Write register
// ============================================================

void lora_write_register(
    uint16_t address,
    const uint8_t *data,
    size_t length)
{
    uint8_t command[3];

    command[0] = SX126X_WRITE_REGISTER;
    command[1] = (uint8_t)(address >> 8);
    command[2] = (uint8_t)(address & 0xFF);

    lora_wait_busy();
    lora_select();

    spi_write_blocking(
        LORA_SPI_PORT,
        command,
        3
    );

    if (data != NULL && length > 0) {
        spi_write_blocking(
            LORA_SPI_PORT,
            data,
            length
        );
    }

    lora_deselect();

    lora_wait_busy();
}


// ============================================================
// Read register
// ============================================================

void lora_read_register(
    uint16_t address,
    uint8_t *data,
    size_t length)
{
    uint8_t command[4];

    command[0] = SX126X_READ_REGISTER;
    command[1] = (uint8_t)(address >> 8);
    command[2] = (uint8_t)(address & 0xFF);
    command[3] = 0x00;

    lora_wait_busy();
    lora_select();

    spi_write_blocking(
        LORA_SPI_PORT,
        command,
        3
    );

    // Dummy/status byte
    uint8_t dummy;

    spi_read_blocking(
        LORA_SPI_PORT,
        0x00,
        &dummy,
        1
    );

    spi_read_blocking(
        LORA_SPI_PORT,
        0x00,
        data,
        length
    );

    lora_deselect();

    lora_wait_busy();
}


// ============================================================
// Write FIFO buffer
// ============================================================

void lora_write_buffer(
    uint8_t offset,
    const uint8_t *data,
    size_t length)
{
    uint8_t command[2];

    command[0] = SX126X_WRITE_BUFFER;
    command[1] = offset;

    lora_wait_busy();

    lora_select();

    spi_write_blocking(
        LORA_SPI_PORT,
        command,
        2
    );

    if (data != NULL && length > 0) {
        spi_write_blocking(
            LORA_SPI_PORT,
            data,
            length
        );
    }

    lora_deselect();

    lora_wait_busy();
}


// ============================================================
// Read FIFO buffer
// ============================================================

void lora_read_buffer(
    uint8_t offset,
    uint8_t *data,
    size_t length)
{
    uint8_t command[2];

    command[0] = SX126X_READ_BUFFER;
    command[1] = offset;

    lora_wait_busy();

    lora_select();

    spi_write_blocking(
        LORA_SPI_PORT,
        command,
        2
    );

    uint8_t status;

    spi_read_blocking(
        LORA_SPI_PORT,
        0x00,
        &status,
        1
    );

    spi_read_blocking(
        LORA_SPI_PORT,
        0x00,
        data,
        length
    );

    lora_deselect();

    lora_wait_busy();
}


// ============================================================
// Set standby mode
// ============================================================

void lora_set_standby(void)
{
    uint8_t mode = SX126X_STDBY_RC;

    lora_write_command(
        SX126X_SET_STANDBY,
        &mode,
        1
    );
}


// ============================================================
// Set packet type to LoRa
// ============================================================

void lora_set_packet_type_lora(void)
{
    uint8_t type = SX126X_PACKET_TYPE_LORA;

    lora_write_command(
        SX126X_SET_PACKET_TYPE,
        &type,
        1
    );
}


// ============================================================
// Set RF frequency
// ============================================================
//
// SX126x frequency:
//
// RF frequency word = frequency_hz * 2^25 / 32 MHz
//
// ============================================================

void lora_set_frequency(uint32_t frequency_hz)
{
    uint32_t frequency_word =
        ((uint64_t)frequency_hz << 25) / 32000000ULL;

    uint8_t data[4];

    data[0] = (uint8_t)(frequency_word >> 24);
    data[1] = (uint8_t)(frequency_word >> 16);
    data[2] = (uint8_t)(frequency_word >> 8);
    data[3] = (uint8_t)(frequency_word);

    lora_write_command(
        SX126X_SET_RF_FREQUENCY,
        data,
        4
    );
}


// ============================================================
// Set TX power
// ============================================================

void lora_set_tx_power(
    int8_t power_dbm,
    uint8_t ramp_time)
{
    uint8_t data[2];

    data[0] = (uint8_t)power_dbm;
    data[1] = ramp_time;

    lora_write_command(
        SX126X_SET_TX_PARAMS,
        data,
        2
    );
}


// ============================================================
// Set LoRa modulation parameters
// ============================================================

void lora_set_modulation(
    uint8_t spreading_factor,
    uint8_t bandwidth,
    uint8_t coding_rate,
    bool low_data_rate_optimize)
{
    uint8_t data[4];

    data[0] = spreading_factor;
    data[1] = bandwidth;
    data[2] = coding_rate;
    data[3] = low_data_rate_optimize ? 0x01 : 0x00;

    lora_write_command(
        SX126X_SET_MODULATION_PARAMS,
        data,
        4
    );
}


// ============================================================
// Set LoRa packet parameters
// ============================================================

void lora_set_packet_parameters(
    uint16_t preamble_length,
    uint8_t header_type,
    uint8_t payload_length,
    bool crc_on,
    bool invert_iq)
{
    uint8_t data[6];

    data[0] = (uint8_t)(preamble_length >> 8);
    data[1] = (uint8_t)(preamble_length & 0xFF);

    data[2] = header_type;
    data[3] = payload_length;
    data[4] = crc_on ? 0x01 : 0x00;
    data[5] = invert_iq ? 0x01 : 0x00;

    lora_write_command(
        SX126X_SET_PACKET_PARAMS,
        data,
        6
    );
}


// ============================================================
// Get IRQ status
// ============================================================

uint16_t lora_get_irq_status(void)
{
    uint8_t data[2] = {0};

    lora_read_command(
        SX126X_GET_IRQ_STATUS,
        data,
        2
    );

    return ((uint16_t)data[0] << 8) |
           data[1];
}


// ============================================================
// Clear IRQ status
// ============================================================

void lora_clear_irq_status(uint16_t irq_mask)
{
    uint8_t data[2];

    data[0] = (uint8_t)(irq_mask >> 8);
    data[1] = (uint8_t)(irq_mask & 0xFF);

    lora_write_command(
        SX126X_CLEAR_IRQ_STATUS,
        data,
        2
    );
}


// ============================================================
// Get radio status
// ============================================================

uint8_t lora_get_status(void)
{
    uint8_t status;

    lora_wait_busy();

    lora_select();

    uint8_t command = SX126X_GET_STATUS;

    spi_write_blocking(
        LORA_SPI_PORT,
        &command,
        1
    );

    spi_read_blocking(
        LORA_SPI_PORT,
        0x00,
        &status,
        1
    );

    lora_deselect();

    return status;
}


// ============================================================
// Initialize hardware
// ============================================================

void lora_init(void)
{
    // --------------------------------------------------------
    // SPI
    // --------------------------------------------------------

    spi_init(
        LORA_SPI_PORT,
        LORA_SPI_BAUDRATE
    );

    gpio_set_function(
        LORA_MISO,
        GPIO_FUNC_SPI
    );

    gpio_set_function(
        LORA_SCK,
        GPIO_FUNC_SPI
    );

    gpio_set_function(
        LORA_MOSI,
        GPIO_FUNC_SPI
    );


    // --------------------------------------------------------
    // Chip select
    // --------------------------------------------------------

    gpio_init(LORA_CS);
    gpio_set_dir(LORA_CS, GPIO_OUT);
    gpio_put(LORA_CS, 1);


    // --------------------------------------------------------
    // RESET
    // --------------------------------------------------------

    gpio_init(LORA_RESET);
    gpio_set_dir(LORA_RESET, GPIO_OUT);
    gpio_put(LORA_RESET, 1);


    // --------------------------------------------------------
    // BUSY
    // --------------------------------------------------------

    gpio_init(LORA_BUSY);
    gpio_set_dir(LORA_BUSY, GPIO_IN);


    // --------------------------------------------------------
    // DIO1
    // --------------------------------------------------------

    gpio_init(LORA_DIO1);
    gpio_set_dir(LORA_DIO1, GPIO_IN);


    // --------------------------------------------------------
    // Reset radio
    // --------------------------------------------------------

    lora_reset();


    // --------------------------------------------------------
    // Put radio into standby
    // --------------------------------------------------------

    lora_set_standby();


    // --------------------------------------------------------
    // Configure LoRa packet type
    // --------------------------------------------------------

    lora_set_packet_type_lora();
}