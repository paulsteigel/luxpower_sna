// luxpower_sna.cpp
#include "luxpower_sna.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace luxpower_sna {

// Static text arrays for status messages (unchanged)
const char *LuxpowerSNAComponent::STATUS_TEXTS[193] = {
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

const char *LuxpowerSNAComponent::BATTERY_STATUS_TEXTS[17] = {
  "Charge Forbidden & Discharge Forbidden", "Unknown", 
  "Charge Forbidden & Discharge Allowed", "Charge Allowed & Discharge Allowed",
  "", "", "", "", "", "", "", "", "", "", "", "", 
  "Charge Allowed & Discharge Forbidden"
};


// Key timing constants for connection management
const uint32_t CONNECT_RETRY_INTERVAL = 10000; // Retry connection after 10 seconds on failure
const uint32_t HEARTBEAT_TIMEOUT = 45000;      // Disconnect if no communication for 45 seconds
const uint32_t RESPONSE_TIMEOUT = 10000;       // Timeout for waiting for a response to a data request

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Luxpower SNA component...");
  this->buffer_.reserve(1024); // Pre-allocate 1KB for the receive buffer
}

void LuxpowerSNAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Luxpower SNA:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%u", host_.c_str(), port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", dongle_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter Serial: %s", inverter_serial_.c_str());
}

void LuxpowerSNAComponent::update() {
  // `update` is now just a "trigger". It's called by the PollingComponent schedule.
  // It will attempt to send a request for the next data bank ONLY if the connection
  // is idle and ready (STATE_CONNECTED).
  if (this->state_ == STATE_CONNECTED) {
    ESP_LOGD(TAG, "Update() triggered. Requesting data for bank index %d.", this->current_bank_index_);
    request_bank_(this->banks_[this->current_bank_index_]);
  } else {
    ESP_LOGW(TAG, "Update() triggered, but the client is not ready. State: %d. Deferring request.", this->state_);
  }
}

void LuxpowerSNAComponent::loop() {
  // `loop` is the main "engine". It runs constantly, managing the state machine,
  // handling timeouts, and processing incoming data.
  handle_connection_();

  // If we are connected (or awaiting a response), read any available data.
  if (this->state_ >= STATE_CONNECTED) {
    while (client_.available()) {
      uint8_t byte;
      if (client_.read(&byte, 1) > 0) {
        this->buffer_.push_back(byte);
        this->last_communication_time_ = millis(); // We received something, reset the heartbeat timer
      }
    }
    // Attempt to process the accumulated data in the buffer.
    process_buffer_();
  }
}

void LuxpowerSNAComponent::handle_connection_() {
  uint32_t now = millis();

  switch (this->state_) {
    case STATE_DISCONNECTED:
      // If disconnected, try to connect periodically.
      if (now - this->last_connection_attempt_ > CONNECT_RETRY_INTERVAL) {
        connect_to_inverter_();
      }
      break;

    case STATE_CONNECTING:
      // This state is handled by connect_to_inverter_(). If it fails, it will
      // set the state back to DISCONNECTED. If successful, to CONNECTED.
      // We add a manual check here in case it gets stuck.
      if (now - this->last_connection_attempt_ > CONNECT_RETRY_INTERVAL) {
        ESP_LOGW(TAG, "Connection attempt timed out. Disconnecting.");
        disconnect_();
      }
      break;

    case STATE_CONNECTED:
      // We are connected and idle. Check for communication timeout (heartbeat).
      if (now - this->last_communication_time_ > HEARTBEAT_TIMEOUT) {
        ESP_LOGW(TAG, "Heartbeat timeout. No communication from inverter. Disconnecting.");
        disconnect_();
      }
      break;

    case STATE_AWAITING_RESPONSE:
      // We've sent a request and are waiting for a reply. Check for a response timeout.
      if (now - this->request_sent_time_ > RESPONSE_TIMEOUT) {
        ESP_LOGE(TAG, "Response timeout for bank request. Disconnecting to reset.");
        disconnect_();
      }
      break;
  }
  
  // Also, if the client is no longer connected at a low level, force disconnect.
  if (this->state_ > STATE_DISCONNECTED && !client_.connected()) {
      ESP_LOGW(TAG, "Client disconnected unexpectedly. Cleaning up.");
      disconnect_();
  }
}

