#pragma once

#include <cstdint>

namespace esphome {
namespace luxclient {

/**
 * @brief Calculate the CRC-16 for the given data.
 * @param data Pointer to the data array.
 * @param len The length of the data.
 * @return The calculated 16-bit CRC.
 */
uint16_t crc16(const uint8_t *data, uint16_t len);

}  // namespace luxclient
}  // namespace esphome
