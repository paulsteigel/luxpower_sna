// components/luxpower_sna/luxpower_sna.cpp
#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <vector>

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// =================================================================================================
// == v5 - Correct implementation based on a full analysis of the LXPPacket.py structure.
// == This version correctly identifies the data payload within the full communication frame.
// =================================================================================================

// The absolute byte offset where the sensor data payload begins in the 117-byte packet.
// This is derived from the Python parser: data_frame starts at 20, value starts at 15 within it.
// 20 + 15 = 35. Our array is 0-indexed, so we use 34.
static const int DATA_PAYLOAD_OFFSET = 34; 

// Helper macros to read from the DATA PAYLOAD, not the whole packet.
// 'reg' is the relative register number (0, 1, 2...) used in the Python 'get(reg)' calls.
#define PAYLOAD_U16(payload_ptr, reg) (payload_ptr[(reg) * 2] << 8 | payload_ptr[(reg) * 2 + 1])
#define PAYLOAD_S16(payload_ptr, reg) (int16_t)(PAYLOAD_U16(payload_ptr, reg))

// Forward declaration of the parsing function
void parse_inverter_data_packet(LuxpowerSNAComponent *component, uint8_t *raw, size_t len);

// ... (Component setup and other functions remain the same) ...
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

  // Call the dedicated parsing function
  parse_inverter_data_packet(this, raw, len);
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

// ===============================================================================================
// == THE PARSING FUNCTION - A direct C++ port of the LXPPacket.py logic
// ===============================================================================================
void parse_inverter_data_packet(LuxpowerSNAComponent *component, uint8_t *raw, size_t len) {
  // 1. Verify packet identifiers to ensure we are parsing the correct data frame
  //    TCP Function: 0xC2 (194) -> TRANSLATED_DATA
  //    Device Function: 0x04 -> READ_INPUT
  if (raw[7] != 0xC2 || raw[21] != 0x04) {
    ESP_LOGW(TAG, "Packet is not a READ_INPUT data frame. Skipping parse.");
    return;
  }

  // 2. Create a pointer to the start of the actual sensor data payload
  uint8_t *payload = &raw[DATA_PAYLOAD_OFFSET];

  // 3. Parse and publish each sensor value, using the RELATIVE register number from python
  //    and applying the correct scaling factor. This is a port of get_device_values_bank0().

  // status = self.readValuesInt.get(0)
  int status_code = PAYLOAD_U16(payload, 0);
  component->publish_state_("status_code", (float)status_code);
  std::string status_text;
  switch(status_code) {
      case 0: status_text = "Standby"; break; case 1: status_text = "Self Test"; break;
      case 2: status_text = "Normal"; break;  case 3: status_text = "Alarm"; break;
      case 4: status_text = "Fault"; break;   default: status_text = "Checking"; break;
  }
  component->publish_state_("status_text", status_text);

  // v_pv_1 = self.readValuesInt.get(1, 0) / 10
  component->publish_state_("pv1_voltage", (float)PAYLOAD_U16(payload, 1) / 10.0f);
  // v_pv_2 = self.readValuesInt.get(2, 0) / 10
  component->publish_state_("pv2_voltage", (float)PAYLOAD_U16(payload, 2) / 10.0f);

  // v_bat = self.readValuesInt.get(4, 0) / 10
  component->publish_state_("battery_voltage", (float)PAYLOAD_U16(payload, 4) / 10.0f);
  
  // soc = self.readValues.get(5)[0]  <- This takes the FIRST BYTE of the 16-bit register
  component->publish_state_("soc", (float)payload[5 * 2]);
  
  // p_pv_1 = self.readValuesInt.get(7, 0)
  float p_pv1 = (float)PAYLOAD_U16(payload, 7);
  // p_pv_2 = self.readValuesInt.get(8, 0)
  float p_pv2 = (float)PAYLOAD_U16(payload, 8);
  component->publish_state_("pv1_power", p_pv1);
  component->publish_state_("pv2_power", p_pv2);
  component->publish_state_("pv_power", p_pv1 + p_pv2);

  // p_charge = self.readValuesInt.get(10, 0)
  float p_charge = (float)PAYLOAD_U16(payload, 10);
  // p_discharge = self.readValuesInt.get(11, 0)
  float p_discharge = (float)PAYLOAD_U16(payload, 11);
  component->publish_state_("charge_power", p_charge);
  component->publish_state_("discharge_power", p_discharge);
  component->publish_state_("battery_power", p_charge - p_discharge);

  // v_ac_r = self.readValuesInt.get(12, 0) / 10
  component->publish_state_("grid_voltage", (float)PAYLOAD_U16(payload, 12) / 10.0f);
  // f_ac = self.readValuesInt.get(15, 0) / 100
  component->publish_state_("grid_frequency", (float)PAYLOAD_U16(payload, 15) / 100.0f);

  // p_inv = self.readValuesInt.get(16, 0)
  component->publish_state_("inverter_power", (float)PAYLOAD_U16(payload, 16));

  // p_to_grid = self.readValuesInt.get(26, 0)
  // p_to_user = self.readValuesInt.get(27, 0)
  float p_to_grid = (float)PAYLOAD_S16(payload, 26); // This can be negative for import
  component->publish_state_("grid_export_power", (p_to_grid > 0 ? p_to_grid : 0));
  component->publish_state_("grid_import_power", (p_to_grid < 0 ? -p_to_grid : 0));
  component->publish_state_("grid_power", -p_to_grid); // HA convention: +import, -export
  component->publish_state_("load_power", (float)PAYLOAD_U16(payload, 27));

  // e_pv_1_day = self.readValuesInt.get(28, 0) / 10
  float e_pv1_day = (float)PAYLOAD_U16(payload, 28) / 10.0f;
  // e_pv_2_day = self.readValuesInt.get(29, 0) / 10
  float e_pv2_day = (float)PAYLOAD_U16(payload, 29) / 10.0f;
  component->publish_state_("pv1_today", e_pv1_day);
  component->publish_state_("pv2_today", e_pv2_day);
  component->publish_state_("pv_today", e_pv1_day + e_pv2_day);

  // e_chg_day = self.readValuesInt.get(33, 0) / 10
  component->publish_state_("charge_today", (float)PAYLOAD_U16(payload, 33) / 10.0f);
  // e_dischg_day = self.readValuesInt.get(34, 0) / 10
  component->publish_state_("discharge_today", (float)PAYLOAD_U16(payload, 34) / 10.0f);

  // e_to_grid_day = self.readValuesInt.get(36, 0) / 10
  component->publish_state_("grid_export_today", (float)PAYLOAD_U16(payload, 36) / 10.0f);
  // e_to_user_day = self.readValuesInt.get(37, 0) / 10
  component->publish_state_("grid_import_today", (float)PAYLOAD_U16(payload, 37) / 10.0f);

  // Parse temperatures from get_device_values_bank1()
  // t_inner = self.readValuesInt.get(64, 0)
  // t_rad_1 = self.readValuesInt.get(65, 0)
  // Temperatures are often signed and need scaling
  component->publish_state_("inverter_temp", (float)PAYLOAD_S16(payload, 64) / 10.0f);
  component->publish_state_("radiator_temp", (float)PAYLOAD_S16(payload, 65) / 10.0f);
}

}  // namespace luxpower_sna
}  // namespace esphome