void LuxpowerSNAComponent::disconnect_() {
  client_.stop();
  this->buffer_.clear();
  this->state_ = STATE_DISCONNECTED;
  ESP_LOGI(TAG, "Disconnected from inverter.");
}

void LuxpowerSNAComponent::connect_to_inverter_() {
  this->last_connection_attempt_ = millis();
  if (this->state_ != STATE_DISCONNECTED) {
    return; // Already connecting or connected
  }

  ESP_LOGI(TAG, "Connecting to %s:%d...", this->host_.c_str(), this->port_);
  this->state_ = STATE_CONNECTING;

  if (client_.connect(this->host_.c_str(), this->port_)) {
    ESP_LOGI(TAG, "Connection successful!");
    this->state_ = STATE_CONNECTED;
    this->last_communication_time_ = millis();
    // After connecting, immediately trigger an update to start the data fetching cycle.
    this->update(); 
  } else {
    ESP_LOGW(TAG, "Connection failed.");
    this->state_ = STATE_DISCONNECTED;
  }
}

void LuxpowerSNAComponent::process_buffer_() {
  // This function continuously tries to parse complete packets from the start of the buffer.
  while (this->buffer_.size() >= sizeof(LuxHeader)) {
    LuxHeader *header = reinterpret_cast<LuxHeader*>(this->buffer_.data());

    // Check for the correct packet prefix
    if (header->prefix != 0x55AA) {
      ESP_LOGW(TAG, "Invalid packet prefix found. Discarding one byte.");
      this->buffer_.erase(this->buffer_.begin());
      continue; // Try again from the next byte
    }
    
    // Check if the full packet has been received
    size_t expected_len = sizeof(LuxHeader) + header->dataLength + 2; // Header + Data + CRC
    if (this->buffer_.size() < expected_len) {
      // Not enough data yet, wait for more
      return;
    }

    // A full packet is available in the buffer. Let's process it.
    std::vector<uint8_t> packet_data(this->buffer_.begin(), this->buffer_.begin() + expected_len);
    this->buffer_.erase(this->buffer_.begin(), this->buffer_.begin() + expected_len); // Remove packet from buffer

    // Verify CRC
    uint16_t received_crc = *reinterpret_cast<uint16_t*>(&packet_data[expected_len - 2]);
    uint16_t calculated_crc = calculate_crc_(packet_data.data(), expected_len - 2);

    if (received_crc != calculated_crc) {
      ESP_LOGW(TAG, "CRC check failed! Discarding packet.");
      continue; // Move to the next potential packet in the buffer
    }
    
    // --- Packet is valid, now identify and handle it ---
    LuxResponseDataHeader *data_header = reinterpret_cast<LuxResponseDataHeader*>(&packet_data[sizeof(LuxHeader)]);
    
    // Check for Heartbeat (function 193 or 0xC1)
    if (header->function == 193) {
      ESP_LOGD(TAG, "Heartbeat received. Responding...");
      client_.write(packet_data.data(), packet_data.size()); // Respond with the same packet
      this->last_communication_time_ = millis();
      // A heartbeat is not a data response, so we don't change the state.
      // If we were AWAITING_RESPONSE, we continue to wait.
    } 
    // Check for Data Response (function 130 or 0x82)
    else if (header->function == 130) {
      ESP_LOGD(TAG, "Data response received for register %d.", data_header->registerStart);

      // Point to the actual data payload
      void *payload = &packet_data[sizeof(LuxHeader) + sizeof(LuxResponseDataHeader)];
      
      // Process the data based on the starting register
      switch (data_header->registerStart) {
        case 0:
          process_section1_(*reinterpret_cast<LuxLogDataRawSection1*>(payload));
          break;
        case 40:
          process_section2_(*reinterpret_cast<LuxLogDataRawSection2*>(payload));
          break;
        case 80:
          process_section3_(*reinterpret_cast<LuxLogDataRawSection3*>(payload));
          break;
        case 120:
          process_section4_(*reinterpret_cast<LuxLogDataRawSection4*>(payload));
          break;
        case 160:
          process_section5_(*reinterpret_cast<LuxLogDataRawSection5*>(payload));
          break;
        default:
          ESP_LOGW(TAG, "Received data for unknown register start: %d", data_header->registerStart);
          break;
      }

      // We have successfully processed a response.
      // The connection is now idle and ready for the next request.
      this->state_ = STATE_CONNECTED; 

      // Move to the next bank for the subsequent update() call.
      this->current_bank_index_ = (this->current_bank_index_ + 1) % 5;
    }
  }
}

