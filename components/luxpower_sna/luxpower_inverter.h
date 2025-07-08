#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <vector> // Required for std::vector

// For TCP/IP communication, we use WiFiClient from the Arduino framework
#include <WiFiClient.h>

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

 protected:
  // Private members for TCP client and connection management
  WiFiClient client_;
  std::string host_;
  uint16_t port_;
  uint32_t last_connect_attempt_;
  std::vector<uint8_t> data_buffer_;

  // Members for managing periodic requests based on update_interval
  uint32_t last_request_time_;
  uint32_t update_interval_; // Stored in milliseconds after conversion in constructor

  // Private helper methods for internal logic
  void parse_luxpower_response_packet(const std::vector<uint8_t> &data);
  void parse_and_publish_register(uint16_t reg_address, uint16_t value); // Example for publishing

  // Corrected: ESPHome base class method declaration now accepts an optional message
  void status_set_warning(const std::string &message = "");
  void status_set_ok(); // This still correctly takes no arguments for clearing status
};

} // namespace luxpower_sna
} // namespace esphome
