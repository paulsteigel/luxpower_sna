#include "luxpower_inverter.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

void LuxPowerInverter::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxPower Inverter...");
  this->last_connect_attempt_ = 0;
  this->last_request_time_ = 0; // Initialize new member
  this->client_.stop(); // Ensure client is stopped initially
}

float LuxPowerInverter::get_setup_priority() const {
  return setup_priority::AFTER_WIFI; // Ensure Wi-Fi is set up before trying to connect
}

void LuxPowerInverter::loop() {
  const uint32_t now = millis();

  // 1. Manage TCP Connection
  if (!this->client_.connected()) {
    // If not connected, try to reconnect after the specified interval
    if ((now - this->last_connect_attempt_) > RECONNECT_INTERVAL_MS) {
      ESP_LOGI(TAG, "Attempting to connect to %s:%u...", this->host_.c_str(), this->port_);
      this->client_.stop(); // Ensure any previous connection is closed
      if (this->client_.connect(this->host_.c_str(), this->port_)) {
        ESP_LOGI(TAG, "Successfully connected to LuxPower Inverter!");
        this->status_set_ok(); // Corrected: Call base class method
      } else {
        ESP_LOGW(TAG, "Failed to connect to LuxPower Inverter. Client status: %d", this->client_.status());
        this->status_set_warning(); // Corrected: Call base class method
      }
      this->last_connect_attempt_ = now; // Update timestamp of this connection attempt
      this->data_buffer_.clear();       // Clear buffer on (re)connection attempt to avoid stale data
    }
    return; // Do not proceed to read/parse data if not connected
  }

  // 2. Read incoming data
  while (this->client_.available()) {
    uint8_t byte = this->client_.read();
    this->data_buffer_.push_back(byte);
    // Check if we have received enough bytes for a potential packet (minimum size)
    if (this->data_buffer_.size() >= PACKET_MIN_LENGTH) {
      parse_luxpower_response_packet(this->data_buffer_);
      // After parsing (or attempting to parse), clear the buffer for the next packet
      this->data_buffer_.clear();
    }
  }

  // 3. Periodically send requests (if needed, based on update_interval)
  if ((now - this->last_request_time_) > this->update_interval_) { // Changed 1000 to use pre-converted update_interval_
      ESP_LOGD(TAG, "Sending periodic request to inverter.");
      // Example: Send a heartbeat or data request (replace with actual LuxPower protocol)
      uint8_t request_bytes[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD}; // Example: Modbus read holding registers
      this->client_.write(request_bytes, sizeof(request_bytes));

      this->last_request_time_ = now;
  }
}

void LuxPowerInverter::parse_luxpower_response_packet(const std::vector<uint8_t> &data) {
    if (data.size() < PACKET_MIN_LENGTH) {
        ESP_LOGW(TAG, "Received data too short for a valid packet.");
        return;
    }

    ESP_LOGD(TAG, "Parsing received packet (length: %zu)", data.size());
    std::string hex_data = "";
    for (uint8_t byte : data) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", byte);
        hex_data += buf;
    }
    ESP_LOGV(TAG, "Received Hex: %s", hex_data.c_str());

    if (data.size() >= 5) { // Example check for data size
        uint16_t example_reg_address = 0x0001; // Placeholder
        uint16_t example_value = (data[3] << 8) | data[4]; // Placeholder
        parse_and_publish_register(example_reg_address, example_value);
    }
}

void LuxPowerInverter::parse_and_publish_register(uint16_t reg_address, uint16_t value) {
    ESP_LOGD(TAG, "Register 0x%04X: Value %u", reg_address, value);
    // This is where you would map the received register address and value to your ESPHome sensors
    // and publish them, e.g., this->voltage_sensor_->publish_state(value / 10.0f);
}

void LuxPowerInverter::status_set_warning() {
    this->status_set_warning_(); // Calls the protected method inherited from Component
}

void LuxPowerInverter::status_set_ok() {
    this->status_clear_warning_(); // Calls the protected method inherited from Component
}

} // namespace luxpower_sna
} // namespace esphome
