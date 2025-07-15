#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome {
namespace luxpower_sna {

const char *LuxpowerSNAComponent::STATUS_TEXTS[193] = {
  "Unknown", // Placeholder for index 0
  // Add actual status text mappings here (up to 193 entries)
  // Example: "Standby", "Running", etc.
  // For now, initialize with placeholders
};
const char *LuxpowerSNAComponent::BATTERY_STATUS_TEXTS[17] = {
  "Unknown", // Placeholder for index 0
  // Add actual battery status text mappings here (up to 17 entries)
  // Example: "Charging", "Discharging", etc.
};

void LuxpowerSNAComponent::setup() {
  current_bank_ = 0;
  next_bank_index_ = 0;
  connected_ = false;
  last_request_ = 0;
  last_heartbeat_ = 0;
  request_in_progress_ = false; // Lock-like mechanism
}

void LuxpowerSNAComponent::loop() {
  // Handle incoming data in the main loop to process heartbeats and responses
  if (client_.available()) {
    uint8_t buffer[512];
    size_t bytes_read = client_.readBytes(buffer, sizeof(buffer));
    if (bytes_read > 0) {
      packet_buffer_.insert(packet_buffer_.end(), buffer, buffer + bytes_read);
      process_packet_buffer_(current_bank_);
    }
  }
}

void LuxpowerSNAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Luxpower SNA:");
  ESP_LOGCONFIG(TAG, "  Host: %s", host_.c_str());
  ESP_LOGCONFIG(TAG, "  Port: %u", port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", dongle_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter Serial: %s", inverter_serial_.c_str());
}

void LuxpowerSNAComponent::update() {
  uint32_t now = millis();
  
  // Prevent overlapping requests (mimic Python's lock)
  if (request_in_progress_) {
    ESP_LOGD(TAG, "Request in progress, skipping update");
    return;
  }

  // Ensure at least 20 seconds between requests to match Python polling
  if (now - last_request_ < 20000) {
    ESP_LOGD(TAG, "Too soon since last request, skipping update");
    return;
  }

  check_connection_();
  if (!connected_) {
    ESP_LOGE(TAG, "Not connected, cannot update");
    return;
  }

  request_in_progress_ = true;
  current_bank_ = banks_[next_bank_index_];
  request_bank_(current_bank_);
  last_request_ = now;
}

bool LuxpowerSNAComponent::is_heartbeat_packet_(const uint8_t *data) {
  LuxHeader *header = reinterpret_cast<LuxHeader*>(const_cast<uint8_t*>(data));
  return (header->prefix == 0x1AA1 && header->function == 193); // HEARTBEAT = 193 from LXPPacket.py
}

void LuxpowerSNAComponent::handle_heartbeat_(const uint8_t *data, size_t len) {
  if (len != 19 && len != 21) { // Heartbeat packets are 19 or 21 bytes in LXPPacket.py
    ESP_LOGE(TAG, "Invalid heartbeat packet length: %zu", len);
    return;
  }
  LuxHeader *header = reinterpret_cast<LuxHeader*>(const_cast<uint8_t*>(data));
  if (header->prefix != 0x1AA1 || header->function != 193) {
    ESP_LOGE(TAG, "Invalid heartbeat packet: prefix=0x%04X, function=0x%02X", header->prefix, header->function);
    return;
  }
  if (client_.connected()) {
    client_.write(data, len);
    ESP_LOGD(TAG, "Responded to heartbeat packet of length %zu", len);
    last_heartbeat_ = millis();
  } else {
    ESP_LOGW(TAG, "Cannot respond to heartbeat: not connected");
  }
}

bool LuxpowerSNAComponent::receive_response_(uint8_t bank) {
  uint8_t buffer[512];
  uint32_t start = millis();
  size_t total_read = 0;

  // Wait up to 15 seconds for response (matches inverter's 10-15s prep time)
  while (millis() - start < 15000) {
    if (client_.available()) {
      size_t bytes_read = client_.readBytes(buffer + total_read, sizeof(buffer) - total_read);
      if (bytes_read > 0) {
        total_read += bytes_read;
        packet_buffer_.insert(packet_buffer_.end(), buffer, buffer + bytes_read);
        if (process_packet_buffer_(bank)) {
          request_in_progress_ = false; // Release lock
          next_bank_index_ = (next_bank_index_ + 1) % 5; // Move to next bank
          return true;
        }
      }
    }
    delay(10);
  }

  if (total_read == 0) {
    ESP_LOGE(TAG, "No data received for bank %d after 15s", bank);
    request_in_progress_ = false; // Release lock on timeout
    return false;
  }
  request_in_progress_ = false; // Release lock
  return false;
}

