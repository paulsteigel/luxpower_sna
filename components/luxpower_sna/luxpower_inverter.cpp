// src/esphome/components/luxpower_sna/luxpower_sna_sensor.cpp

#include "luxpower_sna_sensor.h" // Must be included first for the class definition
#include "esphome/core/log.h"     // For logging macros like ESP_LOGD, ESP_LOGCONFIG

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna.sensor"; // Tag for logging output from this file

// Constructor implementation for LuxpowerSnaSensor
// IMPORTANT: Ensure your initializer list does NOT call sensor::Sensor(name).
// The base sensor::Sensor class is default-constructed implicitly here.
LuxpowerSnaSensor::LuxpowerSnaSensor(const std::string &name, uint16_t register_address, LuxpowerRegType reg_type, uint8_t bank)
    : register_address_(register_address), reg_type_(reg_type), bank_(bank) {
    // Set the name using the base Sensor's inherited method.
    // Convert std::string 'name' to const char* using .c_str()
    this->set_name(name.c_str()); // <--- **CRITICAL CHANGE HERE**

    ESP_LOGD(TAG, "LuxPower SNA Sensor '%s' created for Reg: 0x%04X, Type: %u, Bank: %u",
             name.c_str(), register_address, reg_type, bank);
}

void LuxpowerSnaSensor::setup() {
  // Add any setup logic specific to this sensor instance, if needed.
  // This method is called once at startup.
  ESP_LOGCONFIG(TAG, "Setting up LuxPower SNA Sensor: %s", get_name().c_str());
}

void LuxpowerSnaSensor::dump_config() {
  // Used for printing the configuration details to the ESPHome logs during startup.
  LOG_SENSOR(TAG, "LuxPower SNA Sensor", this); // ESPHome helper for sensor config output
  ESP_LOGCONFIG(TAG, "  Register Address: 0x%04X", register_address_);
  ESP_LOGCONFIG(TAG, "  Register Type: %u", reg_type_);
  ESP_LOGCONFIG(TAG, "  Bank: %u", bank_);
}

void LuxpowerSnaSensor::loop() {
  // This method is called repeatedly.
  // For sensors whose state is updated by a parent component (like LuxPowerInverterComponent),
  // this method might be empty unless the sensor has independent periodic tasks.
}

} // namespace luxpower_sna
} // namespace esphome