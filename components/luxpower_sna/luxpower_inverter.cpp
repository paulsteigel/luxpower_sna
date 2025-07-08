#include "luxpower_inverter.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

void LuxPowerInverter::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxPower Inverter...");
  this->last_connect_attempt_ = 0;
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
    if ((now - this->last_connect_attempt_) > RECONNECT_INTERVAL_MS) { // Removed network::is_connected()
      ESP_LOGI(TAG, "Attempting to connect to %s:%u...", this->host_.c_str(), this->port_);
      this->client_.stop(); // Ensure any previous connection is closed
      if (this->client_.connect(this->host_.c_str(), this->port_)) {
        ESP_LOGI(TAG, "Successfully connected to LuxPower Inverter!");
        this->status_set_ok(); // Indicate successful connection
      } else {
        ESP_LOGW(TAG, "Failed to connect to LuxPower Inverter. Client status: %d", this->client_.status());
        this->status_set_warning(); // Indicate connection failure
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
  if ((now - this->last_request_time_) > this->update_interval_ * 1000) {
      // Example: Request holding registers (adjust based on actual inverter communication)
      // This is a placeholder; you'll implement the actual requests here
      // For LuxPower, you might send specific commands or read specific register banks.
      // E.g., this->client_.write(command_bytes);
      ESP_LOGD(TAG, "Sending periodic request to inverter.");
      // Example: Send a heartbeat or data request (replace with actual LuxPower protocol)
      // For demonstration, let's assume a simple read of a known register
      // In a real scenario, you'd construct and send Modbus/LuxPower specific requests.
      uint8_t request_bytes[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD}; // Example: Modbus read holding registers
      this->client_.write(request_bytes, sizeof(request_bytes));

      this->last_request_time_ = now;
  }
} // <--- ENSURE THIS CLOSING BRACE IS HERE FOR loop()

// parse_luxpower_response_packet() method definition
void LuxPowerInverter::parse_luxpower_response_packet(const std::vector<uint8_t> &data) {
    if (data.size() < PACKET_MIN_LENGTH) {
        ESP_LOGW(TAG, "Received data too short for a valid packet.");
        return;
    }

    // Basic packet structure check (replace with actual LuxPower protocol parsing)
    // For example, if it's a Modbus RTU response:
    // Byte 0: Slave Address
    // Byte 1: Function Code
    // Byte 2: Byte Count
    // ... Data bytes ...
    // Last 2 bytes: CRC

    // This is a highly simplified example; you'll need to implement the actual LuxPower
    // packet parsing logic based on its communication protocol (e.g., Modbus RTU over TCP).
    // The received data needs to be interpreted into meaningful register values.

    ESP_LOGD(TAG, "Parsing received packet (length: %zu)", data.size());
    // Example: Assuming data represents a response, extract some values
    // This part requires understanding the LuxPower packet format.
    // Let's assume for now, it's a simple byte stream and we just log it.
    std::string hex_data = "";
    for (uint8_t byte : data) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", byte);
        hex_data += buf;
    }
    ESP_LOGV(TAG, "Received Hex: %s", hex_data.c_str());

    // If you extract register values, call parse_and_publish_register
    // For example, if you know byte 3 and 4 form a register value:
    if (data.size() >= 5) {
        uint16_t example_reg_address = 0x0001; // This needs to be dynamically determined from the packet
        uint16_t example_value = (data[3] << 8) | data[4]; // Example: Big-endian value from bytes 3 & 4
        parse_and_publish_register(example_reg_address, example_value);
    }
} // <--- ENSURE THIS CLOSING BRACE IS HERE FOR parse_luxpower_response_packet()

// parse_and_publish_register() method definition
void LuxPowerInverter::parse_and_publish_register(uint16_t reg_address, uint16_t value) {
    // This is where you would map the received register address and value to your ESPHome sensors
    // and publish them.
    ESP_LOGD(TAG, "Register 0x%04X: Value %u", reg_address, value);

    // Example: If you had a sensor named 'inverter_temperature_sensor_'
    // if (reg_address == TEMPERATURE_REGISTER_ADDRESS && this->inverter_temperature_sensor_ != nullptr) {
    //     this->inverter_temperature_sensor_->publish_state(value / 10.0f); // Example scaling
    // }
    // You would add similar checks for all registers you want to expose.
} // <--- ENSURE THIS CLOSING BRACE IS HERE FOR parse_and_publish_register()

// status_set_warning() method definition
void LuxPowerInverter::status_set_warning() {
    this->status_set_warning_();
} // <--- ENSURE THIS CLOSING BRACE IS HERE FOR status_set_warning()

// status_set_ok() method definition
void LuxPowerInverter::status_set_ok() {
    this->status_clear_warning_();
} // <--- ENSURE THIS CLOSING BRACE IS HERE FOR status_set_ok()

} // namespace luxpower_sna
} // namespace esphome
