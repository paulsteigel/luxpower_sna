#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace luxpower_sna {

// Using declarations for the functions that DO exist in helpers.h
using esphome::crc16;
using esphome::format_hex_pretty;

// ===================================================================================
// CORRECTED: Re-implemented hex_to_data to be exception-free, as required by ESPHome.
// This version manually parses hex characters instead of using std::stoul.
// ===================================================================================
optional<std::vector<uint8_t>> hex_to_data(const std::string &hex_string) {
  if (hex_string.length() % 2 != 0) {
    return {};  // Hex string must have an even number of characters
  }
  std::vector<uint8_t> data;
  data.reserve(hex_string.length() / 2);
  
  for (size_t i = 0; i < hex_string.length(); i += 2) {
    uint8_t byte = 0;
    for (size_t j = 0; j < 2; ++j) {
      char c = hex_string[i + j];
      int value = 0;
      if (c >= '0' && c <= '9') {
        value = c - '0';
      } else if (c >= 'a' && c <= 'f') {
        value = c - 'a' + 10;
      } else if (c >= 'A' && c <= 'F') {
        value = c - 'A' + 10;
      } else {
        return {}; // Invalid character
      }
      byte = (byte << 4) | value;
    }
    data.push_back(byte);
  }
  return data;
}

static const char *const TAG = "luxpower_sna";

// REGISTER MAP (remains the same)
enum LuxpowerRegister {
  REG_STATUS_CODE = 0, REG_V_PV1 = 1, REG_V_PV2 = 2, REG_V_BATTERY = 4, REG_SOC = 5,
  REG_P_PV1 = 7, REG_P_PV2 = 8, REG_P_CHARGE = 10, REG_P_DISCHARGE = 11, REG_V_GRID = 12,
  REG_F_GRID = 15, REG_P_INVERTER = 16, REG_P_TO_GRID = 26, REG_P_LOAD = 27, REG_E_PV1_DAY = 28,
  REG_E_PV2_DAY = 29, REG_E_CHARGE_DAY = 33, REG_E_DISCHARGE_DAY = 34, REG_E_EXPORT_DAY = 36,
  REG_E_IMPORT_DAY = 37, REG_E_PV1_ALL_L = 40, REG_E_PV1_ALL_H = 41, REG_E_PV2_ALL_L = 42,
  REG_E_PV2_ALL_H = 43, REG_T_INNER = 64, REG_T_RADIATOR = 65, REG_T_BAT = 67,
};

#define U16_REG(reg) (this->data_buffer_[(reg) * 2] << 8 | this->data_buffer_[(reg) * 2 + 1])
#define S16_REG(reg) (int16_t)(this->data_buffer_[(reg) * 2] << 8 | this->data_buffer_[(reg) * 2 + 1])
#define U8_REG(reg) (this->data_buffer_[(reg) * 2])
#define U32_REG(reg_l, reg_h) ((uint32_t)U16_REG(reg_h) << 16 | U16_REG(reg_l))

void LuxpowerSNAComponent::set_dongle_serial(const std::string &serial) {
  auto data = hex_to_data(serial);
  if (data.has_value()) {
    this->dongle_serial_ = data.value();
  } else {
    ESP_LOGE(TAG, "Invalid dongle serial number provided: %s", serial.c_str());
  }
}

void LuxpowerSNAComponent::set_inverter_serial_number(const std::string &serial) {
  auto data = hex_to_data(serial);
  if (data.has_value()) {
    this->inverter_serial_ = data.value();
  } else {
    ESP_LOGE(TAG, "Invalid inverter serial number provided: %s", serial.c_str());
  }
}

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Luxpower SNA Component...");
  this->data_buffer_.resize(this->num_banks_to_request_ * 80, 0);
}

void LuxpowerSNAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Luxpower SNA Component:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%u", this->host_.c_str(), this->port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", format_hex_pretty(this->dongle_serial_).c_str());
  ESP_LOGCONFIG(TAG, "  Inverter Serial: %s", format_hex_pretty(this->inverter_serial_).c_str());
}

void LuxpowerSNAComponent::update() {
  if (this->is_updating_) {
    ESP_LOGW(TAG, "Update already in progress. Skipping.");
    return;
  }
  this->is_updating_ = true;
  this->current_bank_to_request_ = 0;
  ESP_LOGD(TAG, "Starting update cycle...");
  this->request_bank_(this->current_bank_to_request_);
}

void LuxpowerSNAComponent::end_update_cycle_() {
    this->is_updating_ = false;
    ESP_LOGD(TAG, "Update cycle finished.");
}

