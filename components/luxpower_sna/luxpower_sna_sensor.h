// components/luxpower_sna/luxpower_sna_sensor.h
#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include <map> // For std::map<uint16_t, uint16_t>
#include <string>

namespace esphome {
namespace luxpower_sna {

// Forward declaration of the main component
class LuxpowerSnaComponent;

class LuxpowerSnaSensor : public sensor::Sensor {
 public:
  // Set the parent component (LuxpowerSnaComponent)
  void set_parent(LuxpowerSnaComponent *parent) { this->parent_ = parent; }
  // Set the register address this sensor will read from
  void set_register_address(uint16_t address) { this->register_address_ = address; }
  // Set the factor to divide the raw register value by (e.g., 10 for 0.1 precision)
  void set_divisor(float divisor) { this->divisor_ = divisor; }
  // Set whether the value is signed
  void set_is_signed(bool is_signed) { this->is_signed_ = is_signed; }

  // This method will be called by the main component when new data is available.
  void update_value(const std::map<uint16_t, uint16_t>& register_values);

 protected:
  LuxpowerSnaComponent *parent_{nullptr};
  uint16_t register_address_{0};
  float divisor_{1.0f}; // Default to no division
  bool is_signed_{false}; // Default to unsigned
};

} // namespace luxpower_sna
} // namespace esphome
