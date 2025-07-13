#include "LuxParser.h"

// Status text mapping (from Python implementation)
const char* STATUS_TEXTS[] = {
  "Standby",                     // 0
  "Error",                       // 1
  "Inverting",                   // 2
  "",                            // 3
  "Solar > Load - Surplus > Grid", // 4
  "Float",                       // 5
  "",                            // 6
  "Charger Off",                 // 7
  "Supporting",                  // 8
  "Selling",                     // 9
  "Pass Through",                // 10
  "Offsetting",                  // 11
  "Solar > Battery Charging",    // 12
  "", "", "",                    // 13-15
  "Battery Discharging > LOAD - Surplus > Grid", // 16
  "Temperature Over Range",      // 17
  "", "",                        // 18-19
  "Solar + Battery Discharging > LOAD - Surplus > Grid", // 20
  "", "", "", "", "", "", "",    // 21-27
  "AC Battery Charging",         // 32
  "", "", "", "",                // 33-36
  "Solar + Grid > Battery Charging", // 40
  "", "", "", "", "", "", "", "", // 41-48
  "No Grid : Battery > EPS",     // 64
  "", "", "", "", "", "", "", "", // 65-72
  "No Grid : Solar > EPS - Surplus > Battery Charging", // 136
  "", "", "", "",                // 137-140
  "No Grid : Solar + Battery Discharging > EPS" // 192
};

// Battery status text mapping (from Python)
const char* BATTERY_STATUS_TEXTS[] = {
  "Charge Forbidden & Discharge Forbidden",  // 0
  "Unknown",                                 // 1 (Changed from empty string)
  "Charge Forbidden & Discharge Allowed",    // 2
  "Charge Allowed & Discharge Allowed",      // 3
  "", "", "", "", "", "", "", "", "", "", "", "", "", // 4-15
  "Charge Allowed & Discharge Forbidden"     // 16 (Index 16)
};

String LuxData::decodeModelCode(uint16_t reg7, uint16_t reg8) {
  char model[5];
  model[0] = (reg7 >> 8) & 0xFF;
  model[1] = reg7 & 0xFF;
  model[2] = (reg8 >> 8) & 0xFF;
  model[3] = reg8 & 0xFF;
  model[4] = '\0';

  // Set scaling factor based on model
  if (strncmp(model, "FAAB", 4) == 0 ||
      strncmp(model, "EAAB", 4) == 0 ||
      strncmp(model, "ACAB", 4) == 0 ||
      strncmp(model, "CFAA", 4) == 0 ||
      strncmp(model, "CCAA", 4) == 0) {
    system.current_scaling_factor = 10.0f;
  } else {
    system.current_scaling_factor = 100.0f;
  }

  return String(model);
}

bool LuxData::decode(const uint8_t *buffer, uint16_t length) {
  reset();

  // Minimum packet check
  if (length < 8) {
    return false;
  }

  // Handle heartbeat packets
  if (buffer[7] == 0xC1) {
    system.last_heartbeat = millis();
    return true;
  }

  const uint16_t RESPONSE_HEADER_SIZE = sizeof(Header) + sizeof(TranslatedData);
  if (length < RESPONSE_HEADER_SIZE) {
    return false;
  }

  // Copy headers
  memcpy(&header, buffer, sizeof(Header));
  memcpy(&trans, buffer + sizeof(Header), sizeof(TranslatedData));

  uint16_t data_offset = RESPONSE_HEADER_SIZE;
  uint16_t data_payload_length = length - data_offset - 2;

  // Handle different register banks
  if (trans.registerStart == 0 && data_payload_length >= sizeof(LogDataRawSection1)) {
    memcpy(&raw, buffer + data_offset, sizeof(LogDataRawSection1));
    section1.loaded = true;
    scaleSection1();
  }
  else if (trans.registerStart == 40 && data_payload_length >= sizeof(LogDataRawSection2)) {
    memcpy(&raw2, buffer + data_offset, sizeof(LogDataRawSection2));
    section2.loaded = true;
    scaleSection2();
  }
  else if (trans.registerStart == 80 && data_payload_length >= sizeof(LogDataRawSection3)) {
    memcpy(&raw3, buffer + data_offset, sizeof(LogDataRawSection3));
    section3.loaded = true;
    scaleSection3();

    // Try to detect model if we have register values
    if (section3.loaded) {
      detectInverterModel();
    }
  }
  else if (trans.registerStart == 120 && data_payload_length >= sizeof(LogDataRawSection4)) {
    memcpy(&raw4, buffer + data_offset, sizeof(LogDataRawSection4));
    section4.loaded = true;
    scaleSection4();
  }
  else if (trans.registerStart == 160 && data_payload_length >= sizeof(LogDataRawSection5)) {
    memcpy(&raw5, buffer + data_offset, sizeof(LogDataRawSection5));
    section5.loaded = true;
    scaleSection5();
  }
  else {
    return false;
  }

  serialString = String(trans.serialNumber);
  // Renamed from 'system.last_data_received' to 'system.lux_data_last_received_time' for full match
  system.lux_data_last_received_time = millis();
  return true;
}

