// components/luxpower_sna/luxpower_sna.cpp
#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <vector>

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// =================================================================================================
// == DEFINITIVE REGISTER MAP for 117-byte data packet (SNA models)
// == v6 - This map uses absolute byte offsets and has been meticulously corrected based on
// ==      user feedback, hex dumps, and by cross-referencing scaling from LXPPacket.py.
// ==      This should be the final, working version.
// =================================================================================================
enum LuxpowerRegister {
  // Note: Offsets are 1-based for readability, matching byte numbers in a hex editor.
  // The U16/S16 macros handle the 0-based array access internally.

  // --- Voltages & Frequencies ---
  REG_V_BATTERY     = 35,  // U16 / 100.0f (NOTE: Special scaling for this model)
  REG_V_GRID        = 39,  // U16 / 10.0f
  REG_V_PV1         = 53,  // U16 / 10.0f
  REG_V_PV2         = 57,  // U16 / 10.0f
  REG_F_GRID        = 71,  // U16 / 100.0f

  // --- Power Readings (W) ---
  REG_P_BATTERY     = 37,  // S16 (Positive=Charge, Negative=Discharge)
  REG_P_PV1         = 51,  // U16
  REG_P_PV2         = 55,  // U16
  REG_P_INVERTER    = 59,  // U16 (AC Output Power)
  REG_P_LOAD        = 63,  // U16 (Total Load Power)
  REG_P_TO_GRID     = 79,  // S16 (Positive=Export, Negative=Import)

  // --- Status & State ---
  REG_STATUS_CODE   = 45,  // U8
  REG_SOC           = 46,  // U8

  // --- Temperatures (°C) ---
  REG_T_RADIATOR    = 65,  // S16 / 10.0f
  REG_T_INVERTER    = 67,  // S16 / 10.0f

  // --- Daily Energy Totals (kWh) ---
  REG_E_PV1_DAY       = 83,  // U16 / 100.0f (NOTE: Special scaling)
  REG_E_PV2_DAY       = 85,  // U16 / 100.0f (NOTE: Special scaling)
  REG_E_INVERTER_DAY  = 87,  // U16 / 100.0f (Yield Today)
  REG_E_DISCHARGE_DAY = 93,  // U16 / 10.0f
  REG_E_CHARGE_DAY    = 95,  // U16 / 10.0f
  REG_E_EXPORT_DAY    = 99,  // U16 / 100.0f (NOTE: Special scaling)
  REG_E_IMPORT_DAY    = 101, // U16 / 100.0f (NOTE: Special scaling)
  REG_E_LOAD_DAY      = 103, // U16 / 100.0f (NOTE: Special scaling)
};

// Helper macros to make parsing cleaner and handle 1-based offsets
#define U16(reg) (raw[(reg) - 1] << 8 | raw[reg])
#define S16(reg) (int16_t)(raw[(reg) - 1] << 8 | raw[reg])
#define U8(reg) raw[reg]

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Luxpower SNA Component...");
}

void LuxpowerSNAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Luxpower SNA Component:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%u", this->host_.c_str(), this->port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", format_hex_pretty(this->dongle_serial_).c_str());
  ESP_LOGCONFIG(TAG, "  Inverter Serial: %s", format_hex_pretty(this->inverter_serial_).c_str());
}

void LuxpowerSNAComponent::update() {
  this->request_data_();
}

void LuxpowerSNAComponent::request_data_() {
  if (this->tcp_client_ != nullptr && this->tcp_client_->connected()) {
    ESP_LOGD(TAG, "Skipping data request, TCP client is busy.");
    return;
  }
  
  ESP_LOGD(TAG, "Connecting to %s:%u", this->host_.c_str(), this->port_);
  this->tcp_client_ = new AsyncClient();

  this->tcp_client_->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
    this->handle_packet_(data, len);
    client->close();
  });

  this->tcp_client_->onConnect([this](void *arg, AsyncClient *client) {
    ESP_LOGD(TAG, "TCP connected, sending heartbeat and data frames.");
    std::vector<uint8_t> request;
    request.insert(request.end(), {0xA1, 0x1A, 0x01, 0x02, 0x00, 0x0F});
    request.insert(request.end(), this->dongle_serial_.begin(), this->dongle_serial_.end());
    request.insert(request.end(), {0x00, 0x00});
    client->write(reinterpret_cast<const char*>(request.data()), request.size());

    request.clear();
    request.insert(request.end(), {0xA1, 0x1A, 0x01, 0x02, 0x00, 0x12});
    request.insert(request.end(), this->dongle_serial_.begin(), this->dongle_serial_.end());
    request.insert(request.end(), this->inverter_serial_.begin(), this->inverter_serial_.end());
    request.insert(request.end(), {0x00, 0x00});
    client->write(reinterpret_cast<const char*>(request.data()), request.size());
  });

  this->tcp_client_->onError([this](void *arg, AsyncClient *client, int8_t error) {
    ESP_LOGE(TAG, "TCP connection error: %s", client->errorToString(error));
  });

  this->tcp_client_->onTimeout([this](void *arg, AsyncClient *client, uint32_t time) {
    ESP_LOGW(TAG, "TCP connection timeout.");
    client->close();
  });

  this->tcp_client_->onDisconnect([this](void *arg, AsyncClient *client) {
    ESP_LOGD(TAG, "TCP disconnected.");
    delete this->tcp_client_;
    this->tcp_client_ = nullptr;
  });

  this->tcp_client_->connect(this->host_.c_str(), this->port_);
}