bool LuxpowerSNAComponent::process_packet_buffer_(uint8_t bank) {
  while (packet_buffer_.size() >= sizeof(LuxHeader)) {
    LuxHeader *header = reinterpret_cast<LuxHeader*>(packet_buffer_.data());
    if (header->prefix != 0x1AA1) {
      ESP_LOGE(TAG, "Invalid packet prefix: 0x%04X", header->prefix);
      packet_buffer_.erase(packet_buffer_.begin(), packet_buffer_.begin() + 1);
      continue;
    }

    uint16_t total_length = header->packetLength + 6;
    if (packet_buffer_.size() < total_length) {
      ESP_LOGD(TAG, "Incomplete packet, waiting for more data (%zu/%d bytes)", packet_buffer_.size(), total_length);
      return false;
    }

    if (is_heartbeat_packet_(packet_buffer_.data())) {
      handle_heartbeat_(packet_buffer_.data(), total_length);
      packet_buffer_.erase(packet_buffer_.begin(), packet_buffer_.begin() + total_length);
      continue;
    }

    // Validate packet fields
    if (header->protocolVersion != 2 || header->function != 194) { // TRANSLATED_DATA = 194
      ESP_LOGE(TAG, "Invalid protocol or function: protocol=0x%04X, function=0x%02X", 
               header->protocolVersion, header->function);
      packet_buffer_.erase(packet_buffer_.begin(), packet_buffer_.begin() + total_length);
      continue;
    }

    // Validate CRC
    uint16_t crc_calc = calculate_crc_(packet_buffer_.data() + sizeof(LuxHeader), 
                                      total_length - sizeof(LuxHeader) - 2);
    uint16_t crc_received = (packet_buffer_[total_length - 1] << 8) | packet_buffer_[total_length - 2];
    if (crc_calc != crc_received) {
      ESP_LOGE(TAG, "CRC mismatch: calc=0x%04X, recv=0x%04X", crc_calc, crc_received);
      packet_buffer_.erase(packet_buffer_.begin(), packet_buffer_.begin() + total_length);
      continue;
    }

    // Validate TranslatedData
    LuxTranslatedData *trans = reinterpret_cast<LuxTranslatedData*>(packet_buffer_.data() + sizeof(LuxHeader));
    if (trans->deviceFunction != 0x04) { // READ_INPUT = 4
      ESP_LOGE(TAG, "Invalid device function: 0x%02X", trans->deviceFunction);
      packet_buffer_.erase(packet_buffer_.begin(), packet_buffer_.begin() + total_length);
      continue;
    }

    // Validate register start
    uint16_t expected_register = bank * 40;
    if (trans->registerStart != expected_register) {
      ESP_LOGW(TAG, "Unexpected register start: expected=%d, received=%d", 
               expected_register, trans->registerStart);
      packet_buffer_.erase(packet_buffer_.begin(), packet_buffer_.begin() + total_length);
      continue;
    }

    // Process data packet
    size_t data_offset = sizeof(LuxHeader) + sizeof(LuxTranslatedData);
    uint8_t* payload = packet_buffer_.data() + data_offset;
    size_t payload_size = total_length - data_offset - 2;

    switch (bank) {
      case 0:
        if (payload_size >= sizeof(LuxLogDataRawSection1)) {
          process_section1_(*reinterpret_cast<const LuxLogDataRawSection1*>(payload));
        } else {
          ESP_LOGE(TAG, "Payload too small for bank 0: %zu bytes", payload_size);
        }
        break;
      case 40:
        if (payload_size >= sizeof(LuxLogDataRawSection2)) {
          process_section2_(*reinterpret_cast<const LuxLogDataRawSection2*>(payload));
        } else {
          ESP_LOGE(TAG, "Payload too small for bank 40: %zu bytes", payload_size);
        }
        break;
      case 80:
        if (payload_size >= sizeof(LuxLogDataRawSection3)) {
          process_section3_(*reinterpret_cast<const LuxLogDataRawSection3*>(payload));
        } else {
          ESP_LOGE(TAG, "Payload too small for bank 80: %zu bytes", payload_size);
        }
        break;
      case 120:
        if (payload_size >= sizeof(LuxLogDataRawSection4)) {
          process_section4_(*reinterpret_cast<const LuxLogDataRawSection4*>(payload));
        } else {
          ESP_LOGE(TAG, "Payload too small for bank 120: %zu bytes", payload_size);
        }
        break;
      case 160:
        if (payload_size >= sizeof(LuxLogDataRawSection5)) {
          process_section5_(*reinterpret_cast<const LuxLogDataRawSection5*>(payload));
        } else {
          ESP_LOGE(TAG, "Payload too small for bank 160: %zu bytes", payload_size);
        }
        break;
      default:
        ESP_LOGW(TAG, "Unknown bank: %d", bank);
    }

    packet_buffer_.erase(packet_buffer_.begin(), packet_buffer_.begin() + total_length);
    return true;
  }
  return false;
}

