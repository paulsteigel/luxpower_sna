#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include <vector>
#include "ESPAsyncTCP.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

class LuxpowerInverterComponent : public PollingComponent {
 public:
  void setup() override {
    ESP_LOGCONFIG(TAG, "Setting up Luxpower SNA Component...");
    this->client_ = new AsyncClient();

    this->client_->onConnect([this](void *arg, AsyncClient *client) {
      ESP_LOGI(TAG, "Connected to inverter at %s:%d", this->host_.c_str(), this->port_);
      this->is_connected_ = true;
      this->update(); // Trigger first update on connect
    });

    this->client_->onDisconnect([this](void *arg, AsyncClient *client) {
      ESP_LOGW(TAG, "Disconnected from inverter.");
      this->is_connected_ = false;
    });

    this->client_->onError([this](void *arg, AsyncClient *client, int8_t error) {
      ESP_LOGE(TAG, "Connection error: %s", client->errorToString(error));
      this->is_connected_ = false;
    });

    // THIS IS THE MOST IMPORTANT PART FOR OUR TEST
    this->client_->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
      ESP_LOGI(TAG, "Received %d bytes from inverter.", len);
      
      std::string raw_data_hex = "";
      for (size_t i = 0; i < len; i++) {
        char hex_byte[4];
        sprintf(hex_byte, "%02X ", ((uint8_t*)data)[i]);
        raw_data_hex += hex_byte;
      }
      ESP_LOGD(TAG, "Received raw data: %s", raw_data_hex.c_str());

      // We will add parsing logic here later. For now, we just log.
    });
  }

  void update() override {
    if (!this->is_connected_) {
      ESP_LOGD(TAG, "Not connected. Attempting to connect...");
      this->connect_to_inverter();
      return;
    }
    
    ESP_LOGD(TAG, "Polling update: Requesting test data from inverter.");
    this->request_test_data();
  }

  void dump_config() override {
    ESP_LOGCONFIG(TAG, "Luxpower SNA Component:");
    ESP_LOGCONFIG(TAG, "  Host: %s:%d", this->host_.c_str(), this->port_);
  }

  // Setters from YAML
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::vector<uint8_t> &serial) { this->dongle_serial_ = serial; }
  void set_inverter_serial_number(const std::vector<uint8_t> &serial) { this->inverter_serial_ = serial; }

 protected:
  void connect_to_inverter() {
    if (this->is_connected_ || this->client_->connected()) return;
    this->client_->connect(this->host_.c_str(), this->port_);
  }

  void request_test_data() {
    if (!this->is_connected_) return;
    
    uint16_t start_register = 0;
    uint16_t num_registers = 40;
    uint8_t function = 4; // READ_INPUT
    
    auto packet = this->prepare_packet_for_read(start_register, num_registers, function);
    
    if (this->client_->space() > packet.size() && this->client_->canSend()) {
      this->client_->write((const char*)packet.data(), packet.size());
      ESP_LOGD(TAG, "Sent test data request (start=%d, count=%d)", start_register, num_registers);
    } else {
      ESP_LOGW(TAG, "Cannot send data request, client buffer full or not ready.");
    }
  }

  std::vector<uint8_t> prepare_packet_for_read(uint16_t start, uint16_t count, uint8_t func) {
    std::vector<uint8_t> packet, data_frame;
    data_frame.push_back(0); data_frame.push_back(func);
    data_frame.insert(data_frame.end(), this->inverter_serial_.begin(), this->inverter_serial_.end());
    data_frame.push_back(start & 0xFF); data_frame.push_back(start >> 8);
    data_frame.push_back(count & 0xFF); data_frame.push_back(count >> 8);
    uint16_t crc = compute_crc(data_frame);
    packet.push_back(0xA1); packet.push_back(0x1A);
    packet.push_back(0x02); packet.push_back(0x00);
    uint16_t frame_len = 14 + data_frame.size();
    packet.push_back(frame_len & 0xFF); packet.push_back(frame_len >> 8);
    packet.push_back(0x01); packet.push_back(194);
    packet.insert(packet.end(), this->dongle_serial_.begin(), this->dongle_serial_.end());
    uint16_t data_len = 2 + data_frame.size();
    packet.push_back(data_len & 0xFF); packet.push_back(data_len >> 8);
    packet.insert(packet.end(), data_frame.begin(), data_frame.end());
    packet.push_back(crc & 0xFF); packet.push_back(crc >> 8);
    return packet;
  }

  uint16_t compute_crc(const std::vector<uint8_t> &data) {
    uint16_t crc = 0xFFFF;
    for (uint8_t byte : data) {
      crc ^= byte;
      for (int i = 0; i < 8; i++) {
        if (crc & 1) { crc >>= 1; crc ^= 0xA001; } else { crc >>= 1; }
      }
    }
    return crc;
  }

  std::string host_;
  uint16_t port_;
  std::vector<uint8_t> dongle_serial_;
  std::vector<uint8_t> inverter_serial_;
  AsyncClient *client_{nullptr};
  bool is_connected_{false};
};

}  // namespace luxpower_sna
}  // namespace esphome
