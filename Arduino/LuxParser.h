#pragma once
#include <Arduino.h>

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

// --- RAW Data Structs ---
struct LogDataRawSection1 { // Registers 0-39
  uint16_t status; // Matches LXPPacket.py
  int16_t  v_pv_1; // Renamed from pv1_voltage to match LXPPacket.py
  int16_t  v_pv_2; // Renamed from pv2_voltage to match LXPPacket.py
  int16_t  v_pv_3; // Renamed from pv3_voltage to match LXPPacket.py
  int16_t  v_bat; // Renamed from battery_voltage to match LXPPacket.py
  uint8_t  soc; // Matches LXPPacket.py
  uint8_t  soh; // No direct LXPPacket.py key, kept as-is
  uint16_t internal_fault; // Matches LXPPacket.py
  int16_t  p_pv_1; // Renamed from pv1_power to match LXPPacket.py
  int16_t  p_pv_2; // Renamed from pv2_power to match LXPPacket.py
  int16_t  p_pv_3; // Renamed from pv3_power to match LXPPacket.py
  int16_t  p_charge; // Renamed from charge_power to match LXPPacket.py
  int16_t  p_discharge; // Renamed from discharge_power to match LXPPacket.py
  int16_t  v_ac_r; // Renamed from voltage_ac_r to match LXPPacket.py
  int16_t  v_ac_s; // Renamed from voltage_ac_s to match LXPPacket.py
  int16_t  v_ac_t; // Renamed from voltage_ac_t to match LXPPacket.py
  int16_t  f_ac; // Renamed from frequency_grid to match LXPPacket.py
  int16_t  p_inv; // Renamed from activeInverter_power to match LXPPacket.py
  int16_t  p_rec; // Renamed from activeCharge_power to match LXPPacket.py
  int16_t  rms_current; // Renamed from ct_clamp_live to match LXPPacket.py
  int16_t  pf; // Renamed from grid_power_factor to match LXPPacket.py
  int16_t  v_eps_r; // Renamed from voltage_eps_r to match LXPPacket.py
  int16_t  v_eps_s; // Renamed from voltage_eps_s to match LXPPacket.py
  int16_t  v_eps_t; // Renamed from voltage_eps_t to match LXPPacket.py
  int16_t  f_eps; // Renamed from frequency_eps to match LXPPacket.py
  int16_t  p_to_eps; // Renamed from active_eps_power to match LXPPacket.py
  int16_t  apparent_eps_power; // No LXPPacket.py key provided, kept as-is
  int16_t  p_to_grid; // Matches LXPPacket.py
  int16_t  p_to_user; // Renamed from power_from_grid to match LXPPacket.py
  int16_t  e_pv_1_day; // Renamed from pv1_energy_today to match LXPPacket.py
  int16_t  e_pv_2_day; // Renamed from pv2_energy_today to match LXPPacket.py
  int16_t  e_pv_3_day; // Renamed from pv3_energy_today to match LXPPacket.py
  int16_t  e_inv_day; // Renamed from activeInverter_energy_today to match LXPPacket.py
  int16_t  e_rec_day; // Renamed from ac_charging_today to match LXPPacket.py
  int16_t  e_chg_day; // Renamed from charging_today to match LXPPacket.py
  int16_t  e_dischg_day; // Renamed from discharging_today to match LXPPacket.py
  int16_t  e_eps_day; // Renamed from eps_today to match LXPPacket.py
  int16_t  e_to_grid_day; // Renamed from exported_today to match LXPPacket.py
  int16_t  e_to_user_day; // Renamed from grid_today to match LXPPacket.py
  int16_t  v_bus_1; // Renamed from bus1_voltage to match LXPPacket.py
  int16_t  v_bus_2; // Renamed from bus2_voltage to match LXPPacket.py
} __attribute__((packed));

