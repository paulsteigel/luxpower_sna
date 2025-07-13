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
  uint16_t status;
  int16_t  pv1_voltage;
  int16_t  pv2_voltage;
  int16_t  pv3_voltage;
  int16_t  battery_voltage;
  uint8_t  soc;
  uint8_t  soh;
  uint16_t internal_fault;  // Changed from _reserved1
  int16_t  pv1_power;
  int16_t  pv2_power;
  int16_t  pv3_power;
  int16_t  charge_power;
  int16_t  discharge_power;
  int16_t  voltage_ac_r;
  int16_t  voltage_ac_s;
  int16_t  voltage_ac_t;
  int16_t  frequency_grid;
  int16_t  activeInverter_power;
  int16_t  activeCharge_power;
  int16_t  ct_clamp_live;  // RMS current
  int16_t  grid_power_factor;
  int16_t  voltage_eps_r;
  int16_t  voltage_eps_s;
  int16_t  voltage_eps_t;
  int16_t  frequency_eps;
  int16_t  active_eps_power;
  int16_t  apparent_eps_power;
  int16_t  power_to_grid;
  int16_t  power_from_grid;  // p_to_user
  int16_t  pv1_energy_today;
  int16_t  pv2_energy_today;
  int16_t  pv3_energy_today;
  int16_t  activeInverter_energy_today;
  int16_t  ac_charging_today;
  int16_t  charging_today;
  int16_t  discharging_today;
  int16_t  eps_today;
  int16_t  exported_today;
  int16_t  grid_today;  // e_to_user_day
  int16_t  bus1_voltage;
  int16_t  bus2_voltage;
} __attribute__((packed));

struct LogDataRawSection2 { // Registers 40-79
  int32_t  e_pv_1_all;
  int32_t  e_pv_2_all;
  int32_t  e_pv_3_all;
  int32_t  e_inv_all;
  int32_t  e_rec_all;
  int32_t  e_chg_all;
  int32_t  e_dischg_all;
  int32_t  e_eps_all;
  int32_t  e_to_grid_all;
  int32_t  e_to_user_all;
  uint32_t fault_code;
  uint32_t warning_code;
  int16_t  t_inner;
  int16_t  t_rad_1;
  int16_t  t_rad_2;
  int16_t  t_bat;
  uint16_t _reserved2;
  uint32_t uptime;
} __attribute__((packed));

struct LogDataRawSection3 { // Registers 80-119
  uint16_t _reserved3;         // reg80
  int16_t  max_chg_curr;       // reg81
  int16_t  max_dischg_curr;    // reg82
  int16_t  charge_volt_ref;    // reg83
  int16_t  dischg_cut_volt;    // reg84
  uint8_t  placeholder[20];    // reg85-94
  int16_t  bat_status_inv;     // reg95
  int16_t  bat_count;          // reg96
  int16_t  bat_capacity;       // reg97
  int16_t  bat_current;        // reg98 (signed)
  int16_t  reg99;              // reg99
  int16_t  reg100;             // reg100
  int16_t  max_cell_volt;      // reg101
  int16_t  min_cell_volt;      // reg102
  int16_t  max_cell_temp;      // reg103
  int16_t  min_cell_temp;      // reg104
  uint16_t _reserved4;         // reg105
  int16_t  bat_cycle_count;    // reg106
  uint8_t  _reserved5[14];     // reg107-113
  int16_t  p_load2;            // reg114
  uint8_t  _reserved6[10];     // reg115-119
} __attribute__((packed));

struct LogDataRawSection4 {  // Registers 120-159
  uint16_t reg120;           // Placeholder
  int16_t  gen_input_volt;   // reg121
  int16_t  gen_input_freq;   // reg122
  int16_t  gen_power_watt;   // reg123
  int16_t  gen_power_day;    // reg124
  int16_t  gen_power_all;    // reg125
  uint16_t reg126;           // Placeholder
  int16_t  eps_L1_volt;      // reg127
  int16_t  eps_L2_volt;      // reg128
  int16_t  eps_L1_watt;      // reg129
  int16_t  eps_L2_watt;      // reg130
  uint8_t  placeholder[50];  // Remaining registers
} __attribute__((packed));

