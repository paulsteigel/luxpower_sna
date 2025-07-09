#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include <cstring> // For memcpy

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// NEW: CRC16 Modbus calculation function (ported from app.js)
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
  // Resize buffer based on user config. 40 registers * 2 bytes/register = 80 bytes/bank
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
  this->start_update_cycle_();
}

void LuxpowerSNAComponent::start_update_cycle_() {
  this->is_updating_ = true;
  this->current_bank_to_request_ = 0;
  ESP_LOGI(TAG, "Starting update cycle...");
  if (this->status_text_sensor_) this->status_text_sensor_->publish_state("Updating...");

  this->socket_ = socket::Socket::create_tcp(this->host_.c_str(), this->port_);
  if (!this->socket_) {
    ESP_LOGE(TAG, "Could not create socket.");
    this->end_update_cycle_(false);
    return;
  }
  
  // Set a timeout for the connection and reads
  this->socket_->set_timeout(10000); 

  this->send_next_request_();
}

void LuxpowerSNAComponent::send_next_request_() {
  if (this->current_bank_to_request_ >= this->num_banks_to_request_) {
    // We have received all requested banks
    this->end_update_cycle_(true);
    return;
  }

  uint16_t start_reg = this->current_bank_to_request_ * 40;
  uint16_t num_regs = 40;

  ESP_LOGD(TAG, "Requesting Bank %d (Registers %d-%d)", this->current_bank_to_request_, start_reg, start_reg + num_regs - 1);

  std::vector<uint8_t> request = this->build_request_packet_(start_reg, num_regs);
  
  // LOG THE SENT PACKET
  this->log_hex_buffer("--> SENT", request);

  this->socket_->write(request);

  // Now, we wait for the response, which will be handled by the read_data_once callback.
  // We use read_data_once for better state management.
  this->socket_->read_data_once([this](void *data, size_t len) {
      this->handle_response_(data, len);
  });
}

void LuxpowerSNAComponent::handle_response_(void *data, size_t len) {
  uint8_t *raw = (uint8_t *) data;
  std::vector<uint8_t> response(raw, raw + len);

  // LOG THE RECEIVED PACKET
  this->log_hex_buffer("<-- RECV", response);

  // Validation based on app.js parser
  if (len < 37 || response[0] != 0xA1 || response[1] != 0x1A) {
    ESP_LOGW(TAG, "Received invalid or short packet. Aborting update.");
    this->end_update_cycle_(false);
    return;
  }
  
  if (response[7] != 194) { // "Translate" response function code
    ESP_LOGW(TAG, "Received packet with unexpected function code: %d", response[7]);
    this->end_update_cycle_(false);
    return;
  }

  // A valid response for 40 registers should have 80 bytes of data.
  // The data starts at offset 35. Total length must be at least 35 + 80 = 115.
  if (len < 115) {
      ESP_LOGW(TAG, "Packet for bank %d is too short (%d bytes). Aborting.", this->current_bank_to_request_, len);
      this->end_update_cycle_(false);
      return;
  }

  ESP_LOGD(TAG, "Received and stored valid data for Bank %d.", this->current_bank_to_request_);
  // Copy the 80 bytes of register data, which starts at offset 35, into our main buffer
  memcpy(&this->data_buffer_[this->current_bank_to_request_ * 80], &response[35], 80);

  // Move to the next bank
  this->current_bank_to_request_++;
  this->send_next_request_();
}

void LuxpowerSNAComponent::end_update_cycle_(bool success) {
  if (this->socket_) {
    this->socket_->close();
    this->socket_.reset();
  }
  
  if (success) {
    ESP_LOGI(TAG, "Update cycle successful. Parsing and publishing data.");
    if (this->status_text_sensor_) this->status_text_sensor_->publish_state("OK");
    this->parse_and_publish_();
  } else {
    ESP_LOGE(TAG, "Update cycle failed.");
    if (this->status_text_sensor_) this->status_text_sensor_->publish_state("Error");
  }
  
  this->is_updating_ = false;
}

// NEW: Full implementation of the packet building logic
std::vector<uint8_t> LuxpowerSNAComponent::build_request_packet_(uint16_t start_register, uint16_t num_registers) {
    // Layer 1: Build inner Modbus-like command
    std::vector<uint8_t> modbus_cmd(18);
    modbus_cmd[0] = 0; // address
    modbus_cmd[1] = 4; // function code R_INPUT (4)
    std::vector<uint8_t> empty_inverter_sn(10, 0x00);
    memcpy(&modbus_cmd[2], empty_inverter_sn.data(), 10);
    modbus_cmd[12] = start_register & 0xFF;
    modbus_cmd[13] = (start_register >> 8) & 0xFF;
    modbus_cmd[14] = num_registers & 0xFF;
    modbus_cmd[15] = (num_registers >> 8) & 0xFF;
    uint16_t crc = crc16_modbus(modbus_cmd, 16);
    modbus_cmd[16] = crc & 0xFF;
    modbus_cmd[17] = (crc >> 8) & 0xFF;

    // Layer 2: Build Transfer Data buffer
    size_t transfer_data_len = 10 + 2 + modbus_cmd.size();
    std::vector<uint8_t> transfer_data(transfer_data_len);
    memcpy(&transfer_data[0], this->dongle_serial_.data(), 10);
    transfer_data[10] = modbus_cmd.size() & 0xFF;
    transfer_data[11] = (modbus_cmd.size() >> 8) & 0xFF;
    memcpy(&transfer_data[12], modbus_cmd.data(), modbus_cmd.size());

    // Layer 3: Build final TCP Frame
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

// NEW: Helper to log byte buffers in hex format
void LuxpowerSNAComponent::log_hex_buffer(const std::string& prefix, const std::vector<uint8_t>& buffer) {
    char hex_buffer[buffer.size() * 3 + 1];
    for (size_t i = 0; i < buffer.size(); i++) {
        sprintf(&hex_buffer[i * 3], "%02X ", buffer[i]);
    }
    hex_buffer[buffer.size() * 3] = '\0';
    ESP_LOGD(TAG, "%s (%d bytes): %s", prefix.c_str(), buffer.size(), hex_buffer);
}

uint16_t LuxpowerSNAComponent::get_register_value_(int register_index) {
    int byte_pos = register_index * 2;
    if (byte_pos + 1 >= this->data_buffer_.size()) {
        ESP_LOGE(TAG, "Read failed: Register %d is out of the requested banks.", register_index);
        return 0;
    }
    // Inverter data is Big Endian (High byte first)
    return (this->data_buffer_[byte_pos] << 8) | this->data_buffer_[byte_pos + 1];
}

void LuxpowerSNAComponent::parse_and_publish_() {
  // TODO: Fill this in with the correct register numbers for your sensors.
  // Example from app.js (note: these are likely not power, but we use them as an example)
  // uint16_t r7 = get_register_value_(7);
  // uint16_t r8 = get_register_value_(8);
  // uint16_t r9 = get_register_value_(9);
  // float pv_power = r7 + r8 + r9;
  // if (this->pv1_power_sensor_) this->pv1_power_sensor_->publish_state(pv_power);

  // Example: Publish SOC (Register 15 is a common one)
  uint16_t soc = get_register_value_(15);
  if (this->soc_sensor_) this->soc_sensor_->publish_state(soc);

  ESP_LOGI(TAG, "Published updated sensor values.");
}

} // namespace luxpower_sna
} // namespace esphome