struct LogDataRawSection2 { // Registers 40-79
  int32_t  e_pv_1_all; // Matches LXPPacket.py
  int32_t  e_pv_2_all; // Matches LXPPacket.py
  int32_t  e_pv_3_all; // Matches LXPPacket.py
  int32_t  e_inv_all; // Matches LXPPacket.py
  int32_t  e_rec_all; // Matches LXPPacket.py
  int32_t  e_chg_all; // Matches LXPPacket.py
  int32_t  e_dischg_all; // Matches LXPPacket.py
  int32_t  e_eps_all; // Matches LXPPacket.py
  int32_t  e_to_grid_all; // Matches LXPPacket.py
  int32_t  e_to_user_all; // Matches LXPPacket.py
  uint32_t fault_code; // Matches LXPPacket.py
  uint32_t warning_code; // Matches LXPPacket.py
  int16_t  t_inner; // Matches LXPPacket.py
  int16_t  t_rad_1; // Matches LXPPacket.py
  int16_t  t_rad_2; // Matches LXPPacket.py
  int16_t  t_bat; // Matches LXPPacket.py
  uint16_t _reserved2;
  uint32_t uptime; // Matches LXPPacket.py
} __attribute__((packed));

struct LogDataRawSection3 { // Registers 80-119
  uint16_t _reserved3;
  int16_t  max_chg_curr; // Matches LXPPacket.py
  int16_t  max_dischg_curr; // Matches LXPPacket.py
  int16_t  charge_volt_ref; // Matches LXPPacket.py
  int16_t  dischg_cut_volt; // Matches LXPPacket.py
  uint8_t  placeholder[20];
  int16_t  bat_status_inv; // Matches LXPPacket.py
  int16_t  bat_count; // Matches LXPPacket.py
  int16_t  bat_capacity; // Matches LXPPacket.py
  int16_t  bat_current; // Matches LXPPacket.py
  int16_t  reg99;
  int16_t  reg100;
  int16_t  max_cell_volt; // Matches LXPPacket.py
  int16_t  min_cell_volt; // Matches LXPPacket.py
  int16_t  max_cell_temp; // Matches LXPPacket.py
  int16_t  min_cell_temp; // Matches LXPPacket.py
  uint16_t _reserved4;
  int16_t  bat_cycle_count; // Matches LXPPacket.py
  uint8_t  _reserved5[14];
  int16_t  p_load2; // Matches LXPPacket.py
  uint8_t  _reserved6[10];
} __attribute__((packed));

struct LogDataRawSection4 {  // Registers 120-159
  uint16_t reg120;
  int16_t  gen_input_volt; // Matches LXPPacket.py
  int16_t  gen_input_freq; // Matches LXPPacket.py
  int16_t  gen_power_watt; // Matches LXPPacket.py
  int16_t  gen_power_day; // Matches LXPPacket.py
  int16_t  gen_power_all; // Matches LXPPacket.py
  uint16_t reg126;
  int16_t  eps_L1_volt; // Matches LXPPacket.py
  int16_t  eps_L2_volt; // Matches LXPPacket.py
  int16_t  eps_L1_watt; // Matches LXPPacket.py
  int16_t  eps_L2_watt; // Matches LXPPacket.py
  uint8_t  placeholder[50];
} __attribute__((packed));

struct LogDataRawSection5 {  // Registers 160-199
  uint8_t  _reserved7[20];
  int16_t  p_load_ongrid; // Matches LXPPacket.py
  int16_t  e_load_day; // Matches LXPPacket.py
  int16_t  e_load_all_l; // Matches LXPPacket.py
  uint8_t  _reserved8[54];
} __attribute__((packed));

// --- SCALED Data Structs ---
struct Section1 {
  bool loaded = false;

