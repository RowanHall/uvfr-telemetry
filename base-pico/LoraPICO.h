#ifndef LORA_PICO_H
#define LORA_PICO_H

#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/spi.h"


// ============================================================
// SX1261 / SX1262 Raspberry Pi Pico Configuration
// ============================================================

// SPI peripheral
#define LORA_SPI_PORT       spi1
#define LORA_SPI_BAUDRATE  16000000

// SX126x pins
#define LORA_DIO1           20
#define LORA_RESET          15
#define LORA_MISO           12
#define LORA_MOSI           11
#define LORA_SCK            10
#define LORA_CS             3
#define LORA_BUSY           2


// ============================================================
// SX126x SPI Commands
// ============================================================

#define SX126X_SET_SLEEP                0x84
#define SX126X_SET_STANDBY              0x80
#define SX126X_SET_FS                   0xC1
#define SX126X_SET_TX                   0x83
#define SX126X_SET_RX                   0x82
#define SX126X_SET_RX_DUTY_CYCLE        0x94
#define SX126X_SET_CAD                  0xC5

#define SX126X_SET_TX_CONTINUOUS_WAVE   0xD1
#define SX126X_SET_TX_INFINITE_PREAMBLE 0xD2

#define SX126X_SET_REGULATOR_MODE       0x96
#define SX126X_CALIBRATE                0x89
#define SX126X_CALIBRATE_IMAGE         0x98

#define SX126X_SET_PA_CONFIG             0x95
#define SX126X_SET_RX_TX_FALLBACK_MODE   0x93

#define SX126X_WRITE_REGISTER            0x0D
#define SX126X_READ_REGISTER             0x1D

#define SX126X_WRITE_BUFFER              0x0E
#define SX126X_READ_BUFFER               0x1E

#define SX126X_SET_DIO_IRQ_PARAMS        0x08
#define SX126X_GET_IRQ_STATUS            0x12
#define SX126X_CLEAR_IRQ_STATUS          0x02

#define SX126X_SET_DIO2_RF_SWITCH        0x9D
#define SX126X_SET_DIO3_TCXO_CTRL        0x97

#define SX126X_SET_RF_FREQUENCY          0x86
#define SX126X_SET_PACKET_TYPE            0x8A
#define SX126X_GET_PACKET_TYPE            0x11

#define SX126X_SET_TX_PARAMS              0x8E
#define SX126X_SET_MODULATION_PARAMS     0x8B
#define SX126X_SET_PACKET_PARAMS         0x8C

#define SX126X_SET_CAD_PARAMS             0x88
#define SX126X_SET_BUFFER_BASE_ADDRESS    0x8F
#define SX126X_SET_LORA_SYMB_NUM_TIMEOUT 0xA0

#define SX126X_GET_STATUS                 0xC0
#define SX126X_GET_RX_BUFFER_STATUS       0x13
#define SX126X_GET_PACKET_STATUS          0x14
#define SX126X_GET_RSSI_INST              0x15

#define SX126X_GET_STATS                  0x10
#define SX126X_RESET_STATS                0x00

#define SX126X_GET_DEVICE_ERRORS          0x17
#define SX126X_CLEAR_DEVICE_ERRORS        0x07


// ============================================================
// SX126x Packet Types
// ============================================================

#define SX126X_PACKET_TYPE_GFSK          0x00
#define SX126X_PACKET_TYPE_LORA           0x01


// ============================================================
// SX126x Standby Modes
// ============================================================

#define SX126X_STDBY_RC                  0x00
#define SX126X_STDBY_XOSC                0x01


// ============================================================
// LoRa Spreading Factors
// ============================================================

#define LORA_SF5                         0x05
#define LORA_SF6                         0x06
#define LORA_SF7                         0x07
#define LORA_SF8                         0x08
#define LORA_SF9                         0x09
#define LORA_SF10                        0x0A
#define LORA_SF11                        0x0B
#define LORA_SF12                        0x0C


