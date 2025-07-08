#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
//#include "esphome/components/uart/uart.h" // Required for UARTDevice

// Include your constants file
#include "luxpower_sna_constants.h"

// If you have sensors associated with this component, include the sensor header
// #include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace luxpower_sna {

class LuxPowerInverter : public uart::UARTDevice, public Component {
 public:
  // Standard ESPHome component methods
  void setup() override;
  void loop() override; // Declare loop as a member function
  float get_setup_priority() const override;

  // Declare the parse_luxpower_response_packet function
  void parse_luxpower_response_packet(const std::vector<uint8_t> &data);

  // Declare parse_and_publish_register (if not already declared elsewhere or implemented)
  // This function will be responsible for mapping decoded register values to ESPHome sensors
  void parse_and_publish_register(uint16_t reg_address, uint16_t value);

  // Dummy status functions (you can expand these to update a status sensor)
  void status_set_warning();
  void status_set_ok();

  // Add any specific sensor pointers or other member variables here that your component will manage.
  // For example:
  // sensor::Sensor *some_voltage_sensor_{nullptr};
  // void set_some_voltage_sensor(sensor::Sensor *sensor) { this->some_voltage_sensor_ = sensor; }

 protected:
  // Member variables for internal buffer and state tracking
  std::vector<uint8_t> data_buffer_;
  uint32_t last_byte_received_{0};
  uint32_t read_timeout_{50}; // Example timeout in milliseconds for UART buffer

  uint32_t rx_count_{0}; // Counter for received packets
  uint32_t tx_count_{0}; // Counter for transmitted packets (if you implement sending commands)
};

} // namespace luxpower_sna
} // namespace esphome