// luxpower_sna.cpp

void LuxpowerSNAComponent::request_bank_(uint8_t bank_start_register) {
  if (this->state_ != STATE_CONNECTED) {
    ESP_LOGW(TAG, "Cannot send request: Not in CONNECTED state.");
    return;
  }

  uint8_t packet[25];
  memset(packet, 0, sizeof(packet)); // Clear the packet to zeros first

  // 1. Fill the Header
  LuxHeader* header = reinterpret_cast<LuxHeader*>(packet);
  header->prefix = 0x55AA; // Let's test with the original byte order
  header->protocolVersion = 0x0101;
  header->packetLength = 25;
  header->address = 1;
  header->function = 130; // READ_INPUT
  strncpy(header->serialNumber, this->dongle_serial_.c_str(), 10);
  header->dataLength = 3;

  // 2. Fill the Data Payload (3 bytes)
  uint16_t reg_start = bank_start_register;
  uint8_t reg_len = 40;
  
  packet[20] = reg_start & 0xFF;
  packet[21] = (reg_start >> 8) & 0xFF;
  packet[22] = reg_len;

  // 3. Calculate CRC over the first 23 bytes
  uint16_t crc = calculate_crc_(packet, 23);

  // 4. Place the CRC at the end
  packet[23] = crc & 0xFF;
  packet[24] = (crc >> 8) & 0xFF;

  // ***** THIS IS THE CRITICAL NEW LINE *****
  ESP_LOGD(TAG, "Sending Request Packet HEX: %s", format_hex_pretty(packet, sizeof(packet)).c_str());
  
  client_.write(packet, sizeof(packet));

  // 5. Update state
  this->state_ = STATE_AWAITING_RESPONSE;
  this->request_sent_time_ = millis();
}

