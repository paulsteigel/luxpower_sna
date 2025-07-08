// src/esphome/components/luxpower_sna/luxpower_sna_constants.h
#pragma once

#include <cstdint> // For uint8_t, uint16_t etc.

namespace esphome {
namespace luxpower_sna {

// In luxpower_sna_constants.h

// Define the prefix for A1 packets
#define LUXPOWER_A1_PREFIX 0x1AA1 // This is 0xA1 0x1A as a 16-bit little-endian value

// Minimum length of an A1 packet (prefix + protocol + frame_length + tcp_function + dongle_serial + data_length + address_action + device_function + inverter_serial + start_register + num_registers + CRC)
// 2 + 2 + 2 + 1 + 10 + 2 + 1 + 1 + 10 + 2 + 2 + 2 = 37 bytes
#define LUXPOWER_A1_MIN_LENGTH 37

// TCP Function Codes (from LXPPacket.py)
#define LUXPOWER_TCP_TRANSLATED_DATA 0xC2 // 194

// Modbus Function Codes (from LXPPacket.py)
#define MODBUS_CMD_READ_HOLDING_REGISTER 0x03
#define MODBUS_CMD_READ_INPUT_REGISTER 0x04

// Luxpower Protocol Constants
const uint8_t LUXPOWER_START_BYTE = 0xA1; // Start of Luxpower frame
const uint8_t LUXPOWER_END_BYTE = 0x16;   // End of Luxpower frame
const uint8_t LUXPOWER_END_BYTE_LENGTH = 1; // Length of the end byte for calculations

// Min total packet length for response.
// 1 (Start Byte) + 2 (Length Field) + 10 (Inverter Serial) + 10 (Dongle Serial) +
// 1 (Function Code) + 1 (Byte Count, min 0 for data) + 2 (CRC) + 1 (End Byte) = 28
const size_t LUXPOWER_PACKET_MIN_TOTAL_LENGTH = 28;

// CRC16-Modbus Polynomial
const uint16_t LUXPOWER_CRC16_POLY = 0xA001; // Also 0x8005 reversed

// Minimum expected length of the payload *for CRC calculation* (from Inverter Serial to CRC, inclusive)
// This is: 10 (Inverter Serial) + 10 (Dongle Serial) + 1 (Function Code) + 1 (Byte Count) + 2 (CRC)
const size_t LUXPOWER_MIN_PAYLOAD_FOR_CRC_LENGTH = 24;

// Enum for Luxpower Register Types (for scaling/interpretation)
enum LuxpowerRegType : uint8_t {
  LUX_REG_TYPE_INT = 0,        // Raw integer value (e.g., 1)
  LUX_REG_TYPE_FLOAT_DIV10,    // Integer / 10.0 (e.g., 123 -> 12.3)
  LUX_REG_TYPE_SIGNED_INT,     // Signed 16-bit integer
  LUX_REG_TYPE_FIRMWARE,       // Special string interpretation for firmware version
  LUX_REG_TYPE_MODEL,          // Special string interpretation for model name
  LUX_REG_TYPE_BITMASK,        // Bitmask / flags
  LUX_REG_TYPE_TIME_MINUTES,   // Value represents minutes
  // Add other types as needed
};

} // namespace luxpower_sna
} // namespace esphome
