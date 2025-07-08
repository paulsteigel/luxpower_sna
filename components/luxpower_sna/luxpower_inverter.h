#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include <vector>
#include "ESPAsyncTCP.h"

namespace esphome {
namespace luxpower_sna {

class LuxpowerInverterComponent : public PollingComponent {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;

  void set_host(const std::string &host);
  void set_port(uint16_t port);
  void set_dongle_serial(const std::vector<uint8_t> &serial);
  void set_inverter_serial_number(const std::vector<uint8_t> &serial);

 protected:
  void connect_to_inverter();
  void request_test_data();
  
  // Renamed for clarity
  void parse_inverter_data(void *data, size_t len);

  std::vector<uint8_t> prepare_packet_for_read(uint16_t start, uint16_t count, uint8_t func);
  uint16_t compute_crc(const std::vector<uint8_t> &data);
  
  // Helper to get a 16-bit value from the data stream
  uint16_t get_16bit_unsigned(const std::vector<uint8_t> &data, int offset);

  std::string host_;
  uint16_t port_;
  std::vector<uint8_t> dongle_serial_;
  std::vector<uint8_t> inverter_serial_;
  AsyncClient *client_{nullptr};
  bool is_connected_{false};
};

}  // namespace luxpower_sna
}  // namespace esphome
