#pragma once

#include <cstdint>

namespace esphome {
namespace luxclient {

// Calculates the CRC16-Modbus for a given data buffer.
inline uint16_t crc16(const uint8_t *data, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= (uint16_t) data[i];
    for (int j = 8; j != 0; j--) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

}  // namespace luxclient
}  // namespace esphome
