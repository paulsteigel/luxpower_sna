// components/luxpower_sna/luxpower_sna.h
#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include <vector>

#ifdef USE_ESP32
#include <AsyncTCP.h>
#elif USE_ESP8266
#include <ESPAsyncTCP.h>
#endif

namespace esphome {
namespace luxpower_sna {

class LuxpowerSNAComponent : public PollingComponent {
 public:
  // --- Setters for configuration ---
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::vector<uint8_t> &serial) { this->dongle_serial_ = serial; }
  void set_inverter_serial_number(const std::vector<uint8_t> &serial) { this->inverter_serial_ = serial; }

  // --- Setters for SENSORS (called by sensor.py) ---
  void set_battery_voltage_sensor(sensor::Sensor *s) { this->battery_voltage_sensor_ = s; }
  void set_battery_current_sensor(sensor::Sensor *s) { this->battery_current_sensor_ = s; }
  void set_battery_capacity_ah_sensor(sensor::Sensor *s) { this->battery_capacity_ah_sensor_ = s; }
  void set_power_from_grid_sensor(sensor::Sensor *s) { this->power_from_grid_sensor_ = s; }
  void set_daily_solar_generation_sensor(sensor::Sensor *s) { this->daily_solar_generation_sensor_ = s; }

  // --- Standard ESPHome methods ---
  void setup() override;
  void dump_config() override;
  void update() override;

 protected:
  // --- Helper to safely publish state ---
  template<typename T> void publish_state(sensor::Sensor *sensor, T value);

  // --- Communication methods ---
  void request_data_();
  void handle_packet_(void *arg, AsyncClient *client, void *data, size_t len);

  // --- Member variables ---
  std::string host_;
  uint16_t port_;
  std::vector<uint8_t> dongle_serial_;
  std::vector<uint8_t> inverter_serial_;
  AsyncClient *tcp_client_{nullptr};

  // --- Pointers to SENSORS ---
  sensor::Sensor *battery_voltage_sensor_{nullptr};
  sensor::Sensor *battery_current_sensor_{nullptr};
  sensor::Sensor *battery_capacity_ah_sensor_{nullptr};
  sensor::Sensor *power_from_grid_sensor_{nullptr};
  sensor::Sensor *daily_solar_generation_sensor_{nullptr};
};

}  // namespace luxpower_sna
}  // namespace esphome
