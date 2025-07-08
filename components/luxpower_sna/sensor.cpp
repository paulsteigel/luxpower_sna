// custom_components/luxpower_inverter/sensor.cpp
#include "sensor.h" // NEW: Include the self-referential header
#include "luxpower_inverter.h" // Include the main component to access decode_luxpower_value

namespace esphome {
namespace luxpower_inverter {

void LuxpowerSensor::update_state(uint16_t raw_value) {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "LuxpowerSensor parent component is null!");
    this->publish_state(NAN);
    return;
  }

  float value = this->parent_->decode_luxpower_value(raw_value, this->reg_type_);

  if (std::isnan(value)) {
    ESP_LOGW(TAG, "Sensor '%s' (Reg 0x%04X): Decoding returned NAN. Publishing unavailable.",
             this->get_name().c_str(), this->register_address_);
    this->publish_state(NAN);
  } else {
    this->publish_state(value);
  }
}

} // namespace luxpower_inverter
} // namespace esphome