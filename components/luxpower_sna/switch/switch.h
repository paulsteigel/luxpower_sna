#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "../luxpower_sna.h"

namespace esphome {
namespace luxpower_sna {

// All bit definitions in the switch component where they're used
static const uint16_t FEED_IN_GRID = 1 << 15;
static const uint16_t DCI_ENABLE = 1 << 14;
static const uint16_t GFCI_ENABLE = 1 << 13;
static const uint16_t R21_UNKNOWN_BIT_12 = 1 << 12;
static const uint16_t CHARGE_PRIORITY = 1 << 11;
static const uint16_t FORCED_DISCHARGE_ENABLE = 1 << 10;
static const uint16_t NORMAL_OR_STANDBY = 1 << 9;
static const uint16_t SEAMLESS_EPS_SWITCHING = 1 << 8;
static const uint16_t AC_CHARGE_ENABLE = 1 << 7;
static const uint16_t GRID_ON_POWER_SS = 1 << 6;
static const uint16_t NEUTRAL_DETECT_ENABLE = 1 << 5;
static const uint16_t ANTI_ISLAND_ENABLE = 1 << 4;
static const uint16_t R21_UNKNOWN_BIT_3 = 1 << 3;
static const uint16_t DRMS_ENABLE = 1 << 2;
static const uint16_t OVF_LOAD_DERATE_ENABLE = 1 << 1;
static const uint16_t POWER_BACKUP_ENABLE = 1 << 0;

// Register 110 bits
static const uint16_t TAKE_LOAD_TOGETHER = 1 << 10;
static const uint16_t CHARGE_LAST = 1 << 4;
static const uint16_t MICRO_GRID_ENABLE = 1 << 2;
static const uint16_t FAST_ZERO_EXPORT_ENABLE = 1 << 1;
static const uint16_t RUN_WITHOUT_GRID = 1 << 1;
static const uint16_t PV_GRID_OFF_ENABLE = 1 << 0;

// Register 120 bits
static const uint16_t GEN_CHRG_ACC_TO_SOC = 1 << 7;
static const uint16_t R120_UNKNOWN_BIT_06 = 1 << 6;
static const uint16_t R120_UNKNOWN_BIT_05 = 1 << 5;
static const uint16_t DISCHARG_ACC_TO_SOC = 1 << 4;
static const uint16_t R120_UNKNOWN_BIT_03 = 1 << 3;
static const uint16_t AC_CHARGE_MODE_B_02 = 1 << 2;
static const uint16_t AC_CHARGE_MODE_B_01 = 1 << 1;
static const uint16_t R120_UNKNOWN_BIT_00 = 1 << 0;

// Register 179 bits
static const uint16_t ENABLE_PEAK_SHAVING = 1 << 7;
static const uint16_t R179_UNKNOWN_BIT_15 = 1 << 15;
static const uint16_t R179_UNKNOWN_BIT_14 = 1 << 14;
static const uint16_t R179_UNKNOWN_BIT_13 = 1 << 13;
static const uint16_t R179_UNKNOWN_BIT_12 = 1 << 12;
static const uint16_t R179_UNKNOWN_BIT_11 = 1 << 11;
static const uint16_t R179_UNKNOWN_BIT_10 = 1 << 10;
static const uint16_t R179_UNKNOWN_BIT_09 = 1 << 9;
static const uint16_t R179_UNKNOWN_BIT_08 = 1 << 8;
static const uint16_t R179_UNKNOWN_BIT_06 = 1 << 6;
static const uint16_t R179_UNKNOWN_BIT_05 = 1 << 5;
static const uint16_t R179_UNKNOWN_BIT_04 = 1 << 4;
static const uint16_t R179_UNKNOWN_BIT_03 = 1 << 3;
static const uint16_t R179_UNKNOWN_BIT_02 = 1 << 2;
static const uint16_t R179_UNKNOWN_BIT_01 = 1 << 1;
static const uint16_t R179_UNKNOWN_BIT_00 = 1 << 0;

class LuxPowerSwitch : public switch_::Switch, public Component {
 public:
  // Configuration
  void set_parent(LuxpowerSNAComponent *parent) { parent_ = parent; }
  void set_register_address(uint16_t reg) { register_address_ = reg; }
  void set_bitmask(uint16_t mask) { bitmask_ = mask; }
  void set_switch_type(const std::string &type) { switch_type_ = type; }

  // Component lifecycle
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  void write_state(bool state) override;

 private:
  LuxpowerSNAComponent *parent_{nullptr};
  uint16_t register_address_{0};
  uint16_t bitmask_{0};
  std::string switch_type_;
  
  uint32_t last_read_attempt_{0};
  uint32_t read_interval_{30000};  // Read every 30s to sync state
  uint16_t cached_register_value_{0};
  bool has_cached_value_{false};
  
  // Bit manipulation (from Python prepare_binary_value)
  uint16_t prepare_binary_value_(uint16_t old_value, uint16_t mask, bool enable);
  void update_state_from_register_(uint16_t reg_value);
};

}  // namespace luxpower_sna
}  // namespace esphome
