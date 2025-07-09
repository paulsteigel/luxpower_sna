#pragma once

#include "esphome/core/component.h"
#include "esphome/components/socket/socket.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <memory>

namespace esphome {
namespace luxpower_sna {

// Helper macro to reduce boilerplate for declaring a sensor and its setter
#define LUX_SENSOR(name) \
 protected: \
  sensor::Sensor *name##_sensor_{nullptr}; \
 public: \
  void set_##name##_sensor(sensor::Sensor *sens) { this->name##_sensor_ = sens; }

// Helper macro for text sensors
#define LUX_TEXT_SENSOR(name) \
 protected: \
  text_sensor::TextSensor *name##_sensor_{nullptr}; \
 public: \
  void set_##name##_sensor(text_sensor::TextSensor *sens) { this->name##_sensor_ = sens; }


class LuxpowerSNAComponent : public PollingComponent {
 public:
  void setup() override;
  void update() override;
  void on_shutdown() override;
  float get_setup_priority() const override;

  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::string &dongle_serial) { this->dongle_serial_ = dongle_serial; }
  void set_inverter_serial(const std::string &inverter_serial) { this->inverter_serial_ = inverter_serial; }

  // Use the macros to declare all our sensors with their internal C++ names
  LUX_SENSOR(status_code)
  LUX_TEXT_SENSOR(status_text)
  LUX_SENSOR(battery_voltage)
  LUX_SENSOR(soc)
  LUX_SENSOR(battery_power)
  LUX_SENSOR(charge_power)
  LUX_SENSOR(discharge_power)
  LUX_SENSOR(pv_power)
  LUX_SENSOR(inverter_power)
  LUX_SENSOR(grid_power)
  LUX_SENSOR(load_power)
  LUX_SENSOR(eps_power)
  LUX_SENSOR(pv1_voltage)
  LUX_SENSOR(pv1_power)
  LUX_SENSOR(pv2_voltage)
  LUX_SENSOR(pv2_power)
  LUX_SENSOR(grid_voltage)
  LUX_SENSOR(grid_frequency)
  LUX_SENSOR(power_factor)
  LUX_SENSOR(eps_voltage)
  LUX_SENSOR(eps_frequency)
  LUX_SENSOR(pv_today)
  LUX_SENSOR(inverter_today)
  LUX_SENSOR(charge_today)
  LUX_SENSOR(discharge_today)
  LUX_SENSOR(grid_export_today)
  LUX_SENSOR(grid_import_today)
  LUX_SENSOR(load_today)
  LUX_SENSOR(eps_today)
  LUX_SENSOR(inverter_temp)
  LUX_SENSOR(radiator_temp)
  LUX_SENSOR(battery_temp)

 protected:
  void request_data_();
  void parse_lux_packet_(const uint8_t *raw, uint32_t len);
  uint16_t calculate_crc_(const uint8_t *data, size_t len);
  const char *get_status_text_(uint16_t status_code);

  std::string host_;
  uint16_t port_;
  std::string dongle_serial_;
  std::string inverter_serial_;
  std::unique_ptr<esphome::socket::Socket> socket_;
};

}  // namespace luxpower_sna
}  // namespace esphome