uint16_t LuxpowerSNAComponent::calculate_crc_(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

void LuxpowerSNAComponent::publish_sensor_(sensor::Sensor *sensor, float value) {
    if (sensor != nullptr && (!sensor->has_state() || sensor->get_state() != value)) {
        sensor->publish_state(value);
    }
}

void LuxpowerSNAComponent::publish_text_sensor_(text_sensor::TextSensor *sensor, const std::string &value) {
    if (sensor != nullptr && (!sensor->has_state() || sensor->get_state() != value)) {
        sensor->publish_state(value);
    }
}

void LuxpowerSNAComponent::process_section1_(const LuxLogDataRawSection1 &data) {
  ESP_LOGD(TAG, "Processing Section 1 data...");

  publish_text_sensor_(lux_status_text_sensor_, (data.status < 193) ? STATUS_TEXTS[data.status] : "Unknown");
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
  publish_sensor_(grid_power_factor_sensor_, data.pf / 100.0f);
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

  // Calculated sensors
  float solar_total = data.p_pv_1 + data.p_pv_2 + data.p_pv_3;
  publish_sensor_(lux_current_solar_output_sensor_, solar_total);

  float solar_daily_total = (data.e_pv_1_day + data.e_pv_2_day + data.e_pv_3_day) / 10.0f;
  publish_sensor_(lux_daily_solar_sensor_, solar_daily_total);

  float to_home = data.p_inv - data.p_to_grid;
  publish_sensor_(lux_power_to_home_sensor_, to_home);

  float battery_flow = data.p_charge - data.p_discharge;
  publish_sensor_(lux_battery_flow_sensor_, battery_flow);
  
  float grid_flow = data.p_to_grid - data.p_to_user;
  publish_sensor_(lux_grid_flow_sensor_, grid_flow);
  
  float home_consumption = solar_total + data.p_discharge - data.p_charge + data.p_to_user - data.p_to_grid;
  publish_sensor_(lux_home_consumption_live_sensor_, home_consumption);

  float home_daily_consumption = (data.e_inv_day - data.e_to_grid_day + data.e_to_user_day) / 10.0f;
  publish_sensor_(lux_home_consumption_sensor_, home_daily_consumption);
}

void LuxpowerSNAComponent::process_section2_(const LuxLogDataRawSection2 &data) {
  ESP_LOGD(TAG, "Processing Section 2 data...");
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

  float total_solar = (data.e_pv_1_all + data.e_pv_2_all + data.e_pv_3_all) / 10.0f;
  publish_sensor_(lux_total_solar_sensor_, total_solar);
  
  float home_total = (data.e_inv_all + data.e_to_user_all - data.e_to_grid_all) / 10.0f;
  publish_sensor_(lux_home_consumption_total_sensor_, home_total);
}

void LuxpowerSNAComponent::process_section3_(const LuxLogDataRawSection3 &data) {
  ESP_LOGD(TAG, "Processing Section 3 data...");
  publish_text_sensor_(lux_battery_status_text_sensor_, (data.bat_status_inv < 17) ? BATTERY_STATUS_TEXTS[data.bat_status_inv] : "Unknown");
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
}

void LuxpowerSNAComponent::process_section4_(const LuxLogDataRawSection4 &data) {
  ESP_LOGD(TAG, "Processing Section 4 data...");
  publish_sensor_(lux_current_generator_voltage_sensor_, data.gen_input_volt / 10.0f);
  publish_sensor_(lux_current_generator_frequency_sensor_, data.gen_input_freq / 100.0f);
  publish_sensor_(lux_current_generator_power_sensor_, data.gen_power_watt);
  publish_sensor_(lux_current_generator_power_daily_sensor_, data.gen_power_day / 10.0f);
  publish_sensor_(lux_current_generator_power_all_sensor_, data.gen_power_all / 10.0f);
  publish_sensor_(lux_current_eps_L1_voltage_sensor_, data.eps_L1_volt / 10.0f);
  publish_sensor_(lux_current_eps_L2_voltage_sensor_, data.eps_L2_volt / 10.0f);
  publish_sensor_(lux_current_eps_L1_watt_sensor_, data.eps_L1_watt);
  publish_sensor_(lux_current_eps_L2_watt_sensor_, data.eps_L2_watt);
}

void LuxpowerSNAComponent::process_section5_(const LuxLogDataRawSection5 &data) {
  ESP_LOGD(TAG, "Processing Section 5 data...");
  publish_sensor_(p_load_ongrid_sensor_, data.p_load_ongrid);
  publish_sensor_(e_load_day_sensor_, data.e_load_day / 10.0f);
  publish_sensor_(e_load_all_l_sensor_, data.e_load_all_l / 10.0f);
}

std::string format_hex_pretty(const uint8_t* data, size_t length) {
  std::string result;
  char buffer[4];
  for (size_t i = 0; i < length; ++i) {
    sprintf(buffer, "%02X ", data[i]);
    result += buffer;
  }
  return result;
}
}  // namespace luxpower_sna
}  // namespace esphome
