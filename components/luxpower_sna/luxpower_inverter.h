#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/log.h"
#include <vector>
#include "ESPAsyncTCP.h"

namespace esphome {
namespace luxpower_sna {

class LuxpowerInverterComponent : public PollingComponent {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;

  // Setters from YAML
  void set_host(const std::string &host);
  void set_port(uint16_t port);
  void set_dongle_serial(const std::vector<uint8_t> &serial);
  void set_inverter_serial_number(const std::vector<uint8_t> &serial);

  // Sensor setters
  void set_battery_voltage_sensor(sensor::Sensor *sensor) { this->battery_voltage_sensor_ = sensor; }
  void set_battery_current_sensor(sensor::Sensor *sensor) { this->battery_current_sensor_ = sensor; }
  void set_battery_capacity_ah_sensor(sensor::Sensor *sensor) { this->battery_capacity_ah_sensor_ = sensor; }
  void set_power_from_grid_sensor(sensor::Sensor *sensor) { this->power_from_grid_sensor_ = sensor; }
  void set_daily_solar_generation_sensor(sensor::Sensor *sensor) { this->daily_solar_generation_sensor_ = sensor; }

 protected:
  void connect_to_inverter();
  void request_inverter_data();
  void on_data(void *data, size_t len);
  void handle_packet(const std::vector<uint8_t> &packet);
  
  std::vector<uint8_t> prepare_packet_for_read(uint16_t start, uint16_t count, uint8_t func);
  uint16_t compute_crc(const std::vector<uint8_t> &data);
  uint16_t get_16bit_unsigned(const std::vector<uint8_t> &data, int offset);
  int16_t get_16bit_signed(const std::vector<uint8_t> &data, int offset);

  // Connection details
  std::string host_;
  uint16_t port_;
  std::vector<uint8_t> dongle_serial_;
  std::vector<uint8_t> inverter_serial_;
  AsyncClient *client_{nullptr};
  bool is_connected_{false};
  std::vector<uint8_t> rx_buffer_; // Our new receive buffer

  // Sensor pointers
  sensor::Sensor *battery_voltage_sensor_{nullptr};
  sensor::Sensor *battery_current_sensor_{nullptr};
  sensor::Sensor *battery_capacity_ah_sensor_{nullptr};
  sensor::Sensor *power_from_grid_sensor_{nullptr};
  sensor::Sensor *daily_solar_generation_sensor_{nullptr};
};

}  // namespace luxpower_sna
}  // namespace esphome
