#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// =================================================================================================
// == REGISTER MAP - v11 (Bank-Based, Sequential Requests)
// == This map uses ABSOLUTE REGISTER NUMBERS (0-119) as found in the Python code.
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
  REG_E_EXPORT_DAY = 36,
  REG_E_IMPORT_DAY = 37,

  // --- BANK 1 (Registers 40-79) ---
  REG_E_PV1_ALL_L = 40,
  REG_E_PV1_ALL_H = 41,
  REG_E_PV2_ALL_L = 42,
  REG_E_PV2_ALL_H = 43,
  REG_T_INNER = 64,
  REG_T_RADIATOR = 65,
  REG_T_BAT = 67,

  // --- BANK 2 (Registers 80-119) ---
  // Add registers here if needed in the future
};

// Helper macros to read from the combined data_buffer_ using ABSOLUTE register numbers
#define U16_REG(reg) (this->data_buffer_[(reg) * 2] << 8 | this->data_buffer_[(reg) * 2 + 1])
#define S16_REG(reg) (int16_t)(this->data_buffer_[(reg) * 2] << 8 | this->data_buffer_[(reg) * 2 + 1])
#define U8_REG(reg) (this->data_buffer_[(reg) * 2]) // SOC is the first byte of its register
#define U32_REG(reg_l, reg_h) ((uint32_t)U16_REG(reg_h) << 16 | U16_REG(reg_l))

void LuxpowerSNAComponent::set_dongle_serial(const std::string &serial) {
  this->dongle_serial_ = hex_to_data(serial);
}

void LuxpowerSNAComponent::set_inverter_serial(const std::string &serial) {
  this->inverter_serial_ = hex_to_data(serial);
}

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Luxpower SNA Component...");
  this->data_buffer_.resize(this->num_banks_to_request_ * 80, 0); // 40 registers * 2 bytes/reg
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
  client->setRxTimeout(5);

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
  uint16_t start_reg = data_frame[12] | (data_frame[13] << 8);
  uint8_t value_len = data_frame[14];
  uint8_t *values = &data_frame[15];

  if (value_len != 80) {
      ESP_LOGW(TAG, "Received packet with unexpected data length (%d) for bank %d. Aborting.", value_len, bank_num);
      this->end_update_cycle_();
      return;
  }

  ESP_LOGD(TAG, "Received and stored data for Bank %d.", bank_num);
  memcpy(&this->data_buffer_[bank_num * 80], values, 80);

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
  this->publish_state_("inverter_temp", (float)S16_REG(REG_T_INNER) / 10.0f);
  this->publish_state_("radiator_temp", (float)S16_REG(REG_T_RADIATOR) / 10.0f);
  this->publish_state_("battery_temp", (float)S16_REG(REG_T_BAT) / 10.0f);

  // Daily Energy Totals
  float e_pv1_day = (float)U16_REG(REG_E_PV1_DAY) / 10.0f;
  float e_pv2_day = (float)U16_REG(REG_E_PV2_DAY) / 10.0f;
  this->publish_state_("pv1_today", e_pv1_day);
  this->publish_state_("pv2_today", e_pv2_day);
  this->publish_state_("pv_today", e_pv1_day + e_pv2_day);
  
  this->publish_state_("charge_today", (float)U16_REG(REG_E_CHARGE_DAY) / 10.0f);
  this->publish_state_("discharge_today", (float)U16_REG(REG_E_DISCHARGE_DAY) / 10.0f);
  this->publish_state_("grid_export_today", (float)U16_REG(REG_E_EXPORT_DAY) / 10.0f);
  this->publish_state_("grid_import_today", (float)U16_REG(REG_E_IMPORT_DAY) / 10.0f);
  
  // Total Energy
  this->publish_state_("pv1_total", (float)U32_REG(REG_E_PV1_ALL_L, REG_E_PV1_ALL_H) / 10.0f);
  this->publish_state_("pv2_total", (float)U32_REG(REG_E_PV2_ALL_L, REG_E_PV2_ALL_H) / 10.0f);
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
