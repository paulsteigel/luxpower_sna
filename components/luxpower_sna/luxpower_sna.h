#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <ESPAsyncTCP.h>
#include <vector>

namespace esphome {
namespace luxpower_sna {

class LuxpowerSNAComponent : public PollingComponent {
 public:
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::string &serial);
  void set_inverter_serial_number(const std::string &serial); // Renamed to match main.cpp

  // --- Re-instated individual setters to match YAML generation ---
  void set_battery_voltage_sensor(sensor::Sensor *s) { this->battery_voltage_sensor_ = s; }
  void set_soc_sensor(sensor::Sensor *s) { this->soc_sensor_ = s; }
  void set_pv_power_sensor(sensor::Sensor *s) { this->pv_power_sensor_ = s; }
  void set_grid_power_sensor(sensor::Sensor *s) { this->grid_power_sensor_ = s; }
  void set_load_power_sensor(sensor::Sensor *s) { this->load_power_sensor_ = s; }
  void set_load_today_sensor(sensor::Sensor *s) { this->load_today_sensor_ = s; }
  void set_battery_temp_sensor(sensor::Sensor *s) { this->battery_temp_sensor_ = s; }
  void set_status_text_sensor(text_sensor::TextSensor *s) { this->status_text_sensor_ = s; }
  // You can add more setters here for any other sensors you define in YAML

  void setup() override;
  void dump_config() override;
  void update() override;

 private:
  // --- Bank-Based Request Logic ---
  void request_bank_(int bank_num);
  std::vector<uint8_t> build_request_packet_(uint16_t start_register, uint16_t num_registers);
  void handle_packet_(void *data, size_t len, int bank_num);
  void parse_and_publish_();
  void end_update_cycle_();

  // --- Member Variables ---
  std::string host_;
  uint16_t port_{8000};
  std::vector<uint8_t> dongle_serial_;
  std::vector<uint8_t> inverter_serial_;

  // --- Re-instated individual sensor pointers ---
  sensor::Sensor *battery_voltage_sensor_{nullptr};
  sensor::Sensor *soc_sensor_{nullptr};
  sensor::Sensor *pv_power_sensor_{nullptr};
  sensor::Sensor *grid_power_sensor_{nullptr};
  sensor::Sensor *load_power_sensor_{nullptr};
  sensor::Sensor *load_today_sensor_{nullptr};
  sensor::Sensor *battery_temp_sensor_{nullptr};
  text_sensor::TextSensor *status_text_sensor_{nullptr};

  // --- State for Bank-Based Requests ---
  const int num_banks_to_request_ = 3;
  std::vector<uint8_t> data_buffer_;
  bool is_updating_ = false;
  int current_bank_to_request_ = 0;
};

}  // namespace luxpower_sna
}  // namespace esphome

