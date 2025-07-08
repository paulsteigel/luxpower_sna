#include "luxpower_sna_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna.sensor";

void LuxpowerSnaSensor::dump_config() {
  LOG_SENSOR("", "Luxpower SNA Sensor", this);
  ESP_LOGCONFIG(TAG, "  Register: 0x%04X (%d)", this->register_address_, this->register_address_);
  ESP_LOGCONFIG(TAG, "  Divisor: %.2f", this->divisor_);
  ESP_LOGCONFIG(TAG, "  Is Signed: %s", this->is_signed_ ? "true" : "false");
}

}  // namespace luxpower_sna
}  // namespace esphome
