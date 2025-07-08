// components/luxpower_sna/luxpower_sna_constants.h
#pragma once

#include <cstdint> // For uint8_t, uint16_t etc.
#include <vector>

namespace esphome {
namespace luxpower_sna {

// --- Luxpower Protocol Constants (from LXPPacket.py) ---
static const uint8_t LUX_START_BYTE_1 = 0xAA;
static const uint8_t LUX_START_BYTE_2 = 0x55;

static const uint8_t LUX_HEARTBEAT = 0xC1;          // 193
static const uint8_t LUX_TRANSLATED_DATA = 0xC2;    // 194
static const uint8_t LUX_READ_PARAM = 0xC3;         // 195 (Used for Read Holding/Input Registers)
static const uint8_t LUX_WRITE_PARAM = 0xC4;        // 196

static const uint8_t MODBUS_READ_HOLDING_REGISTERS = 0x03; // Modbus Function Code 3
static const uint8_t MODBUS_READ_INPUT_REGISTERS = 0x04;  // Modbus Function Code 4
static const uint8_t MODBUS_WRITE_SINGLE_REGISTER = 0x06; // Modbus Function Code 6
static const uint8_t MODBUS_WRITE_MULTIPLE_REGISTERS = 0x10; // Modbus Function Code 16

static const std::vector<uint8_t> LUX_NULL_DONGLE = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const std::vector<uint8_t> LUX_NULL_SERIAL = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// --- Register Addresses and Bank Definitions (from LXPPacket.py observations) ---
// These are examples. You'll need to expand this based on your full LXPPacket.py
// and what registers you want to read.

// Example Holding Registers (Bank 0 - 0x0000 to 0x0027)
static const uint16_t REG_HOLD_START_BANK0 = 0x0000;
static const uint16_t REG_HOLD_COUNT_BANK0 = 40; // 0x0000 to 0x0027 (40 registers)

// Example Input Registers (Bank 0 - 0x0000 to 0x0027)
static const uint16_t REG_INPUT_START_BANK0 = 0x0000;
static const uint16_t REG_INPUT_COUNT_BANK0 = 40;

// Example specific register addresses (from LXPPacket.py, e.g., `gen_power_watt = self.readValuesInt.get(125, 0)`)
// These are just examples, you'll map them to actual sensor definitions later.
static const uint16_t REG_GEN_POWER_WATT = 125; // Example: Generation Power (Watt)
static const uint16_t REG_GEN_POWER_DAY = 126;  // Example: Generation Power (Daily)
static const uint16_t REG_BATTERY_VOLTAGE = 100; // Example: Battery Voltage

} // namespace luxpower_sna
} // namespace esphome
