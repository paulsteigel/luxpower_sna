// luxpower_sna.cpp
#include "luxpower_sna.h"

namespace esphome {
namespace luxpower_sna {

const char *LuxpowerSNAComponent::STATUS_TEXTS[193] = {
  "Standby", "Error", "Inverting", "", "Solar > Load - Surplus > Grid", "Float", "", "Charger Off", "Supporting", "Selling", "Pass Through", "Offsetting", "Solar > Battery Charging", "", "", "",
  "Battery Discharging > LOAD - Surplus > Grid", "Temperature Over Range", "", "", "Solar + Battery Discharging > LOAD - Surplus > Grid", "", "", "", "", "", "", "", "AC Battery Charging", "", "", "", "", "", "Solar + Grid > Battery Charging",
  "", "", "", "", "", "", "", "", "", "No Grid : Battery > EPS", "", "", "", "", "", "", "", "", "No Grid : Solar > EPS - Surplus > Battery Charging", "", "", "", "", "No Grid : Solar + Battery Discharging > EPS"
};
const char *LuxpowerSNAComponent::BATTERY_STATUS_TEXTS[17] = {
  "Charge Forbidden & Discharge Forbidden", "Unknown", "Charge Forbidden & Discharge Allowed", "Charge Allowed & Discharge Allowed",
  "", "", "", "", "", "", "", "", "", "", "", "", "Charge Allowed & Discharge Forbidden"
};

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Luxpower SNA component...");
}

void LuxpowerSNAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Luxpower SNA:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%u", host_.c_str(), port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", dongle_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter Serial: %s", inverter_serial_.c_str());
}

void LuxpowerSNAComponent::update() {
  ESP_LOGD(TAG, "Starting new update cycle.");

  if (!client_.connect(host_.c_str(), port_)) {
    ESP_LOGW(TAG, "Connection to %s:%u failed. Skipping update.", host_.c_str(), port_);
    return;
  }

  ESP_LOGD(TAG, "Connection successful. Polling all banks.");
  bool all_banks_successful = true;

  for (uint8_t bank : banks_) {
    request_bank_(bank);
    
    if (!receive_response_(bank)) {
      ESP_LOGW(TAG, "Failed to process bank %d. Aborting rest of update cycle.", bank);
      all_banks_successful = false;
      break; 
    }
    
    ESP_LOGD(TAG, "Successfully processed bank %d.", bank);
    delay(200); 
  }

  if (all_banks_successful) {
    ESP_LOGI(TAG, "Update cycle completed successfully.");
  }

  client_.stop();
  ESP_LOGD(TAG, "Connection closed.");
}

void LuxpowerSNAComponent::request_bank_(uint8_t bank) {
  uint8_t pkt[38] = {
    0xA1, 0x1A, 0x02, 0x00, 0x20, 0x00, 0x01, 0xC2,
    0,0,0,0,0,0,0,0,0,0, 0x12, 0x00, 0x00, 0x04,
    0,0,0,0,0,0,0,0,0,0, static_cast<uint8_t>(bank), 0x00, 0x28, 0x00, 0x00, 0x00
  };
  memcpy(pkt + 8, dongle_serial_.c_str(), 10);
  memcpy(pkt + 22, inverter_serial_.c_str(), 10);
  uint16_t crc = calculate_crc_(pkt + 20, 16);
  pkt[36] = crc & 0xFF;
  pkt[37] = crc >> 8;

  ESP_LOGV(TAG, "Sending request for bank %d", bank);
  client_.write(pkt, sizeof(pkt));
}

