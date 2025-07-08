#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"

namespace esphome {
namespace luxpower_sna {

// Forward-declare the parent component class to avoid circular dependencies
class LuxpowerInverterComponent;

class LuxpowerSnaSensor : public sensor::Sensor, public Component {
 public:
  // Methods to set configuration from YAML (called by to_code)
  void set_parent(LuxpowerInverterComponent *parent) { this->parent_ = parent; }
  void set_register_address(uint16_t address) { this->register_address_ = address; }
  void set_divisor(float divisor) { this->divisor_ = divisor; }
  void set_is_signed(bool is_signed) { this->is_signed_ = is_signed; }

  // Getter methods for the parent component to use
  uint16_t get_register_address() const { return this->register_address_; }
  float get_divisor() const { return this->divisor_; }
  bool get_is_signed() const { return this->is_signed_; }

  void dump_config() override;

 protected:
  LuxpowerInverterComponent *parent_;
  uint16_t register_address_{0};
  float divisor_{1.0};
  bool is_signed_{false};
};

}  // namespace luxpower_sna
}  // namespace esphome
