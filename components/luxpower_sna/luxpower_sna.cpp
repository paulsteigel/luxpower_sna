// luxpower_sna.cpp
#include "luxpower_sna.h"

namespace esphome {
namespace luxpower_sna {

// --- YOUR ORIGINAL STATIC ARRAYS: UNCHANGED ---
const char *LuxpowerSNAComponent::STATUS_TEXTS[193] = {
  "Standby", "Error", "Inverting", "", "Solar > Load - Surplus > Grid", "Float", "", "Charger Off", "Supporting", "Selling", "Pass Through", "Offsetting", "Solar > Battery Charging", "", "", "",
  "Battery Discharging > LOAD - Surplus > Grid", "Temperature Over Range", "", "", "Solar + Battery Discharging > LOAD - Surplus > Grid", "", "", "", "", "", "", "", "AC Battery Charging", "", "", "", "", "", "Solar + Grid > Battery Charging",
  "", "", "", "", "", "", "", "", "", "No Grid : Battery > EPS", "", "", "", "", "", "", "", "", "No Grid : Solar > EPS - Surplus > Battery Charging", "", "", "", "", "No Grid : Solar + Battery Discharging > EPS"
};
const char *LuxpowerSNAComponent::BATTERY_STATUS_TEXTS[17] = {
  "Charge Forbidden & Discharge Forbidden", "Unknown", "Charge Forbidden & Discharge Allowed", "Charge Allowed & Discharge Allowed",
  "", "", "", "", "", "", "", "", "", "", "", "", "Charge Allowed & Discharge Forbidden"
};


// =========================================================================
// Implementation of the Client class
// =========================================================================
void Client::setup(const std::string &host, uint16_t port) {
  this->host_ = host;
  this->port_ = port;
}

void Client::disconnect_() {
  client_.stop();
  state_ = STATE_DISCONNECTED;
}

void Client::connect_() {
  state_ = STATE_CONNECTING;
  ESP_LOGI(TAG, "Connecting to %s:%u...", host_.c_str(), port_);
  if (client_.connect(host_.c_str(), port_)) {
    ESP_LOGI(TAG, "Connection successful!");
    state_ = STATE_CONNECTED;
  } else {
    ESP_LOGW(TAG, "Connection failed.");
    disconnect_();
  }
}

void Client::loop() {
  switch (state_) {
    case STATE_DISCONNECTED:
      if (millis() - last_connect_attempt_ > 10000) {
        last_connect_attempt_ = millis();
        this->connect_();
      }
      break;
    case STATE_CONNECTED:
      if (!client_.connected()) {
        ESP_LOGW(TAG, "Inverter disconnected.");
        this->disconnect_();
      }
      break;
    case STATE_CONNECTING:
      break;
  }
}

bool Client::is_connected() {
    return state_ == STATE_CONNECTED && client_.connected();
}

size_t Client::write(const uint8_t *buffer, size_t size) {
    if (!is_connected()) return 0;
    return client_.write(buffer, size);
}

int Client::read(uint8_t *buffer, size_t size) {
    if (!is_connected()) return 0;
    return client_.read(buffer, size);
}

int Client::available() {
    if (!is_connected()) return 0;
    return client_.available();
}

// =========================================================================
// Your main component methods, now using the Client correctly
// =========================================================================

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Luxpower SNA...");
  client_.setup(host_, port_);
}

void LuxpowerSNAComponent::loop() {
  client_.loop();
}

void LuxpowerSNAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Luxpower SNA:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%u", host_.c_str(), port_);
  ESP_LOGCONFIG(TAG, "  Dongle: %s", dongle_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter: %s", inverter_serial_.c_str());
}

void LuxpowerSNAComponent::update() {
  if (!client_.is_connected()) {
    ESP_LOGD(TAG, "Not connected, skipping update.");
    return;
  }

  uint8_t bank = banks_[next_bank_index_];
  request_bank_(bank);
  
  if (receive_response_(bank)) {
    ESP_LOGD(TAG, "Successfully processed bank %d", bank);
  } else {
    ESP_LOGW(TAG, "Failed to process bank %d", bank);
  }
  
  next_bank_index_ = (next_bank_index_ + 1) % 5;
}

void LuxpowerSNAComponent::request_bank_(uint8_t bank) {
  uint8_t pkt[38] = {
    0xA1, 0x1A, 0x02, 0x00, 0x20, 0x00, 0x01, 0xC2,
    0,0,0,0,0,0,0,0,0,0, // dongle serial
    0x12, 0x00, 0x00, 0x04,
    0,0,0,0,0,0,0,0,0,0, // inverter serial
    static_cast<uint8_t>(bank), 0x00, 0x28, 0x00, 0x00, 0x00
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
  
  while (millis() - start < 5000) {
    if (client_.available()) {
      int bytes_read = client_.read(buffer + total_read, sizeof(buffer) - total_read);
      if (bytes_read > 0) { total_read += bytes_read; }
    } else {
      delay(10);
    }
    
    if (total_read >= sizeof(LuxHeader) + sizeof(LuxTranslatedData) + 2) { break; }
  }
  
  if (total_read == 0) {
    ESP_LOGD(TAG, "No data received for bank %d", bank);
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

// =========================================================================
// UNCHANGED: All your original, working functions below this line
// =========================================================================
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
