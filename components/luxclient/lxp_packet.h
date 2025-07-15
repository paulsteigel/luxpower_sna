#pragma once

#include "esphome.h"
#include <vector>

namespace esphome {
namespace luxpower {

class LxpPacket {
 public:
  // Function Codes
  static const uint8_t READ_INPUT = 0x04;
  static const uint8_t READ_HOLD = 0x03;
  static const uint8_t WRITE_SINGLE = 0x06;
  static const uint8_t HEARTBEAT = 0x00;
  
  LxpPacket(bool debug, const std::string &dongle, const std::string &serial);
  
  // Packet Operations
  std::vector<uint8_t> prepare_read_packet(uint16_t reg, uint8_t count, uint8_t type);
  std::vector<uint8_t> prepare_write_packet(uint16_t reg, uint16_t value);
  std::vector<uint8_t> prepare_heartbeat_response(const std::vector<uint8_t> &data);
  
  // Parsing
  struct ParseResult {
    uint8_t tcp_function;
    uint8_t device_function;
    uint16_t register_addr;
    std::vector<uint16_t> values;
    bool packet_error;
  };
  
  ParseResult parse_packet(const std::vector<uint8_t> &data);

 private:
  bool debug_;
  std::string dongle_serial_;
  std::string inverter_serial_;
  
  uint16_t calculate_crc(const std::vector<uint8_t> &data);
};

}  // namespace luxpower
}  // namespace esphome
