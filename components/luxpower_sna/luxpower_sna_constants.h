#pragma once

// Constants for LuxPower SNA Inverter communication
const uint32_t RECONNECT_INTERVAL_MS = 5000; // Time in ms before attempting to reconnect
const uint8_t PACKET_MIN_LENGTH = 9;        // Minimum length for a LuxPower/Modbus TCP packet (e.g., 7 bytes MBAP + 2 bytes function code/length)

// Define the enum for LuxpowerRegType
// This enum helps categorize how a raw register value should be interpreted.
enum class LuxpowerRegType {
  RAW,         // Raw 16-bit unsigned integer
  VOLTAGE,     // Value needs division (e.g., by 10 or 100) to get Volts
  CURRENT,     // Value needs division to get Amps
  POWER,       // Value needs scaling to get Watts/kW
  TEMPERATURE, // Value needs scaling to get Celsius/Fahrenheit
  PERCENT,     // Value represents a percentage (e.g., 0-100)
  // Add other types as needed based on your inverter's data
};

// Add any other constants you might need based on the LuxPower protocol here.
