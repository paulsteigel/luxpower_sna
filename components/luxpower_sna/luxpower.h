#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <vector>
#include "ESPAsyncTCP.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

class LuxpowerSnaComponent : public PollingComponent {
 public:
  // Constructor
  LuxpowerSnaComponent() = default;

  // ========== ESPHome Core Methods ==========
  void setup() override {
    ESP_LOGCONFIG(TAG, "Setting up Luxpower SNA Component...");
    this->client_ = new AsyncClient();

    // Set up callbacks for the TCP client
    this->client_->onConnect([this](void *arg, AsyncClient *client) {
      ESP_LOGI(TAG, "Connected to inverter at %s:%d", this->host_.c_str(), this->port_);
      this->is_connected_ = true;
      // On successful connection, immediately trigger the first update to send a request
      this->update();
    });

    this->client_->onDisconnect([this](void *arg, AsyncClient *client) {
      ESP_LOGW(TAG, "Disconnected from inverter.");
      this->is_connected_ = false;
    });

    this->client_->onError([this](void *arg, AsyncClient *client, int8_t error) {
      ESP_LOGE(TAG, "Connection error: %s", client->errorToString(error));
      this->is_connected_ = false;
    });

    // This is the most important part for our test: what to do when data arrives
    this->client_->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
      ESP_LOGI(TAG, "Received %d bytes from inverter.", len);
      
      // Log the raw data in hex format for debugging
      std::string raw_data_hex = "";
      for (size_t i = 0; i < len; i++) {
        char hex_byte[4];
        sprintf(hex_byte, "%02X ", ((uint8_t*)data)[i]);
        raw_data_hex += hex_byte;
      }
      ESP_LOGD(TAG, "Received raw data: %s", raw_data_hex.c_str());

      // Future work: this is where we will call the parsing function
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
    ESP_LOGCONFIG(TAG, "  Update Interval: %.2f seconds", this->get_update_interval() / 1000.0);
  }

  // ========== Configuration Setters from YAML ==========
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::vector<uint8_t> &serial) { this->dongle_serial_ = serial; }
  void set_inverter_serial(const std::vector<uint8_t> &serial) { this->inverter_serial_ = serial; }

 protected:
  // ========== Core Logic Methods ==========
  void connect_to_inverter() {
    if (this->is_connected_ || this->client_->connected()) {
      return; // Already connected or connecting
    }
    ESP_LOGD(TAG, "Attempting to connect to %s:%d", this->host_.c_str(), this->port_);
    this->client_->connect(this->host_.c_str(), this->port_);
  }

  void request_test_data() {
    if (!this->is_connected_) return;
    
    // Let's request the first bank of data registers (start=0, count=40)
    // This is a common request that should always get a response.
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

  // ========== Packet Creation and CRC (Translated from LXPPacket.py) ==========
  std::vector<uint8_t> prepare_packet_for_read(uint16_t start_register, uint16_t num_registers, uint8_t function) {
    std::vector<uint8_t> packet;
    std::vector<uint8_t> data_frame;

    // Data frame first (inner part of the packet)
    data_frame.push_back(0); // ACTION_WRITE (Python code uses this for reads too)
    data_frame.push_back(function);
    data_frame.insert(data_frame.end(), this->inverter_serial_.begin(), this->inverter_serial_.end());
    data_frame.push_back(start_register & 0xFF);
    data_frame.push_back(start_register >> 8);
    data_frame.push_back(num_registers & 0xFF);
    data_frame.push_back(num_registers >> 8);
    
    uint16_t crc = compute_crc(data_frame);

    // Main packet (outer wrapper)
    packet.push_back(0xA1); packet.push_back(0x1A); // Prefix
    packet.push_back(0x02); packet.push_back(0x00); // Protocol
    uint16_t frame_length = 14 + data_frame.size();
    packet.push_back(frame_length & 0xFF); packet.push_back(frame_length >> 8);
    packet.push_back(0x01); // Unknown byte
    packet.push_back(194);  // TCP_FUNCTION_TRANSLATED_DATA
    packet.insert(packet.end(), this->dongle_serial_.begin(), this->dongle_serial_.end());
    uint16_t data_length = 2 + data_frame.size();
    packet.push_back(data_length & 0xFF); packet.push_back(data_length >> 8);
    
    // Add the data frame and CRC to the main packet
    packet.insert(packet.end(), data_frame.begin(), data_frame.end());
    packet.push_back(crc & 0xFF);
    packet.push_back(crc >> 8);

    return packet;
  }

  uint16_t compute_crc(const std::vector<uint8_t> &data) {
    uint16_t crc = 0xFFFF;
    for (uint8_t byte : data) {
      crc ^= byte;
      for (int i = 0; i < 8; i++) {
        if (crc & 1) {
          crc >>= 1;
          crc ^= 0xA001;
        } else {
          crc >>= 1;
        }
      }
    }
    return crc;
  }

  // ========== Member Variables ==========
  std::string host_;
  uint16_t port_;
  std::vector<uint8_t> dongle_serial_;
  std::vector<uint8_t> inverter_serial_;

  AsyncClient *client_{nullptr};
  bool is_connected_{false};
};

}  // namespace luxpower_sna
}  // namespace esphome
