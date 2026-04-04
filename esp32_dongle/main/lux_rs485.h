#pragma once
#include <stdint.h>
#include <stdbool.h>

// ── Return codes ──────────────────────────────────────────────
typedef enum {
    RS485_OK            = 0,
    RS485_ERR_TIMEOUT   = 1,   // no response from inverter
    RS485_ERR_CRC       = 2,   // response CRC mismatch
    RS485_ERR_EXCEPTION = 3,   // inverter returned Modbus exception
    RS485_ERR_FRAME     = 4,   // malformed frame / bad args
} rs485_err_t;

// ── Init ──────────────────────────────────────────────────────
// Call once after NVS init, before any reads/writes.
// Configures UART_NUM (from config.h) in RS485_HALF_DUPLEX mode;
// DE/RE pin is driven automatically via hardware RTS.
void lux_rs485_init(void);

// ── Read registers ────────────────────────────────────────────
// fn=0x04  Input registers  (real-time: voltage, power, SOC …)
// fn=0x03  Hold registers   (config: charge rate, EOD SOC …)
//
// out[] receives `count` uint16_t values in host byte order (LE).
// Modbus RTU Big-Endian ↔ host conversion is done internally.
// max count per call: 125 (Modbus spec limit)
rs485_err_t lux_rs485_read_input(uint16_t start, uint16_t count, uint16_t *out);
rs485_err_t lux_rs485_read_hold (uint16_t start, uint16_t count, uint16_t *out);

// ── Write registers ───────────────────────────────────────────
// fn=0x06  Write single holding register
rs485_err_t lux_rs485_write_single(uint16_t reg, uint16_t value);

// fn=0x10  Write multiple holding registers
// values[] in host byte order; converted to BE on the wire.
// max count: 123 (Modbus spec limit)
rs485_err_t lux_rs485_write_multi(uint16_t start,
                                   const uint16_t *values, uint16_t count);

// ── Diagnostics ───────────────────────────────────────────────
// Human-readable error string (for logging)
static inline const char *rs485_err_str(rs485_err_t e) {
    switch (e) {
        case RS485_OK:            return "OK";
        case RS485_ERR_TIMEOUT:   return "TIMEOUT";
        case RS485_ERR_CRC:       return "CRC";
        case RS485_ERR_EXCEPTION: return "EXCEPTION";
        case RS485_ERR_FRAME:     return "FRAME";
        default:                  return "?";
    }
}