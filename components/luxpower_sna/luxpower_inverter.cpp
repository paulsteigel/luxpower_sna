#include "luxpower_inverter.h"
#include "esphome/core/log.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna.inverter";

void LuxpowerInverterComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Luxpower Inverter Component...");
  this->client_ = new AsyncClient();

  this->client_->onConnect([this](void *arg, AsyncClient *client) {
    ESP_LOGI(TAG, "Connected to inverter at %s:%d", this->host_.c_str(), this->port_);
    this->is_connected_ = true;
    this->current_request_bank_ = 0;
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

  this->client_->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
    std::vector<uint8_t> received_data;
    received_data.assign((uint8_t *) data, (uint8_t *) data + len);
    this->parse_inverter_data(received_data);
  });
  
  this->connect_to_inverter();
}

void LuxpowerInverterComponent::update() {
  if (!this->is_connected_) {
    this->connect_to_inverter();
    return;
  }
  
  // We poll in banks of 40 registers to be efficient.
  // We will cycle through banks 0, 1, and 2. Add more if needed.
  ESP_LOGD(TAG, "Requesting data bank %d", this->current_request_bank_);
  this->request_inverter_data(this->current_request_bank_);
  this->current_request_bank_ = (this->current_request_bank_ + 1) % 3;
}

void LuxpowerInverterComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Luxpower Inverter Component:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%d", this->host_.c_str(), this->port_);
  for (auto *sensor : this->sensors_) {
    sensor->dump_config();
  }
}

void LuxpowerInverterComponent::connect_to_inverter() {
  if (this->is_connected_ || this->client_->connected()) return;
  ESP_LOGD(TAG, "Attempting connection to %s:%d", this->host_.c_str(), this->port_);
  this->client_->connect(this->host_.c_str(), this->port_);
}

void LuxpowerInverterComponent::request_inverter_data(int bank) {
  if (!this->is_connected_) return;
  
  uint16_t start_register = bank * 40;
  uint16_t num_registers = 40;
  
  auto packet = this->prepare_packet_for_read(start_register, num_registers, DEVICE_FUNCTION_READ_INPUT);
  
  if (this->client_->space() > packet.size() && this->client_->canSend()) {
    this->client_->write((const char*)packet.data(), packet.size());
  } else {
    ESP_LOGW(TAG, "Cannot send data request for bank %d", bank);
  }
}

void LuxpowerInverterComponent::parse_inverter_data(const std::vector<uint8_t> &data) {
  if (data.size() < 37 || data[0] != 0xA1 || data[1] != 0x1A) {
    ESP_LOGW(TAG, "Received invalid or incomplete packet");
    return;
  }
  if (data[7] != TCP_FUNCTION_TRANSLATED_DATA) return;

  std::vector<uint8_t> data_frame(data.begin() + 20, data.end() - 2);
  uint16_t received_crc = get_16bit_unsigned(data, data.size() - 2);
  if (received_crc != compute_crc(data_frame)) {
    ESP_LOGW(TAG, "CRC mismatch");
    return;
  }

  uint16_t start_register = get_16bit_unsigned(data_frame, 12);
  uint16_t num_registers = data_frame[14] / 2;
  ESP_LOGD(TAG, "Parsing response for start register %d, count %d", start_register, num_registers);

  // Iterate through all our registered sensors
  for (auto *sensor : this->sensors_) {
    uint16_t sensor_reg = sensor->get_register_address();
    // Check if this sensor's register is within the bank of data we just received
    if (sensor_reg >= start_register && sensor_reg < (start_register + num_registers)) {
      int offset = 15 + (sensor_reg - start_register) * 2;
      float value;

      if (sensor->get_is_signed()) {
        value = (float)get_16bit_signed(data_frame, offset);
      } else {
        value = (float)get_16bit_unsigned(data_frame, offset);
      }
      
      value /= sensor->get_divisor();
      
      ESP_LOGD(TAG, "Updating sensor '%s' (reg %d) with value %.2f", sensor->get_name().c_str(), sensor_reg, value);
      sensor->publish_state(value);
    }
  }
}

// --- Helper functions (same as before) ---
uint16_t LuxpowerInverterComponent::get_16bit_unsigned(const std::vector<uint8_t> &data, int offset) {
  return (uint16_t(data[offset+1]) << 8) | data[offset];
}
int16_t LuxpowerInverterComponent::get_16bit_signed(const std::vector<uint8_t> &data, int offset) {
  return (int16_t)get_16bit_unsigned(data, offset);
}
std::vector<uint8_t> LuxpowerInverterComponent::prepare_packet_for_read(uint16_t start_register, uint16_t num_registers, uint8_t function) {
  std::vector<uint8_t> packet, data_frame;
  data_frame.push_back(ACTION_WRITE);
  data_frame.push_back(function);
  data_frame.insert(data_frame.end(), this->inverter_serial_.begin(), this->inverter_serial_.end());
  data_frame.push_back(start_register & 0xFF); data_frame.push_back(start_register >> 8);
  data_frame.push_back(num_registers & 0xFF); data_frame.push_back(num_registers >> 8);
  uint16_t crc = compute_crc(data_frame);
  packet.push_back(0xA1); packet.push_back(0x1A);
  packet.push_back(0x02); packet.push_back(0x00);
  uint16_t frame_length = 14 + data_frame.size();
  packet.push_back(frame_length & 0xFF); packet.push_back(frame_length >> 8);
  packet.push_back(0x01);
  packet.push_back(TCP_FUNCTION_TRANSLATED_DATA);
  packet.insert(packet.end(), this->dongle_serial_.begin(), this->dongle_serial_.end());
  uint16_t data_length = 2 + data_frame.size();
  packet.push_back(data_length & 0xFF); packet.push_back(data_length >> 8);
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
