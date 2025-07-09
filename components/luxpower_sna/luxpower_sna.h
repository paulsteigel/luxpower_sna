#pragma once

#include "esphome/core/component.hh"
#include "esphome/components/socket/socket.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace luxpower_sna {

class LuxpowerSNAComponent : public PollingComponent {
 public:
  void setup() override;
  void update() override;
  void on_shutdown() override;
  float get_setup_priority() const override;

  void set_address(const std::string &address) { this->address_ = address; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::string &dongle_serial) { this->dongle_serial_ = dongle_serial; }
  void set_inverter_serial(const std::string &inverter_serial) { this->inverter_serial_ = inverter_serial; }

  // SENSOR DEFINITIONS - Aligned with PDF
  // --- Core Sensors ---
  SENSOR_PLATFORM_SCHEMA(this->status_code_sensor_, "Status Code", "mdi:information-outline", 0);
  TEXT_SENSOR_PLATFORM_SCHEMA(this->status_text_sensor_, "Status", "mdi:information-outline");
  SENSOR_PLATFORM_SCHEMA(this->battery_voltage_sensor_, "Battery Voltage", "mdi:battery-high", 1, "V");
  SENSOR_PLATFORM_SCHEMA(this->soc_sensor_, "Battery SOC", "mdi:battery-high", 0, "%");
  SENSOR_PLATFORM_SCHEMA(this->battery_power_sensor_, "Battery Power", "mdi:battery-charging", 0, "W");
  SENSOR_PLATFORM_SCHEMA(this->charge_power_sensor_, "Charge Power", "mdi:battery-plus", 0, "W");
  SENSOR_PLATFORM_SCHEMA(this->discharge_power_sensor_, "Discharge Power", "mdi:battery-minus", 0, "W");
  SENSOR_PLATFORM_SCHEMA(this->pv_power_sensor_, "PV Power", "mdi:solar-power", 0, "W");
  SENSOR_PLATFORM_SCHEMA(this->inverter_power_sensor_, "Inverter Power", "mdi:power-plug", 0, "W");
  SENSOR_PLATFORM_SCHEMA(this->grid_power_sensor_, "Grid Power", "mdi:transmission-tower", 0, "W");
  SENSOR_PLATFORM_SCHEMA(this->load_power_sensor_, "Load Power", "mdi:home-lightning-bolt", 0, "W");
  SENSOR_PLATFORM_SCHEMA(this->eps_power_sensor_, "EPS Power", "mdi:power-plug-off", 0, "W");
  
  // --- PV Detail Sensors ---
  SENSOR_PLATFORM_SCHEMA(this->pv1_voltage_sensor_, "PV1 Voltage", "mdi:solar-panel", 1, "V");
  SENSOR_PLATFORM_SCHEMA(this->pv1_power_sensor_, "PV1 Power", "mdi:solar-power", 0, "W");
  SENSOR_PLATFORM_SCHEMA(this->pv2_voltage_sensor_, "PV2 Voltage", "mdi:solar-panel", 1, "V");
  SENSOR_PLATFORM_SCHEMA(this->pv2_power_sensor_, "PV2 Power", "mdi:solar-power", 0, "W");

  // --- Grid Detail Sensors ---
  SENSOR_PLATFORM_SCHEMA(this->grid_voltage_sensor_, "Grid Voltage", "mdi:transmission-tower", 1, "V");
  SENSOR_PLATFORM_SCHEMA(this->grid_frequency_sensor_, "Grid Frequency", "mdi:sine-wave", 2, "Hz");
  SENSOR_PLATFORM_SCHEMA(this->power_factor_sensor_, "Power Factor", "mdi:angle-acute", 3, "");

  // --- EPS Detail Sensors ---
  SENSOR_PLATFORM_SCHEMA(this->eps_voltage_sensor_, "EPS Voltage", "mdi:power-plug-off", 1, "V");
  SENSOR_PLATFORM_SCHEMA(this->eps_frequency_sensor_, "EPS Frequency", "mdi:sine-wave", 2, "Hz");

  // --- Daily Energy Sensors ---
  SENSOR_PLATFORM_SCHEMA(this->pv_today_sensor_, "PV Energy Today", "mdi:solar-power", 1, "kWh");
  SENSOR_PLATFORM_SCHEMA(this->inverter_today_sensor_, "Inverter Energy Today", "mdi:power-plug", 1, "kWh");
  SENSOR_PLATFORM_SCHEMA(this->charge_today_sensor_, "Charge Energy Today", "mdi:battery-plus", 1, "kWh");
  SENSOR_PLATFORM_SCHEMA(this->discharge_today_sensor_, "Discharge Energy Today", "mdi:battery-minus", 1, "kWh");
  SENSOR_PLATFORM_SCHEMA(this->grid_export_today_sensor_, "Grid Export Today", "mdi:transmission-tower-export", 1, "kWh");
  SENSOR_PLATFORM_SCHEMA(this->grid_import_today_sensor_, "Grid Import Today", "mdi:transmission-tower-import", 1, "kWh");
  SENSOR_PLATFORM_SCHEMA(this->load_today_sensor_, "Load Energy Today", "mdi:home-lightning-bolt", 1, "kWh");
  SENSOR_PLATFORM_SCHEMA(this->eps_today_sensor_, "EPS Energy Today", "mdi:power-plug-off", 1, "kWh");

  // --- Temperature Sensors ---
  SENSOR_PLATFORM_SCHEMA(this->inverter_temp_sensor_, "Inverter Temperature", "mdi:thermometer", 0, "°C");
  SENSOR_PLATFORM_SCHEMA(this->radiator_temp_sensor_, "Radiator Temperature", "mdi:thermometer", 0, "°C");
  SENSOR_PLATFORM_SCHEMA(this->battery_temp_sensor_, "Battery Temperature", "mdi:thermometer", 0, "°C");


 protected:
  void request_data_();
  void parse_lux_packet_(const uint8_t *raw, uint32_t len);
  uint16_t calculate_crc_(const uint8_t *data, size_t len);
  const char *get_status_text_(uint16_t status_code);

  template<typename T>
  void publish_state_if_changed_(T *sensor, float value) {
    if (sensor && (!sensor->has_state() || sensor->get_raw_state() != value)) {
      sensor->publish_state(value);
    }
  }
  
  void publish_state_if_changed_(text_sensor::TextSensor *sensor, const std::string &value) {
    if (sensor && (!sensor->has_state() || sensor->get_state() != value)) {
      sensor->publish_state(value);
    }
  }

  std::string address_;
  uint16_t port_;
  std::string dongle_serial_;
  std::string inverter_serial_;
  long last_poll_{0};

  esphome::socket::Socket *socket_{nullptr};
};

}  // namespace luxpower_sna
}  // namespace esphome

