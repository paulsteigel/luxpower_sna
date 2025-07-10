// components/luxpower_sna/luxpower_sna.cpp
#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// Helper to publish to a regular sensor
void LuxpowerSNAComponent::publish_state_(const std::string &key, float value) {
    if (this->sensors_.count(key)) {
        auto *sensor = (sensor::Sensor *)this->sensors_[key];
        sensor->publish_state(value);
    }
}

// Helper to publish to a text sensor
void LuxpowerSNAComponent::publish_state_(const std::string &key, const std::string &value) {
    if (this->sensors_.count(key)) {
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

// This is called by the PollingComponent scheduler.
void LuxpowerSNAComponent::update() {
  // It will trigger a request for the next bank in the sequence.
  this->request_bank_(this->next_bank_to_request_);
  // Cycle to the next bank for the subsequent poll.
  this->next_bank_to_request_ = (this->next_bank_to_request_ == 80) ? 0 : this->next_bank_to_request_ + 40;
}

// CRC-16 (Modbus) calculation, identical to your Arduino sketch
uint16_t LuxpowerSNAComponent::calculate_crc_(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; ++i)
      crc = (crc >> 1) ^ (crc & 1 ? 0xA001 : 0);
  }
  return crc;
}

void LuxpowerSNAComponent::request_bank_(uint8_t bank) {
  if (this->tcp_client_ != nullptr) {
    ESP_LOGD(TAG, "Request for bank %d skipped, connection already in progress.", bank);
    return;
  }
  
  ESP_LOGD(TAG, "Connecting to %s:%d to request bank %d", this->host_.c_str(), this->port_, bank);
  this->tcp_client_ = new AsyncClient();

  this->tcp_client_->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
    ESP_LOGD(TAG, "Received %d bytes of data.", len);
    this->handle_response_((uint8_t*)data, len);
    client->close();
  }, nullptr);

  this->tcp_client_->onConnect([this, bank](void *arg, AsyncClient *client) {
    ESP_LOGD(TAG, "Successfully connected. Sending request for bank %d...", bank);
    
    // --- Build the request packet (matches your Arduino sketch) ---
    uint8_t pkt[29] = {0xAA, 0x55, 0x12, 0x00, 0x01, 0xC2, 0x14};
    memcpy(pkt + 7, this->dongle_serial_.data(), 10);
    pkt[17] = bank; // The register bank to read (0, 40, or 80)
    pkt[18] = 0x00;
    memcpy(pkt + 19, this->inverter_serial_.data(), 10);
    uint16_t crc = this->calculate_crc_(pkt + 2, 25);
    pkt[27] = crc & 0xFF;
    pkt[28] = crc >> 8;
    
    ESP_LOGD(TAG, "Sending data request (29 bytes)...");
    client->write((const char*)pkt, 29);
  }, nullptr);

  auto cleanup = [this]() {
    delete this->tcp_client_;
    this->tcp_client_ = nullptr;
  };

  this->tcp_client_->onError([this, cleanup](void *arg, AsyncClient *client, int8_t error) {
    ESP_LOGW(TAG, "Connection error: %s", client->errorToString(error));
    cleanup();
  }, nullptr);

  this->tcp_client_->onTimeout([this, cleanup](void *arg, AsyncClient *client, uint32_t time) {
    ESP_LOGW(TAG, "Connection timeout after %d ms", time);
    cleanup();
  }, nullptr);

  this->tcp_client_->onDisconnect([this, cleanup](void *arg, AsyncClient *client) {
    ESP_LOGD(TAG, "Disconnected from host.");
    cleanup();
  }, nullptr);

  if (!this->tcp_client_->connect(this->host_.c_str(), this->port_)) {
    ESP_LOGW(TAG, "Failed to initiate connection.");
    cleanup();
  }
}

void LuxpowerSNAComponent::handle_response_(const uint8_t *buffer, size_t length) {
  const uint16_t RESPONSE_HEADER_SIZE = sizeof(LuxHeader) + sizeof(LuxTranslatedData);
  if (length < RESPONSE_HEADER_SIZE) {
    ESP_LOGW(TAG, "Packet too small for headers (%d bytes)", length);
    return;
  }

  LuxHeader header;
  LuxTranslatedData trans;
  memcpy(&header, buffer, sizeof(LuxHeader));
  memcpy(&trans, buffer + sizeof(LuxHeader), sizeof(LuxTranslatedData));

  // Validate packet headers
  if (header.prefix != 0x55AA || header.function != 0xC2 || trans.deviceFunction != 0x04) {
    ESP_LOGW(TAG, "Invalid packet header/function. Prefix: 0x%04X, Func: 0x%02X, DevFunc: 0x%02X",
             header.prefix, header.function, trans.deviceFunction);
    return;
  }

  ESP_LOGD(TAG, "Decoding response for Inverter S/N: %s, Register Bank: %d",
           std::string(trans.serialNumber, 10).c_str(), trans.registerStart);
  this->publish_state_("inverter_serial", std::string(trans.serialNumber, 10));

  uint16_t data_offset = RESPONSE_HEADER_SIZE;
  
  // --- Decode based on the register bank received ---
  if (trans.registerStart == 0 && length >= data_offset + sizeof(LuxLogDataRawSection1)) {
    LuxLogDataRawSection1 raw;
    memcpy(&raw, buffer + data_offset, sizeof(LuxLogData
