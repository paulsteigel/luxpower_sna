#include "luxpower_inverter.h" // Include your component's header file
#include "esphome/core/log.h"  // Required for ESP_LOGx macros
#include "esphome/core/application.h" // For App.loop() if needed for global loop access

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// --- Implementations of LuxPowerInverter Class Member Functions ---

// setup() method: Initializes the component and attempts initial TCP connection
void LuxPowerInverter::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxPower SNA Inverter TCP component...");
  // Check if host is configured from YAML
  if (this->host_.empty()) {
    ESP_LOGE(TAG, "Host not configured for LuxPower Inverter TCP component! Please add 'host:' to your YAML.");
    this->mark_failed(); // Mark component as failed if required parameters are missing
    return;
  }
  ESP_LOGI(TAG, "Configured to connect to LuxPower Inverter at %s:%u", this->host_.c_str(), this->port_);
  // Set the initial connection attempt time to ensure it tries connecting soon
  this->last_connect_attempt_ = millis() - RECONNECT_INTERVAL_MS; // Force an immediate attempt
}

// get_setup_priority() method: Defines when this component should be set up
float LuxPowerInverter::get_setup_priority() const {
  // Ensure WiFi and network are fully initialized before attempting TCP connections
  return setup_priority::AFTER_WIFI;
}