struct LogDataRawSection5 {  // Registers 160-199
  uint8_t  _reserved7[20];   // 160-169
  int16_t  p_load_ongrid;    // reg170
  int16_t  e_load_day;       // reg171
  int16_t  e_load_all_l;     // reg172
  uint8_t  _reserved8[54];   // 173-199
} __attribute__((packed));

// --- SCALED Data Structs ---

struct Section1 {
  bool loaded = false;

  // Raw values
  float pv1_voltage, pv2_voltage, pv3_voltage;
  float battery_voltage;
  float grid_voltage, grid_voltage_s, grid_voltage_t;
  float frequency_grid;
  float eps_voltage_r, eps_voltage_s, eps_voltage_t;
  float eps_frequency;
  
  int16_t pv1_power, pv2_power, pv3_power;
  int16_t charge_power, discharge_power;
  int16_t inverter_power;
  int16_t activeInverter_power;
  int16_t activeCharge_power;
  float ct_clamp_live;
  float grid_power_factor;
  
  int16_t eps_active_power;
  int16_t eps_apparent_power;
  int16_t power_to_grid, power_from_grid;
  
  float pv1_energy_today, pv2_energy_today, pv3_energy_today;
  float activeInverter_energy_today;
  float ac_charging_today, charging_today, discharging_today;
  float eps_today, exported_today, grid_today;
  
  float bus1_voltage, bus2_voltage;
  
  uint8_t soc, soh;
  uint16_t internal_fault;
  
  // Calculated fields
  float battery_flow;
  float grid_flow;
  float home_consumption_live;
  float home_consumption_daily;
};

struct Section2 {
  bool loaded = false;
  
  float total_pv1_energy, total_pv2_energy, total_pv3_energy;
  float total_inverter_output;
  float total_recharge_energy;
  float total_charged, total_discharged;
  float total_eps_energy;
  float total_exported, total_imported;
  
  uint32_t fault_code;
  uint32_t warning_code;
  
  float temp_inner;
  float temp_radiator;
  float temp_radiator2;
  float temp_battery;
  
  uint32_t uptime_seconds;
  
  // Calculated fields
  float home_consumption_total;
};

struct Section3 {
  bool loaded = false;
  
  float max_charge_current;
  float max_discharge_current;
  float charge_voltage_ref;
  float discharge_cutoff_voltage;
  
  int16_t battery_status_inv;
  int16_t battery_count;
  int16_t battery_capacity;
  float battery_current;
  
  float max_cell_voltage;
  float min_cell_voltage;
  float max_cell_temp;
  float min_cell_temp;
  
  int16_t cycle_count;
  int16_t p_load2;
  
  // Calculated field
  float home_consumption2;
};

struct Section4 {
  bool loaded = false;
  float gen_input_volt;
  float gen_input_freq;
  int16_t gen_power_watt;
  float gen_power_day;
  float gen_power_all;
  float eps_L1_volt;
  float eps_L2_volt;
  int16_t eps_L1_watt;
  int16_t eps_L2_watt;
};

struct Section5 {
  bool loaded = false;
  int16_t p_load_ongrid;
  float e_load_day;
  float e_load_all_l;
};

class LuxData {
private:
  void publish_status_text_(const std::string &value);
  void publish_battery_status_text_(const std::string &value);
  std::map<std::string, text_sensor::TextSensor *> text_sensors_;
public:
  bool decode(const uint8_t *buffer, uint16_t length);
  void scaleSection1();
  void scaleSection2();
  void scaleSection3();
  void scaleSection4();
  void scaleSection5();
  
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

  String serialString;
  // for text sensors self prepared from raw data
  void set_status_text_sensor(text_sensor::TextSensor *s) { 
    this->text_sensors_["status_text"] = s; 
  }
  void set_battery_status_text_sensor(text_sensor::TextSensor *s) { 
      this->text_sensors_["battery_status_text"] = s; 
  }
  void set_grid_voltage_avg_sensor(sensor::Sensor *s) {
      this->float_sensors_["grid_voltage_avg"] = s;
  }

  void reset() {
    section1.loaded = false;
    section2.loaded = false;
    section3.loaded = false;
    section4.loaded = false;
    section5.loaded = false;
  }
};
