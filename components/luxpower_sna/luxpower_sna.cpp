#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// Helper to convert a string like "1A2B3C" to a vector of bytes {0x1A, 0x2B, 0x3C}
std::vector<uint8_t> hex_to_data(const std::string &hex) {
  std::vector<uint8_t> data;
  for (size_t i = 0; i < hex.length(); i += 2) {
    uint8_t byte = std::stoul(hex.substr(i, 2), nullptr, 16);
    data.push_back(byte);
  }
  return data;
}

// --- SETUP & CONFIG ---

void LuxpowerSNAComponent::set_dongle_serial(const std::string &serial) { this->dongle_serial_ = hex_to_data(serial); }
void LuxpowerSNAComponent::set_inverter_serial_number(const std::string &serial) { this->inverter_serial_ = hex_to_data(serial); }

void LuxpowerSNAComponent::setup() {
  ESP_LOGI(TAG, "Setting up LuxpowerSNAComponent...");
  // Initial buffer allocation can happen here if needed, or in start_update_cycle_
}

void LuxpowerSNAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Luxpower SNA Component:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%d", this->host_.c_str(), this->port_);
  ESP_LOGCONFIG(TAG, "  Update Interval: %dms", this->get_update_interval());
  ESP_LOGCONFIG(TAG, "  Number of banks to request: %d", this->num_banks_to_request_);
}

// --- CORE LOGIC ---

void LuxpowerSNAComponent::update() {
  if (this->is_updating_) {
    ESP_LOGW(TAG, "Update already in progress. Skipping.");
    return;
  }
  this->start_update_cycle_();
}

void LuxpowerSNAComponent::start_update_cycle_() {
  ESP_LOGD(TAG, "Starting update cycle...");
  this->is_updating_ = true;
  this->current_bank_to_request_ = 0;
  this->data_buffer_.assign(this->num_banks_to_request_ * 80, 0); // 40 registers * 2 bytes/register

  this->update_timeout_handle_ = App.scheduler.set_timeout(this, "update", 15000, [this]() {
    ESP_LOGE(TAG, "Update cycle timed out! Aborting.");
    if (this->status_text_sensor_) this->status_text_sensor_->publish_state("Error: Timeout");
    this->end_update_cycle_(false);
  });

  this->client_ = new AsyncClient();
  this->client_->onConnect([this](void *arg, AsyncClient *c) {
    ESP_LOGD(TAG, "Successfully connected to %s:%d", this->host_.c_str(), this->port_);
    if (this->status_text_sensor_) this->status_text_sensor_->publish_state("Connected");
    this->send_next_request_();
  });
  this->client_->onData([this](void *arg, AsyncClient *c, void *data, size_t len) {
    this->handle_response_(data, len);
  });
  this->client_->onError([this](void *arg, AsyncClient *c, int8_t error) {
    ESP_LOGE(TAG, "TCP connection error: %s", c->errorToString(error));
    if (this->status_text_sensor_) this->status_text_sensor_->publish_state("Error: Connection Failed");
    this->end_update_cycle_(false);
  });
  this->client_->onDisconnect([this](void *arg, AsyncClient *c) {
    ESP_LOGD(TAG, "Disconnected from inverter.");
    this->end_update_cycle_(false);
  });

  ESP_LOGD(TAG, "Attempting to connect to inverter...");
  if (this->status_text_sensor_) this->status_text_sensor_->publish_state("Connecting...");
  this->client_->connect(this->host_.c_str(), this->port_);
}

void LuxpowerSNAComponent::send_next_request_() {
  if (this->current_bank_to_request_ >= this->num_banks_to_request_) {
    ESP_LOGI(TAG, "Successfully received all %d banks.", this->num_banks_to_request_);
    if (this->status_text_sensor_) this->status_text_sensor_->publish_state("Processing Data");
    this->parse_and_publish_();
    this->end_update_cycle_(true);
    return;
  }

  uint16_t start_register = this->current_bank_to_request_ * 40;
  std::vector<uint8_t> request = this->build_request_packet_(start_register, 40);
  
  ESP_LOGD(TAG, "Requesting Bank %d (Registers %d-%d)...", this->current_bank_to_request_, start_register, start_register + 39);
  if (this->status_text_sensor_) this->status_text_sensor_->publish_state("Requesting Bank " + to_string(this->current_bank_to_request_));
  this->client_->write(reinterpret_cast<const char*>(request.data()), request.size());
}

