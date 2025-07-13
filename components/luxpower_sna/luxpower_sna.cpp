#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include <cstring>
#include <queue>
#include <utility>
#define PUBLISH_DELAY_MS 50

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";
// revised 13/07
const char* LuxpowerSNAComponent::STATUS_TEXTS[193] = {
  "Standby", "Error", "Inverting", "", "Solar > Load - Surplus > Grid", 
  "Float", "", "Charger Off", "Supporting", "Selling", "Pass Through", 
  "Offsetting", "Solar > Battery Charging", "", "", "",
  "Battery Discharging > LOAD - Surplus > Grid", "Temperature Over Range", "", "",
  "Solar + Battery Discharging > LOAD - Surplus > Grid", "", "", "", "", "", "", "",
  "AC Battery Charging", "", "", "", "", "", "Solar + Grid > Battery Charging",
  "", "", "", "", "", "", "", "", "", "No Grid : Battery > EPS", "", "", "", "", 
  "", "", "", "", "No Grid : Solar > EPS - Surplus > Battery Charging", "", "", 
  "", "", "No Grid : Solar + Battery Discharging > EPS"
};

// Battery status text mapping
const char* LuxpowerSNAComponent::BATTERY_STATUS_TEXTS[17] = {
  "Charge Forbidden & Discharge Forbidden", "Unknown", 
  "Charge Forbidden & Discharge Allowed", "Charge Allowed & Discharge Allowed",
  "", "", "", "", "", "", "", "", "", "", "", "", 
  "Charge Allowed & Discharge Forbidden"
};

void LuxpowerSNAComponent::log_hex_buffer(const char* title, const uint8_t *buffer, size_t len) {
  if (len == 0) return;
  char str[len * 3 + 1];
  for (size_t i = 0; i < len; i++) {
    sprintf(str + i * 3, "%02X ", buffer[i]);
  }
  str[len * 3 - 1] = '\0';
  ESP_LOGI(TAG, "%s (%d bytes): %s", title, len, str);
}

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxpowerSNA...");
  this->tcp_client_ = new AsyncClient();
  this->tcp_client_->setRxTimeout(15000);

  this->tcp_client_->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
    this->log_hex_buffer("<- Received", static_cast<uint8_t *>(data), len);
    this->handle_response_(static_cast<uint8_t *>(data), len);
    client->close();
  });

  this->tcp_client_->onConnect([this](void *arg, AsyncClient *client) {
    ESP_LOGI(TAG, "Connected. Requesting bank %d", this->banks_[this->next_bank_to_request_]);
    this->request_bank_(this->banks_[this->next_bank_to_request_]);
  });

  this->tcp_client_->onError([this](void *arg, AsyncClient *client, int8_t error) {
    ESP_LOGW(TAG, "Connection error: %s", client->errorToString(error));
    client->close();
  });

  this->tcp_client_->onDisconnect([this](void *arg, AsyncClient *client) { 
    ESP_LOGI(TAG, "Disconnected"); 
  });
  
  this->tcp_client_->onTimeout([this](void *arg, AsyncClient *client, uint32_t time) {
    ESP_LOGW(TAG, "Timeout after %d ms", time);
    client->close();
  });
}

void LuxpowerSNAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Luxpower SNA:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%u", this->host_.c_str(), this->port_);
  ESP_LOGCONFIG(TAG, "  Dongle: %s", this->dongle_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter: %s", this->inverter_serial_.c_str());
}

void LuxpowerSNAComponent::update() {
  if (this->tcp_client_->connected()) {
    ESP_LOGI(TAG, "Connection in progress, skipping update");
    return;
  }
  ESP_LOGI(TAG, "Connecting to %s:%u...", this->host_.c_str(), this->port_);
  this->tcp_client_->connect(this->host_.c_str(), this->port_);
}

void LuxpowerSNAComponent::request_bank_(uint8_t bank) {
  uint8_t pkt[38] = {
    0xA1, 0x1A,       // Prefix
    0x02, 0x00,       // Protocol version 2
    0x20, 0x00,       // Frame length (32)
    0x01,             // Address
    0xC2,             // Function (TRANSLATED_DATA)
    // Dongle serial (10 bytes) - filled below
    0,0,0,0,0,0,0,0,0,0,
    0x12, 0x00,       // Data length (18)
    // Data frame starts here
    0x00,             // Address action
    0x04,             // Device function (READ_INPUT)
    // Inverter serial (10 bytes) - filled below
    0,0,0,0,0,0,0,0,0,0,
    // Register and value
    static_cast<uint8_t>(bank), 0x00, // Register (low, high)
    0x28, 0x00        // Value (40 registers)
  };

  // Copy serial numbers
  memcpy(pkt + 8, this->dongle_serial_.c_str(), 10);
  memcpy(pkt + 22, this->inverter_serial_.c_str(), 10);

  // Calculate CRC for data frame portion only (16 bytes)
  uint16_t crc = calculate_crc_(pkt + 20, 16);
  pkt[36] = crc & 0xFF;
  pkt[37] = crc >> 8;

  log_hex_buffer("-> Request", pkt, sizeof(pkt));

  if (this->tcp_client_->space() > sizeof(pkt)) {
    this->tcp_client_->add(reinterpret_cast<const char *>(pkt), sizeof(pkt));
    this->tcp_client_->send();
  } else {
    ESP_LOGW(TAG, "TCP buffer full");
    this->tcp_client_->close();
  }
}

