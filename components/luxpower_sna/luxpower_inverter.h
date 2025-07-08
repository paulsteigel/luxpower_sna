// custom_components/luxpower_inverter/luxpower_inverter.h
#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "sensor.h" // NEW: Include the renamed sensor header

// Include networking headers based on your ESP board type
#include <AsyncTCP.h> // For ESP32
// #include <ESPAsyncTCP.h> // For ESP8266

#include <vector>
#include <string>
#include <map>
#include <deque>

namespace esphome {
namespace luxpower_inverter {

static const char *const TAG = "luxpower_inverter"; // For logging

// --- Luxpower Register Types (from const.py) ---
// This enum must match the `LuxpowerRegType` defined in `__init__.py`
// Keep this here as it's a fundamental type used by both the main component and sensors
enum LuxpowerRegType {
  LUX_REG_TYPE_INT = 0,
  LUX_REG_TYPE_FLOAT_DIV10,
  LUX_REG_TYPE_SIGNED_INT,
  LUX_REG_TYPE_FIRMWARE,
  LUX_REG_TYPE_MODEL,
  LUX_REG_TYPE_BITMASK,
  LUX_REG_TYPE_TIME_MINUTES,
  // Add other types as needed from your const.py
};

// --- Main Luxpower Inverter Component ---
class LuxPowerInverterComponent : public Component {
 public:
  LuxPowerInverterComponent();

  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return esphome::setup_priority::DATA; }

  // Setters for YAML configuration
  void set_inverter_host(const std::string &host) { this->inverter_host_ = host; }
  void set_inverter_port(int port) { this->inverter_port_ = port; }
  void set_dongle_serial(const std::string &serial) { this->dongle_serial_ = serial; }
  void set_inverter_serial_number(const std::string &serial) { this->inverter_serial_number_ = serial; }
  void set_update_interval(uint32_t interval_ms) { this->update_interval_ = interval_ms; }

  // Method to register Luxpower sensors
  // Note: `sensor::Sensor *obj` is from esphome/components/sensor/sensor.h,
  // ensure that include is still effective.
  void add_luxpower_sensor(esphome::components::sensor::Sensor *obj, const std::string &name, uint16_t reg_addr, LuxpowerRegType reg_type, uint8_t bank);

  // --- TCP Client Management ---
  bool connect_to_inverter();
  void disconnect_from_inverter();
  bool is_connected() const { return client_ && client_->connected(); }
  bool send_data(const std::vector<uint8_t>& data);

  // --- Callbacks for AsyncTCP events ---
  void on_connect_cb(void *arg, AsyncClient *client);
  void on_disconnect_cb(void *arg, AsyncClient *client);
  void on_data_cb(void *arg, AsyncClient *client, void *data, size_t len);
  void on_error_cb(void *arg, AsyncClient *client, int error);

  // --- Public helper for decoding values (used by LuxpowerSensor) ---
  float decode_luxpower_value(uint16_t raw_value, LuxpowerRegType reg_type);

 protected:
  std::string inverter_host_;
  int inverter_port_;
  std::string dongle_serial_;
  std::string inverter_serial_number_;
  uint32_t update_interval_;

  AsyncClient *client_{nullptr};
  bool client_connected_ = false;

  std::deque<uint8_t> rx_buffer_;

  uint32_t last_connect_attempt_ = 0;
  uint32_t connect_retry_interval_ = 5000;

  uint32_t last_request_time_ = 0;

  std::vector<LuxpowerSensor *> luxpower_sensors_; // Vector of pointers to our custom sensors

  std::map<uint16_t, uint16_t> current_raw_registers_; // Stores raw register values

  // Luxpower Protocol Helpers (still in main component as they relate to general comms)
  uint16_t calculate_crc16_modbus(const std::vector<uint8_t>& data);
  std::vector<uint8_t> create_modbus_read_holding_registers_request(uint8_t unit_id, uint16_t start_address, uint16_t num_registers);
  bool parse_modbus_read_holding_registers_response(const std::vector<uint8_t>& response, uint8_t unit_id, uint16_t start_address, uint16_t num_registers);
};

} // namespace luxpower_inverter
} // namespace esphome