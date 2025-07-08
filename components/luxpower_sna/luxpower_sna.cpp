// components/luxpower_sna/luxpower_sna.cpp
#include "luxpower_sna.h"
#include "esphome/core/log.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxpowerSNA Hub...");
}

void LuxpowerSNAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "LuxpowerSNA Hub:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%d", this->host_.c_str(), this->port_);
  LOG_UPDATE_INTERVAL(this);
  // Log sensors
  LOG_SENSOR("  ", "Battery Voltage", this->battery_voltage_sensor_);
  LOG_SENSOR("  ", "Battery Current", this->battery_current_sensor_);
  LOG_SENSOR("  ", "Battery Capacity AH", this->battery_capacity_ah_sensor_);
  LOG_SENSOR("  ", "Power from Grid", this->power_from_grid_sensor_);
  LOG_SENSOR("  ", "Daily Solar Generation", this->daily_solar_generation_sensor_);
}

void LuxpowerSNAComponent::update() {
  this->request_data_();
}

template<typename T> void LuxpowerSNAComponent::publish_state(sensor::Sensor *sensor, T value) {
  if (sensor != nullptr) {
    sensor->publish_state(value);
  }
}

void LuxpowerSNAComponent::request_data_() {
  if (this->tcp_client_ != nullptr && (this->tcp_client_->connected() || this->tcp_client_->connecting())) {
    ESP_LOGD(TAG, "Connection in progress, skipping new request.");
    return;
  }
  
  ESP_LOGD(TAG, "Connecting to %s:%d", this->host_.c_str(), this->port_);
  this->tcp_client_ = new AsyncClient();

  this->tcp_client_->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
    this->handle_packet_(arg, client, data, len);
    client->close();
    delete this->tcp_client_;
    this->tcp_client_ = nullptr;
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

  // --- THE FIX: Corrected .c_c_str() to .c_str() ---
  if (!this->tcp_client_->connect(this->host_.c_str(), this->port_)) {
    ESP_LOGW(TAG, "Failed to initiate connection.");
    delete this->tcp_client_;
    this->tcp_client_ = nullptr;
    return;
  }

  // Simplified request packet (25 bytes)
  uint8_t request[25] = {0xAA, 0x55, 0x01, 0x1A, 0x01, 0x02, 0x19};
  memcpy(request + 7, this->dongle_serial_.data(), 10);
  memcpy(request + 17, this->inverter_serial_.data(), 10);
  
  this->tcp_client_->write((char*)request, 25);
  ESP_LOGD(TAG, "Data request sent.");
}

void LuxpowerSNAComponent::handle_packet_(void *arg, AsyncClient *client, void *data, size_t len) {
  if (len < 61) {
    ESP_LOGW(TAG, "Received packet too short: %d bytes", len);
    return;
  }

  uint8_t *raw = (uint8_t *) data;
  ESP_LOGD(TAG, "Received %d bytes of data.", len);

  // --- Parse data from packet ---
  float battery_voltage = (raw[11] << 8 | raw[12]) / 10.0f;
  float battery_current = (raw[13] << 8 | raw[14]) / 10.0f;
  float battery_capacity_ah = raw[19];
  float power_from_grid = (raw[43] << 8 | raw[44]);
  float daily_solar_gen = (raw[59] << 8 | raw[60]) / 10.0f;

  ESP_LOGD(TAG, "Parsed Data: V=%.1fV, I=%.1fA, Cap=%.0fAh, Grid=%.0fW, Solar=%.1fkWh", 
           battery_voltage, battery_current, battery_capacity_ah, power_from_grid, daily_solar_gen);

  // --- Publish to registered sensors using the safe helper ---
  this->publish_state(this->battery_voltage_sensor_, battery_voltage);
  this->publish_state(this->battery_current_sensor_, battery_current);
  this->publish_state(this->battery_capacity_ah_sensor_, battery_capacity_ah);
  this->publish_state(this->power_from_grid_sensor_, power_from_grid);
  this->publish_state(this->daily_solar_generation_sensor_, daily_solar_gen);
}

}  // namespace luxpower_sna
}  // namespace esphome
