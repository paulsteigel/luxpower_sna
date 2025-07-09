#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include "esphome/components/socket/socket.h" // The only socket header we need
#include <cstring> // For memcpy

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// CRC16 function remains the same
uint16_t crc16_modbus(const std::vector<uint8_t>& data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t pos = 0; pos < length; pos++) {
        crc ^= data[pos];
        for (int i = 8; i != 0; i--) {
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

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxpowerSNAComponent...");
  this->data_buffer_.resize(this->num_banks_to_request_ * 80, 0);
}

void LuxpowerSNAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Luxpower SNA Inverter:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%d", this->host_.c_str(), this->port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: '%s'", this->dongle_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Banks to request: %d", this->num_banks_to_request_);
}

void LuxpowerSNAComponent::update() {
  if (this->is_updating_) {
    ESP_LOGW(TAG, "Update already in progress. Skipping.");
    return;
  }
  this->is_updating_ = true;

  // Step 1: Create the socket object.
  this->socket_ = socket::socket(AF_INET, SOCK_STREAM, 0);
  if (this->socket_ == nullptr) {
    ESP_LOGE(TAG, "Could not create socket object. Aborting update.");
    this->is_updating_ = false;
    if (this->status_text_sensor_) this->status_text_sensor_->publish_state("Error: Create Socket");
    return;
  }

  // ====================================================================
  // *** THE FINAL, CORRECT CONNECTION LOGIC ***
  // Step 2: Prepare the address structure.
  // Step 3: Connect using the now-available virtual method.
  // ====================================================================
  sockaddr_storage address;
  socklen_t address_len = sizeof(address);
  if (!socket::set_sockaddr(this->host_.c_str(), this->port_, reinterpret_cast<sockaddr *>(&address), &address_len)) {
    ESP_LOGW(TAG, "Could not resolve IP address of '%s'", this->host_.c_str());
    this->socket_->close();
    this->socket_ = nullptr;
    this->is_updating_ = false;
    if (this->status_text_sensor_) this->status_text_sensor_->publish_state("Error: DNS Failed");
    return;
  }

  if (this->socket_->connect(reinterpret_cast<sockaddr *>(&address), address_len) != 0) {
    ESP_LOGW(TAG, "Could not connect to %s:%d. Aborting update.", this->host_.c_str(), this->port_);
    this->socket_->close();
    this->socket_ = nullptr;
    this->is_updating_ = false;
    if (this->status_text_sensor_) this->status_text_sensor_->publish_state("Error: Connection Failed");
    return;
  }

  ESP_LOGD(TAG, "Successfully connected to %s:%d", this->host_.c_str(), this->port_);

  // The rest of the logic remains the same
  for (int bank = 0; bank < this->num_banks_to_request_; bank++) {
    uint16_t start_reg = bank * 40;
    uint16_t num_regs = 40;

    ESP_LOGD(TAG, "Requesting Bank %d (Registers %d-%d)", bank, start_reg, start_reg + num_regs - 1);
    std::vector<uint8_t> request = this->build_request_packet_(start_reg, num_regs);
    
    this->log_hex_buffer("--> SENT", request);

    this->socket_->write(request.data(), request.size());

    std::vector<uint8_t> response_buffer(256);
    ssize_t len = this->socket_->read(response_buffer.data(), response_buffer.size());

    if (len <= 0) {
      ESP_LOGW(TAG, "Read failed for bank %d. Error: %s", bank, strerror(errno));
      this->socket_->close();
      this->socket_ = nullptr;
      this->is_updating_ = false;
      if (this->status_text_sensor_) this->status_text_sensor_->publish_state("Error: Read Failed");
      return;
    }

    response_buffer.resize(len);
    this->log_hex_buffer("<-- RECV", response_buffer);

    if (len < 115 || response_buffer[0] != 0xA1 || response_buffer[1] != 0x1A || response_buffer[7] != 194) {
      ESP_LOGW(TAG, "Received invalid packet for bank %d. Aborting update.", bank);
      this->socket_->close();
      this->socket_ = nullptr;
      this->is_updating_ = false;
      if (this->status_text_sensor_) this->status_text_sensor_->publish_state("Error: Invalid Packet");
      return;
    }
    
    ESP_LOGD(TAG, "Received and stored valid data for Bank %d.", bank);
    memcpy(&this->data_buffer_[bank * 80], &response_buffer[35], 80);
  }

  this->socket_->close();
  this->socket_ = nullptr;
  ESP_LOGI(TAG, "Update cycle successful. Parsing and publishing data.");
  if (this->status_text_sensor_) this->status_text_sensor_->publish_state("OK");
  
  this->parse_and_publish_();
  this->is_updating_ = false;
}

