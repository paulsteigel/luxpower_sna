// components/luxpower_sna/luxpower_sna.cpp
#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <vector>

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// =================================================================================================
// == DEFINITIVE REGISTER MAP - v10 (Bank-Based Architecture)
// == This map uses ABSOLUTE REGISTER NUMBERS (0-119) as found in the Python code.
// == It is used to parse a complete data buffer assembled from multiple bank requests.
// =================================================================================================
enum LuxpowerRegister {
  // --- BANK 0 (Registers 0-39) ---
  REG_STATUS_CODE = 0,
  REG_V_PV1 = 1,
  REG_V_PV2 = 2,
  REG_V_BATTERY = 4,
  REG_SOC = 5, // Special case: U8
  REG_P_PV1 = 7,
  REG_P_PV2 = 8,
  REG_P_CHARGE = 10,
  REG_P_DISCHARGE = 11,
  REG_V_GRID = 12,
  REG_F_GRID = 15,
  REG_P_INVERTER = 16,
  REG_P_TO_GRID = 26, // S16
  REG_P_LOAD = 27,    // "p_to_user" in python
  REG_E_PV1_DAY = 28,
  REG_E_PV2_DAY = 29,
  REG_E_CHARGE_DAY = 33,
  REG_E_DISCHARGE_DAY = 34,
  REG_E_EXPORT_DAY = 36, // "e_to_grid_day"
  REG_E_IMPORT_DAY = 37, // "e_to_user_day" - This is import, not load.

  // --- BANK 1 (Registers 40-79) ---
  REG_E_PV1_ALL_L = 40, // Low word
  REG_E_PV1_ALL_H = 41, // High word
  REG_E_PV2_ALL_L = 42,
  REG_E_PV2_ALL_H = 43,
  // ... other bank 1 registers if needed
  REG_T_INNER = 64,
  REG_T_RADIATOR = 65,
  REG_T_BAT = 67,

  // --- BANK 2 (Registers 80-119) ---
  // ... bank 2 registers if needed
};

// Helper macros to read from the combined data_buffer_ using ABSOLUTE register numbers
#define U16_REG(reg) (this->data_buffer_[(reg) * 2] << 8 | this->data_buffer_[(reg) * 2 + 1])
#define S16_REG(reg) (int16_t)(this->data_buffer_[(reg) * 2] << 8 | this->data_buffer_[(reg) * 2 + 1])
#define U8_REG(reg) (this->data_buffer_[(reg) * 2]) // SOC is the first byte of its register
#define U32_REG(reg_l, reg_h) ((uint32_t)U16_REG(reg_h) << 16 | U16_REG(reg_l))

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Luxpower SNA Component...");
  this->data_buffer_.resize(this->num_banks_to_request_ * 80, 0); // 40 registers * 2 bytes/reg
  this->banks_received_.resize(this->num_banks_to_request_, false);
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
  std::fill(this->banks_received_.begin(), this->banks_received_.end(), false);
  this->banks_received_count_ = 0;

  ESP_LOGD(TAG, "Starting update cycle. Requesting %d banks.", this->num_banks_to_request_);
  for (int i = 0; i < this->num_banks_to_request_; ++i) {
    this->request_bank_(i);
  }
}

void LuxpowerSNAComponent::request_bank_(int bank_num) {
  uint16_t start_register = bank_num * 40;
  uint16_t num_registers = 40;

  AsyncClient *client = new AsyncClient();
  client->setRxTimeout(5); // 5 second timeout for the response

  client->onData([this, client, bank_num](void *arg, AsyncClient *c, void *data, size_t len) {
    this->handle_packet_(data, len);
    c->close();
  });

  client->onConnect([this, client, start_register, num_registers, bank_num](void *arg, AsyncClient *c) {
    std::vector<uint8_t> request = this->build_request_packet_(start_register, num_registers);
    ESP_LOGD(TAG, "Requesting Bank %d (Reg %d, Count %d): %s",
             bank_num, start_register, num_registers, format_hex_pretty(request.data(), request.size()).c_str());
    c->write(reinterpret_cast<const char*>(request.data()), request.size());
  });

  client->onError([this, client](void *arg, AsyncClient *c, int8_t error) {
    ESP_LOGE(TAG, "TCP connection error: %s", c->errorToString(error));
    this->is_updating_ = false; // Abort update on error
  });

  client->onTimeout([this, client, bank_num](void *arg, AsyncClient *c, uint32_t time) {
    ESP_LOGW(TAG, "TCP connection timeout for bank %d.", bank_num);
    this->is_updating_ = false; // Abort update on timeout
  });

  client->onDisconnect([this, client](void *arg, AsyncClient *c) {
    delete client;
  });

  client->connect(this->host_.c_str(), this->port_);
}

