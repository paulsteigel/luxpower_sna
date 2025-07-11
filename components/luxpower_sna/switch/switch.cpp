#include "switch.h"
#include "luxpower_sna.h" // Include the parent header
#include "esphome/core/log.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna.switch";

void LuxpowerSNASwitch::dump_config() {
  LOG_SWITCH("", "LuxpowerSNA Switch", this);
  ESP_LOGCONFIG(TAG, "  Register: 0x%02X, Bitmask: 0x%04X", this->register_address_, this->bitmask_);
}

void LuxpowerSNASwitch::write_state(bool state) {
  // Call the public method on the parent component to handle the write
  this->parent_->queue_write_register_bit(this->register_address_, this->bitmask_, state);
}

}  // namespace luxpower_sna
}  // namespace esphome