void LuxpowerSNAComponent::request_bank_(uint8_t bank) {
  if (!client_.connected()) {
    ESP_LOGE(TAG, "Cannot send request for bank %d: not connected", bank);
    request_in_progress_ = false;
    return;
  }

  uint8_t pkt[38] = {
    0xA1, 0x1A,       // Prefix
    0x02, 0x00,       // Protocol version 2
    0x20, 0x00,       // Frame length (32)
    0x01,             // Address
    0xC2,             // Function (TRANSLATED_DATA = 194)
    // Dongle serial (10 bytes)
    0,0,0,0,0,0,0,0,0,0,
    0x12, 0x00,       // Data length (18)
    // Data frame starts here
    0x01,             // Address action (ACTION_READ = 1)
    0x04,             // Device function (READ_INPUT = 4)
    // Inverter serial (10 bytes)
    0,0,0,0,0,0,0,0,0,0,
    // Register and value
    static_cast<uint8_t>(bank * 40), 0x00, // Register (low, high)
    0x28, 0x00        // Value (40 registers)
  };

  // Copy serial numbers
  memcpy(pkt + 8, dongle_serial_.c_str(), std::min(dongle_serial_.length(), size_t(10)));
  memcpy(pkt + 22, inverter_serial_.c_str(), std::min(inverter_serial_.length(), size_t(10)));

  // Calculate CRC for data frame portion only (16 bytes)
  uint16_t crc = calculate_crc_(pkt + 20, 16);
  pkt[36] = crc & 0xFF;
  pkt[37] = crc >> 8;

  ESP_LOGV(TAG, "Sending request for bank %d", bank);
  client_.write(pkt, sizeof(pkt));
}

void LuxpowerSNAComponent::check_connection_() {
  if (!client_.connected()) {
    if (connected_) {
      ESP_LOGW(TAG, "Connection lost, attempting to reconnect");
      safe_disconnect_();
    }
    if (client_.connect(host_.c_str(), port_)) {
      connected_ = true;
      packet_buffer_.clear();
      ESP_LOGI(TAG, "Reconnected to inverter");
      last_heartbeat_ = millis();
    } else {
      ESP_LOGE(TAG, "Reconnection failed");
      connected_ = false;
      request_in_progress_ = false; // Release lock on failed connection
    }
  }
}

void LuxpowerSNAComponent::safe_disconnect_() {
  client_.stop();
  packet_buffer_.clear();
  connected_ = false;
  request_in_progress_ = false; // Release lock
}

void LuxpowerSNAComponent::publish_sensor_(sensor::Sensor *sensor, float value) {
  if (sensor != nullptr) {
    sensor->publish_state(value);
  }
}

void LuxpowerSNAComponent::publish_text_sensor_(text_sensor::TextSensor *sensor, const std::string &value) {
  if (sensor != nullptr) {
    sensor->publish_state(value);
  }
}