bool LuxpowerSNAComponent::receive_response_(uint8_t bank) {
  uint8_t buffer[512];
  uint32_t start = millis();
  size_t total_read = 0;
  
  while (millis() - start < 3000) { 
    if (client_.available()) {
      int bytes_read = client_.read(buffer + total_read, sizeof(buffer) - total_read);
      if (bytes_read > 0) { total_read += bytes_read; }
    }
    if (total_read >= sizeof(LuxHeader) + sizeof(LuxTranslatedData) + 2) { break; }
    delay(10);
  }
  
  if (total_read == 0) {
    ESP_LOGW(TAG, "No data received for bank %d (timeout)", bank);
    return false;
  }

  LuxHeader *header = reinterpret_cast<LuxHeader *>(buffer);
  if (header->prefix != 0x1AA1) {
    ESP_LOGE(TAG, "Invalid header prefix: 0x%04X", header->prefix);
    return false;
  }

  LuxTranslatedData *trans = reinterpret_cast<LuxTranslatedData *>(buffer + sizeof(LuxHeader));
  if (trans->deviceFunction != 0x04) {
    ESP_LOGE(TAG, "Invalid device function: 0x%02X", trans->deviceFunction);
    return false;
  }

  uint16_t crc_calc = calculate_crc_(buffer + sizeof(LuxHeader), total_read - sizeof(LuxHeader) - 2);
  uint16_t crc_received = buffer[total_read - 2] | (buffer[total_read - 1] << 8);
  if (crc_calc != crc_received) {
    ESP_LOGE(TAG, "CRC mismatch: calc=0x%04X, recv=0x%04X", crc_calc, crc_received);
    return false;
  }

  size_t data_offset = sizeof(LuxHeader) + sizeof(LuxTranslatedData);
  size_t data_size = total_read - data_offset - 2;
  
  switch (bank) {
    case 0: if (data_size >= sizeof(LuxLogDataRawSection1)) process_section1_(*reinterpret_cast<LuxLogDataRawSection1 *>(buffer + data_offset)); break;
    case 40: if (data_size >= sizeof(LuxLogDataRawSection2)) process_section2_(*reinterpret_cast<LuxLogDataRawSection2 *>(buffer + data_offset)); break;
    case 80: if (data_size >= sizeof(LuxLogDataRawSection3)) process_section3_(*reinterpret_cast<LuxLogDataRawSection3 *>(buffer + data_offset)); break;
    case 120: if (data_size >= sizeof(LuxLogDataRawSection4)) process_section4_(*reinterpret_cast<LuxLogDataRawSection4 *>(buffer + data_offset)); break;
    case 160: if (data_size >= sizeof(LuxLogDataRawSection5)) process_section5_(*reinterpret_cast<LuxLogDataRawSection5 *>(buffer + data_offset)); break;
    default: ESP_LOGW(TAG, "Unknown bank: %d", bank);
  }
  return true;
}

uint16_t LuxpowerSNAComponent::calculate_crc_(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; ++i)
      crc = (crc >> 1) ^ (crc & 1 ? 0xA001 : 0);
  }
  return crc;
}

void LuxpowerSNAComponent::publish_sensor_(sensor::Sensor *sensor, float value) {
  if (sensor != nullptr) sensor->publish_state(value);
}

void LuxpowerSNAComponent::publish_text_sensor_(text_sensor::TextSensor *sensor, const std::string &value) {
  if (sensor != nullptr) sensor->publish_state(value);
}