void LuxData::detectInverterModel() {
  // Only detect model once
  // Renamed from 'system.inverter_model' to 'system.lux_inverter_model' for full match
  if (system.lux_inverter_model.length() > 0) return;

  // +++ SENSOR 3 IMPROVEMENT: PLACEHOLDER FOR ACTUAL MODEL DETECTION +++
  // Actual implementation requires reading holding registers 7-8
  // Currently not available in this packet structure
  // Renamed from 'system.inverter_model' to 'system.lux_inverter_model' for full match
  system.lux_inverter_model = "UNKNOWN";
}

void LuxData::scaleSection1() {
  // Basic scaling
  section1.lux_status = raw.status; // Matches LXPPacket.py raw name
  section1.lux_current_solar_voltage_1 = raw.v_pv_1 / 10.0f; // Renamed from pv1_voltage
  section1.lux_current_solar_voltage_2 = raw.v_pv_2 / 10.0f; // Renamed from pv2_voltage
  section1.lux_current_solar_voltage_3 = raw.v_pv_3 / 10.0f; // Renamed from pv3_voltage
  section1.lux_battery_voltage = raw.v_bat / 10.0f; // Renamed from battery_voltage
  section1.lux_battery_percent = raw.soc; // Matches LXPPacket.py raw name
  section1.soh = raw.soh;
  section1.lux_internal_fault = raw.internal_fault; // Matches LXPPacket.py raw name
  section1.lux_current_solar_output_1 = raw.p_pv_1; // Renamed from pv1_power
  section1.lux_current_solar_output_2 = raw.p_pv_2; // Renamed from pv2_power
  section1.lux_current_solar_output_3 = raw.p_pv_3; // Renamed from pv3_power
  section1.lux_battery_charge = raw.p_charge; // Renamed from charge_power
  section1.lux_battery_discharge = raw.p_discharge; // Renamed from discharge_power
  section1.grid_voltage_r = raw.v_ac_r / 10.0f; // Renamed from voltage_ac_r
  section1.grid_voltage_s = raw.v_ac_s / 10.0f; // Renamed from voltage_ac_s
  section1.grid_voltage_t = raw.v_ac_t / 10.0f; // Renamed from voltage_ac_t
  section1.lux_grid_frequency_live = raw.f_ac / 100.0f; // Renamed from frequency_grid

  // +++ SENSOR 28: ADDED GRID VOLTAGE AVERAGE CALCULATION +++
  section1.lux_grid_voltage_live = (section1.grid_voltage_r +
                              section1.grid_voltage_s +
                              section1.grid_voltage_t) / 3.0f;

  section1.lux_power_from_inverter_live = raw.p_inv; // Renamed from activeInverter_power
  section1.lux_power_to_inverter_live = raw.p_rec; // Renamed from activeCharge_power
  section1.lux_power_current_clamp = raw.rms_current / 100.0f; // Renamed from ct_clamp_live
  section1.grid_power_factor = raw.pf / 1000.0f; // Renamed from grid_power_factor
  section1.eps_voltage_r = raw.v_eps_r / 10.0f; // Renamed from voltage_eps_r
  section1.eps_voltage_s = raw.v_eps_s / 10.0f; // Renamed from voltage_eps_s
  section1.eps_voltage_t = raw.v_eps_t / 10.0f; // Renamed from voltage_eps_t
  section1.eps_frequency = raw.f_eps / 100.0f; // Renamed from frequency_eps
  section1.lux_power_to_eps = raw.p_to_eps; // Renamed from active_eps_power
  section1.apparent_eps_power = raw.apparent_eps_power;
  int16_t lux_power_to_grid_live = raw.p_to_grid; // Matches LXPPacket.py raw name
  section1.lux_power_from_grid_live = raw.p_to_user; // Renamed from power_from_grid
  section1.lux_daily_solar_array_1 = raw.e_pv_1_day / 10.0f; // Renamed from pv1_energy_today
  section1.lux_daily_solar_array_2 = raw.e_pv_2_day / 10.0f; // Renamed from pv2_energy_today
  section1.lux_daily_solar_array_3 = raw.e_pv_3_day / 10.0f; // Renamed from pv3_energy_today
  section1.lux_power_from_inverter_daily = raw.e_inv_day / 10.0f; // Renamed from activeInverter_energy_today
  section1.lux_power_to_inverter_daily = raw.e_rec_day / 10.0f; // Renamed from ac_charging_today
  section1.lux_daily_battery_charge = raw.e_chg_day / 10.0f; // Renamed from charging_today
  section1.lux_daily_battery_discharge = raw.e_dischg_day / 10.0f; // Renamed from discharging_today
  section1.lux_power_to_eps_daily = raw.e_eps_day / 10.0f; // Renamed from eps_today
  section1.lux_power_to_grid_daily = raw.e_to_grid_day / 10.0f; // Renamed from exported_today
  section1.lux_power_from_grid_daily = raw.e_to_user_day / 10.0f; // Renamed from grid_today
  float bus1_voltage = raw.v_bus_1 / 10.0f; // Renamed from bus1_voltage
  float bus2_voltage = raw.v_bus_2 / 10.0f; // Renamed from bus2_voltage

  // Calculated fields
  section1.lux_current_solar_output = raw.p_pv_1 + raw.p_pv_2 + raw.p_pv_3; // Uses LXPPacket.py raw names
  section1.lux_daily_solar = section1.lux_daily_solar_array_1 +
                             section1.lux_daily_solar_array_2 +
                             section1.lux_daily_solar_array_3;
  section1.lux_power_to_home = raw.p_to_user - raw.p_rec; // Uses LXPPacket.py raw names
  section1.lux_battery_flow = (raw.p_discharge > 0) ?
      -static_cast<float>(raw.p_discharge) : static_cast<float>(raw.p_charge); // Uses LXPPacket.py raw names
  section1.lux_grid_flow = (raw.p_to_user > 0) ?
      -static_cast<float>(raw.p_to_user) : static_cast<float>(lux_power_to_grid_live); // Uses LXPPacket.py raw names
  section1.lux_home_consumption_live =
      static_cast<float>(raw.p_to_user) -
      static_cast<float>(raw.p_rec) +
      static_cast<float>(raw.p_inv) -
      static_cast<float>(lux_power_to_grid_live); // Uses LXPPacket.py raw names
  section1.lux_home_consumption =
      section1.lux_power_from_grid_daily -
      section1.lux_power_to_inverter_daily +
      section1.lux_power_from_inverter_daily -
      section1.lux_power_to_grid_daily;

  // Generate status text
  generateStatusText();
}

