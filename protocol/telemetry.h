#ifndef UVFR_TELEMETRY_H
#define UVFR_TELEMETRY_H

#include <stdint.h>

/*
 * UVFR telemetry wire protocol — shared by BOTH ends of the link.
 *
 *   car-pi (TX)  --LoRa-->  base-pico (RX)  --USB serial-->  laptop
 *
 * Both sides #include this file so the frame layout and radio settings
 * can never drift apart. If you change the struct, bump TELEM_VERSION.
 *
 * Design rules:
 *   - Fixed layout, packed, little-endian (Pi and Pico are both LE).
 *   - Scaled integers only, NO floats (smaller, portable, deterministic).
 *     Each field's comment gives its unit and scale factor.
 *   - One frame = one radio packet. Keep it small; LoRa airtime is scarce.
 */

#define TELEM_VERSION 1

/* ----------------------------------------------------------------------
 * Radio configuration — MUST be identical on TX and RX or they go deaf.
 * (This is what the old frequency mismatch between the TX files violated.)
 * Centralize every RF parameter here.
 * -------------------------------------------------------------------- */
// SetRfFrequency payload bytes for 915.5 MHz (US 902-928 ISM band).
// Opcode 0x86 followed by the 32-bit frequency word.
#define TELEM_RF_FREQ_BYTES { 0x86, 0x39, 0x38, 0x00, 0x00 }
// TODO: pin down SF / BW / CR here too and have both sides read from these,
// e.g. #define TELEM_SF 10, #define TELEM_BW ..., #define TELEM_CR ...

/* ----------------------------------------------------------------------
 * Frame types (the `type` field below).
 * -------------------------------------------------------------------- */
#define TELEM_TYPE_DATA      0x01   // normal telemetry sample
#define TELEM_TYPE_HEARTBEAT 0x02   // link-alive ping, no useful payload

/* ----------------------------------------------------------------------
 * The telemetry frame. One of these goes out per radio packet.
 *
 * EDIT THE CHANNEL BLOCK to match your car. These are common FSAE
 * channels as a starting point — add/remove/rename freely, then bump
 * TELEM_VERSION so a stale receiver can't silently misread the layout.
 * -------------------------------------------------------------------- */
typedef struct __attribute__((packed)) {
    // --- header (8 bytes) ---
    uint8_t  version;        // == TELEM_VERSION; RX rejects mismatches
    uint8_t  type;           // TELEM_TYPE_*
    uint16_t seq;            // increments each frame; RX uses it to count drops
    uint32_t timestamp_ms;   // car-pi uptime in ms when sampled

    // --- channels (EDIT ME) --- UV26 is an EV; these map to CAN1 traffic
    //     (BMS / inverter / energy meter). Confirm exact signals against
    //     the inverter and BMS DBCs, then adjust units/scales to match.
    // Accumulator / BMS:
    uint16_t pack_voltage;   // volts, x10   (e.g. 4021 -> 402.1 V)
    int16_t  pack_current;   // amps,  x10   (signed: + discharge, - regen)
    uint8_t  soc_pct;        // state of charge, 0-100 %
    int16_t  cell_temp_max;  // hottest cell, deg C x10
    uint16_t cell_volt_min;  // lowest cell, millivolts
    // Motor controller / inverter:
    int16_t  motor_rpm;      // motor speed, 1:1 (signed for reverse)
    int16_t  motor_temp;     // deg C, x10
    int16_t  inverter_temp;  // deg C, x10
    int16_t  torque_actual;  // Nm, x10
    // Driver inputs / VCU:
    uint8_t  throttle_pct;   // APPS, 0-100 %
    uint8_t  brake_pct;      // brake pressure, 0-100 %
    uint16_t speed;          // vehicle speed, km/h x10
    uint8_t  flags;          // bitfield: faults / warnings (define as needed)

    // --- integrity (2 bytes, must stay last) ---
    uint16_t crc;            // CRC-16/CCITT over all preceding bytes
} telem_frame_t;

/* ----------------------------------------------------------------------
 * CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). Compute over every byte
 * of the frame EXCEPT the trailing crc field, then compare on RX.
 * Small and self-contained so both the Pi and the Pico can use it as-is.
 * -------------------------------------------------------------------- */
static inline uint16_t telem_crc16(const void *data, uint32_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc = 0xFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)p[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

// Number of bytes the CRC covers (everything up to, but not including, crc).
#define TELEM_CRC_LEN (sizeof(telem_frame_t) - sizeof(uint16_t))

// Fill in version/crc and stamp the frame ready to send.
static inline void telem_finalize(telem_frame_t *f) {
    f->version = TELEM_VERSION;
    f->crc = telem_crc16(f, TELEM_CRC_LEN);
}

// Returns 1 if a received frame is valid (version + CRC), 0 otherwise.
static inline int telem_valid(const telem_frame_t *f) {
    return f->version == TELEM_VERSION &&
           f->crc == telem_crc16(f, TELEM_CRC_LEN);
}

#endif // UVFR_TELEMETRY_H