std::vector<uint8_t> LuxpowerSNAComponent::build_request_packet_(uint16_t start_register, uint16_t num_registers) {
    std::vector<uint8_t> packet;
    // Header
    packet.insert(packet.end(), {0xA1, 0x1A, 0x02, 0x00, 0x20, 0x00, 0x01, 0xC2}); // Protocol 2, Frame Len 32, TCP Func 194
    packet.insert(packet.end(), this->dongle_serial_.begin(), this->dongle_serial_.end());
    packet.insert(packet.end(), {0x12, 0x00}); // Data Len 18

    // Data Frame
    std::vector<uint8_t> data_frame;
    data_frame.push_back(0x00); // Action: Write (as per python code)
    data_frame.push_back(0x04); // Device Function: READ_INPUT
    data_frame.insert(data_frame.end(), this->inverter_serial_.begin(), this->inverter_serial_.end());
    data_frame.push_back(start_register & 0xFF);
    data_frame.push_back(start_register >> 8);
    data_frame.push_back(num_registers & 0xFF);
    data_frame.push_back(num_registers >> 8);

    // CRC
    uint16_t crc = crc16(data_frame.data(), data_frame.size());
    
    // Combine and return
    packet.insert(packet.end(), data_frame.begin(), data_frame.end());
    packet.push_back(crc & 0xFF);
    packet.push_back(crc >> 8);
    return packet;
}

void LuxpowerSNAComponent::handle_packet_(void *data, size_t len) {
  uint8_t *raw = (uint8_t *) data;

  // Basic validation
  if (len < 37 || raw[0] != 0xA1 || raw[1] != 0x1A) {
    ESP_LOGW(TAG, "Received invalid or short packet (len: %d)", len);
    return;
  }
  
  uint16_t data_len = raw[18] | (raw[19] << 8);
  uint8_t *data_frame = &raw[20];
  
  uint16_t start_reg = data_frame[12] | (data_frame[13] << 8);
  uint8_t value_len = data_frame[14];
  uint8_t *values = &data_frame[15];

  if (value_len != 80) { // We expect 40 registers = 80 bytes
      ESP_LOGW(TAG, "Received packet with unexpected data length: %d", value_len);
      return;
  }

  int bank_index = start_reg / 40;
  if (bank_index < 0 || bank_index >= this->num_banks_to_request_) {
      ESP_LOGW(TAG, "Received data for unexpected bank index: %d", bank_index);
      return;
  }

  ESP_LOGD(TAG, "Received data for Bank %d.", bank_index);
  
  // Copy received data into the correct part of the main buffer
  memcpy(&this->data_buffer_[bank_index * 80], values, 80);

  if (!this->banks_received_[bank_index]) {
      this->banks_received_[bank_index] = true;
      this->banks_received_count_++;
  }

  // Check if all expected banks have been received
  if (this->banks_received_count_ >= this->num_banks_to_request_) {
      ESP_LOGI(TAG, "All %d banks received. Parsing and publishing data.", this->num_banks_to_request_);
      this->parse_and_publish_();
      this->is_updating_ = false; // End of update cycle
  }
}

