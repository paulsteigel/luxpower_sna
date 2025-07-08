#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <vector> // Required for std::vector

// For TCP/IP communication, we use WiFiClient from the Arduino framework
#include <WiFiClient.h>
// #include "esphome/components/network/network.h" // Keep this line commented out for now

// Include your constants file
#include "luxpower_sna_constants.h"

namespace esphome {
namespace luxpower_sna {

class LuxPowerInverter : public Component {
 public:
  // Constructor to pass host, port, and update interval from YAML config
  LuxPowerInverter(const std::string &host, uint16_t port, uint32_t update_interval)
      : host_(host), port_(port), update_interval_(update_interval * 1000) {} // Convert seconds to milliseconds

  void setup() override;
  void loop() override;
  float get_setup_priority() const override;

  // You might want to add methods here for publishing sensor data later
  // For example:
  // void set_voltage_sensor(sensor::Sensor *voltage_sensor) { this->voltage_sensor_ = voltage_sensor; }

 protected:
  // Private members for TCP client and connection management
  WiFiClient client_;
  std::string host_;
  uint16_t port_;
  uint32_t last_connect_attempt_;
  std::vector<uint8_t> data_buffer_;

  // New: Members for managing periodic requests based on update_interval
  uint32_t last_request_time_;
  uint32_t update_interval_; // Stored in milliseconds after conversion in constructor

  // Private helper methods for internal logic
  void parse_luxpower_response_packet(const std::vector<uint8_t> &data);
  void parse_and_publish_register(uint16_t reg_address, uint16_t value); // Example for publishing
  void status_set_warning(); // Corrected: ESPHome base class method
  void status_set_ok();      // Corrected: ESPHome base class method

  // Add any sensor pointers here if you add sensors later in your YAML
  // sensor::Sensor *voltage_sensor_{nullptr}; // Example sensor
};

} // namespace luxpower_sna
} // namespace esphome
