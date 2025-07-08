// src/esphome/components/luxpower_sna/luxpower_inverter.h

#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h" // Might be needed for some ESPHome macros
#include "esphome/core/log.h"     // For ESP_LOGx macros if used in header
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