// ... The rest of the file (build_request_packet_, etc.) is unchanged ...
std::vector<uint8_t> LuxpowerSNAComponent::build_request_packet_(uint16_t start_register, uint16_t num_registers) {
    std::vector<uint8_t> modbus_cmd(18);
    modbus_cmd[0] = 0;
    modbus_cmd[1] = 4;
    std::vector<uint8_t> empty_inverter_sn(10, 0x00);
    memcpy(&modbus_cmd[2], empty_inverter_sn.data(), 10);
    modbus_cmd[12] = start_register & 0xFF;
    modbus_cmd[13] = (start_register >> 8) & 0xFF;
    modbus_cmd[14] = num_registers & 0xFF;
    modbus_cmd[15] = (num_registers >> 8) & 0xFF;
    uint16_t crc = crc16_modbus(modbus_cmd, 16);
    modbus_cmd[16] = crc & 0xFF;
    modbus_cmd[17] = (crc >> 8) & 0xFF;
    size_t transfer_data_len = 10 + 2 + modbus_cmd.size();
    std::vector<uint8_t> transfer_data(transfer_data_len);
    memcpy(&transfer_data[0], this->dongle_serial_.data(), 10);
    transfer_data[10] = modbus_cmd.size() & 0xFF;
    transfer_data[11] = (modbus_cmd.size() >> 8) & 0xFF;
    memcpy(&transfer_data[12], modbus_cmd.data(), modbus_cmd.size());
    uint16_t protocol = 1;
    uint8_t function_code = 194;
    uint16_t payload_len = transfer_data.size() + 2;
    size_t frame_len = 8 + transfer_data.size();
    std::vector<uint8_t> tcp_frame(frame_len);
    tcp_frame[0] = 0xA1;
    tcp_frame[1] = 0x1A;
    tcp_frame[2] = protocol & 0xFF;
    tcp_frame[3] = (protocol >> 8) & 0xFF;
    tcp_frame[4] = payload_len & 0xFF;
    tcp_frame[5] = (payload_len >> 8) & 0xFF;
    tcp_frame[6] = 1;
    tcp_frame[7] = function_code;
    memcpy(&tcp_frame[8], transfer_data.data(), transfer_data.size());
    return tcp_frame;
}

void LuxpowerSNAComponent::log_hex_buffer(const std::string& prefix, const std::vector<uint8_t>& buffer) {
    char hex_buffer[buffer.size() * 3 + 1];
    for (size_t i = 0; i < buffer.size(); i++) {
        sprintf(&hex_buffer[i * 3], "%02X ", buffer[i]);
    }
    hex_buffer[buffer.size() * 3] = '\0';
    ESP_LOGD(TAG, "%s (%zu bytes): %s", prefix.c_str(), buffer.size(), hex_buffer);
}

uint16_t LuxpowerSNAComponent::get_register_value_(int register_index) {
    int byte_pos = register_index * 2;
    if (byte_pos + 1 >= this->data_buffer_.size()) {
        ESP_LOGE(TAG, "Read failed: Register %d is out of the requested banks.", register_index);
        return 0;
    }
    return (this->data_buffer_[byte_pos] << 8) | this->data_buffer_[byte_pos + 1];
}

void LuxpowerSNAComponent::parse_and_publish_() {
  uint16_t soc = get_register_value_(15);
  if (this->soc_sensor_) this->soc_sensor_->publish_state(soc);
  ESP_LOGI(TAG, "Published updated sensor values.");
}

} // namespace luxpower_sna
} // namespace esphome
