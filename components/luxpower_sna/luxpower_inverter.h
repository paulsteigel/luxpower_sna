// src/esphome/components/luxpower_sna/luxpower_inverter.h
// Corrected to add missing member variable declarations

#pragma once

#include "esphome/core/component.h" // For esphome::Component base class
#include "esphome/core/helpers.h" // For optional utility functions (e.g., format_hex_pretty)
#include "esphome/core/time.h" // For ESPHome's internal time (millis(), etc.)
#include "esphome/components/sensor/sensor.h" // Needed if LuxpowerSnaSensor is a sensor::Sensor
#include "luxpower_sna_sensor.h" // For LuxpowerSnaSensor class definition
#include "luxpower_sna_constants.h" // For constants like LUXPOWER_START_BYTE, etc.

#include <AsyncTCP.h>      // For AsyncClient
#include <vector>          // For std::vector
#include <string>          // For std::string
#include <deque>           // For std::deque (rx_buffer_)
#include <map>             // For std::map (current_raw_registers_)
#include <chrono>          // <--- ADD THIS INCLUDE FOR std::chrono::milliseconds

namespace esphome {
namespace luxpower_sna {

// Forward declaration for the component itself, needed for set_parent() in LuxpowerSnaSensor
class LuxPowerInverterComponent;

class LuxPowerInverterComponent : public Component { // Inherit from esphome::Component
public:
  // Constructor
  LuxPowerInverterComponent();

  // Lifecycle methods
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override; // Component setup priority

  // Setters for configuration variables (called by YAML config)
  void set_inverter_host(const std::string &host) { this->inverter_host_ = host; }
  void set_inverter_port(uint16_t port) { this->inverter_port_ = port; }
  void set_dongle_serial(const std::string &serial) { this->dongle_serial_ = serial; } // <--- ADD THIS SETTER
  void set_inverter_serial_number(const std::string &serial) { this->inverter_serial_number_ = serial; } // <--- ADD THIS SETTER
  void set_update_interval(uint32_t update_interval_ms) { this->update_interval_ = std::chrono::milliseconds(update_interval_ms); } // <--- ADD THIS SETTER

  // Method to add LuxpowerSnaSensor instances created from YAML
  // obj is already a configured LuxpowerSnaSensor from the YAML
  void add_luxpower_sensor(LuxpowerSnaSensor *obj, const std::string &name, uint16_t reg_addr, LuxpowerRegType reg_type, uint8_t bank);


protected:
  // TCP Client and connection management
  AsyncClient *client_{nullptr};
  bool client_connected_{false};
  std::deque<uint8_t> rx_buffer_; // Buffer for incoming TCP data
  uint32_t last_request_time_;
  uint32_t last_connection_attempt_time_; // <--- CORRECTED NAME, WAS last_connect_attempt_
  const uint32_t connect_retry_interval_ = 5000; // 5 seconds retry interval

  // Configuration variables
  std::string inverter_host_;
  uint16_t inverter_port_;
  std::string dongle_serial_;       // <--- ADD THIS MEMBER
  std::string inverter_serial_number_; // <--- ADD THIS MEMBER
  std::chrono::milliseconds update_interval_; // <--- ADD THIS MEMBER

  // Map to store current raw register values (address -> value)
  std::map<uint16_t, uint16_t> current_raw_registers_;

  // List of associated LuxpowerSnaSensor objects
  std::vector<LuxpowerSnaSensor *> luxpower_sensors_;

  // Internal connection helper methods
  bool is_connected();
  bool connect_to_inverter();
  void disconnect_from_inverter();
  bool send_data(const std::vector<uint8_t>& data);

  // AsyncTCP Callbacks
  static void on_connect_cb(void *arg, AsyncClient *client);
  static void on_disconnect_cb(void *arg, AsyncClient *client);
  static void on_data_cb(void *arg, AsyncClient *client, void *data, size_t len);
  static void on_error_cb(void *arg, AsyncClient *client, int error);

  // Luxpower Proprietary Protocol Helpers
  uint16_t calculate_luxpower_crc16(const std::vector<uint8_t>& data);
  std::vector<uint8_t> build_luxpower_request_packet(uint8_t function_code, uint16_t register_address, uint16_t num_registers_or_data, const std::vector<uint8_t>& additional_data = {});
  bool parse_luxpower_response_packet(const std::vector<uint8_t>& response_packet, std::vector<uint8_t>& out_payload);
  bool interpret_modbus_read_holding_registers_payload(const std::vector<uint8_t>& payload, uint16_t expected_start_address, uint16_t expected_num_registers);

  // Sensor value interpretation
  float get_sensor_value_(uint16_t raw_value, LuxpowerRegType reg_type);
  std::string get_firmware_version_(const std::vector<uint16_t>& data); // New helper for firmware string
  std::string get_model_name_(const std::vector<uint16_t>& data);     // New helper for model string
};

} // namespace luxpower_sna
} // namespace esphome