void LuxpowerSNAComponent::process_section1_(const LuxLogDataRawSection1 &data) {
  publish_sensor_(lux_status_text_sensor_, STATUS_TEXTS[std::min(data.status, static_cast<uint16_t>(192))]);
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
  publish_sensor_(lux_power_current_clamp_sensor_, data.rms_current / 10.0f);
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
  publish_sensor_(lux_current_solar_output_sensor_, data.p_pv_1 + data.p_pv_2 + data.p_pv_3);
  publish_sensor_(lux_daily_solar_sensor_, (data.e_pv_1_day + data.e_pv_2_day + data.e_pv_3_day) / 10.0f);
  publish_sensor_(lux_power_to_home_sensor_, data.p_to_user);
  publish_sensor_(lux_battery_flow_sensor_, data.p_charge - data.p_discharge);
  publish_sensor_(lux_grid_flow_sensor_, data.p_to_grid - data.p_to_user);
  publish_sensor_(lux_home_consumption_live_sensor_, data.p_to_user + data.p_inv);
  publish_sensor_(lux_home_consumption_sensor_, data.e_to_user_day / 10.0f);
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
  publish_sensor_(lux_radiator1_temp_sensor_, data.t_rad_1 / 10.0f);
  publish_sensor_(lux_radiator2_temp_sensor_, data.t_rad_2 / 10.0f);
  publish_sensor_(lux_battery_temperature_live_sensor_, data.t_bat / 10.0f);
  publish_sensor_(lux_uptime_sensor_, data.uptime);
  publish_sensor_(lux_total_solar_sensor_, (data.e_pv_1_all + data.e_pv_2_all + data.e_pv_3_all) / 10.0f);
  publish_sensor_(lux_home_consumption_total_sensor_, data.e_to_user_all / 10.0f);
}

void LuxpowerSNAComponent::process_section3_(const LuxLogDataRawSection3 &data) {
  publish_sensor_(lux_bms_limit_charge_sensor_, data.max_chg_curr / 10.0f);
  publish_sensor_(lux_bms_limit_discharge_sensor_, data.max_dischg_curr / 10.0f);
  publish_sensor_(charge_voltage_ref_sensor_, data.charge_volt_ref / 10.0f);
  publish_sensor_(discharge_cutoff_voltage_sensor_, data.dischg_cut_volt / 10.0f);
  publish_sensor_(battery_status_inv_sensor_, data.bat_status_inv);
  publish_sensor_(lux_battery_count_sensor_, data.bat_count);
  publish_sensor_(lux_battery_capacity_ah_sensor_, data.bat_capacity);
  publish_sensor_(lux_battery_current_sensor_, data.bat_current / 10.0f);
  publish_sensor_(max_cell_volt_sensor_, data.max_cell_volt / 1000.0f);
  publish_sensor_(min_cell_volt_sensor_, data.min_cell_volt / 1000.0f);
  publish_sensor_(max_cell_temp_sensor_, data.max_cell_temp / 10.0f);
  publish_sensor_(min_cell_temp_sensor_, data.min_cell_temp / 10.0f);
  publish_sensor_(lux_battery_cycle_count_sensor_, data.bat_cycle_count);
  publish_sensor_(lux_home_consumption_2_live_sensor_, data.p_load2);
  publish_text_sensor_(lux_battery_status_text_sensor_, BATTERY_STATUS_TEXTS[std::min(data.bat_status_inv, 16)]);
}

void LuxpowerSNAComponent::process_section4_(const LuxLogDataRawSection4 &data) {
  publish_sensor_(lux_current_generator_voltage_sensor_, data.gen_input_volt / 10.0f);
  publish_sensor_(lux_current_generator_frequency_sensor_, data.gen_input_freq / 10.0f);
  publish_sensor_(lux_current_generator_power_sensor_, data.gen_power_watt);
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

uint16_t LuxpowerSNAComponent::calculate_crc_(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t pos = 0; pos < len; pos++) {
    crc ^= data[pos];
    for (uint8_t i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

}  // namespace luxpower_sna
}  // namespace esphome
