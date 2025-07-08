#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include "sensor.h"
#include "const.h"

#include <vector>
#include <map>
#include "ESPAsyncTCP.h"

namespace esphome {
namespace luxpower_sna {

class LuxpowerSnaComponent : public PollingComponent {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;

  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::vector<uint8_t> &serial) { this->dongle_serial_ = serial; }
  void set_inverter_serial(const std::vector<uint8_t> &serial) { this->inverter_serial_ = serial; }

  void register_sensor(const std::string &type, sensor::Sensor *sens);

 protected:
  void connect_to_inverter();
  void request_inverter_data(int bank);
  void parse_inverter_data(const std::vector<uint8_t> &data);
  
  std::vector<uint8_t> prepare_packet_for_read(uint16_t start_register, uint16_t num_registers, uint8_t function);
  uint16_t compute_crc(const std::vector<uint8_t> &data);

  // Helper to get values from byte vector
  int16_t get_16bit_signed(const std::vector<uint8_t> &data, int offset);
  uint16_t get_16bit_unsigned(const std::vector<uint8_t> &data, int offset);

  std::string host_;
  uint16_t port_;
  std::vector<uint8_t> dongle_serial_;
  std::vector<uint8_t> inverter_serial_;

  AsyncClient *client_{nullptr};
  bool is_connected_{false};
  int current_request_bank_{0};

  // Map to hold our sensors
  std::map<std::string, sensor::Sensor*> sensors_;
};

}  // namespace luxpower_sna
}  // namespace esphome
