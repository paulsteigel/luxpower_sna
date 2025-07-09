#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <vector>
#include <map>

#ifdef USE_ESP32
#include <AsyncTCP.h>
#elif USE_ESP8266
#include <ESPAsyncTCP.h>
#endif

namespace esphome {
namespace luxpower_sna {

class LuxpowerSNAComponent : public PollingComponent {
 public:
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::string &serial);
  void set_inverter_serial(const std::string &serial);
  void register_sensor(const std::string &key, sensor::Sensor *sensor) { this->sensors_[key] = sensor; }
  void register_text_sensor(const std::string &key, text_sensor::TextSensor *sensor) { this->sensors_[key] = sensor; }

  void setup() override;
  void dump_config() override;
  void update() override;

 private:
  // --- New Bank-Based Request Logic ---
  void request_bank_(int bank_num);
  std::vector<uint8_t> build_request_packet_(uint16_t start_register, uint16_t num_registers);
  void handle_packet_(void *data, size_t len, int bank_num);
  void parse_and_publish_();
  void end_update_cycle_();

  // --- Helper Functions ---
  void publish_state_(const std::string &key, float value);
  void publish_state_(const std::string &key, const std::string &value);

  // --- Member Variables ---
  std::string host_;
  uint16_t port_{8000};
  std::vector<uint8_t> dongle_serial_;
  std::vector<uint8_t> inverter_serial_;
  std::map<std::string, Component *> sensors_;

  // --- State for Bank-Based Requests ---
  const int num_banks_to_request_ = 3; // Request banks 0, 1, and 2 (regs 0-119)
  std::vector<uint8_t> data_buffer_;
  bool is_updating_ = false;
  int current_bank_to_request_ = 0;
};

}  // namespace luxpower_sna
}  // namespace esphome