void LuxData::scaleSection2() {
  section2.lux_total_solar_array_1 = raw2.e_pv_1_all / 10.0f; // Matches LXPPacket.py raw name
  section2.lux_total_solar_array_2 = raw2.e_pv_2_all / 10.0f; // Matches LXPPacket.py raw name
  section2.lux_total_solar_array_3 = raw2.e_pv_3_all / 10.0f; // Matches LXPPacket.py raw name
  section2.lux_power_from_inverter_total = raw2.e_inv_all / 10.0f; // Matches LXPPacket.py raw name
  section2.lux_power_to_inverter_total = raw2.e_rec_all / 10.0f; // Matches LXPPacket.py raw name
  section2.lux_total_battery_charge = raw2.e_chg_all / 10.0f; // Matches LXPPacket.py raw name
  section2.lux_total_battery_discharge = raw2.e_dischg_all / 10.0f; // Matches LXPPacket.py raw name
  section2.lux_power_to_eps_total = raw2.e_eps_all / 10.0f; // Matches LXPPacket.py raw name
  section2.lux_power_to_grid_total = raw2.e_to_grid_all / 10.0f; // Matches LXPPacket.py raw name
  section2.lux_power_from_grid_total = raw2.e_to_user_all / 10.0f; // Matches LXPPacket.py raw name
  section2.lux_fault_code = raw2.fault_code; // Matches LXPPacket.py raw name
  section2.lux_warning_code = raw2.warning_code; // Matches LXPPacket.py raw name
  section2.lux_internal_temp = raw2.t_inner; // Matches LXPPacket.py raw name
  section2.lux_radiator1_temp = raw2.t_rad_1; // Matches LXPPacket.py raw name
  section2.lux_radiator2_temp = raw2.t_rad_2; // Matches LXPPacket.py raw name
  section2.lux_battery_temperature_live = raw2.t_bat; // Matches LXPPacket.py raw name
  section2.lux_uptime = raw2.uptime; // Matches LXPPacket.py raw name

  // Calculated fields
  section2.lux_total_solar = section2.lux_total_solar_array_1 +
                            section2.lux_total_solar_array_2 +
                            section2.lux_total_solar_array_3;
  section2.lux_home_consumption_total =
      section2.lux_power_from_grid_total -
      section2.lux_power_to_inverter_total +
      section2.lux_power_from_inverter_total -
      section2.lux_power_to_grid_total;
}

