#include "luxpower_switch.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace luxpower {

static const char *const TAG = "jk_bms.switch";

void LuxpowerSwitch::dump_config() { LOG_SWITCH("", "Luxpower Switch", this); }
void LuxpowerSwitch::write_state(bool state) {
  this->parent_->write_register(this->holding_register_, (uint8_t) state);
  this->publish_state(state);
}

}  // namespace LuxpowerSwitch
}  // namespace esphome