void LuxpowerSNAComponent::process_section1_(const LuxLogDataRawSection1 &data) {
  publish_sensor_(lux_current_solar_voltage_1_sensor_, data.v_pv_1 / 10.0f);
  publish_sensor_(lux_current_solar_voltage_2_sensor_, data.v_pv_2 / 10.0f);
  publish_sensor_(lux_current_solar_voltage_3_sensor_, data.v_pv_3 / 10.0f);
  publish_sensor_(lux_battery_voltage_sensor_, data.v_bat / 10.0f);
  publish_sensor_(lux_battery_percent_sensor_, data.soc);
  publish_sensor_(soh_sensor_, data.soh);
  publish_sensor_(lux_internal_fault_sensor_, data.internal_fault);
  publish_sensor_(lux_current_solar_output_1_sensor_, data.p_pv_1);
  publish_sensor_(lux_current_solar_output_2_sensor_, data.p_pv_2);
  publish_sensor_(lux_current_solar_output_3_sensor_, data.p_pv_3);
  publish_sensor_(lux_battery_charge_sensor_, data.p_charge);
  publish_sensor_(lux_battery_discharge_sensor_, data.p_discharge);
  publish_sensor_(lux_grid_voltage_r_sensor_, data.v_ac_r / 10.0f);
  publish_sensor_(lux_grid_voltage_s_sensor_, data.v_ac_s / 10.0f);
  publish_sensor_(lux_grid_voltage_t_sensor_, data.v_ac_t / 10.0f);
  publish_sensor_(lux_grid_frequency_live_sensor_, data.f_ac / 100.0f);
  publish_sensor_(lux_power_from_inverter_live_sensor_, data.p_inv);
  publish_sensor_(lux_power_to_inverter_live_sensor_, data.p_rec);
  publish_sensor_(lux_power_current_clamp_sensor_, data.rms_current / 100.0f);
  publish_sensor_(grid_power_factor_sensor_, data.pf / 1000.0f);
  publish_sensor_(eps_voltage_r_sensor_, data.v_eps_r / 10.0f);
  publish_sensor_(eps_voltage_s_sensor_, data.v_eps_s / 10.0f);
  publish_sensor_(eps_voltage_t_sensor_, data.v_eps_t / 10.0f);
  publish_sensor_(eps_frequency_sensor_, data.f_eps / 100.0f);
  publish_sensor_(lux_power_to_eps_sensor_, data.p_to_eps);
  publish_sensor_(lux_power_to_grid_live_sensor_, data.p_to_grid);
  publish_sensor_(lux_power_from_grid_live_sensor_, data.p_to_user);
  publish_sensor_(lux_daily_solar_array_1_sensor_, data.e_pv_1_day / 10.0f);
  publish_sensor_(lux_daily_solar_array_2_sensor_, data.e_pv_2_day / 10.0f);
  publish_sensor_(lux_daily_solar_array_3_sensor_, data.e_pv_3_day / 10.0f);
  publish_sensor_(lux_power_from_inverter_daily_sensor_, data.e_inv_day / 10.0f);
  publish_sensor_(lux_power_to_inverter_daily_sensor_, data.e_rec_day / 10.0f);
  publish_sensor_(lux_daily_battery_charge_sensor_, data.e_chg_day / 10.0f);
  publish_sensor_(lux_daily_battery_discharge_sensor_, data.e_dischg_day / 10.0f);
  publish_sensor_(lux_power_to_eps_daily_sensor_, data.e_eps_day / 10.0f);
  publish_sensor_(lux_power_to_grid_daily_sensor_, data.e_to_grid_day / 10.0f);
  publish_sensor_(lux_power_from_grid_daily_sensor_, data.e_to_user_day / 10.0f);
  publish_sensor_(bus1_voltage_sensor_, data.v_bus_1 / 10.0f);
  publish_sensor_(bus2_voltage_sensor_, data.v_bus_2 / 10.0f);
    
  float lux_grid_voltage_live = data.v_ac_r;
  int16_t lux_current_solar_output = data.p_pv_1 + data.p_pv_2 + data.p_pv_3;
  float lux_daily_solar = (data.e_pv_1_day + data.e_pv_2_day + data.e_pv_3_day) / 10.0f;
  int16_t lux_power_to_home = data.p_to_user - data.p_rec;
  float lux_battery_flow = (data.p_discharge > 0) ? -data.p_discharge : data.p_charge;
  float lux_grid_flow = (data.p_to_user > 0) ? -data.p_to_user : data.p_to_grid;
  float lux_home_consumption_live = data.p_to_user - data.p_rec + data.p_inv - data.p_to_grid;
  float lux_home_consumption = (data.e_to_user_day - data.e_rec_day + data.e_inv_day - data.e_to_grid_day) / 10.0f;

  publish_sensor_(lux_grid_voltage_live_sensor_, lux_grid_voltage_live);
  publish_sensor_(lux_current_solar_output_sensor_, lux_current_solar_output);
  publish_sensor_(lux_daily_solar_sensor_, lux_daily_solar);
  publish_sensor_(lux_power_to_home_sensor_, lux_power_to_home);
  publish_sensor_(lux_battery_flow_sensor_, lux_battery_flow);
  publish_sensor_(lux_grid_flow_sensor_, lux_grid_flow);
  publish_sensor_(lux_home_consumption_live_sensor_, lux_home_consumption_live);
  publish_sensor_(lux_home_consumption_sensor_, lux_home_consumption);

  if (data.status < 193 && STATUS_TEXTS[data.status] != nullptr && strlen(STATUS_TEXTS[data.status]) > 0) {
    publish_text_sensor_(lux_status_text_sensor_, STATUS_TEXTS[data.status]);
  } else {
    publish_text_sensor_(lux_status_text_sensor_, "Unknown Status");
  }
}

void LuxpowerSNAComponent::process_section2_(const LuxLogDataRawSection2 &data) {
  publish_sensor_(lux_total_solar_array_1_sensor_, data.e_pv_1_all / 10.0f);
  publish_sensor_(lux_total_solar_array_2_sensor_, data.e_pv_2_all / 10.0f);
  publish_sensor_(lux_total_solar_array_3_sensor_, data.e_pv_3_all / 10.0f);
  publish_sensor_(lux_power_from_inverter_total_sensor_, data.e_inv_all / 10.0f);
  publish_sensor_(lux_power_to_inverter_total_sensor_, data.e_rec_all / 10.0f);
  publish_sensor_(lux_total_battery_charge_sensor_, data.e_chg_all / 10.0f);
  publish_sensor_(lux_total_battery_discharge_sensor_, data.e_dischg_all / 10.0f);
  publish_sensor_(lux_power_to_eps_total_sensor_, data.e_eps_all / 10.0f);
  publish_sensor_(lux_power_to_grid_total_sensor_, data.e_to_grid_all / 10.0f);
  publish_sensor_(lux_power_from_grid_total_sensor_, data.e_to_user_all / 10.0f);
  publish_sensor_(lux_fault_code_sensor_, data.fault_code);
  publish_sensor_(lux_warning_code_sensor_, data.warning_code);
  publish_sensor_(lux_internal_temp_sensor_, data.t_inner / 10.0f);
  publish_sensor_(lux_radiator1_temp_sensor_, data.t_rad_1);
  publish_sensor_(lux_radiator2_temp_sensor_, data.t_rad_2);
  publish_sensor_(lux_battery_temperature_live_sensor_, data.t_bat / 10.0f);
  publish_sensor_(lux_uptime_sensor_, data.uptime);

  float lux_total_solar = (data.e_pv_1_all + data.e_pv_2_all + data.e_pv_3_all) / 10.0f;
  float lux_home_consumption_total = (data.e_to_user_all - data.e_rec_all + data.e_inv_all - data.e_to_grid_all) / 10.0f;
  
  publish_sensor_(lux_total_solar_sensor_, lux_total_solar);
  publish_sensor_(lux_home_consumption_total_sensor_, lux_home_consumption_total);
}

