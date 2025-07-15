#include "luxclient.h"
#include "crc.h"
// This is the ONLY place where helpers.h should be included.
#include "esphome/core/helpers.h"

// Platform-specific WiFi includes
#ifdef USE_ESP32
#include <WiFi.h>
#elif USE_ESP8266
#include <ESP8266WiFi.h>
#endif

namespace esphome {
namespace luxclient {

static const char *const TAG = "luxclient";

static const uint8_t START_FLAG = 0xA8;
static const uint8_t END_FLAG = 0x8A;
static const uint8_t PROTOCOL_VERSION = 0x01;
static const uint8_t PACKET_TYPE_TCP = 0x01;
static const uint8_t FC_READ_HOLDING_REGISTERS = 0x03;
static const uint8_t FC_WRITE_HOLDING_REGISTER = 0x06;

void LuxClient::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxClient (WiFi)...");
  // Initialize the mutex pointer here.
  this->client_mutex_ = make_unique<Mutex>();
}

void LuxClient::dump_config() {
  ESP_LOGCONFIG(TAG, "LuxClient:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%u", this->host_.c_str(), this->port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", this->dongle_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter Serial: %s", this->inverter_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Read Timeout: %ums", this->read_timeout_);
}

float LuxClient::get_setup_priority() const { return setup_priority::AFTER_WIFI; }

std::vector<uint8_t> LuxClient::build_request_packet(uint8_t function_code, uint16_t start_reg,
                                                     uint16_t reg_count_or_value) {
  std::vector<uint8_t> packet;
  packet.push_back(START_FLAG);
  packet.push_back(PROTOCOL_VERSION);
  packet.push_back(PACKET_TYPE_TCP);
  packet.push_back(0);
  packet.push_back(0);
  for (int i = 0; i < 10; i++) packet.push_back(this->dongle_serial_[i]);
  for (int i = 0; i < 10; i++) packet.push_back(this->inverter_serial_[i]);
  packet.push_back(function_code);
  packet.push_back(highByte(start_reg));
  packet.push_back(lowByte(start_reg));
  packet.push_back(highByte(reg_count_or_value));
  packet.push_back(lowByte(reg_count_or_value));
  uint16_t total_length = packet.size() - 1;
  packet[3] = lowByte(total_length);
  packet[4] = highByte(total_length);
  uint16_t crc = crc16(packet.data() + 1, packet.size() - 1);
  packet.push_back(lowByte(crc));
  packet.push_back(highByte(crc));
  packet.push_back(END_FLAG);
  return packet;
}

std::optional<std::vector<uint8_t>> LuxClient::execute_transaction(const std::vector<uint8_t> &request) {
  WiFiClient client;
  if (!client.connect(this->host_.c_str(), this->port_)) {
    ESP_LOGW(TAG, "Connection to %s:%d failed", this->host_.c_str(), this->port_);
    return {};
  }
  ESP_LOGD(TAG, "Sending %d bytes: %s", request.size(), format_hex_pretty(request).c_str());
  client.write(request.data(), request.size());
  std::vector<uint8_t> response;
  response.reserve(256);
  uint32_t start_time = millis();
  while (client.connected() && (millis() - start_time < this->read_timeout_)) {
    while (client.available()) {
      response.push_back(client.read());
      if (response.back() == END_FLAG) {
        goto response_received;
      }
    }
    yield();
  }
response_received:
  client.stop();
  if (response.empty()) {
    ESP_LOGW(TAG, "No response received from inverter (timeout).");
    return {};
  }
  ESP_LOGD(TAG, "Received %d bytes: %s", response.size(), format_hex_pretty(response).c_str());
  if (response.front() != START_FLAG || response.back() != END_FLAG) {
    ESP_LOGW(TAG, "Invalid start/end flags in response.");
    return {};
  }
  if (response.size() < 28) {
    ESP_LOGW(TAG, "Response too short: %d bytes", response.size());
    return {};
  }
  uint16_t received_crc = (uint16_t(response[response.size() - 2]) << 8) | response[response.size() - 3];
  uint16_t calculated_crc = crc16(response.data() + 1, response.size() - 4);
  if (received_crc != calculated_crc) {
    ESP_LOGW(TAG, "CRC check failed! Received: 0x%04X, Calculated: 0x%04X", received_crc, calculated_crc);
    return {};
  }
  if (response[25] & 0x80) {
    ESP_LOGW(TAG, "Inverter returned an error frame. Function code: 0x%02X", response[25]);
    return {};
  }
  uint8_t data_len = response[27];
  if (response.size() < 28 + data_len) {
    ESP_LOGW(TAG, "Response data length mismatch. Expected %d bytes, got less.", data_len);
    return {};
  }
  std::vector<uint8_t> data_payload(response.begin() + 28, response.begin() + 28 + data_len);
  return data_payload;
}

std::optional<std::vector<uint8_t>> LuxClient::read_holding_registers(uint16_t reg_address, uint8_t reg_count) {
  MutexLock lock(*this->client_mutex_); // MutexLock will now be recognized
  auto request = this->build_request_packet(FC_READ_HOLDING_REGISTERS, reg_address, reg_count);
  return this->execute_transaction(request);
}

bool LuxClient::write_holding_register(uint16_t reg_address, uint16_t value) {
  MutexLock lock(*this->client_mutex_); // MutexLock will now be recognized
  auto request = this->build_request_packet(FC_WRITE_HOLDING_REGISTER, reg_address, value);
  auto response = this->execute_transaction(request);
  return response.has_value();
}

}  // namespace luxclient
}  // namespace esphome
