#pragma once
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h" // RESTORED correct path for built-in ESPHome sensor
#include "luxpower_inverter.h" // Now refers to luxpower_inverter.h in the same directory

namespace esphome {
namespace luxpower_sna {

class LuxpowerSnaSensor : public sensor::Sensor, public Component { // RENAMED CLASS
 public:
  LuxpowerSnaSensor(const std::string &name, uint16_t register_address, LuxpowerRegType reg_type, uint8_t bank);

  void setup() override;
  void dump_config() override;
  void loop() override;

  void set_parent(LuxPowerInverterComponent *parent) { parent_ = parent; }

 protected:
  LuxPowerInverterComponent *parent_;
  uint16_t register_address_;
  LuxpowerRegType reg_type_;
  uint8_t bank_;
};

} // namespace luxpower_sna
} // namespace esphome