void LuxData::scaleSection3() {
  // Use model-based scaling if available, otherwise default to 10
  float current_scale = system.current_scaling_factor;

  section3.lux_bms_limit_charge = raw3.max_chg_curr / current_scale; // Matches LXPPacket.py raw name
  section3.lux_bms_limit_discharge = raw3.max_dischg_curr / current_scale; // Matches LXPPacket.py raw name
  section3.charge_voltage_ref = raw3.charge_volt_ref / 10.0f; // Matches LXPPacket.py raw name
  section3.discharge_cutoff_voltage = raw3.dischg_cut_volt / 10.0f; // Matches LXPPacket.py raw name
  section3.battery_status_inv = raw3.bat_status_inv; // Matches LXPPacket.py raw name
  section3.lux_battery_count = raw3.bat_count; // Matches LXPPacket.py raw name
  section3.lux_battery_capacity_ah = raw3.bat_capacity; // Matches LXPPacket.py raw name

  // Handle signed battery current
  int16_t raw_current = raw3.bat_current;
  if (raw_current & 0x8000) {
    raw_current = raw_current - 0x10000;
  }
  section3.lux_battery_current = raw_current / 10.0f; // Matches LXPPacket.py raw name

  section3.max_cell_volt = raw3.max_cell_volt / 1000.0f; // Matches LXPPacket.py raw name
  section3.min_cell_volt = raw3.min_cell_volt / 1000.0f; // Matches LXPPacket.py raw name

  // Handle signed temperatures
  int16_t raw_max_temp = raw3.max_cell_temp;
  int16_t raw_min_temp = raw3.min_cell_temp;

  if (raw_max_temp & 0x8000) raw_max_temp -= 0x10000;
  if (raw_min_temp & 0x8000) raw_min_temp -= 0x10000;

  section3.max_cell_temp = raw_max_temp / 10.0f; // Matches LXPPacket.py raw name
  section3.min_cell_temp = raw_min_temp / 10.0f; // Matches LXPPacket.py raw name

  section3.lux_battery_cycle_count = raw3.bat_cycle_count; // Matches LXPPacket.py raw name

  // Calculated fields
  section3.lux_home_consumption_2_live = raw3.p_load2; // Matches LXPPacket.py raw name
  section3.lux_home_consumption_2_live_alias = static_cast<float>(raw3.p_load2); // Uses LXPPacket.py raw name

  // Generate battery status text
  generateBatteryStatusText();
}

