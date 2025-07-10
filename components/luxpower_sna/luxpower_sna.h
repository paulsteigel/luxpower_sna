#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <vector>
#include <map>
#include <string>

#ifdef USE_ESP32
#include <AsyncTCP.h>
#elif USE_ESP8266
#include <ESPAsyncTCP.h>
#endif

namespace esphome {
namespace luxpower_sna {

// --- DATA STRUCTS PORTED FROM LuxParser.h ---
// Use __attribute__((packed)) to ensure structs have no memory padding.
// This is critical for correctly mapping network data to structs.

struct Header {
  uint16_t prefix;
  uint16_t protocolVersion;
  uint16_t packetLength;
  uint8_t  address;
  uint8_t  function;
  char     serialNumber[10];
  uint16_t dataLength;
} __attribute__((packed));

struct TranslatedData {
  uint8_t  address;
  uint8_t  deviceFunction;
  char     serialNumber[10];
  uint16_t registerStart;
  uint8_t  dataFieldLength;
} __attribute__((packed));

struct LogDataRawSection1 { // Registers 0-39
  uint16_t status;
  int16_t  pv1_voltage; int16_t  pv2_voltage; int16_t  pv3_voltage;
  int16_t  battery_voltage; uint8_t  soc; uint8_t  soh;
  uint16_t _reserved1;
  int16_t  pv1_power; int16_t  pv2_power; int16_t  pv3_power;
  int16_t  charge_power; int16_t  discharge_power;
  int16_t  voltage_ac_r; int16_t  voltage_ac_s; int16_t  voltage_ac_t;
  int16_t  frequency_grid; int16_t  activeInverter_power;
  int16_t  activeCharge_power; int16_t  inductor_current;
  int16_t  grid_power_factor;
  int16_t  voltage_eps_r; int16_t  voltage_eps_s; int16_t  voltage_eps_t;
  int16_t  frequency_eps; int16_t  active_eps_power; int16_t  apparent_eps_power;
  int16_t  power_to_grid; int16_t  power_from_grid;
  int16_t  pv1_energy_today; int16_t  pv2_energy_today; int16_t  pv3_energy_today;
  int16_t  activeInverter_energy_today; int16_t  ac_charging_today;
  int16_t  charging_today; int16_t  discharging_today; int16_t  eps_today;
  int16_t  exported_today; int16_t  grid_today;
  int16_t  bus1_voltage; int16_t  bus2_voltage;
} __attribute__((packed));

struct LogDataRawSection2 { // Registers 40-79
  int32_t  e_pv_1_all; int32_t  e_pv_2_all; int32_t  e_pv_3_all;
  int32_t  e_inv_all; int32_t  e_rec_all; int32_t  e_chg_all;
  int32_t  e_dischg_all; int32_t  e_eps_all; int32_t  e_to_grid_all;
  int32_t  e_to_user_all;
  uint32_t fault_code; uint32_t warning_code;
  int16_t  t_inner; int16_t  t_rad_1; int16_t  t_rad_2; int16_t  t_bat;
  uint16_t _reserved2; uint32_t uptime;
} __attribute__((packed));

struct LogDataRawSection3 { // Registers 80-119
  uint16_t _reserved3;
  int16_t  max_chg_curr; int16_t  max_dischg_curr;
  int16_t  charge_volt_ref; int16_t  dischg_cut_volt;
  uint8_t  placeholder[20];
  int16_t  bat_status_inv; int16_t  bat_count; int16_t  bat_capacity;
  int16_t  bat_current;
  uint8_t  placeholder2[6];
  int16_t  max_cell_volt; int16_t  min_cell_volt;
  int16_t  max_cell_temp; int16_t  min_cell_temp;
  uint16_t _reserved4; int16_t  bat_cycle_count;
} __attribute__((packed));


class LuxpowerSNAComponent : public PollingComponent {
 public:
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::string &serial_str) { this->dongle_serial_ = serial_str; }
  void set_inverter_serial_number(const std::string &serial_str) { this->inverter_serial_ = serial_str; }

  // Generic sensor setter template
  template<typename T> void set_sensor(const std::string &key, T *sensor) {
      this->sensors_[key] = (EntityBase *)sensor;
  }
  
