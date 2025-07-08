// custom_components/luxpower_sna/luxpower_sna_constants.h
#pragma once

#include <cstdint> // For uint8_t, uint16_t etc.

namespace esphome {
namespace luxpower_sna {

// --- LuxPower Protocol Constants (Adjust based on your inverter's actual protocol) ---
// These are placeholders/examples based on typical serial/TCP protocols.
// You need to confirm the exact values from your LuxPower protocol documentation.

// Example packet structure constants
// You mentioned LUXPOWER_END_BYTE_LENGTH, LUXPOWER_MIN_PACKET_LENGTH, LUXPOWER_MAX_PACKET_LENGTH
static const uint8_t LUXPOWER_START_BYTE = 0xAA; // Common start byte, adjust if yours is different
static const uint8_t LUXPOWER_END_BYTE = 0xBB;   // Common end byte, adjust if yours is different

// Placeholder minimum/maximum packet lengths. You MUST determine these from your protocol.
// A typical minimum might include start byte, length bytes, command, address, count, CRC, end byte.
// A typical maximum depends on how many registers you can read at once.
static const size_t LUXPOWER_MIN_PACKET_LENGTH = 10;  // Example: 1 start + 2 len + 1 cmd + 2 addr + 2 count + 2 CRC = 10
static const size_t LUXPOWER_MAX_PACKET_LENGTH = 200; // Example: Enough for 50 registers (50 * 2 data bytes = 100) + overhead

// Example CRC calculation function declaration (you'll need to implement this in luxpower_inverter.cpp)
// If your protocol uses CRC, this is where you'd declare it.
// uint16_t calculate_crc16(const uint8_t* data, size_t length);


// --- Register Types (Based on your LuxpowerSnaSensor class and LXPPacket.py implications) ---
// This enum defines how different register values should be interpreted and converted.
enum LuxpowerRegType : uint8_t {
  LUX_REG_TYPE_INT = 0,         // Raw integer value
  LUX_REG_TYPE_FLOAT_DIV10,     // Integer value divided by 10 (e.g., 123 -> 12.3)
  LUX_REG_TYPE_SIGNED_INT,      // Signed 16-bit integer
  LUX_REG_TYPE_FIRMWARE,        // Represents firmware version (e.g., 0x0102 -> 1.02)
  LUX_REG_TYPE_MODEL,           // Model number (often an integer or needs specific decoding)
  LUX_REG_TYPE_BITMASK,         // Value where individual bits represent different states
  LUX_REG_TYPE_TIME_MINUTES,    // Value in minutes (e.g., for daily run time)
  // Add any other register types your LuxPower inverter uses
};


// --- Other potentially useful constants (derived from const.py or common Luxpower setups) ---
// You can add more specific register addresses or command codes here if they are directly used
// in the C++ code to structure requests or parse responses.

// Example: Command codes from LXPPacket.py
// static const uint8_t LUXPOWER_CMD_HEARTBEAT = 193;
// static const uint8_t LUXPOWER_CMD_TRANSLATED_DATA = 194;
// static const uint8_t LUXPOWER_CMD_READ_PARAM = 195;
// static const uint8_t LUXPOWER_CMD_WRITE_PARAM = 196;

} // namespace luxpower_sna
} // namespace esphome