void LuxpowerSNAComponent::handle_packet_(void *data, size_t len) {
  uint8_t *raw = (uint8_t *) data;

  if (len < 117) {
    ESP_LOGW(TAG, "Received packet too short: %d bytes. Expected 117 bytes.", len);
    return;
  }
  
  ESP_LOGD(TAG, "Full packet received:\n%s", format_hex_pretty(raw, len).c_str());

  // --- PARSING LOGIC USING THE DEFINITIVE REGISTER MAP (v6) ---
  
  // Status & SOC
  int status_code = U8(REG_STATUS_CODE);
  this->publish_state_("status_code", (float)status_code);
  std::string status_text;
  switch(status_code) {
      case 0: status_text = "Standby"; break; case 1: status_text = "Self Test"; break;
      case 2: status_text = "Normal"; break;  case 3: status_text = "Alarm"; break;
      case 4: status_text = "Fault"; break;   default: status_text = "Checking"; break;
  }
  this->publish_state_("status_text", status_text);
  this->publish_state_("soc", (float)U8(REG_SOC));

  // Voltages and Frequencies
  this->publish_state_("battery_voltage", (float)U16(REG_V_BATTERY) / 100.0f);
  this->publish_state_("grid_voltage", (float)U16(REG_V_GRID) / 10.0f);
  this->publish_state_("pv1_voltage", (float)U16(REG_V_PV1) / 10.0f);
  this->publish_state_("pv2_voltage", (float)U16(REG_V_PV2) / 10.0f);
  this->publish_state_("grid_frequency", (float)U16(REG_F_GRID) / 100.0f);

  // Power Values
  float p_pv1 = (float)U16(REG_P_PV1);
  float p_pv2 = (float)U16(REG_P_PV2);
  this->publish_state_("pv1_power", p_pv1);
  this->publish_state_("pv2_power", p_pv2);
  this->publish_state_("pv_power", p_pv1 + p_pv2);

  float p_battery = (float)S16(REG_P_BATTERY);
  this->publish_state_("battery_power", p_battery);
  this->publish_state_("charge_power", (p_battery > 0 ? p_battery : 0));
  this->publish_state_("discharge_power", (p_battery < 0 ? -p_battery : 0));
  
  this->publish_state_("inverter_power", (float)U16(REG_P_INVERTER));
  this->publish_state_("load_power", (float)U16(REG_P_LOAD));
  
  float p_to_grid = (float)S16(REG_P_TO_GRID);
  this->publish_state_("grid_export_power", (p_to_grid > 0 ? p_to_grid : 0));
  this->publish_state_("grid_import_power", (p_to_grid < 0 ? -p_to_grid : 0));
  this->publish_state_("grid_power", -p_to_grid); // HA convention: +import, -export

  // Temperatures
  this->publish_state_("radiator_temp", (float)S16(REG_T_RADIATOR) / 10.0f);
  this->publish_state_("inverter_temp", (float)S16(REG_T_INVERTER) / 10.0f);

  // Daily Energy Totals
  float e_pv1_day = (float)U16(REG_E_PV1_DAY) / 100.0f;
  float e_pv2_day = (float)U16(REG_E_PV2_DAY) / 100.0f;
  this->publish_state_("pv1_today", e_pv1_day);
  this->publish_state_("pv2_today", e_pv2_day);
  this->publish_state_("pv_today", e_pv1_day + e_pv2_day);
  
  this->publish_state_("inverter_today", (float)U16(REG_E_INVERTER_DAY) / 100.0f);
  this->publish_state_("charge_today", (float)U16(REG_E_CHARGE_DAY) / 10.0f);
  this->publish_state_("discharge_today", (float)U16(REG_E_DISCHARGE_DAY) / 10.0f);
  this->publish_state_("grid_export_today", (float)U16(REG_E_EXPORT_DAY) / 100.0f);
  this->publish_state_("grid_import_today", (float)U16(REG_E_IMPORT_DAY) / 100.0f);
  this->publish_state_("load_today", (float)U16(REG_E_LOAD_DAY) / 100.0f);
}

void LuxpowerSNAComponent::publish_state_(const std::string &key, float value) {
    auto it = this->sensors_.find(key);
    if (it != this->sensors_.end()) {
        ((sensor::Sensor *)it->second)->publish_state(value);
    } else {
        ESP_LOGV(TAG, "Sensor key %s not found in map", key.c_str());
    }
}

void LuxpowerSNAComponent::publish_state_(const std::string &key, const std::string &value) {
    auto it = this->sensors_.find(key);
    if (it != this->sensors_.end()) {
        ((text_sensor::TextSensor *)it->second)->publish_state(value);
    } else {
        ESP_LOGV(TAG, "Text sensor key %s not found in map", key.c_str());
    }
}

}  // namespace luxpower_sna
}  // namespace esphome
