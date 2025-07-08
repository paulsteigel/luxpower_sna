#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

// For TCP/IP communication, we use WiFiClient from the Arduino framework
#include <WiFiClient.h>
// Optionally include network component for network status checks
#include "esphome/components/network/network.h"

// Include your constants file
#include "luxpower_sna_constants.h"

// If you have sensors associated with this component, include the sensor header
// #include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace luxpower_sna {

// The class now inherits only from Component, not uart::UARTDevice
class LuxPowerInverter : public Component {
 public:
  // Standard ESPHome component methods
  void setup() override;
  void loop() override;
  float get_setup_priority() const override;

  // Setters for host and port, to be configured from YAML
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }

  // Declare the parse_luxpower_response_packet function
  void parse_luxpower_response_packet(const std::vector<uint8_t> &data);

  // Declare parse_and_publish_register
  void parse_and_publish_register(uint16_t reg_address, uint16_t value);

  // Status functions
  void status_set_warning();
  void status_set_ok();

  // Add any specific sensor pointers or other member variables here
  // sensor::Sensor *some_voltage_sensor_{nullptr};
  // void set_some_voltage_sensor(sensor::Sensor *sensor) { this->some_voltage_sensor_ = sensor; }

 protected:
  std::string host_;  // Host address (IP or hostname) of the inverter/gateway
  uint16_t port_;     // Port number for the TCP connection
  WiFiClient client_; // The TCP client object for communication

  std::vector<uint8_t> data_buffer_;     // Buffer to store incoming bytes
  uint32_t last_byte_received_{0};       // Timestamp of the last received byte
  uint32_t read_timeout_{50};            // Timeout for clearing the buffer if no data

  uint32_t rx_count_{0}; // Counter for received packets
  uint32_t tx_count_{0}; // Counter for transmitted packets (if sending commands)

  uint32_t last_connect_attempt_{0};       // Timestamp of the last connection attempt
  const uint32_t RECONNECT_INTERVAL_MS = 5000; // Interval to try reconnecting if disconnected
};

} // namespace luxpower_sna
} // namespace esphome
