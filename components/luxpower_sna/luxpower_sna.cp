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
  for (auto const& [key, val] : this->sensors_) {
    LOG_SENSOR("  ", key.c_str(), val);
  }
}

void LuxpowerSNAComponent::update() {
  this->request_data_();
}

void LuxpowerSNAComponent::add_sensor(const std::string &sensor_type, sensor::Sensor *sensor_obj) {
    this->sensors_[sensor_type] = sensor_obj;
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

  if (!this->tcp_client_->connect(this->host_.c_str(), this->port_)) {
    ESP_LOGW(TAG, "Failed to initiate connection.");
    delete this->tcp_client_;
    this->tcp_client_ = nullptr;
    return;
  }

  uint8_t request[29];
  request[0] = 0xAA; request[1] = 0x55; request[2] = 0x01; request[3] = 0x1A;
  request[4] = 0x01; request[5] = 0x02; request[6] = 20;
  memcpy(request + 7, this->dongle_serial_.data(), 10);
  memcpy(request + 17, this->inverter_serial_.data(), 10);
  uint16_t crc = this->calculate_crc_(request + 2, 25);
  request[27] = crc & 0xFF;
  request[28] = (crc >> 8) & 0xFF;
  
  this->tcp_client_->write((char*)request, 29);
  ESP_LOGD(TAG, "Data request (29 bytes) sent.");
}

void LuxpowerSNAComponent::handle_packet_(void *arg, AsyncClient *client, void *data, size_t len) {
  if (len < 61) {
    ESP_LOGW(TAG, "Received packet too short: %d bytes", len);
    return;
  }

  uint8_t *raw = (uint8_t *) data;
  ESP_LOGD(TAG, "Received %d bytes of data.", len);

  // Parse all possible values
  float battery_voltage = (raw[11] << 8 | raw[12]) / 10.0f;
  float battery_current = (raw[13] << 8 | raw[14]) / 10.0f;
  float battery_capacity_ah = raw[19];
  float power_from_grid = (raw[43] << 8 | raw[44]);
  float daily_solar_gen = (raw[59] << 8 | raw[60]) / 10.0f;

  // Publish to a sensor ONLY if it has been registered
  if (this->sensors_.count("battery_voltage")) {
    this->sensors_["battery_voltage"]->publish_state(battery_voltage);
  }
  if (this->sensors_.count("battery_current")) {
    this->sensors_["battery_current"]->publish_state(battery_current);
  }
  if (this->sensors_.count("battery_capacity_ah")) {
    this->sensors_["battery_capacity_ah"]->publish_state(battery_capacity_ah);
  }
  if (this->sensors_.count("power_from_grid")) {
    this->sensors_["power_from_grid"]->publish_state(power_from_grid);
  }
  if (this->sensors_.count("daily_solar_generation")) {
    this->sensors_["daily_solar_generation"]->publish_state(daily_solar_gen);
  }
}

}  // namespace luxpower_sna
}  // namespace esphome
