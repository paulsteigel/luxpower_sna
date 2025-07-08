
#include "esphome/core/component.h"
//#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/switch/switch.h"
#pragma once
#include <vector>
#include <map>

#include <ESPAsyncTCP.h> // <--- ADD THIS LINE for AsyncClient
//#include "luxpower_sna_sensor.h" // <--- ADD THIS LINE for LuxpowerSnaSensor definition

// Other includes as needed

namespace esphome {
namespace luxpower_sna {

class LuxPowerInverterComponent : public Component {
  // ... rest of your class definition ...
  protected:
    AsyncClient *client_; // This will now be recognized
    // ...
};

} // namespace luxpower_sna
} // namespace esphome