void LuxpowerSNAComponent::handle_response_(void *data, size_t len) {
  uint8_t *raw = (uint8_t *) data;
  if (len < 37 || raw[0] != 0xA1 || raw[1] != 0x1A || raw[7] != 0xC2) {
    ESP_LOGW(TAG, "Received invalid packet. Aborting update.");
    if (this->status_text_sensor_) this->status_text_sensor_->publish_state("Error: Invalid Packet");
    this->end_update_cycle_(false);
    return;
  }
  
  uint8_t *data_frame = &raw[20];
  uint8_t value_len = data_frame[14];
  if (value_len != 80) {
      ESP_LOGW(TAG, "Packet for bank %d has wrong data length (%d). Aborting.", this->current_bank_to_request_, value_len);
      if (this->status_text_sensor_) this->status_text_sensor_->publish_state("Error: Bad Length");
      this->end_update_cycle_(false);
      return;
  }

  ESP_LOGD(TAG, "Received and stored valid data for Bank %d.", this->current_bank_to_request_);
  memcpy(&this->data_buffer_[this->current_bank_to_request_ * 80], &data_frame[15], 80);

  this->current_bank_to_request_++;
  this->send_next_request_();
}

void LuxpowerSNAComponent::end_update_cycle_(bool success) {
  if (!this->is_updating_) return;

  App.scheduler.cancel_timeout(this->update_timeout_handle_);
  this->update_timeout_handle_ = 0;

  if (this->client_) {
    this->client_->close();
    delete this->client_;
    this->client_ = nullptr;
  }

  if (success) {
      if (this->status_text_sensor_) this->status_text_sensor_->publish_state("Idle");
  } else {
      if (this->status_text_sensor_ && this->status_text_sensor_->state != "Error: Timeout") {
          this->status_text_sensor_->publish_state("Error: Disconnected");
      }
  }
  
  ESP_LOGD(TAG, "Update cycle finished. Releasing lock.");
  this->is_updating_ = false;
}

// ========== YOU NEED TO IMPLEMENT THESE FUNCTIONS ==========

std::vector<uint8_t> LuxpowerSNAComponent::build_request_packet_(uint16_t start_register, uint16_t num_registers) {
  // TODO: Implement the packet building logic here.
  // This needs to create the exact byte sequence for a request packet,
  // including prefix, serial numbers, register info, function codes, and checksum.
  // Use the Python LXPPacket class as your reference.
  
  // Placeholder implementation:
  std::vector<uint8_t> packet;
  // ... build the real packet here ...
  return packet;
}

uint16_t LuxpowerSNAComponent::get_register_value_(int register_index) {
    int byte_pos = register_index * 2;
    if (byte_pos + 1 >= this->data_buffer_.size()) {
        ESP_LOGE(TAG, "Attempted to read beyond data_buffer_ bounds for register %d", register_index);
        return 0;
    }
    // Inverter data is Big Endian (High byte first)
    return (this->data_buffer_[byte_pos] << 8) | this->data_buffer_[byte_pos + 1];
}

void LuxpowerSNAComponent::parse_and_publish_() {
  // TODO: Implement the data parsing and sensor publishing logic here.
  // The `data_buffer_` now contains all the raw data from all banks.
  // Use get_register_value_ to extract values and publish them to your sensors.

  // Example:
  // (These register numbers are examples, you must verify them from documentation)
  uint16_t soc = get_register_value_(15); // Example: SOC is in register 15
  if (this->soc_sensor_) this->soc_sensor_->publish_state(soc);

  uint16_t pv1_power = get_register_value_(2); // Example: PV1 Power is register 2
  if (this->pv1_power_sensor_) this->pv1_power_sensor_->publish_state(pv1_power);
  
  // ... and so on for all your other sensors.
}

}  // namespace luxpower_sna
}  // namespace esphome

