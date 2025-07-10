// components/luxpower_sna/luxpower_sna.cpp
#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h" // For format_hex_pretty

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// Helper function to publish state to the correct sensor type
void LuxpowerSNAComponent::publish_state_(const std::string &key, float value) {
    if (this->sensors_.count(key)) {
        // Cast the base entity pointer to a sensor pointer
        auto *sensor = (sensor::Sensor *)this->sensors_[key];
        sensor->publish_state(value);
    }
}

void LuxpowerSNAComponent::publish_state_(const std::string &key, const std::string &value) {
    if (this->sensors_.count(key)) {
        // Cast the base entity pointer to a text_sensor pointer
        auto *text_sensor = (text_sensor::TextSensor *)this->sensors_[key];
        text_sensor->publish_state(value);
    }
}

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxpowerSNA Hub...");
}

void LuxpowerSNAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "LuxpowerSNA Hub:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%d", this->host_.c_str(), this->port_);
  LOG_UPDATE_INTERVAL(this);
  
  ESP_LOGCONFIG(TAG, "  Configured Sensors:");
  for (auto const& [key, val] : this->sensors_) {
    ESP_LOGCONFIG(TAG, "    - %s", key.c_str());
  }
}

void LuxpowerSNAComponent::update() {
  // This is called by the PollingComponent scheduler.
  // It will trigger the async request sequence.
  this->request_data_();
}

uint16_t LuxpowerSNAComponent::calculate_crc_(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
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

void LuxpowerSNAComponent::request_data_() {
  // If a connection is already in progress, do nothing.
  if (this->tcp_client_ != nullptr) {
    ESP_LOGD(TAG, "Connection already in progress, skipping new request.");
    return;
  }
  
  ESP_LOGD(TAG, "Connecting to %s:%d", this->host_.c_str(), this->port_);
  this->tcp_client_ = new AsyncClient();

  // --- Define Callbacks ---
  this->tcp_client_->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
    ESP_LOGD(TAG, "Received %d bytes of data.", len);
    this->handle_packet_(data, len);
    client->close(); // We are done, close the connection.
  }, nullptr);

  this->tcp_client_->onConnect([this](void *arg, AsyncClient *client) {
    ESP_LOGD(TAG, "Successfully connected to host.");
    // --- Build and send the request packet ---
    uint8_t request[29];
    request[0] = 0xAA; request[1] = 0x55; request[2] = 0x12; request[3] = 0x00;
    request[4] = 0x01; request[5] = 0xC1; request[6] = 20; // Action: Read
    memcpy(request + 7, this->dongle_serial_.data(), 10);
    memcpy(request + 17, this->inverter_serial_.data(), 10);
    uint16_t crc = this->calculate_crc_(request + 2, 25);
    request[27] = crc & 0xFF;
    request[28] = (crc >> 8) & 0xFF;
      
    ESP_LOGD(TAG, "Sending data request (29 bytes)...");
    client->write((char*)request, 29);
  }, nullptr);

  this->tcp_client_->onError([this](void *arg, AsyncClient *client, int8_t error) {
    ESP_LOGW(TAG, "Connection error: %s", client->errorToString(error));
    delete this->tcp_client_;
    this->tcp_client_ = nullptr;
  }, nullptr);

  this->tcp_client_->onTimeout([this](void *arg, AsyncClient *client, uint32_t time) {
    ESP_LOGW(TAG, "Connection timeout");
    delete this->tcp_client_;
    this->tcp_client_ = nullptr;
  }, nullptr);

  this->tcp_client_->onDisconnect([this](void *arg, AsyncClient *client) {
    ESP_LOGD(TAG, "Disconnected from host.");
    delete this->tcp_client_;
    this->tcp_client_ = nullptr;
  }, nullptr);

  // --- Initiate the connection ---
  if (!this->tcp_client_->connect(this->host_.c_str(), this->port_)) {
    ESP_LOGW(TAG, "Failed to initiate connection.");
    delete this->tcp_client_;
    this->tcp_client_ = nullptr;
  }
}

