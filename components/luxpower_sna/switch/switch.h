#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"

// Forward-declare the parent class to avoid circular dependencies
namespace esphome {
namespace luxpower_sna {
class LuxpowerSNAComponent;

class LuxpowerSNASwitch : public switch_::Switch, public Component {
 public:
  void set_parent(LuxpowerSNAComponent *parent) { this->parent_ = parent; }
  void set_register_address(uint16_t address) { this->register_address_ = address; }
  void set_bitmask(uint16_t mask) { this->bitmask_ = mask; }

  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

 protected:
  void write_state(bool state) override;

  LuxpowerSNAComponent *parent_;
  uint16_t register_address_;
  uint16_t bitmask_;
};

}  // namespace luxpower_sna
}  // namespace esphome