// loop() method: Called repeatedly to manage TCP connection and process incoming data
// loop() method: Called repeatedly to manage TCP connection and process incoming data
void LuxPowerInverter::loop() {
  const uint32_t now = millis();

  // 1. Manage TCP Connection
  if (!this->client_.connected()) {
    // If not connected, try to reconnect after the specified interval
    // Original: if (network::is_connected() && (now - this->last_connect_attempt_) > RECONNECT_INTERVAL_MS) {
    if ((now - this->last_connect_attempt_) > RECONNECT_INTERVAL_MS) { // MODIFIED LINE: Removed network::is_connected()
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

// parse_luxpower_response_packet() method: Decodes the A1 packet contents
void LuxPowerInverter::parse_luxpower_response_packet(const std::vector<uint8_t> &data) {
  this->rx_count_++; // Increment received packet counter

  // Sanity check: Ensure the packet has at least the minimum expected length
  if (data.size() < LUXPOWER_A1_MIN_LENGTH) {
    ESP_LOGW(TAG, "A1 response too short (%zu bytes)! Minimum expected: %u.", data.size(), LUXPOWER_A1_MIN_LENGTH);
    this->status_set_warning();
    return;
  }

  // Re-verify the frame length consistency with the packet header
  uint16_t frame_length = (data[5] << 8) | data[4]; // Bytes 4-5: Frame Length (little-endian)
  uint16_t expected_packet_length = frame_length + 6; // Total packet length = Frame Length + 6 header bytes

  // Final length check
  if (data.size() != expected_packet_length) {
    ESP_LOGW(TAG, "A1 response length mismatch! Header indicates %u bytes, but received %zu. Dropping packet.",
             expected_packet_length, data.size());
    this->status_set_warning();
    return;
  }

  // --- Extracting relevant fields from the A1 packet (based on LXPPacket.py) ---
  uint8_t tcp_function_code = data[6]; // Byte 6: TCP Function Code (e.g., 0xC2 for TRANSLATED_DATA)
  uint16_t modbus_data_length = (data[18] << 8) | data[17]; // Bytes 17-18: Modbus Data Length (little-endian)
  uint8_t modbus_function_code = data[20]; // Byte 20: Modbus Function Code (e.g., 0x03 Read Holding, 0x04 Read Input)
  uint16_t modbus_start_register = (data[32] << 8) | data[31]; // Bytes 31-32: Starting Modbus Register Address (little-endian)
  uint16_t number_of_registers = (data[34] << 8) | data[33]; // Bytes 33-34: Number of Registers (little-endian)

  const uint8_t *modbus_payload_ptr = &data[35]; // Pointer to the start of the Modbus data payload

  // Consistency check: `modbus_data_length` should be twice the `number_of_registers`
  if (modbus_data_length != (number_of_registers * 2)) {
      ESP_LOGW(TAG, "A1 response Modbus data length (%u) does not match expected (%u registers * 2 bytes). Dropping packet.",
               modbus_data_length, number_of_registers);
      this->status_set_warning();
      return;
  }

  // Process the packet based on TCP function code and Modbus function code
  if (tcp_function_code == LUXPOWER_TCP_TRANSLATED_DATA) { // Ensure it's a data packet (0xC2)
      if (modbus_function_code == MODBUS_CMD_READ_INPUT_REGISTER ||
          modbus_function_code == MODBUS_CMD_READ_HOLDING_REGISTER) { // Handles both read functions
        ESP_LOGD(TAG, "Received A1 Read Registers response for registers %u-%u, count %u (Modbus Function: 0x%02X)",
                 modbus_start_register, modbus_start_register + number_of_registers - 1, number_of_registers, modbus_function_code);

        // Iterate through the Modbus payload, extracting and processing each 16-bit register value
        for (int i = 0; i < number_of_registers; ++i) {
          uint16_t reg_address = modbus_start_register + i;
          // Modbus register values are typically Big-Endian (MSB first, LSB second).
          // `modbus_payload_ptr[i * 2]` is the Most Significant Byte,
          // `modbus_payload_ptr[(i * 2) + 1]` is the Least Significant Byte.
          uint16_t value = (modbus_payload_ptr[i * 2] << 8) | modbus_payload_ptr[(i * 2) + 1];
          this->parse_and_publish_register(reg_address, value);
        }
        this->status_set_ok(); // All good, set status to OK
      } else {
        ESP_LOGW(TAG, "Unsupported Modbus function code (0x%02X) in A1 packet from TCP function 0x%02X",
                 modbus_function_code, tcp_function_code);
        this->status_set_warning();
      }
  } else {
      ESP_LOGW(TAG, "Unsupported TCP function code (0x%02X) in A1 packet", tcp_function_code);
      this->status_set_warning();
  }
}

// --- Helper/Dummy Functions (Implement these based on your specific sensor needs) ---

// Placeholder for `parse_and_publish_register`.
// This function needs to be expanded to map the received Modbus register
// addresses and their `value` to your actual ESPHome sensor entities.
void LuxPowerInverter::parse_and_publish_register(uint16_t reg_address, uint16_t value) {
  ESP_LOGV(TAG, "Parsed register 0x%04X: Value 0x%04X (decimal: %u)", reg_address, value, value);

  // Example of how you might publish to a sensor:
  /*
  if (reg_address == 100 && this->grid_voltage_sensor_ != nullptr) {
      // Assuming 'grid_voltage_sensor_' is a 'sensor::Sensor*' member of your class
      // and the value needs to be divided by 10 (e.g., 2305 for 230.5V)
      this->grid_voltage_sensor_->publish_state(static_cast<float>(value) / 10.0f);
  } else if (reg_address == 101 && this->battery_soc_sensor_ != nullptr) {
      // Assuming battery SOC is a direct percentage value
      this->battery_soc_sensor_->publish_state(static_cast<float>(value));
  }
  // ... add more mappings for other LuxPower registers you want to expose
  */
}

// Placeholder for `status_set_warning`.
void LuxPowerInverter::status_set_warning() {
  ESP_LOGW(TAG, "LuxPower Inverter communication status: WARNING (An issue was detected)");
  // You might implement logic to update a status sensor here.
}

// Placeholder for `status_set_ok`.
void LuxPowerInverter::status_set_ok() {
  ESP_LOGD(TAG, "LuxPower Inverter communication status: OK");
  // You might implement logic to update a status sensor here, e.g., after successful packet parsing.
}

} // namespace luxpower_sna
} // namespace esphome
