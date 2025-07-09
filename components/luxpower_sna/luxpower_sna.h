#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/helpers.h"

#ifdef USE_ESP32
#include <AsyncTCP.h>
#elif USE_ESP8266
#include <ESPAsyncTCP.hh>
#endif

#include <vector>

namespace esphome {
namespace luxpower_sna {

class LuxpowerSNAComponent : public PollingComponent {
 public:
  // --- Configuration Setters ---
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::string &serial);
  void set_inverter_serial_number(const std::string &serial);
  void set_num_banks_to_request(int num_banks) { this->num_banks_to_request_ = num_banks; }

  // --- Sensor Setters ---
  void set_status_text_sensor(text_sensor::TextSensor *sensor) { this->status_text_sensor_ = sensor; }
  void set_soc_sensor(sensor::Sensor *sensor) { this->soc_sensor_ = sensor; }
  void set_pv1_power_sensor(sensor::Sensor *sensor) { this->pv1_power_sensor_ = sensor; }
  void set_pv2_power_sensor(sensor::Sensor *sensor) { this->pv2_power_sensor_ = sensor; }
  void set_battery_power_sensor(sensor::Sensor *sensor) { this->battery_power_sensor_ = sensor; }
  void set_grid_power_sensor(sensor::Sensor *sensor) { this->grid_power_sensor_ = sensor; }
  void set_inverter_power_sensor(sensor::Sensor *sensor) { this->inverter_power_sensor_ = sensor; }

  // --- Core ESPHome Functions ---
  void setup() override;
  void dump_config() override;
  void update() override;

 protected:
  // --- State Machine & Connection Management ---
  void start_update_cycle_();
  void send_next_request_();
  void handle_response_(void *data, size_t len);
  void end_update_cycle_(bool success);

  // --- Helper Functions ---
  void parse_and_publish_();
  std::vector<uint8_t> build_request_packet_(uint16_t start_register, uint16_t num_registers);
  uint16_t get_register_value_(int register_index);

  // --- Configuration Members ---
  std::string host_;
  uint16_t port_{8000};
  std::vector<uint8_t> dongle_serial_;
  std::vector<uint8_t> inverter_serial_;
  int num_banks_to_request_{2}; // Default to 2 banks (registers 0-79)

  // --- State Members ---
  bool is_updating_{false};
  int current_bank_to_request_{0};
  std::vector<uint8_t> data_buffer_;
  AsyncClient *client_{nullptr};
  uint32_t update_timeout_handle_{0};

  // --- Sensor Members ---
  text_sensor::TextSensor *status_text_sensor_{nullptr};
  sensor::Sensor *soc_sensor_{nullptr};
  sensor::Sensor *pv1_power_sensor_{nullptr};
  sensor::Sensor *pv2_power_sensor_{nullptr};
  sensor::Sensor *battery_power_sensor_{nullptr};
  sensor::Sensor *grid_power_sensor_{nullptr};
  sensor::Sensor *inverter_power_sensor_{nullptr};
};

}  // namespace luxpower_sna
}  // namespace esphome
