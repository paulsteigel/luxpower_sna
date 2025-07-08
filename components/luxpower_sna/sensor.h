#pragma once
#include "esphome/core/component.h"
#include "sensor/sensor.h" // MODIFIED LINE
#include "luxpower_inverter.h"

namespace esphome {
namespace luxpower_sna {

class LuxpowerSensor : public sensor::Sensor, public Component {
 public:
  LuxpowerSensor(const std::string &name, uint16_t register_address, LuxpowerRegType reg_type, uint8_t bank);

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