#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"

namespace esphome {
namespace luxpower_sna {

class LuxpowerSnaSensor : public sensor::Sensor, public Component {
 public:
  void dump_config() override;
};

}  // namespace luxpower_sna
}  // namespace esphome