void LuxData::scaleSection4() {
  section4.lux_current_generator_voltage = raw4.gen_input_volt / 10.0f; // Matches LXPPacket.py raw name
  section4.lux_current_generator_frequency = raw4.gen_input_freq / 100.0f; // Matches LXPPacket.py raw name

  // Apply threshold from Python implementation
  section4.lux_current_generator_power = (raw4.gen_power_watt < 125) ? 0 : raw4.gen_power_watt; // Matches LXPPacket.py raw name

  section4.lux_current_generator_power_daily = raw4.gen_power_day / 10.0f; // Matches LXPPacket.py raw name
  section4.lux_current_generator_power_all = raw4.gen_power_all / 10.0f; // Matches LXPPacket.py raw name
  section4.lux_current_eps_L1_voltage = raw4.eps_L1_volt / 10.0f; // Matches LXPPacket.py raw name
  section4.lux_current_eps_L2_voltage = raw4.eps_L2_volt / 10.0f; // Matches LXPPacket.py raw name
  section4.lux_current_eps_L1_watt = raw4.eps_L1_watt; // Matches LXPPacket.py raw name
  section4.lux_current_eps_L2_watt = raw4.eps_L2_watt; // Matches LXPPacket.py raw name
}

void LuxData::scaleSection5() {
  section5.p_load_ongrid = raw5.p_load_ongrid; // Matches LXPPacket.py raw name
  section5.e_load_day = raw5.e_load_day / 10.0f; // Matches LXPPacket.py raw name
  section5.e_load_all_l = raw5.e_load_all_l / 10.0f; // Matches LXPPacket.py raw name
}

void LuxData::generateStatusText() {
  if (section1.lux_status <= 192) {
    switch(section1.lux_status) {
      case 0: system.lux_status_text = STATUS_TEXTS[0]; break;
      case 1: system.lux_status_text = STATUS_TEXTS[1]; break;
      case 2: system.lux_status_text = STATUS_TEXTS[2]; break;
      case 4: system.lux_status_text = STATUS_TEXTS[4]; break;
      case 5: system.lux_status_text = STATUS_TEXTS[5]; break;
      case 7: system.lux_status_text = STATUS_TEXTS[7]; break;
      case 8: system.lux_status_text = STATUS_TEXTS[8]; break;
      case 9: system.lux_status_text = STATUS_TEXTS[9]; break;
      case 10: system.lux_status_text = STATUS_TEXTS[10]; break;
      case 11: system.lux_status_text = STATUS_TEXTS[11]; break;
      case 12: system.lux_status_text = STATUS_TEXTS[12]; break;
      case 16: system.lux_status_text = STATUS_TEXTS[16]; break;
      case 17: system.lux_status_text = STATUS_TEXTS[17]; break;
      case 20: system.lux_status_text = STATUS_TEXTS[20]; break;
      case 32: system.lux_status_text = STATUS_TEXTS[32]; break;
      case 40: system.lux_status_text = STATUS_TEXTS[40]; break;
      case 64: system.lux_status_text = STATUS_TEXTS[64]; break;
      case 136: system.lux_status_text = STATUS_TEXTS[136]; break;
      case 192: system.lux_status_text = STATUS_TEXTS[192]; break;
      default: system.lux_status_text = "Unknown (" + String(section1.lux_status) + ")";
    }
  } else {
    system.lux_status_text = "Invalid Status";
  }
}

void LuxData::generateBatteryStatusText() {
  // +++ SENSOR 72: FIXED BATTERY STATUS MAPPING +++
  switch (section3.battery_status_inv) {
    case 0:  system.lux_battery_status_text = BATTERY_STATUS_TEXTS[0]; break;
    case 2:  system.lux_battery_status_text = BATTERY_STATUS_TEXTS[2]; break;
    case 3:  system.lux_battery_status_text = BATTERY_STATUS_TEXTS[3]; break;
    case 17: system.lux_battery_status_text = BATTERY_STATUS_TEXTS[16]; break;
    default:
      system.lux_battery_status_text = "Unknown (" +
                                  String(section3.battery_status_inv) + ")";
  }
}