void LuxpowerSNAComponent::handle_packet_(void *data, size_t len) {
  uint8_t *raw = (uint8_t *) data;

  if (len < 129) { // The full data packet is large, let's check for a reasonable minimum
    ESP_LOGW(TAG, "Received packet too short: %d bytes. Full data may not be available.", len);
    // You can decide to return here or try to parse what you can
  }
  
  ESP_LOGV(TAG, "Full packet received:\n%s", format_hex_pretty(raw, len).c_str());

  // --- Parse all possible values from the data packet ---
  // Note: These offsets are based on reverse-engineering and need to be verified.
  this->publish_state_("battery_voltage", (float)(raw[11] << 8 | raw[12]) / 10.0f);
  this->publish_state_("soc", (float)raw[15]);
  this->publish_state_("battery_power", (float)(raw[13] << 8 | raw[14])); // Example: may need adjustment for charge/discharge
  this->publish_state_("charge_power", (float)(raw[13] << 8 | raw[14])); // Placeholder, needs logic
  this->publish_state_("discharge_power", (float)0); // Placeholder, needs logic
  this->publish_state_("pv_power", (float)((raw[21] << 8 | raw[22]) + (raw[25] << 8 | raw[26])));
  this->publish_state_("inverter_power", (float)(raw[31] << 8 | raw[32]));
  this->publish_state_("grid_power", (float)(raw[33] << 8 | raw[34]));
  this->publish_state_("load_power", (float)(raw[35] << 8 | raw[36]));
  this->publish_state_("eps_power", (float)(raw[37] << 8 | raw[38]));
  
  this->publish_state_("pv1_voltage", (float)(raw[19] << 8 | raw[20]) / 10.0f);
  this->publish_state_("pv1_power", (float)(raw[21] << 8 | raw[22]));
  this->publish_state_("pv2_voltage", (float)(raw[23] << 8 | raw[24]) / 10.0f);
  this->publish_state_("pv2_power", (float)(raw[25] << 8 | raw[26]));
  
  this->publish_state_("grid_voltage", (float)(raw[27] << 8 | raw[28]) / 10.0f);
  this->publish_state_("grid_frequency", (float)(raw[29] << 8 | raw[30]) / 100.0f);
  this->publish_state_("power_factor", (float)(raw[41] << 8 | raw[42]) / 1000.0f);
  this->publish_state_("eps_voltage", (float)(raw[39] << 8 | raw[40]) / 10.0f);
  // EPS Frequency is often the same as grid, might not be in the packet. Placeholder.
  this->publish_state_("eps_frequency", (float)(raw[29] << 8 | raw[30]) / 100.0f);

  // Daily Energy Values
  this->publish_state_("pv_today", (float)(raw[59] << 8 | raw[60]) / 10.0f);
  this->publish_state_("inverter_today", (float)(raw[61] << 8 | raw[62]) / 10.0f);
  this->publish_state_("charge_today", (float)(raw[63] << 8 | raw[64]) / 10.0f);
  this->publish_state_("discharge_today", (float)(raw[65] << 8 | raw[66]) / 10.0f);
  this->publish_state_("grid_export_today", (float)(raw[67] << 8 | raw[68]) / 10.0f);
  this->publish_state_("grid_import_today", (float)(raw[69] << 8 | raw[70]) / 10.0f);
  this->publish_state_("load_today", (float)(raw[71] << 8 | raw[72]) / 10.0f);
  this->publish_state_("eps_today", (float)(raw[73] << 8 | raw[74]) / 10.0f);

  // Temperatures
  this->publish_state_("inverter_temp", (float)(raw[45] << 8 | raw[46]) / 10.0f);
  this->publish_state_("radiator_temp", (float)(raw[47] << 8 | raw[48]) / 10.0f);
  this->publish_state_("battery_temp", (float)(raw[49] << 8 | raw[50]) / 10.0f);

  // Status
  int status_code = raw[51];
  this->publish_state_("status_code", (float)status_code);
  // You would add a switch statement here to map the code to a text string
  std::string status_text = "Unknown";
  switch(status_code) {
      case 0: status_text = "Standby"; break;
      case 1: status_text = "Self Test"; break;
      case 2: status_text = "Normal"; break;
      case 3: status_text = "Alarm"; break;
      case 4: status_text = "Fault"; break;
  }
  this->publish_state_("status_text", status_text);

}

}  // namespace luxpower_sna
}  // namespace esphome