  // Raw values (after direct mapping from Modbus)
  uint16_t lux_status; // Renamed from 'status' to 'lux_status' for full match
  float lux_current_solar_voltage_1; // Renamed from 'v_pv_1' to 'lux_current_solar_voltage_1' for full match
  float lux_current_solar_voltage_2; // Renamed from 'v_pv_2' to 'lux_current_solar_voltage_2' for full match
  float lux_current_solar_voltage_3; // Renamed from 'v_pv_3' to 'lux_current_solar_voltage_3' for full match
  float lux_battery_voltage; // Renamed from 'v_bat' to 'lux_battery_voltage' for full match
  uint8_t lux_battery_percent; // Renamed from 'soc' to 'lux_battery_percent' for full match
  uint8_t soh; // No direct 'lux_' key, kept as-is
  uint16_t lux_internal_fault; // Renamed from 'internal_fault' to 'lux_internal_fault' for full match
  int16_t lux_current_solar_output_1; // Renamed from 'p_pv_1' to 'lux_current_solar_output_1' for full match
  int16_t lux_current_solar_output_2; // Renamed from 'p_pv_2' to 'lux_current_solar_output_2' for full match
  int16_t lux_current_solar_output_3; // Renamed from 'p_pv_3' to 'lux_current_solar_output_3' for full match
  int16_t lux_battery_charge; // Renamed from 'p_charge' to 'lux_battery_charge' for full match
  int16_t lux_battery_discharge; // Renamed from 'p_discharge' to 'lux_battery_discharge' for full match
  float grid_voltage_r, grid_voltage_s, grid_voltage_t; // No direct 'lux_' keys, kept as-is
  float lux_grid_frequency_live; // Renamed from 'f_ac' to 'lux_grid_frequency_live' for full match
  float lux_grid_voltage_live; // Renamed from 'grid_voltage_avg' to 'lux_grid_voltage_live' for full match
  int16_t lux_power_from_inverter_live; // Renamed from 'p_inv' to 'lux_power_from_inverter_live' for full match
  int16_t lux_power_to_inverter_live; // Renamed from 'p_rec' to 'lux_power_to_inverter_live' for full match
  float lux_power_current_clamp; // Renamed from 'rms_current' to 'lux_power_current_clamp' for full match
  float grid_power_factor; // Renamed from 'pf' to 'grid_power_factor', kept as-is
  float eps_voltage_r, eps_voltage_s, eps_voltage_t; // Renamed from 'v_eps_r', etc., kept as-is
  float eps_frequency; // Renamed from 'f_eps', kept as-is
  int16_t lux_power_to_eps; // Renamed from 'p_to_eps' to 'lux_power_to_eps' for full match
  int16_t apparent_eps_power; // No direct 'lux_' key, kept as-is
  int16_t lux_power_to_grid_live; // Renamed from 'p_to_grid' to 'lux_power_to_grid_live' for full match
  int16_t lux_power_from_grid_live; // Renamed from 'p_to_user' to 'lux_power_from_grid_live' for full match
  float lux_daily_solar_array_1; // Renamed from 'e_pv_1_day' to 'lux_daily_solar_array_1' for full match
  float lux_daily_solar_array_2; // Renamed from 'e_pv_2_day' to 'lux_daily_solar_array_2' for full match
  float lux_daily_solar_array_3; // Renamed from 'e_pv_3_day' to 'lux_daily_solar_array_3' for full match
  float lux_power_from_inverter_daily; // Renamed from 'e_inv_day' to 'lux_power_from_inverter_daily' for full match
  float lux_power_to_inverter_daily; // Renamed from 'e_rec_day' to 'lux_power_to_inverter_daily' for full match
  float lux_daily_battery_charge; // Renamed from 'e_chg_day' to 'lux_daily_battery_charge' for full match
  float lux_daily_battery_discharge; // Renamed from 'e_dischg_day' to 'lux_daily_battery_discharge' for full match
  float lux_power_to_eps_daily; // Renamed from 'e_eps_day' to 'lux_power_to_eps_daily' for full match
  float lux_power_to_grid_daily; // Renamed from 'e_to_grid_day' to 'lux_power_to_grid_daily' for full match
  float lux_power_from_grid_daily; // Renamed from 'e_to_user_day' to 'lux_power_from_grid_daily' for full match
  float bus1_voltage, bus2_voltage; // Renamed from 'v_bus_1', 'v_bus_2', kept as-is

