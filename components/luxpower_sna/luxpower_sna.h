// esphome_config/custom_components/luxpower_sna/luxpower_sna.h

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include <vector>

#include "lwip/err.h"

struct tcp_pcb;
struct pbuf;

namespace esphome {
namespace luxpower_sna {

class LuxpowerSNAComponent : public PollingComponent {
 public:
  void setup() override;
  void dump_config() override;
  void update() override;

  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::string &serial) { this->dongle_serial_ = serial; }

  // --- Setters for our sensors (names must match sensor.py) ---
  void set_pv1_voltage_sensor(sensor::Sensor *s) { this->pv1_voltage_sensor_ = s; }
  void set_pv1_power_sensor(sensor::Sensor *s) { this->pv1_power_sensor_ = s; }
  void set_battery_voltage_sensor(sensor::Sensor *s) { this->battery_voltage_sensor_ = s; }
  void set_charge_power_sensor(sensor::Sensor *s) { this->charge_power_sensor_ = s; }
  void set_discharge_power_sensor(sensor::Sensor *s) { this->discharge_power_sensor_ = s; }
  void set_inverter_power_sensor(sensor::Sensor *s) { this->inverter_power_sensor_ = s; }
  void set_soc_sensor(sensor::Sensor *s) { this->soc_sensor_ = s; }

  // Public members/methods for LwIP callbacks
  void close_connection();
  void parse_response(const std::vector<uint8_t> &data);
  struct tcp_pcb *pcb_ = nullptr;
  std::vector<uint8_t> rx_buffer_;

  std::vector<uint8_t> build_request_packet(uint8_t function_code, uint8_t start_reg, uint8_t reg_count);
  uint16_t calculate_lux_checksum(const std::vector<uint8_t> &data);

 private:
  uint16_t get_register_value(const std::vector<uint8_t> &data, int offset);

  std::string host_;
  uint16_t port_;
  std::string dongle_serial_;
  
  // --- Pointers to our sensor objects (names must match) ---
  sensor::Sensor *pv1_voltage_sensor_{nullptr};
  sensor::Sensor *pv1_power_sensor_{nullptr};
  sensor::Sensor *battery_voltage_sensor_{nullptr};
  sensor::Sensor *charge_power_sensor_{nullptr};
  sensor::Sensor *discharge_power_sensor_{nullptr};
  sensor::Sensor *inverter_power_sensor_{nullptr};
  sensor::Sensor *soc_sensor_{nullptr};
};

}  // namespace luxpower_sna
}  // namespace esphome
