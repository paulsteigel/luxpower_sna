// components/luxpower_sna/luxpower_sna.cpp
#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <vector> // Ensure vector is included

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// =================================================================================================
// == REGISTER MAP for 117-byte data packet
// =================================================================================================
enum LuxpowerRegister {
  REG_STATUS_CODE = 45, REG_POWER_FACTOR = 47, REG_SOC = 46, REG_BATTERY_VOLTAGE = 35,
  REG_GRID_VOLTAGE = 39, REG_GRID_FREQUENCY = 71, REG_PV1_VOLTAGE = 53, REG_PV2_VOLTAGE = 57,
  REG_BATTERY_POWER = 37, REG_PV1_POWER = 51, REG_PV2_POWER = 55, REG_GRID_POWER = 59,
  REG_INVERTER_POWER = 61, REG_LOAD_POWER = 63, REG_RADIATOR_TEMP = 65, REG_INVERTER_TEMP = 67,
  REG_PV_TODAY = 89, REG_LOAD_TODAY = 91, REG_CHARGE_TODAY = 95, REG_GRID_EXPORT_TODAY = 99,
  REG_GRID_IMPORT_TODAY = 101,
};

// Helper macros to make parsing cleaner
#define U16(reg) (raw[(reg) - 1] << 8 | raw[reg])
#define S16(reg) (int16_t)(raw[(reg) - 1] << 8 | raw[reg])
#define U8(reg) raw[reg]

// =================================================================================================


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

  // CORRECTED onData LAMBDA SIGNATURE
  this->tcp_client_->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
    this->handle_packet_(data, len);
    client->close(); // Use the client pointer from the callback
  });

  this->tcp_client_->onConnect([this](void *arg, AsyncClient *client) {
    ESP_LOGD(TAG, "TCP connected, sending heartbeat and data frames.");
    // Heartbeat Frame
    std::vector<uint8_t> request;
    request.insert(request.end(), {0xA1, 0x1A, 0x01, 0x02, 0x00, 0x0F});
    request.insert(request.end(), this->dongle_serial_.begin(), this->dongle_serial_.end());
    request.insert(request.end(), {0x00, 0x00});
    client->write(reinterpret_cast<const char*>(request.data()), request.size());

    // Data Frame
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

  // --- PARSING LOGIC USING THE REGISTER MAP ---
  this->publish_state_("battery_voltage", (float)U16(REG_BATTERY_VOLTAGE) / 100.0f);
  this->publish_state_("soc", (float)U8(REG_SOC));
  this->publish_state_("grid_voltage", (float)U16(REG_GRID_VOLTAGE) / 10.0f);
  this->publish_state_("grid_frequency", (float)U16(REG_GRID_FREQUENCY) / 100.0f);
  this->publish_state_("power_factor", (float)U8(REG_POWER_FACTOR) / 100.0f);

  int16_t battery_power_raw = S16(REG_BATTERY_POWER);
  this->publish_state_("battery_power", (float)battery_power_raw);
  this->publish_state_("charge_power", (float)(battery_power_raw > 0 ? battery_power_raw : 0));
  this->publish_state_("discharge_power", (float)(battery_power_raw < 0 ? -battery_power_raw : 0));

  float pv1_power = (float)U16(REG_PV1_POWER);
  float pv2_power = (float)U16(REG_PV2_POWER);
  this->publish_state_("pv1_power", pv1_power);
  this->publish_state_("pv2_power", pv2_power);
  this->publish_state_("pv_power", pv1_power + pv2_power);

  int16_t grid_power_raw = S16(REG_GRID_POWER);
  this->publish_state_("grid_power", (float)grid_power_raw);
  this->publish_state_("grid_import_power", (float)(grid_power_raw > 0 ? grid_power_raw : 0));
  this->publish_state_("grid_export_power", (float)(grid_power_raw < 0 ? -grid_power_raw : 0));

  this->publish_state_("load_power", (float)U16(REG_LOAD_POWER));
  this->publish_state_("inverter_power", (float)U16(REG_INVERTER_POWER));
  this->publish_state_("eps_power", (float)U16(REG_INVERTER_POWER));

  this->publish_state_("pv1_voltage", (float)U16(REG_PV1_VOLTAGE) / 10.0f);
  this->publish_state_("pv2_voltage", (float)U16(REG_PV2_VOLTAGE) / 10.0f);
  
  this->publish_state_("radiator_temp", (float)U16(REG_RADIATOR_TEMP) / 10.0f);
  this->publish_state_("inverter_temp", (float)U16(REG_INVERTER_TEMP) / 10.0f);
  
  this->publish_state_("pv_today", (float)U16(REG_PV_TODAY) / 10.0f);
  this->publish_state_("load_today", (float)U16(REG_LOAD_TODAY) / 10.0f);
  this->publish_state_("charge_today", (float)U16(REG_CHARGE_TODAY) / 10.0f);
  this->publish_state_("grid_import_today", (float)U16(REG_GRID_IMPORT_TODAY) / 10.0f);
  this->publish_state_("grid_export_today", (float)U16(REG_GRID_EXPORT_TODAY) / 10.0f);

  int status_code = U8(REG_STATUS_CODE);
  this->publish_state_("status_code", (float)status_code);
  std::string status_text;
  switch(status_code) {
      case 0: status_text = "Standby"; break;
      case 1: status_text = "Self Test"; break;
      case 2: status_text = "Normal"; break;
      case 3: status_text = "Alarm"; break;
      case 4: status_text = "Fault"; break;
      default: status_text = "Checking"; break;
  }
  this->publish_state_("status_text", status_text);
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
