#include "sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna.sensor";

void LuxpowerSnaSensor::dump_config() { LOG_SENSOR("", "Luxpower SNA Sensor", this); }

}  // namespace luxpower_sna
}  // namespace esphome
