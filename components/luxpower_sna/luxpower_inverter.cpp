// src/esphome/components/luxpower_sna/luxpower_sna_sensor.cpp

#include "luxpower_sna_sensor.h" // Must be first
#include "esphome/core/log.h"
// No explicit need to include sensor/sensor.h here if it's already in luxpower_sna_sensor.h
// #include "esphome/components/sensor/sensor.h" // Usually not needed directly here if it's in the .h file

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna.sensor"; // Tag for logging in this specific sensor file

// Constructor implementation
// NOTE: Removed 'sensor::Sensor(name)' from initializer list.
// Base Sensor class does not have a constructor that takes only a name.
LuxpowerSnaSensor::LuxpowerSnaSensor(const std::string &name, uint16_t register_address, LuxpowerRegType reg_type, uint8_t bank)
    : register_address_(register_address), reg_type_(reg_type), bank_(bank) {
    // Set the name using the base Sensor's inherited method
    this->set_name(name);
    ESP_LOGD(TAG, "LuxPower SNA Sensor '%s' created for Reg: 0x%04X, Type: %u, Bank: %u",
             name.c_str(), register_address, reg_type, bank);
}

void LuxpowerSnaSensor::setup() {
  // Add any setup logic specific to this sensor instance, if needed.
  ESP_LOGCONFIG(TAG, "Setting up LuxPower SNA Sensor: %s", get_name().c_str());
}

void LuxpowerSnaSensor::dump_config() {
  // LOG_SENSOR is a helper macro for standard ESPHome sensor logging.
  LOG_SENSOR(TAG, "LuxPower SNA Sensor", this);
  ESP_LOGCONFIG(TAG, "  Register Address: 0x%04X", register_address_);
  ESP_LOGCONFIG(TAG, "  Register Type: %u", reg_type_);
  ESP_LOGCONFIG(TAG, "  Bank: %u", bank_);
}

void LuxpowerSnaSensor::loop() {
  // Sensors often don't need a loop() method if their state is updated
  // by the parent component (LuxPowerInverterComponent) directly.
  // If this sensor needs to perform periodic checks or tasks independent
  // of the main inverter component's polling, implement them here.
}

// No need to implement get_name() or publish_state() here, as they are inherited
// from the esphome::sensor::Sensor base class.

} // namespace luxpower_sna
} // namespace esphome