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
// == v9 - This version is identical to v8 but adds logging to the request_data_() function
// ==      to show the exact commands being sent to the inverter.
// =================================================================================================
enum LuxpowerRegister {
  // Note: Offsets are 1-based for readability, matching byte numbers in a hex editor.
  // The U16/S16/U8 macros handle the 0-based array access internally.

  // --- Mapped from LXPPacket.py get_device_values_bank0() ---
  REG_STATUS_CODE   = 45,  // U16, from get(0)
  REG_V_PV1         = 35,  // U16 / 10.0f, from get(1)
  REG_V_PV2         = 37,  // U16 / 10.0f, from get(2)
  REG_V_BATTERY     = 41,  // U16 / 10.0f, from get(4)
  REG_SOC           = 43,  // U8, from get(5)[0] - Note: this is a single byte
  REG_P_PV1         = 47,  // U16, from get(7)
  REG_P_PV2         = 49,  // U16, from get(8)
  REG_P_CHARGE      = 53,  // U16, from get(10)
  REG_P_DISCHARGE   = 55,  // U16, from get(11)
  REG_V_GRID        = 57,  // U16 / 10.0f, from get(12)
  REG_F_GRID        = 63,  // U16 / 100.0f, from get(15)
  REG_P_INVERTER    = 65,  // U16, from get(16) (Inverter power)
  REG_P_TO_GRID     = 85,  // S16, from get(26) (Export power)
  REG_P_LOAD        = 87,  // U16, from get(27) (Load power, "p_to_user")
  REG_E_PV1_DAY     = 89,  // U16 / 10.0f, from get(28)
  REG_E_PV2_DAY     = 91,  // U16 / 10.0f, from get(29)
  REG_E_CHARGE_DAY  = 99,  // U16 / 10.0f, from get(33)
  REG_E_DISCHARGE_DAY = 101, // U16 / 10.0f, from get(34)
  REG_E_EXPORT_DAY  = 105, // U16 / 10.0f, from get(36)
  REG_E_IMPORT_DAY  = 107, // U16 / 10.0f, from get(37)

  // --- Mapped from LXPPacket.py get_device_values_bank1() ---
  REG_T_INVERTER    = 73,  // S16 / 10.0f, from get(64)
  REG_T_RADIATOR    = 75,  // S16 / 10.0f, from get(65)
};

// Helper macros to make parsing cleaner and handle 1-based offsets
#define U16(reg) (raw[(reg) - 1] << 8 | raw[reg])
#define S16(reg) (int16_t)(raw[(reg) - 1] << 8 | raw[reg])
#define U8(reg) raw[(reg) - 1] // Use -1 to keep consistency with 1-based enum

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

// ===============================================================================================
// == THIS IS THE MODIFIED FUNCTION WITH ADDED LOGGING
// ===============================================================================================
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
    ESP_LOGD(TAG, "TCP connected, preparing to send frames.");
    
    // --- First Frame (Heartbeat) ---
    std::vector<uint8_t> request;
    request.insert(request.end(), {0xA1, 0x1A, 0x01, 0x02, 0x00, 0x0F});
    request.insert(request.end(), this->dongle_serial_.begin(), this->dongle_serial_.end());
    request.insert(request.end(), {0x00, 0x00});
    
    // ** NEW LOGGING LINE **
    ESP_LOGD(TAG, "Sending Heartbeat Frame: %s", format_hex_pretty(request.data(), request.size()).c_str());
    client->write(reinterpret_cast<const char*>(request.data()), request.size());

    // --- Second Frame (Data Request) ---
    request.clear();
    request.insert(request.end(), {0xA1, 0x1A, 0x01, 0x02, 0x00, 0x12});
    request.insert(request.end(), this->dongle_serial_.begin(), this->dongle_serial_.end());
    request.insert(request.end(), this->inverter_serial_.begin(), this->inverter_serial_.end());
    request.insert(request.end(), {0x00, 0x00});

    // ** NEW LOGGING LINE **
    ESP_LOGD(TAG, "Sending Data Request Frame: %s", format_hex_pretty(request.data(), request.size()).c_str());
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
// ===============================================================================================
// == END OF MODIFIED FUNCTION
// ===============================================================================================

void LuxpowerSNAComponent::handle_packet_(void *data, size_t len) {
  uint8_t *raw = (uint8_t *) data;

  ESP_LOGD(TAG, "Packet received (len: %d): %s", len, format_hex_pretty(raw, len).c_str());

  // === START OF ROBUSTNESS CHECKS ===
  // 1. Check if it's the 117-byte data packet we are looking for.
  if (len != 117) {
    ESP_LOGD(TAG, "Ignoring packet: incorrect length.");
    return;
  }
  // 2. Check for the correct protocol number (0x0102).
  if (raw[2] != 0x01 || raw[3] != 0x02) {
    ESP_LOGD(TAG, "Ignoring packet: incorrect protocol number.");
    return;
  }
  // 3. Check for the correct TCP function (0xC2) and Device function (0x04)
  if (raw[7] != 0xC2 || raw[21] != 0x04) {
    ESP_LOGD(TAG, "Ignoring packet: incorrect function code.");
    return;
  }
  // 4. Check that the data payload starts at register 0.
  if (raw[32] != 0x00 || raw[33] != 0x00) {
    ESP_LOGD(TAG, "Ignoring packet: incorrect starting register.");
    return;
  }
  // === END OF ROBUSTNESS CHECKS ===

  ESP_LOGI(TAG, "Parsing inverter data packet...");

  // --- PARSING LOGIC USING THE DEFINITIVE REGISTER MAP ---
  
  // Status & SOC
  int status_code = U16(REG_STATUS_CODE);
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
  this->publish_state_("battery_voltage", (float)U16(REG_V_BATTERY) / 10.0f);
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

  float p_charge = (float)U16(REG_P_CHARGE);
  float p_discharge = (float)U16(REG_P_DISCHARGE);
  this->publish_state_("charge_power", p_charge);
  this->publish_state_("discharge_power", p_discharge);
  this->publish_state_("battery_power", p_charge - p_discharge);
  
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
  float e_pv1_day = (float)U16(REG_E_PV1_DAY) / 10.0f;
  float e_pv2_day = (float)U16(REG_E_PV2_DAY) / 10.0f;
  this->publish_state_("pv1_today", e_pv1_day);
  this->publish_state_("pv2_today", e_pv2_day);
  this->publish_state_("pv_today", e_pv1_day + e_pv2_day);
  
  this->publish_state_("charge_today", (float)U16(REG_E_CHARGE_DAY) / 10.0f);
  this->publish_state_("discharge_today", (float)U16(REG_E_DISCHARGE_DAY) / 10.0f);
  this->publish_state_("grid_export_today", (float)U16(REG_E_EXPORT_DAY) / 10.0f);
  this->publish_state_("grid_import_today", (float)U16(REG_E_IMPORT_DAY) / 10.0f);
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
