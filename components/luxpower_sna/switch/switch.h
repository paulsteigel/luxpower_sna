#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "../luxpower_sna.h"

namespace esphome {
namespace luxpower_sna {

// All switch-specific bit definitions
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
static const uint16_t PV_GRID_OFF_ENABLE = 1 << 0;

// Register 120 bits
static const uint16_t GEN_CHRG_ACC_TO_SOC = 1 << 7;
static const uint16_t DISCHARG_ACC_TO_SOC = 1 << 4;
static const uint16_t AC_CHARGE_MODE_B_02 = 1 << 2;
static const uint16_t AC_CHARGE_MODE_B_01 = 1 << 1;

// Register 179 bits
static const uint16_t ENABLE_PEAK_SHAVING = 1 << 7;

class LuxPowerSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(LuxpowerSNAComponent *parent) { parent_ = parent; }
  void set_register_address(uint16_t reg) { register_address_ = reg; }
  void set_bitmask(uint16_t mask) { bitmask_ = mask; }
  void set_switch_type(const std::string &type) { switch_type_ = type; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  void write_state(bool state) override;

 private:
  LuxpowerSNAComponent *parent_{nullptr};
  uint16_t register_address_;
  uint16_t bitmask_;
  std::string switch_type_;
  
  uint32_t last_sync_{0};
  static const uint32_t SYNC_INTERVAL = 30000;  // 30s
  
  uint16_t prepare_binary_value_(uint16_t old_value, uint16_t mask, bool enable);
  void sync_state_();
};

}  // namespace luxpower_sna
}  // namespace esphome
