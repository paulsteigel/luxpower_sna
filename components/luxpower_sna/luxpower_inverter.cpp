#include "luxpower_inverter.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

void LuxpowerInverterComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Luxpower SNA Component...");
  this->client_ = new AsyncClient();

  this->client_->onConnect([this](void *arg, AsyncClient *client) {
    ESP_LOGI(TAG, "Connected to inverter at %s:%d", this->host_.c_str(), this->port_);
    this->is_connected_ = true;
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

  // The onData lambda now calls our new parsing function
  this->client_->onData([this](void *arg, AsyncClient *c, void *data, size_t len) {
    this->parse_inverter_data(data, len);
  });
}

void LuxpowerInverterComponent::update() {
  if (!this->is_connected_) {
    this->connect_to_inverter();
    return;
  }
  this->request_test_data();
}

void LuxpowerInverterComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Luxpower SNA Component:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%d", this->host_.c_str(), this->port_);
}

void LuxpowerInverterComponent::parse_inverter_data(void *data, size_t len) {
  std::vector<uint8_t> packet;
  packet.assign((uint8_t *) data, (uint8_t *) data + len);

  // Basic validation
  if (len < 37 || packet[0] != 0xA1 || packet[1] != 0x1A || packet[7] != 0xC2) {
    ESP_LOGW(TAG, "Received invalid or non-data packet");
    return;
  }
  
  // The actual register data starts at byte 35
  const int DATA_FRAME_START = 35;
  
  // Extract just the register values
  std::vector<uint8_t> register_data(packet.begin() + DATA_FRAME_START, packet.end() - 2);

  // --- TEST: Extract Battery Voltage (Register 4) ---
  // Each register is 2 bytes, so we look at an offset of 4 * 2 = 8
  int register_offset = 4 * 2; 

  if(register_data.size() > register_offset + 1) {
    uint16_t raw_value = (uint16_t(register_data[register_offset + 1]) << 8) | register_data[register_offset];
    float voltage = raw_value / 10.0f;
    ESP_LOGI(TAG, "SUCCESS! Parsed Battery Voltage: %.1f V", voltage);
  } else {
    ESP_LOGW(TAG, "Data packet too short to read register 4");
  }
}

// --- All other functions remain the same ---

void LuxpowerInverterComponent::set_host(const std::string &host) { this->host_ = host; }
void LuxpowerInverterComponent::set_port(uint16_t port) { this->port_ = port; }
void LuxpowerInverterComponent::set_dongle_serial(const std::vector<uint8_t> &serial) { this->dongle_serial_ = serial; }
void LuxpowerInverterComponent::set_inverter_serial_number(const std::vector<uint8_t> &serial) { this->inverter_serial_ = serial; }

void LuxpowerInverterComponent::connect_to_inverter() {
  if (this->is_connected_ || this->client_->connected()) return;
  ESP_LOGD(TAG, "Attempting connection to %s:%d", this->host_.c_str(), this->port_);
  this->client_->connect(this->host_.c_str(), this->port_);
}

void LuxpowerInverterComponent::request_test_data() {
  if (!this->is_connected_) return;
  ESP_LOGD(TAG, "Polling update: Requesting test data from inverter.");
  
  uint16_t start_register = 0;
  uint16_t num_registers = 40;
  uint8_t function = 4; // READ_INPUT
  
  auto packet = this->prepare_packet_for_read(start_register, num_registers, function);
  
  if (this->client_->space() > packet.size() && this->client_->canSend()) {
    this->client_->write((const char*)packet.data(), packet.size());
  } else {
    ESP_LOGW(TAG, "Cannot send data request, client buffer full or not ready.");
  }
}

std::vector<uint8_t> LuxpowerInverterComponent::prepare_packet_for_read(uint16_t start, uint16_t count, uint8_t func) {
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

uint16_t LuxpowerInverterComponent::compute_crc(const std::vector<uint8_t> &data) {
  uint16_t crc = 0xFFFF;
  for (uint8_t byte : data) {
    crc ^= byte;
    for (int i = 0; i < 8; i++) {
      if (crc & 1) { crc >>= 1; crc ^= 0xA001; } else { crc >>= 1; }
    }
  }
  return crc;
}

}  // namespace luxpower_sna
}  // namespace esphome
