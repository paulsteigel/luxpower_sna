#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
//#include "sensor/sensor.h" // CORRECT: Now points to the renamed file in the subfolder
#include <vector>
#include <string>
#include <chrono> // For std::chrono::steady_clock

namespace esphome {
namespace luxpower_sna { // Correct namespace

// Forward declaration of LuxpowerSnaSensor class from the sensor sub-component
class LuxpowerSnaSensor; 

// Define the enum for Luxpower register types
enum LuxpowerRegType {
    LUX_REG_TYPE_INT = 0,
    LUX_REG_TYPE_FLOAT_DIV10 = 1,
    LUX_REG_TYPE_SIGNED_INT = 2,
    LUX_REG_TYPE_FIRMWARE = 3,
    LUX_REG_TYPE_MODEL = 4,
    LUX_REG_TYPE_BITMASK = 5,
    LUX_REG_TYPE_TIME_MINUTES = 6,
};

class LuxPowerInverterComponent : public Component {
 public:
  // Constructor
  LuxPowerInverterComponent(); // Declare constructor

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  // Configuration setters from YAML
  void set_inverter_host(const std::string &host) { this->inverter_host_ = host; }
  void set_inverter_port(uint16_t port) { this->inverter_port_ = port; }
  void set_dongle_serial(const std::string &serial) { this->dongle_serial_ = serial; }
  void set_inverter_serial_number(const std::string &serial) { this->inverter_serial_number_ = serial; }
  void set_update_interval(uint32_t update_interval_ms) { this->update_interval_ = std::chrono::milliseconds(update_interval_ms); }

  // Method to add sensors dynamically from the configuration
  void add_luxpower_sensor(LuxpowerSnaSensor *obj, const std::string &name, uint16_t register_address, LuxpowerRegType reg_type, uint8_t bank);

 protected:
  std::string inverter_host_;
  uint16_t inverter_port_;
  std::string dongle_serial_;
  std::string inverter_serial_number_;
  std::chrono::milliseconds update_interval_; // Changed to chrono duration

  // Vector to hold pointers to all Luxpower sensors managed by this component
  std::vector<LuxpowerSnaSensor *> luxpower_sensors_;

  // Timestamp for the last successful data update
  std::chrono::steady_clock::time_point last_update_time_;

  // Store raw register values received from inverter
  std::map<uint16_t, uint16_t> current_raw_registers_;

  // Private methods for Modbus communication
  // Reads 'num_registers' from 'start_address' and stores them in 'out_data'
  bool read_registers_(uint16_t start_address, uint16_t num_registers, std::vector<uint16_t>& out_data);
  
  // Sends a raw Modbus TCP command and receives a response
  bool send_modbus_command_(const std::vector<uint8_t>& command, std::vector<uint8_t>& response);
  
  // Placeholder for CRC calculation (not typically used for Modbus TCP, but good to have)
  uint16_t calculate_crc_(const std::vector<uint8_t>& data);

  // Helper to convert raw register value to the appropriate sensor value
  float get_sensor_value_(uint16_t register_value, LuxpowerRegType reg_type);
  
  // Helpers to decode specific string-based registers
  std::string get_firmware_version_(const std::vector<uint16_t>& data);
  std::string get_model_name_(const std::vector<uint16_t>& data);
};

} // namespace luxpower_sna
} // namespace esphome