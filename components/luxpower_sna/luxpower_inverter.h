#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "luxpower_sna_sensor.h"
#include "const.h"

#include <vector>
#include "ESPAsyncTCP.h"

namespace esphome {
namespace luxpower_sna {

class LuxpowerInverterComponent : public PollingComponent {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;

  // Setters from YAML (called by to_code)
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::string &serial) { this->dongle_serial_ = std::vector<uint8_t>(serial.begin(), serial.end()); }
  void set_inverter_serial_number(const std::string &serial) { this->inverter_serial_ = std::vector<uint8_t>(serial.begin(), serial.end()); }
  void add_sensor(LuxpowerSnaSensor *sensor) { this->sensors_.push_back(sensor); }

 protected:
  void connect_to_inverter();
  void request_inverter_data(int bank);
  void parse_inverter_data(const std::vector<uint8_t> &data);
  
  std::vector<uint8_t> prepare_packet_for_read(uint16_t start_register, uint16_t num_registers, uint8_t function);
  uint16_t compute_crc(const std::vector<uint8_t> &data);
  
  int16_t get_16bit_signed(const std::vector<uint8_t> &data, int offset);
  uint16_t get_16bit_unsigned(const std::vector<uint8_t> &data, int offset);

  std::string host_;
  uint16_t port_;
  std::vector<uint8_t> dongle_serial_;
  std::vector<uint8_t> inverter_serial_;

  AsyncClient *client_{nullptr};
  bool is_connected_{false};
  int current_request_bank_{0};

  std::vector<LuxpowerSnaSensor *> sensors_;
};

}  // namespace luxpower_sna
}  // namespace esphome