void LuxpowerSNAComponent::handle_response_(const uint8_t *buffer, size_t length) {
  const uint16_t RESPONSE_HEADER_SIZE = sizeof(LuxHeader) + sizeof(LuxTranslatedData);
  if (length < RESPONSE_HEADER_SIZE) {
    ESP_LOGW(TAG, "Response too small: %d bytes", length);
    return;
  }

  LuxHeader header;
  LuxTranslatedData trans;
  memcpy(&header, buffer, sizeof(LuxHeader));
  memcpy(&trans, buffer + sizeof(LuxHeader), sizeof(LuxTranslatedData));

  if (header.prefix != 0x1AA1 || header.function != 0xC2 || trans.deviceFunction != 0x04) {
    ESP_LOGW(TAG, "Invalid header: prefix=0x%04X, func=0x%02X, devFunc=0x%02X", 
             header.prefix, header.function, trans.deviceFunction);
    return;
  }
  
  ESP_LOGI(TAG, "Processing bank %d", trans.registerStart);

  static bool serial_published = false;
  if (!serial_published) {
    std::string inv_serial(trans.serialNumber, 10);
    publish_state_("inverter_serial", inv_serial);
    serial_published = true;
  }

  const uint8_t *data_ptr = buffer + RESPONSE_HEADER_SIZE;
  size_t data_payload_length = length - RESPONSE_HEADER_SIZE - 2;

  if (trans.registerStart == 0 && data_payload_length >= sizeof(LuxLogDataRawSection1)) {
    LuxLogDataRawSection1 raw;
    memcpy(&raw, data_ptr, sizeof(LuxLogDataRawSection1));
    
    // Section 1: Bank 0
    publish_state_("lux_current_solar_voltage_1", raw.v_pv_1 / 10.0f);
    publish_state_("lux_current_solar_voltage_2", raw.v_pv_2 / 10.0f);
    publish_state_("lux_current_solar_voltage_3", raw.v_pv_3 / 10.0f);
    publish_state_("lux_battery_voltage", raw.v_bat / 10.0f);
    publish_state_("lux_battery_percent", (float)raw.soc);
    publish_state_("soh", (float)raw.soh);
    publish_state_("lux_current_solar_output_1", (float)raw.p_pv_1);
    publish_state_("lux_current_solar_output_2", (float)raw.p_pv_2);
    publish_state_("lux_current_solar_output_3", (float)raw.p_pv_3);
    publish_state_("lux_battery_charge", (float)raw.p_charge);
    publish_state_("lux_battery_discharge", (float)raw.p_discharge);
    publish_state_("lux_power_from_inverter_live", (float)raw.p_inv);
    publish_state_("lux_power_to_inverter_live", (float)raw.p_rec);
    publish_state_("lux_grid_voltage_r", raw.v_ac_r / 10.0f);
    publish_state_("lux_grid_voltage_s", raw.v_ac_s / 10.0f);
    publish_state_("lux_grid_voltage_t", raw.v_ac_t / 10.0f);
    publish_state_("lux_grid_frequency_live", raw.f_ac / 100.0f);
    publish_state_("lux_power_to_eps", (float)raw.p_to_eps);
    publish_state_("lux_power_to_grid_live", (float)raw.p_to_grid);
    publish_state_("lux_power_from_grid_live", (float)raw.p_to_user);
    publish_state_("lux_daily_solar_array_1", raw.e_pv_1_day / 10.0f);
    publish_state_("lux_daily_solar_array_2", raw.e_pv_2_day / 10.0f);
    publish_state_("lux_daily_solar_array_3", raw.e_pv_3_day / 10.0f);
    publish_state_("lux_power_from_inverter_daily", raw.e_inv_day / 10.0f);
    publish_state_("lux_power_to_inverter_daily", raw.e_rec_day / 10.0f);
    publish_state_("lux_daily_battery_charge", raw.e_chg_day / 10.0f);
    publish_state_("lux_daily_battery_discharge", raw.e_dischg_day / 10.0f);
    publish_state_("lux_power_to_eps_daily", raw.e_eps_day / 10.0f);
    publish_state_("lux_power_to_grid_daily", raw.e_to_grid_day / 10.0f);
    publish_state_("lux_power_from_grid_daily", raw.e_to_user_day / 10.0f);
    publish_state_("lux_internal_fault", (float)raw.internal_fault);
    publish_state_("lux_power_current_clamp", raw.rms_current / 100.0f);
    publish_state_("grid_power_factor", raw.pf / 1000.0f);
    
    // Calculated fields
    float lux_grid_voltage_live = (raw.v_ac_r + raw.v_ac_s + raw.v_ac_t) / 30.0f;
    int16_t lux_current_solar_output = raw.p_pv_1 + raw.p_pv_2 + raw.p_pv_3;
    float lux_daily_solar = (raw.e_pv_1_day + raw.e_pv_2_day + raw.e_pv_3_day) / 10.0f;
    int16_t lux_power_to_home = raw.p_to_user - raw.p_rec;
    float lux_battery_flow = (raw.p_discharge > 0) ? -raw.p_discharge : raw.p_charge;
    float lux_grid_flow = (raw.p_to_user > 0) ? -raw.p_to_user : raw.p_to_grid;
    float lux_home_consumption_live = raw.p_to_user - raw.p_rec + raw.p_inv - raw.p_to_grid;
    float lux_home_consumption = raw.e_to_user_day/10.0f - raw.e_rec_day/10.0f + 
                                 raw.e_inv_day/10.0f - raw.e_to_grid_day/10.0f;
    
    publish_state_("lux_grid_voltage_live", lux_grid_voltage_live);
    publish_state_("lux_current_solar_output", (float)lux_current_solar_output);
    publish_state_("lux_daily_solar", lux_daily_solar);
    publish_state_("lux_power_to_home", (float)lux_power_to_home);
    publish_state_("lux_battery_flow", lux_battery_flow);
    publish_state_("lux_grid_flow", lux_grid_flow);
    publish_state_("lux_home_consumption_live", lux_home_consumption_live);
    publish_state_("lux_home_consumption", lux_home_consumption);
    
    // Status text
    if (raw.status < sizeof(STATUS_TEXTS)/sizeof(STATUS_TEXTS[0])) {
      publish_state_("lux_status_text", std::string(STATUS_TEXTS[raw.status]));
    } else {
      publish_state_("lux_status_text", "Unknown Status");
    }

  } else if (trans.registerStart == 40 && data_payload_length >= sizeof(LuxLogDataRawSection2)) {
    LuxLogDataRawSection2 raw;
    memcpy(&raw, data_ptr, sizeof(LuxLogDataRawSection2));
    
    // Section 2: Bank 40
    publish_state_("total_pv1_energy", raw.e_pv_1_all / 10.0f);
    publish_state_("total_pv2_energy", raw.e_pv_2_all / 10.0f);
    publish_state_("total_pv3_energy", raw.e_pv_3_all / 10.0f);
    publish_state_("total_inverter_output", raw.e_inv_all / 10.0f);
    publish_state_("total_recharge_energy", raw.e_rec_all / 10.0f);
    publish_state_("total_charged", raw.e_chg_all / 10.0f);
    publish_state_("total_discharged", raw.e_dischg_all / 10.0f);
    publish_state_("total_eps_energy", raw.e_eps_all / 10.0f);
    publish_state_("total_exported", raw.e_to_grid_all / 10.0f);
    publish_state_("total_imported", raw.e_to_user_all / 10.0f);
    publish_state_("temp_inner", raw.t_inner);
    publish_state_("temp_radiator", raw.t_rad_1);
    publish_state_("temp_radiator2", raw.t_rad_2);
    publish_state_("temp_battery", raw.t_bat);
    publish_state_("uptime", (float)raw.uptime);
    
    // Home consumption total
    float home_consumption_total = (raw.e_to_user_all - raw.e_rec_all + raw.e_inv_all - raw.e_to_grid_all) / 10.0f;
    publish_state_("home_consumption_total", home_consumption_total);

  } else if (trans.registerStart == 80 && data_payload_length >= sizeof(LuxLogDataRawSection3)) {
    LuxLogDataRawSection3 raw;
    memcpy(&raw, data_ptr, sizeof(LuxLogDataRawSection3));

    // Battery current conversion
    int16_t raw_current = raw.bat_current;
    if (raw_current & 0x8000) raw_current -= 0x10000;
    
    publish_state_("lux_bms_limit_charge", raw.max_chg_curr / 10.0f);
    publish_state_("lux_bms_limit_discharge", raw.max_dischg_curr / 10.0f);
    publish_state_("lux_battery_count", (float)raw.bat_count);
    publish_state_("lux_battery_capacity_ah", (float)raw.bat_capacity);
    publish_state_("lux_battery_current", raw_current / 10.0f);
    publish_state_("max_cell_volt", raw.max_cell_volt / 1000.0f);
    publish_state_("min_cell_volt", raw.min_cell_volt / 1000.0f);
    publish_state_("max_cell_temp", raw.max_cell_temp / 10.0f);
    publish_state_("min_cell_temp", raw.min_cell_temp / 10.0f);
    publish_state_("lux_battery_cycle_count", (float)raw.bat_cycle_count);
    publish_state_("lux_home_consumption_2_live", (float)raw.p_load2);
    
    // Battery status text
    if (raw.bat_status_inv < sizeof(BATTERY_STATUS_TEXTS)/sizeof(BATTERY_STATUS_TEXTS[0])) {
      publish_state_("lux_battery_status_text", std::string(BATTERY_STATUS_TEXTS[raw.bat_status_inv]));
    } else {
      publish_state_("lux_battery_status_text", "Unknown Battery Status");
    }

  } else if (trans.registerStart == 120 && data_payload_length >= sizeof(LuxLogDataRawSection4)) {
    LuxLogDataRawSection4 raw;
    memcpy(&raw, data_ptr, sizeof(LuxLogDataRawSection4));
    
    // Section 4: Bank 120
    publish_state_("gen_input_volt", raw.gen_input_volt / 10.0f);
    publish_state_("gen_input_freq", raw.gen_input_freq / 100.0f);
    
    // Apply threshold from Python implementation
    int16_t gen_power = (raw.gen_power_watt < 125) ? 0 : raw.gen_power_watt;
    publish_state_("gen_power_watt", (float)gen_power);
    
    publish_state_("gen_power_day", raw.gen_power_day / 10.0f);
    publish_state_("gen_power_all", raw.gen_power_all / 10.0f);
    publish_state_("eps_L1_volt", raw.eps_L1_volt / 10.0f);
    publish_state_("eps_L2_volt", raw.eps_L2_volt / 10.0f);
    publish_state_("eps_L1_watt", (float)raw.eps_L1_watt);
    publish_state_("eps_L2_watt", (float)raw.eps_L2_watt);

  } else if (trans.registerStart == 160 && data_payload_length >= sizeof(LuxLogDataRawSection5)) {
    LuxLogDataRawSection5 raw;
    memcpy(&raw, data_ptr, sizeof(LuxLogDataRawSection5));
    
    // Section 5: Bank 160
    publish_state_("p_load_ongrid", (float)raw.p_load_ongrid);
    publish_state_("e_load_day", raw.e_load_day / 10.0f);
    publish_state_("e_load_all_l", raw.e_load_all_l / 10.0f);

  } else {
    ESP_LOGW(TAG, "Unrecognized bank %d or data too small (%d bytes)", 
             trans.registerStart, data_payload_length);
    return;
  }

  // Cycle through banks: 0 → 40 → 80 → 120 → 160 → 0
  next_bank_to_request_ = (next_bank_to_request_ + 1) % 5;
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

void LuxpowerSNAComponent::publish_state_(const std::string &key, float value) {
  float_publish_queue_.emplace(key, value);
  if (!float_publishing_) {
    float_publishing_ = true;
    process_next_float_();
  }
}

void LuxpowerSNAComponent::publish_state_(const std::string &key, const std::string &value) {
  string_publish_queue_.emplace(key, value);
  if (!string_publishing_) {
    string_publishing_ = true;
    process_next_string_();
  }
}

void LuxpowerSNAComponent::process_next_float_() {
  if (float_publish_queue_.empty()) {
    float_publishing_ = false;
    return;
  }

  auto item = float_publish_queue_.front();
  float_publish_queue_.pop();

  auto it = this->float_sensors_.find(item.first);
  if (it != this->float_sensors_.end()) {
    it->second->publish_state(item.second);
  }

  this->set_timeout(PUBLISH_DELAY_MS, [this]() {
    this->process_next_float_();
  });
}

void LuxpowerSNAComponent::process_next_string_() {
  if (string_publish_queue_.empty()) {
    string_publishing_ = false;
    return;
  }

  auto item = string_publish_queue_.front();
  string_publish_queue_.pop();

  auto it = this->string_sensors_.find(item.first);
  if (it != this->string_sensors_.end()) {
    it->second->publish_state(item.second);
  }

  this->set_timeout(PUBLISH_DELAY_MS, [this]() {
    this->process_next_string_();
  });
}
  
}  // namespace luxpower_sna
}  // namespace esphome