void LuxpowerSNAComponent::process_section3_(const LuxLogDataRawSection3 &data) {
  int16_t raw_current = data.bat_current;
  if (raw_current & 0x8000) raw_current -= 0x10000;
  
  publish_sensor_(lux_bms_limit_charge_sensor_, data.max_chg_curr / 10.0f);
  publish_sensor_(lux_bms_limit_discharge_sensor_, data.max_dischg_curr / 10.0f);
  publish_sensor_(charge_voltage_ref_sensor_, data.charge_volt_ref / 10.0f);
  publish_sensor_(discharge_cutoff_voltage_sensor_, data.dischg_cut_volt / 10.0f);
  publish_sensor_(battery_status_inv_sensor_, data.bat_status_inv);
  publish_sensor_(lux_battery_count_sensor_, data.bat_count);
  publish_sensor_(lux_battery_capacity_ah_sensor_, data.bat_capacity);
  publish_sensor_(lux_battery_current_sensor_, raw_current / 10.0f);
  publish_sensor_(max_cell_volt_sensor_, data.max_cell_volt / 1000.0f);
  publish_sensor_(min_cell_volt_sensor_, data.min_cell_volt / 1000.0f);
  
  int16_t raw_max_temp = data.max_cell_temp;
  int16_t raw_min_temp = data.min_cell_temp;
  if (raw_max_temp & 0x8000) raw_max_temp -= 0x10000;
  if (raw_min_temp & 0x8000) raw_min_temp -= 0x10000;
  
  publish_sensor_(max_cell_temp_sensor_, raw_max_temp / 10.0f);
  publish_sensor_(min_cell_temp_sensor_, raw_min_temp / 10.0f);
  
  publish_sensor_(lux_battery_cycle_count_sensor_, data.bat_cycle_count);
  publish_sensor_(lux_home_consumption_2_live_sensor_, data.p_load2);
  
  if (data.bat_status_inv < 17 && BATTERY_STATUS_TEXTS[data.bat_status_inv] != nullptr && strlen(BATTERY_STATUS_TEXTS[data.bat_status_inv]) > 0) {
    publish_text_sensor_(lux_battery_status_text_sensor_, BATTERY_STATUS_TEXTS[data.bat_status_inv]);
  } else {
    publish_text_sensor_(lux_battery_status_text_sensor_, "Unknown Battery Status");
  }
}

void LuxpowerSNAComponent::process_section4_(const LuxLogDataRawSection4 &data) {
  publish_sensor_(lux_current_generator_voltage_sensor_, data.gen_input_volt / 10.0f);
  publish_sensor_(lux_current_generator_frequency_sensor_, data.gen_input_freq / 100.0f);
  
  int16_t gen_power = (data.gen_power_watt < 125) ? 0 : data.gen_power_watt;
  publish_sensor_(lux_current_generator_power_sensor_, gen_power);
  
  publish_sensor_(lux_current_generator_power_daily_sensor_, data.gen_power_day / 10.0f);
  publish_sensor_(lux_current_generator_power_all_sensor_, data.gen_power_all / 10.0f);
  publish_sensor_(lux_current_eps_L1_voltage_sensor_, data.eps_L1_volt / 10.0f);
  publish_sensor_(lux_current_eps_L2_voltage_sensor_, data.eps_L2_volt / 10.0f);
  publish_sensor_(lux_current_eps_L1_watt_sensor_, data.eps_L1_watt);
  publish_sensor_(lux_current_eps_L2_watt_sensor_, data.eps_L2_watt);
}

void LuxpowerSNAComponent::process_section5_(const LuxLogDataRawSection5 &data) {
  publish_sensor_(p_load_ongrid_sensor_, data.p_load_ongrid);
  publish_sensor_(e_load_day_sensor_, data.e_load_day / 10.0f);
  publish_sensor_(e_load_all_l_sensor_, data.e_load_all_l / 10.0f);
}

}  // namespace luxpower_sna
}  // namespace esphome
