#include "LuxParser.h"

bool LuxData::decode(const uint8_t *buffer, uint16_t length) {
  reset();  // Reset loaded flags before decoding new data

  if (length < 8) {
    Serial.println("❌ Packet too small");
    return false;
  }

  // Check for heartbeat first
  if (buffer[7] == 0xC1) {
    Serial.println("💓 Heartbeat received");
    return true;
  }

  const uint16_t RESPONSE_HEADER_SIZE = sizeof(Header) + sizeof(TranslatedData);
  if (length < RESPONSE_HEADER_SIZE) {
    Serial.println("❌ Packet too small for headers");
    return false;
  }

  memcpy(&header, buffer, sizeof(Header));
  memcpy(&trans, buffer + sizeof(Header), sizeof(TranslatedData));

  Serial.printf("→ Decoding: Inverter=%s, RegStart=%d\n", String(trans.serialNumber).c_str(), trans.registerStart);

  if (header.prefix != 0x1AA1 || header.function != 0xC2 || trans.deviceFunction != 0x04) {
    Serial.println("❌ Invalid header/function");
    return false;
  }

  uint16_t data_offset = RESPONSE_HEADER_SIZE;
  uint16_t data_payload_length = length - data_offset - 2;

  if (trans.registerStart == 0 && data_payload_length >= sizeof(LogDataRawSection1)) {
    memcpy(&raw, buffer + data_offset, sizeof(LogDataRawSection1));
    section1.loaded = true;
    scaleSection1();
  } else if (trans.registerStart == 40 && data_payload_length >= sizeof(LogDataRawSection2)) {
    memcpy(&raw2, buffer + data_offset, sizeof(LogDataRawSection2));
    section2.loaded = true;
    scaleSection2();
  } else if (trans.registerStart == 80 && data_payload_length >= sizeof(LogDataRawSection3)) {
    memcpy(&raw3, buffer + data_offset, sizeof(LogDataRawSection3));
    section3.loaded = true;
    scaleSection3();
  } else if (trans.registerStart == 120 && data_payload_length >= sizeof(LogDataRawSection4)) {
    memcpy(&raw4, buffer + data_offset, sizeof(LogDataRawSection4));
    section4.loaded = true;
    scaleSection4();
  } else if (trans.registerStart == 160 && data_payload_length >= sizeof(LogDataRawSection5)) {
    memcpy(&raw5, buffer + data_offset, sizeof(LogDataRawSection5));
    section5.loaded = true;
    scaleSection5();
  } else {
    Serial.printf("⚠️ Unrecognized register %d or insufficient data length %d\n", trans.registerStart, data_payload_length);
    return false;
  }

  serialString = String(trans.serialNumber);
  return true;
}

void LuxData::scaleSection1() {
  section1.pv1_voltage              = raw.pv1_voltage / 10.0f;
  section1.pv2_voltage              = raw.pv2_voltage / 10.0f;
  section1.pv3_voltage              = raw.pv3_voltage / 10.0f;
  section1.battery_voltage          = raw.battery_voltage / 10.0f;
  section1.soc                      = raw.soc;
  section1.soh                      = raw.soh;
  section1.internal_fault           = raw.internal_fault;
  section1.pv1_power                = raw.pv1_power;
  section1.pv2_power                = raw.pv2_power;
  section1.pv3_power                = raw.pv3_power;
  section1.charge_power             = raw.charge_power;
  section1.discharge_power          = raw.discharge_power;
  section1.grid_voltage             = raw.voltage_ac_r / 10.0f;
  section1.grid_voltage_s           = raw.voltage_ac_s / 10.0f;
  section1.grid_voltage_t           = raw.voltage_ac_t / 10.0f;
  section1.frequency_grid           = raw.frequency_grid / 100.0f;
  section1.inverter_power           = raw.activeInverter_power;
  section1.activeInverter_power     = raw.activeInverter_power;
  section1.activeCharge_power       = raw.activeCharge_power;
  section1.ct_clamp_live            = raw.ct_clamp_live / 100.0f;
  section1.grid_power_factor        = raw.grid_power_factor / 1000.0f;
  
  section1.eps_voltage_r            = raw.voltage_eps_r / 10.0f;
  section1.eps_voltage_s            = raw.voltage_eps_s / 10.0f;
  section1.eps_voltage_t            = raw.voltage_eps_t / 10.0f;
  section1.eps_frequency            = raw.frequency_eps / 100.0f;
  section1.eps_active_power         = raw.active_eps_power;
  section1.eps_apparent_power       = raw.apparent_eps_power;
  
  section1.power_to_grid            = raw.power_to_grid;
  section1.power_from_grid          = raw.power_from_grid;
  
  section1.pv1_energy_today         = raw.pv1_energy_today / 10.0f;
  section1.pv2_energy_today         = raw.pv2_energy_today / 10.0f;
  section1.pv3_energy_today         = raw.pv3_energy_today / 10.0f;
  section1.activeInverter_energy_today = raw.activeInverter_energy_today / 10.0f;
  section1.ac_charging_today        = raw.ac_charging_today / 10.0f;
  section1.charging_today           = raw.charging_today / 10.0f;
  section1.discharging_today        = raw.discharging_today / 10.0f;
  section1.eps_today                = raw.eps_today / 10.0f;
  section1.exported_today           = raw.exported_today / 10.0f;
  section1.grid_today               = raw.grid_today / 10.0f;
  section1.bus1_voltage             = raw.bus1_voltage / 10.0f;
  section1.bus2_voltage             = raw.bus2_voltage / 10.0f;
  
  // Calculated fields
  section1.battery_flow = (section1.discharge_power > 0) ? 
      -section1.discharge_power : section1.charge_power;
      
  section1.grid_flow = (section1.power_from_grid > 0) ? 
      -section1.power_from_grid : section1.power_to_grid;
      
  section1.home_consumption_live = section1.power_from_grid - 
      section1.activeCharge_power + section1.activeInverter_power - 
      section1.power_to_grid;
      
  section1.home_consumption_daily = section1.grid_today - 
      section1.ac_charging_today + section1.activeInverter_energy_today - 
      section1.exported_today;
}

