#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/sensor/sensor.h" // For LuxpowerSnaSensor
#include <vector>
#include <string>
#include <utility> // For std::move

namespace esphome {
namespace luxpower_sna {

class LuxpowerSnaSensor; // Forward declaration

class LuxPowerInverter : public Component {
public:
  // Updated constructor to accept all 5 arguments
  LuxPowerInverter(const std::string &host, uint16_t port, uint32_t update_interval,
                   const std::string &dongle_serial, const std::string &inverter_serial_number);

  void setup() override;
  void loop() override;
  float get_setup_priority() const override;

  // Method to add a sensor (as referenced in __init__.py)
  void add_luxpower_sensor(sensor::Sensor *obj, const std::string &name, uint16_t reg_address, int reg_type, uint8_t bank);

protected:
  // Internal TCP client
  WiFiClient client_;
  std::string host_;
  uint16_t port_;
  uint32_t last_connect_attempt_{0};
  uint32_t last_request_time_{0};
  uint32_t update_interval_{0}; // Stored in milliseconds after conversion in constructor
  std::vector<uint8_t> data_buffer_;

  // New member variables for serial numbers
  std::string dongle_serial_;
  std::string inverter_serial_number_;

  // Helper functions
  void parse_luxpower_response_packet(const std::vector<uint8_t> &data);
  void parse_and_publish_register(uint16_t reg_address, uint16_t value);
  void status_set_warning(const std::string &message = "");
  void status_set_ok();

  std::vector<LuxpowerSnaSensor *> luxpower_sensors_; // List to hold connected sensors
};

} // namespace luxpower_sna
} // namespace esphome