// ============================================================
// LoRa Bandwidth
//
// SX126x encoding:
// 7.8 kHz   = 0x00
// 10.4 kHz  = 0x08
// 15.6 kHz  = 0x01
// 20.8 kHz  = 0x09
// 31.25 kHz = 0x02
// 41.7 kHz  = 0x0A
// 62.5 kHz  = 0x03
// 125 kHz   = 0x04
// 250 kHz   = 0x05
// 500 kHz   = 0x06
// ============================================================

#define LORA_BW_7_8                       0x00
#define LORA_BW_10_4                      0x08
#define LORA_BW_15_6                      0x01
#define LORA_BW_20_8                      0x09
#define LORA_BW_31_25                     0x02
#define LORA_BW_41_7                      0x0A
#define LORA_BW_62_5                      0x03
#define LORA_BW_125                       0x04
#define LORA_BW_250                       0x05
#define LORA_BW_500                       0x06


// ============================================================
// LoRa Coding Rate
// ============================================================

#define LORA_CR_4_5                       0x01
#define LORA_CR_4_6                       0x02
#define LORA_CR_4_7                       0x03
#define LORA_CR_4_8                       0x04


// ============================================================
// LoRa Packet Header
// ============================================================

#define LORA_HEADER_EXPLICIT              0x00
#define LORA_HEADER_IMPLICIT              0x01


// ============================================================
// LoRa CRC
// ============================================================

#define LORA_CRC_OFF                      0x00
#define LORA_CRC_ON                       0x01


// ============================================================
// IRQ Masks
// ============================================================

#define SX126X_IRQ_TX_DONE                0x0001
#define SX126X_IRQ_RX_DONE                0x0002
#define SX126X_IRQ_PREAMBLE_DETECTED      0x0004
#define SX126X_IRQ_SYNC_WORD_VALID        0x0008
#define SX126X_IRQ_HEADER_VALID            0x0010
#define SX126X_IRQ_HEADER_ERR              0x0020
#define SX126X_IRQ_CRC_ERR                0x0040
#define SX126X_IRQ_CAD_DONE               0x0080
#define SX126X_IRQ_CAD_DETECTED           0x0100
#define SX126X_IRQ_TIMEOUT                0x0200

#define SX126X_IRQ_ALL                    0x03FF


// ============================================================
// Driver Functions
// ============================================================

// Hardware initialization
void lora_init(void);

// Reset SX126x
void lora_reset(void);

// Wait until SX126x BUSY goes low
void lora_wait_busy(void);

// SPI command helper
void lora_write_command(
    uint8_t command,
    const uint8_t *data,
    size_t length
);

void lora_read_command(
    uint8_t command,
    uint8_t *data,
    size_t length
);

// Register access
void lora_write_register(
    uint16_t address,
    const uint8_t *data,
    size_t length
);

void lora_read_register(
    uint16_t address,
    uint8_t *data,
    size_t length
);

// FIFO access
void lora_write_buffer(
    uint8_t offset,
    const uint8_t *data,
    size_t length
);

void lora_read_buffer(
    uint8_t offset,
    uint8_t *data,
    size_t length
);

// Basic radio configuration
void lora_set_standby(void);
void lora_set_packet_type_lora(void);

void lora_set_frequency(uint32_t frequency_hz);

void lora_set_tx_power(
    int8_t power_dbm,
    uint8_t ramp_time
);

// LoRa configuration
void lora_set_modulation(
    uint8_t spreading_factor,
    uint8_t bandwidth,
    uint8_t coding_rate,
    bool low_data_rate_optimize
);

void lora_set_packet_parameters(
    uint16_t preamble_length,
    uint8_t header_type,
    uint8_t payload_length,
    bool crc_on,
    bool invert_iq
);

// Transmit / receive
void lora_transmit(
    const uint8_t *data,
    size_t length,
    uint32_t timeout
);

void lora_receive(uint32_t timeout);

// IRQ
uint16_t lora_get_irq_status(void);
void lora_clear_irq_status(uint16_t irq_mask);

// Status
uint8_t lora_get_status(void);

// Read received packet
uint8_t lora_get_rx_length(void);
uint8_t lora_get_rx_start_pointer(void);

#endif // LORA_PICO_H