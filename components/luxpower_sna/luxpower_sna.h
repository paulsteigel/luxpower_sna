// components/luxpower_sna/luxpower_sna.h
#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <vector>
#include <map>
#include <string>

#ifdef USE_ESP32
#include <AsyncTCP.h>
#elif USE_ESP8266
#include <ESPAsyncTCP.h>
#endif

namespace esphome {
namespace luxpower_sna {

class LuxpowerSNAComponent : public PollingComponent {
 public:
  // --- Setters for configuration from __init__.py ---
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }

  // --- CORRECTED SETTERS for serial numbers ---
  // Accept a string from codegen and convert it to the internal vector format.
  void set_dongle_serial(const std::string &serial_str) {
    this->dongle_serial_.assign(serial_str.begin(), serial_str.end());
  }
  // This function name now matches the YAML key and python call.
  void set_inverter_serial_number(const std::string &serial_str) {
    this->inverter_serial_.assign(serial_str.begin(), serial_str.end());
  }

  // --- Methods called by python to register sensors ---
  template<typename T> void set_sensor(const std::string &key, T *sensor) {
    this->sensors_[key] = (EntityBase *)sensor;
  }
  void set_battery_voltage_sensor(sensor::Sensor *s) { this->set_sensor("battery_voltage", s); }
  void set_soc_sensor(sensor::Sensor *s) { this->set_sensor("soc", s); }
  void set_battery_power_sensor(sensor::Sensor *s) { this->set_sensor("battery_power", s); }
  void set_charge_power_sensor(sensor::Sensor *s) { this->set_sensor("charge_power", s); }
  void set_discharge_power_sensor(sensor::Sensor *s) { this->set_sensor("discharge_power", s); }
  void set_pv_power_sensor(sensor::Sensor *s) { this->set_sensor("pv_power", s); }
  void set_inverter_power_sensor(sensor::Sensor *s) { this->set_sensor("inverter_power", s); }
  void set_grid_power_sensor(sensor::Sensor *s) { this->set_sensor("grid_power", s); }
  void set_load_power_sensor(sensor::Sensor *s) { this->set_sensor("load_power", s); }
  void set_eps_power_sensor(sensor::Sensor *s) { this->set_sensor("eps_power", s); }
  void set_pv1_voltage_sensor(sensor::Sensor *s) { this->set_sensor("pv1_voltage", s); }
  void set_pv1_power_sensor(sensor::Sensor *s) { this->set_sensor("pv1_power", s); }
  void set_pv2_voltage_sensor(sensor::Sensor *s) { this->set_sensor("pv2_voltage", s); }
  void set_pv2_power_sensor(sensor::Sensor *s) { this->set_sensor("pv2_power", s); }
  void set_grid_voltage_sensor(sensor::Sensor *s) { this->set_sensor("grid_voltage", s); }
  void set_grid_frequency_sensor(sensor::Sensor *s) { this->set_sensor("grid_frequency", s); }
  void set_power_factor_sensor(sensor::Sensor *s) { this->set_sensor("power_factor", s); }
  void set_eps_voltage_sensor(sensor::Sensor *s) { this->set_sensor("eps_voltage", s); }
  void set_eps_frequency_sensor(sensor::Sensor *s) { this->set_sensor("eps_frequency", s); }
  void set_pv_today_sensor(sensor::Sensor *s) { this->set_sensor("pv_today", s); }
  void set_inverter_today_sensor(sensor::Sensor *s) { this->set_sensor("inverter_today", s); }
  void set_charge_today_sensor(sensor::Sensor *s) { this->set_sensor("charge_today", s); }
  void set_discharge_today_sensor(sensor::Sensor *s) { this->set_sensor("discharge_today", s); }
  void set_grid_export_today_sensor(sensor::Sensor *s) { this->set_sensor("grid_export_today", s); }
  void set_grid_import_today_sensor(sensor::Sensor *s) { this->set_sensor("grid_import_today", s); }
  void set_load_today_sensor(sensor::Sensor *s) { this->set_sensor("load_today", s); }
  void set_eps_today_sensor(sensor::Sensor *s) { this->set_sensor("eps_today", s); }
  void set_inverter_temp_sensor(sensor::Sensor *s) { this->set_sensor("inverter_temp", s); }
  void set_radiator_temp_sensor(sensor::Sensor *s) { this->set_sensor("radiator_temp", s); }
  void set_battery_temp_sensor(sensor::Sensor *s) { this->set_sensor("battery_temp", s); }
  void set_status_code_sensor(sensor::Sensor *s) { this->set_sensor("status_code", s); }
  void set_status_text_sensor(text_sensor::TextSensor *s) { this->set_sensor("status_text", s); }


  // --- Standard ESPHome methods ---
  void setup() override;
  void dump_config() override;
  void update() override;

 protected:
  // --- Communication methods ---
  void request_data_();
  void handle_packet_(void *data, size_t len);
  uint16_t calculate_crc_(const uint8_t *data, size_t len);
  void publish_state_(const std::string &key, float value);
  void publish_state_(const std::string &key, const std::string &value);

  // --- Member variables ---
  std::string host_;
  uint16_t port_;
  std::vector<uint8_t> dongle_serial_;
  std::vector<uint8_t> inverter_serial_;
  
  AsyncClient *tcp_client_{nullptr};

  std::map<std::string, EntityBase *> sensors_;
};

}  // namespace luxpower_sna
}  // namespace esphome