  // Calculated fields
  int16_t lux_current_solar_output; // Renamed from 'p_pv_total' to 'lux_current_solar_output' for full match
  float lux_daily_solar; // Renamed from 'e_pv_total' to 'lux_daily_solar' for full match
  int16_t lux_power_to_home; // Renamed from 'p_load' to 'lux_power_to_home' for full match
  float lux_battery_flow; // Renamed from 'battery_flow' to 'lux_battery_flow' for full match
  float lux_grid_flow; // Renamed from 'grid_flow' to 'lux_grid_flow' for full match
  float lux_home_consumption_live; // Renamed from 'home_consumption_live' to 'lux_home_consumption_live' for full match
  float lux_home_consumption; // Renamed from 'home_consumption_daily' to 'lux_home_consumption' for full match
};

struct Section2 {
  bool loaded = false;

  // Raw values
  float lux_total_solar_array_1; // Renamed from 'e_pv_1_all' to 'lux_total_solar_array_1' for full match
  float lux_total_solar_array_2; // Renamed from 'e_pv_2_all' to 'lux_total_solar_array_2' for full match
  float lux_total_solar_array_3; // Renamed from 'e_pv_3_all' to 'lux_total_solar_array_3' for full match
  float lux_power_from_inverter_total; // Renamed from 'e_inv_all' to 'lux_power_from_inverter_total' for full match
  float lux_power_to_inverter_total; // Renamed from 'e_rec_all' to 'lux_power_to_inverter_total' for full match
  float lux_total_battery_charge; // Renamed from 'e_chg_all' to 'lux_total_battery_charge' for full match
  float lux_total_battery_discharge; // Renamed from 'e_dischg_all' to 'lux_total_battery_discharge' for full match
  float lux_power_to_eps_total; // Renamed from 'e_eps_all' to 'lux_power_to_eps_total' for full match
  float lux_power_to_grid_total; // Renamed from 'e_to_grid_all' to 'lux_power_to_grid_total' for full match
  float lux_power_from_grid_total; // Renamed from 'e_to_user_all' to 'lux_power_from_grid_total' for full match
  uint32_t lux_fault_code; // Renamed from 'fault_code' to 'lux_fault_code' for full match
  uint32_t lux_warning_code; // Renamed from 'warning_code' to 'lux_warning_code' for full match
  int16_t lux_internal_temp; // Renamed from 't_inner' to 'lux_internal_temp' for full match
  int16_t lux_radiator1_temp; // Renamed from 't_rad_1' to 'lux_radiator1_temp' for full match
  int16_t lux_radiator2_temp; // Renamed from 't_rad_2' to 'lux_radiator2_temp' for full match
  int16_t lux_battery_temperature_live; // Renamed from 't_bat' to 'lux_battery_temperature_live' for full match
  uint32_t lux_uptime; // Renamed from 'uptime' to 'lux_uptime' for full match

  // Calculated fields
  float lux_total_solar; // Renamed from 'e_pv_all' to 'lux_total_solar' for full match
  float lux_home_consumption_total; // Renamed from 'home_consumption_total' to 'lux_home_consumption_total' for full match
};

struct Section3 {
  bool loaded = false;

  // Raw values
  float lux_bms_limit_charge; // Renamed from 'max_chg_curr' to 'lux_bms_limit_charge' for full match
  float lux_bms_limit_discharge; // Renamed from 'max_dischg_curr' to 'lux_bms_limit_discharge' for full match
  float charge_voltage_ref; // Renamed from 'charge_volt_ref', kept as-is
  float discharge_cutoff_voltage; // Renamed from 'dischg_cut_volt', kept as-is
  int16_t battery_status_inv; // Renamed from 'bat_status_inv', kept as-is, used for lux_battery_status_text
  int16_t lux_battery_count; // Renamed from 'bat_count' to 'lux_battery_count' for full match
  int16_t lux_battery_capacity_ah; // Renamed from 'bat_capacity' to 'lux_battery_capacity_ah' for full match
  float lux_battery_current; // Renamed from 'bat_current' to 'lux_battery_current' for full match
  float max_cell_volt; // Renamed from 'max_cell_volt', kept as-is
  float min_cell_volt; // Renamed from 'min_cell_volt', kept as-is
  float max_cell_temp; // Renamed from 'max_cell_temp', kept as-is
  float min_cell_temp; // Renamed from 'min_cell_temp', kept as-is
  int16_t lux_battery_cycle_count; // Renamed from 'bat_cycle_count' to 'lux_battery_cycle_count' for full match

