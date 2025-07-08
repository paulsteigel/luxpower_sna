#include "luxpower_sna_sensor.h" // MODIFIED include (to its new name, relative within the subfolder)
#include "esphome/core/log.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

LuxpowerSnaSensor::LuxpowerSnaSensor(const std::string &name, uint16_t register_address, LuxpowerRegType reg_type, uint8_t bank)
    : sensor::Sensor(name), register_address_(register_address), reg_type_(reg_type), bank_(bank) {}

void LuxpowerSnaSensor::setup() {
  // Nothing specific to set up here for the sensor itself, parent component handles communication
}

void LuxpowerSnaSensor::dump_config() {
  ESP_LOGCONFIG(TAG, "Luxpower SNA Sensor '%s':", get_name().c_str());
  ESP_LOGCONFIG(TAG, "  Register Address: 0x%04X", register_address_);
  switch (reg_type_) {
    case LUX_REG_TYPE_INT:
      ESP_LOGCONFIG(TAG, "  Register Type: INT");
      break;
    case LUX_REG_TYPE_FLOAT_DIV10:
      ESP_LOGCONFIG(TAG, "  Register Type: FLOAT_DIV10");
      break;
    case LUX_REG_TYPE_SIGNED_INT:
      ESP_LOGCONFIG(TAG, "  Register Type: SIGNED_INT");
      break;
    case LUX_REG_TYPE_FIRMWARE:
      ESP_LOGCONFIG(TAG, "  Register Type: FIRMWARE");
      break;
    case LUX_REG_TYPE_MODEL:
      ESP_LOGCONFIG(TAG, "  Register Type: MODEL");
      break;
    case LUX_REG_TYPE_BITMASK:
      ESP_LOGCONFIG(TAG, "  Register Type: BITMASK");
      break;
    case LUX_REG_TYPE_TIME_MINUTES:
      ESP_LOGCONFIG(TAG, "  Register Type: TIME_MINUTES");
      break;
    default:
      ESP_LOGCONFIG(TAG, "  Register Type: Unknown");
      break;
  }
  ESP_LOGCONFIG(TAG, "  Bank: %u", bank_);
}

void LuxpowerSnaSensor::loop() {
  // Handled by the parent component, this sensor just holds a value
}

} // namespace luxpower_sna
} // namespace esphome