void LuxData::scaleSection2() {
  section2.total_pv1_energy        = raw2.e_pv_1_all / 10.0f;
  section2.total_pv2_energy        = raw2.e_pv_2_all / 10.0f;
  section2.total_pv3_energy        = raw2.e_pv_3_all / 10.0f;
  section2.total_inverter_output   = raw2.e_inv_all / 10.0f;
  section2.total_recharge_energy   = raw2.e_rec_all / 10.0f;
  section2.total_charged           = raw2.e_chg_all / 10.0f;
  section2.total_discharged        = raw2.e_dischg_all / 10.0f;
  section2.total_eps_energy        = raw2.e_eps_all / 10.0f;
  section2.total_exported          = raw2.e_to_grid_all / 10.0f;
  section2.total_imported          = raw2.e_to_user_all / 10.0f;
  
  section2.fault_code              = raw2.fault_code;
  section2.warning_code            = raw2.warning_code;
  
  section2.temp_inner              = raw2.t_inner;
  section2.temp_radiator           = raw2.t_rad_1;
  section2.temp_radiator2          = raw2.t_rad_2;
  section2.temp_battery            = raw2.t_bat;
  
  section2.uptime_seconds          = raw2.uptime;
  
  // Calculated field
  section2.home_consumption_total = section2.total_imported - 
      section2.total_recharge_energy + section2.total_inverter_output - 
      section2.total_exported;
}

void LuxData::scaleSection3() {
  section3.max_charge_current      = raw3.max_chg_curr / 10.0f;
  section3.max_discharge_current   = raw3.max_dischg_curr / 10.0f;
  section3.charge_voltage_ref      = raw3.charge_volt_ref / 10.0f;
  section3.discharge_cutoff_voltage= raw3.dischg_cut_volt / 10.0f;
  
  section3.battery_status_inv      = raw3.bat_status_inv;
  section3.battery_count           = raw3.bat_count;
  section3.battery_capacity        = raw3.bat_capacity;
  
  // Handle signed battery current
  section3.battery_current         = raw3.bat_current / 10.0f;
  
  section3.max_cell_voltage        = raw3.max_cell_volt / 1000.0f;
  section3.min_cell_voltage        = raw3.min_cell_volt / 1000.0f;
  section3.max_cell_temp           = raw3.max_cell_temp / 10.0f;
  section3.min_cell_temp           = raw3.min_cell_temp / 10.0f;
  
  section3.cycle_count             = raw3.bat_cycle_count;
  section3.p_load2                 = raw3.p_load2;
  
  // Calculated field
  section3.home_consumption2       = raw3.p_load2;
}

void LuxData::scaleSection4() {
  section4.gen_input_volt = raw4.gen_input_volt / 10.0f;
  section4.gen_input_freq = raw4.gen_input_freq / 100.0f;
  
  // Apply threshold from Python implementation
  section4.gen_power_watt = (raw4.gen_power_watt < 125) ? 
      0 : raw4.gen_power_watt;
  
  section4.gen_power_day = raw4.gen_power_day / 10.0f;
  section4.gen_power_all = raw4.gen_power_all / 10.0f;
  
  section4.eps_L1_volt = raw4.eps_L1_volt / 10.0f;
  section4.eps_L2_volt = raw4.eps_L2_volt / 10.0f;
  section4.eps_L1_watt = raw4.eps_L1_watt;
  section4.eps_L2_watt = raw4.eps_L2_watt;
}

void LuxData::scaleSection5() {
  section5.p_load_ongrid = raw5.p_load_ongrid;
  section5.e_load_day = raw5.e_load_day / 10.0f;
  section5.e_load_all_l = raw5.e_load_all_l / 10.0f;
}