  // Calculated field
  int16_t lux_home_consumption_2_live; // Renamed from 'p_load2' to 'lux_home_consumption_2_live' for full match
  float lux_home_consumption_2_live_alias; // Renamed from 'home_consumption2' to 'lux_home_consumption_2_live_alias' (alias) for full match
};

struct Section4 {
  bool loaded = false;
  // Raw values
  float lux_current_generator_voltage; // Renamed from 'gen_input_volt' to 'lux_current_generator_voltage' for full match
  float lux_current_generator_frequency; // Renamed from 'gen_input_freq' to 'lux_current_generator_frequency' for full match
  int16_t lux_current_generator_power; // Renamed from 'gen_power_watt' to 'lux_current_generator_power' for full match
  float lux_current_generator_power_daily; // Renamed from 'gen_power_day' to 'lux_current_generator_power_daily' for full match
  float lux_current_generator_power_all; // Renamed from 'gen_power_all' to 'lux_current_generator_power_all' for full match
  float lux_current_eps_L1_voltage; // Renamed from 'eps_L1_volt' to 'lux_current_eps_L1_voltage' for full match
  float lux_current_eps_L2_voltage; // Renamed from 'eps_L2_volt' to 'lux_current_eps_L2_voltage' for full match
  int16_t lux_current_eps_L1_watt; // Renamed from 'eps_L1_watt' to 'lux_current_eps_L1_watt' for full match
  int16_t lux_current_eps_L2_watt; // Renamed from 'eps_L2_watt' to 'lux_current_eps_L2_watt' for full match
};

struct Section5 {
  bool loaded = false;
  // Raw values
  int16_t p_load_ongrid; // Matches LXPPacket.py
  float e_load_day; // Matches LXPPacket.py
  float e_load_all_l; // Matches LXPPacket.py
};

// New section for non-bank-specific data
struct SystemData {
  // Text-based fields
  String lux_firmware_version; // Renamed from 'firmware_version' to 'lux_firmware_version' for full match
  String lux_inverter_model; // Renamed from 'inverter_model' to 'lux_inverter_model' for full match
  String lux_status_text; // Renamed from 'status_text' to 'lux_status_text' for full match
  String lux_battery_status_text; // Renamed from 'battery_status_text' to 'lux_battery_status_text' for full match

  // Timestamps
  unsigned long lux_data_last_received_time; // Renamed from 'last_data_received' to 'lux_data_last_received_time' for full match
  unsigned long last_heartbeat; // No direct 'lux_' key, kept as-is

  // Model-based scaling factor
  float current_scaling_factor; // No direct 'lux_' key, kept as-is
};

class LuxData {
public:
  bool decode(const uint8_t *buffer, uint16_t length);
  void scaleSection1();
  void scaleSection2();
  void scaleSection3();
  void scaleSection4();
  void scaleSection5();

  // Text conversion helpers
  void generateStatusText();
  void generateBatteryStatusText();

  // Model detection
  void detectInverterModel();

  Header header;
  TranslatedData trans;
  LogDataRawSection1 raw;
  LogDataRawSection2 raw2;
  LogDataRawSection3 raw3;
  LogDataRawSection4 raw4;
  LogDataRawSection5 raw5;

  Section1 section1;
  Section2 section2;
  Section3 section3;
  Section4 section4;
  Section5 section5;
  SystemData system;

  String serialString; // No direct CSV key, kept as-is
  String getState() {
    return (millis() - system.last_heartbeat) < 15000 ? "ONLINE" : "OFFLINE";
  }
  void reset() {
    section1.loaded = false;
    section2.loaded = false;
    section3.loaded = false;
    section4.loaded = false;
    section5.loaded = false;
    system.current_scaling_factor = 10.0f; // Default scaling
  }

private:
  // Model detection helper
  String decodeModelCode(uint16_t reg7, uint16_t reg8); // No direct CSV key, kept as-is
};