void LuxpowerSNAComponent::parse_and_publish_() {
  // Status & SOC
  int status_code = U16_REG(REG_STATUS_CODE);
  this->publish_state_("status_code", (float)status_code);
  std::string status_text;
  switch(status_code) {
      case 0: status_text = "Standby"; break; case 1: status_text = "Self Test"; break;
      case 2: status_text = "Normal"; break;  case 3: status_text = "Alarm"; break;
      case 4: status_text = "Fault"; break;   default: status_text = "Checking"; break;
  }
  this->publish_state_("status_text", status_text);
  this->publish_state_("soc", (float)U8_REG(REG_SOC));

  // Voltages and Frequencies
  this->publish_state_("battery_voltage", (float)U16_REG(REG_V_BATTERY) / 10.0f);
  this->publish_state_("grid_voltage", (float)U16_REG(REG_V_GRID) / 10.0f);
  this->publish_state_("pv1_voltage", (float)U16_REG(REG_V_PV1) / 10.0f);
  this->publish_state_("pv2_voltage", (float)U16_REG(REG_V_PV2) / 10.0f);
  this->publish_state_("grid_frequency", (float)U16_REG(REG_F_GRID) / 100.0f);

  // Power Values
  float p_pv1 = (float)U16_REG(REG_P_PV1);
  float p_pv2 = (float)U16_REG(REG_P_PV2);
  this->publish_state_("pv1_power", p_pv1);
  this->publish_state_("pv2_power", p_pv2);
  this->publish_state_("pv_power", p_pv1 + p_pv2);

  float p_charge = (float)U16_REG(REG_P_CHARGE);
  float p_discharge = (float)U16_REG(REG_P_DISCHARGE);
  this->publish_state_("charge_power", p_charge);
  this->publish_state_("discharge_power", p_discharge);
  this->publish_state_("battery_power", p_charge - p_discharge);
  
  this->publish_state_("inverter_power", (float)U16_REG(REG_P_INVERTER));
  this->publish_state_("load_power", (float)U16_REG(REG_P_LOAD));
  
  float p_to_grid = (float)S16_REG(REG_P_TO_GRID);
  this->publish_state_("grid_export_power", (p_to_grid > 0 ? p_to_grid : 0));
  this->publish_state_("grid_import_power", (p_to_grid < 0 ? -p_to_grid : 0));
  this->publish_state_("grid_power", -p_to_grid);

  // Temperatures
  this->publish_state_("radiator_temp", (float)S16_REG(REG_T_RADIATOR) / 10.0f);
  this->publish_state_("inverter_temp", (float)S16_REG(REG_T_INNER) / 10.0f);
  this->publish_state_("battery_temp", (float)S16_REG(REG_T_BAT) / 10.0f);

  // Daily Energy Totals
  this->publish_state_("pv1_today", (float)U16_REG(REG_E_PV1_DAY) / 10.0f);
  this->publish_state_("pv2_today", (float)U16_REG(REG_E_PV2_DAY) / 10.0f);
  this->publish_state_("charge_today", (float)U16_REG(REG_E_CHARGE_DAY) / 10.0f);
  this->publish_state_("discharge_today", (float)U16_REG(REG_E_DISCHARGE_DAY) / 10.0f);
  this->publish_state_("grid_export_today", (float)U16_REG(REG_E_EXPORT_DAY) / 10.0f);
  this->publish_state_("grid_import_today", (float)U16_REG(REG_E_IMPORT_DAY) / 10.0f);
  
  // Total Energy (example for PV1)
  uint32_t e_pv1_total_raw = U32_REG(REG_E_PV1_ALL_L, REG_E_PV1_ALL_H);
  this->publish_state_("pv1_total", (float)e_pv1_total_raw / 10.0f);
}

void LuxpowerSNAComponent::publish_state_(const std::string &key, float value) {
    auto it = this->sensors_.find(key);
    if (it != this->sensors_.end()) {
        ((sensor::Sensor *)it->second)->publish_state(value);
    }
}

void LuxpowerSNAComponent::publish_state_(const std::string &key, const std::string &value) {
    auto it = this->sensors_.find(key);
    if (it != this->sensors_.end()) {
        ((text_sensor::TextSensor *)it->second)->publish_state(value);
    }
}

}  // namespace luxpower_sna
}  // namespace esphome
