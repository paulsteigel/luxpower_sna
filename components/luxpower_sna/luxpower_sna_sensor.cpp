// components/luxpower_sna/luxpower_sna_sensor.cpp
#include "luxpower_sna_sensor.h"
#include "luxpower_sna_inverter.h" // Include the main component header
#include "esphome/core/log.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna.sensor";

void LuxpowerSnaSensor::update_value(const std::map<uint16_t, uint16_t>& register_values) {
  // Check if the register address exists in the map of received values
  if (register_values.count(this->register_address_)) {
    uint16_t raw_value = register_values.at(this->register_address_);
    float processed_value = static_cast<float>(raw_value);

    // Apply divisor if specified
    if (this->divisor_ != 0.0f && this->divisor_ != 1.0f) {
      processed_value /= this->divisor_;
    }

    // Handle signed values if specified (e.g., 16-bit signed integer)
    if (this->is_signed_) {
      // Assuming 16-bit signed integer for now. Adjust if different.
      int16_t signed_raw_value = static_cast<int16_t>(raw_value);
      processed_value = static_cast<float>(signed_raw_value);
      if (this->divisor_ != 0.0f && this->divisor_ != 1.0f) {
        processed_value /= this->divisor_;
      }
    }

    // Publish the state to ESPHome
    this->publish_state(processed_value);
    ESP_LOGD(TAG, "Sensor '%s' (Reg 0x%04X): Raw=%u, Processed=%.2f", this->get_name().c_str(), this->register_address_, raw_value, processed_value);
  } else {
    ESP_LOGW(TAG, "Sensor '%s' (Reg 0x%04X): Register value not found in received data.", this->get_name().c_str(), this->register_address_);
    // Optionally publish unavailable state
    this->publish_state(NAN); // Not a Number to indicate unavailable
  }
}

} // namespace luxpower_sna
} // namespace esphome
