#pragma once

// Constants for LuxPower SNA Inverter communication
const uint32_t RECONNECT_INTERVAL_MS = 5000; // Time in ms before attempting to reconnect
const uint8_t PACKET_MIN_LENGTH = 9;        // Minimum length for a LuxPower/Modbus TCP packet (e.g., 7 bytes MBAP + 2 bytes function code/length)

// Add any other constants you might need based on the LuxPower protocol here.
// Example:
// const uint16_t REGISTER_ADDRESS_EXAMPLE = 0x0100;