  // --- Sensor Setters (called by python codegen) ---
  // Section 1
  void set_pv1_voltage_sensor(sensor::Sensor *s) { set_sensor("pv1_voltage", s); }
  void set_pv2_voltage_sensor(sensor::Sensor *s) { set_sensor("pv2_voltage", s); }
  void set_pv3_voltage_sensor(sensor::Sensor *s) { set_sensor("pv3_voltage", s); }
  void set_battery_voltage_sensor(sensor::Sensor *s) { set_sensor("battery_voltage", s); }
  void set_soc_sensor(sensor::Sensor *s) { set_sensor("soc", s); }
  void set_soh_sensor(sensor::Sensor *s) { set_sensor("soh", s); }
  void set_pv1_power_sensor(sensor::Sensor *s) { set_sensor("pv1_power", s); }
  void set_pv2_power_sensor(sensor::Sensor *s) { set_sensor("pv2_power", s); }
  void set_pv3_power_sensor(sensor::Sensor *s) { set_sensor("pv3_power", s); }
  void set_charge_power_sensor(sensor::Sensor *s) { set_sensor("charge_power", s); }
  void set_discharge_power_sensor(sensor::Sensor *s) { set_sensor("discharge_power", s); }
  void set_inverter_power_sensor(sensor::Sensor *s) { set_sensor("inverter_power", s); }
  void set_power_to_grid_sensor(sensor::Sensor *s) { set_sensor("power_to_grid", s); }
  void set_power_from_grid_sensor(sensor::Sensor *s) { set_sensor("power_from_grid", s); }
  void set_grid_voltage_sensor(sensor::Sensor *s) { set_sensor("grid_voltage", s); }
  void set_grid_frequency_sensor(sensor::Sensor *s) { set_sensor("grid_frequency", s); }
  void set_power_factor_sensor(sensor::Sensor *s) { set_sensor("power_factor", s); }
  void set_eps_voltage_sensor(sensor::Sensor *s) { set_sensor("eps_voltage", s); }
  void set_eps_frequency_sensor(sensor::Sensor *s) { set_sensor("eps_frequency", s); }
  void set_eps_active_power_sensor(sensor::Sensor *s) { set_sensor("eps_active_power", s); }
  void set_eps_apparent_power_sensor(sensor::Sensor *s) { set_sensor("eps_apparent_power", s); }
  void set_bus1_voltage_sensor(sensor::Sensor *s) { set_sensor("bus1_voltage", s); }
  void set_bus2_voltage_sensor(sensor::Sensor *s) { set_sensor("bus2_voltage", s); }
  void set_pv1_energy_today_sensor(sensor::Sensor *s) { set_sensor("pv1_energy_today", s); }
  void set_pv2_energy_today_sensor(sensor::Sensor *s) { set_sensor("pv2_energy_today", s); }
  void set_pv3_energy_today_sensor(sensor::Sensor *s) { set_sensor("pv3_energy_today", s); }
  void set_inverter_energy_today_sensor(sensor::Sensor *s) { set_sensor("inverter_energy_today", s); }
  void set_ac_charging_today_sensor(sensor::Sensor *s) { set_sensor("ac_charging_today", s); }
  void set_charging_today_sensor(sensor::Sensor *s) { set_sensor("charging_today", s); }
  void set_discharging_today_sensor(sensor::Sensor *s) { set_sensor("discharging_today", s); }
  void set_eps_today_sensor(sensor::Sensor *s) { set_sensor("eps_today", s); }
  void set_exported_today_sensor(sensor::Sensor *s) { set_sensor("exported_today", s); }
  void set_grid_today_sensor(sensor::Sensor *s) { set_sensor("grid_today", s); }
  
  // Section 2
  void set_total_pv1_energy_sensor(sensor::Sensor *s) { set_sensor("total_pv1_energy", s); }
  void set_total_pv2_energy_sensor(sensor::Sensor *s) { set_sensor("total_pv2_energy", s); }
  void set_total_pv3_energy_sensor(sensor::Sensor *s) { set_sensor("total_pv3_energy", s); }
  void set_total_inverter_output_sensor(sensor::