void LuxpowerSNAComponent::request_bank_(int bank_num) {
  uint16_t start_register = bank_num * 40;
  uint16_t num_registers = 40;

  AsyncClient *client = new AsyncClient();
  client->setRxTimeout(10);

  client->onData([this, client, bank_num](void *arg, AsyncClient *c, void *data, size_t len) {
    this->handle_packet_(data, len, bank_num);
    c->close();
  });

  client->onConnect([this, client, start_register, num_registers, bank_num](void *arg, AsyncClient *c) {
    std::vector<uint8_t> request = this->build_request_packet_(start_register, num_registers);
    ESP_LOGD(TAG, "Requesting Bank %d (Reg %d, Count %d)", bank_num, start_register, num_registers);
    c->write(reinterpret_cast<const char*>(request.data()), request.size());
  });

  client->onError([this, client](void *arg, AsyncClient *c, int8_t error) {
    ESP_LOGE(TAG, "TCP connection error: %s. Aborting update.", c->errorToString(error));
    this->end_update_cycle_();
  });

  client->onTimeout([this, client, bank_num](void *arg, AsyncClient *c, uint32_t time) {
    ESP_LOGW(TAG, "TCP connection timeout for bank %d. Aborting update.", bank_num);
    this->end_update_cycle_();
  });

  client->onDisconnect([this, client](void *arg, AsyncClient *c) {
    delete client;
  });

  client->connect(this->host_.c_str(), this->port_);
}

std::vector<uint8_t> LuxpowerSNAComponent::build_request_packet_(uint16_t start_register, uint16_t num_registers) {
    std::vector<uint8_t> packet;
    packet.insert(packet.end(), {0xA1, 0x1A, 0x02, 0x00, 0x20, 0x00, 0x01, 0xC2});
    packet.insert(packet.end(), this->dongle_serial_.begin(), this->dongle_serial_.end());
    packet.insert(packet.end(), {0x12, 0x00});

    std::vector<uint8_t> data_frame;
    data_frame.push_back(0x00);
    data_frame.push_back(0x04);
    data_frame.insert(data_frame.end(), this->inverter_serial_.begin(), this->inverter_serial_.end());
    data_frame.push_back(start_register & 0xFF);
    data_frame.push_back(start_register >> 8);
    data_frame.push_back(num_registers & 0xFF);
    data_frame.push_back(num_registers >> 8);

    uint16_t crc = crc16(data_frame.data(), data_frame.size());
    
    packet.insert(packet.end(), data_frame.begin(), data_frame.end());
    packet.push_back(crc & 0xFF);
    packet.push_back(crc >> 8);
    return packet;
}

void LuxpowerSNAComponent::handle_packet_(void *data, size_t len, int bank_num) {
  uint8_t *raw = (uint8_t *) data;

  if (len < 37 || raw[0] != 0xA1 || raw[1] != 0x1A || raw[7] != 0xC2) {
    ESP_LOGW(TAG, "Received invalid packet for bank %d. Aborting update.", bank_num);
    this->end_update_cycle_();
    return;
  }
  
  uint8_t *data_frame = &raw[20];
  uint8_t value_len = data_frame[14];

  if (value_len != 80) {
      ESP_LOGW(TAG, "Packet for bank %d has wrong data length (%d). Aborting.", bank_num, value_len);
      this->end_update_cycle_();
      return;
  }

  ESP_LOGD(TAG, "Received and stored data for Bank %d.", bank_num);
  memcpy(&this->data_buffer_[bank_num * 80], &data_frame[15], 80);

  this->current_bank_to_request_++;
  if (this->current_bank_to_request_ < this->num_banks_to_request_) {
      this->request_bank_(this->current_bank_to_request_);
  } else {
      ESP_LOGI(TAG, "All %d banks received. Parsing and publishing data.", this->num_banks_to_request_);
      this->parse_and_publish_();
      this->end_update_cycle_();
  }
}

void LuxpowerSNAComponent::parse_and_publish_() {
  if (this->status_text_sensor_) {
    int status_code = U16_REG(REG_STATUS_CODE);
    std::string status_text;
    switch(status_code) {
        case 0: status_text = "Standby"; break; case 1: status_text = "Self Test"; break;
        case 2: status_text = "Normal"; break;  case 3: status_text = "Alarm"; break;
        case 4: status_text = "Fault"; break;   default: status_text = "Checking"; break;
    }
    this->status_text_sensor_->publish_state(status_text);
  }

  if (this->soc_sensor_) this->soc_sensor_->publish_state((float)U8_REG(REG_SOC));
  if (this->battery_voltage_sensor_) this->battery_voltage_sensor_->publish_state((float)U16_REG(REG_V_BATTERY) / 10.0f);
  if (this->battery_temp_sensor_) this->battery_temp_sensor_->publish_state((float)S16_REG(REG_T_BAT) / 10.0f);
  
  float p_pv1 = (float)U16_REG(REG_P_PV1);
  float p_pv2 = (float)U16_REG(REG_P_PV2);
  if (this->pv_power_sensor_) this->pv_power_sensor_->publish_state(p_pv1 + p_pv2);
  
  float p_to_grid = (float)S16_REG(REG_P_TO_GRID);
  if (this->grid_power_sensor_) this->grid_power_sensor_->publish_state(-p_to_grid);

  if (this->load_power_sensor_) this->load_power_sensor_->publish_state((float)U16_REG(REG_P_LOAD));
  
  if (this->load_today_sensor_) this->load_today_sensor_->publish_state((float)U16_REG(REG_E_IMPORT_DAY) / 10.0f);
}

}  // namespace luxpower_sna
}  // namespace esphome
