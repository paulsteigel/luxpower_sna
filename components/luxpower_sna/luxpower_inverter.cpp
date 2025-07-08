#include "luxpower_inverter.h" // Include your component's header file
#include "esphome/core/log.h"  // Required for ESP_LOGx macros

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// --- Implementations of LuxPowerInverter Class Member Functions ---

// setup() method: Initializes the component, e.g., UART settings
void LuxPowerInverter::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxPower SNA Inverter component...");
  // You might want to set the UART baud rate here if it's not done in YAML
  // this->set_baud_rate(9600); // Example
}

// get_setup_priority() method: Defines when this component should be set up
float LuxPowerInverter::get_setup_priority() const {
  // Ensure UART is initialized before this component starts
  return setup_priority::AFTER_UART;
}

// loop() method: Called repeatedly to process incoming data
void LuxPowerInverter::loop() {
  const uint32_t now = millis();
  // Clear the buffer if no data has been received for a specified timeout period
  if ((now - this->last_byte_received_) > this->read_timeout_) {
    if (this->data_buffer_.size() > 0) {
      ESP_LOGW(TAG, "Buffer timeout! Discarding %zu bytes.", this->data_buffer_.size());
      this->data_buffer_.clear();
    }
  }

  // Read all available bytes from the UART buffer
  while (this->available()) {
    uint8_t byte;
    this->read_byte(&byte);
    this->data_buffer_.push_back(byte); // Add the byte to the internal buffer
    this->last_byte_received_ = now;   // Update last received time

    // Check for the start of an A1 packet (prefix 0xA1 0x1A)
    if (this->data_buffer_.size() >= 2) {
      if (this->data_buffer_[0] == 0xA1 && this->data_buffer_[1] == 0x1A) {
        // A potential A1 packet has been detected.
        // We need at least 6 bytes to read the `frame_length` field (bytes 4 and 5).
        if (this->data_buffer_.size() >= 6) {
          // Extract `frame_length` (little-endian)
          uint16_t frame_length = (this->data_buffer_[5] << 8) | this->data_buffer_[4];
          // Calculate the total expected length of the A1 packet
          // (frame_length covers data from byte 6 to CRC, plus 6 bytes for header)
          uint16_t calculated_packet_length = frame_length + 6;

          // If we have received enough bytes for the complete packet
          if (this->data_buffer_.size() >= calculated_packet_length) {
            ESP_LOGV(TAG, "Full A1 packet received! Length: %u", calculated_packet_length);
            // Extract the complete packet into a temporary vector
            std::vector<uint8_t> packet(this->data_buffer_.begin(), this->data_buffer_.begin() + calculated_packet_length);
            this->parse_luxpower_response_packet(packet); // Parse the packet

            // Remove the processed packet from the buffer
            this->data_buffer_.erase(this->data_buffer_.begin(), this->data_buffer_.begin() + calculated_packet_length);
            // Continue checking the buffer for more packets (e.g., if multiple arrived back-to-back)
            continue;
          }
        }
      } else {
        // The current start of the buffer is not an A1 prefix.
        // Discard the first byte to slide the window and check the next byte.
        ESP_LOGW(TAG, "Unknown prefix 0x%02X%02X. Discarding byte 0x%02X from buffer.",
                 this->data_buffer_[0], (this->data_buffer_.size() > 1 ? this->data_buffer_[1] : 0x00), this->data_buffer_[0]);
        this->data_buffer_.erase(this->data_buffer_.begin());
        // Continue to the next iteration of the `while (this->available())` loop
        // to re-evaluate the buffer from its new start.
        continue;
      }
    }
  }
}

// parse_luxpower_response_packet() method: Decodes the A1 packet contents
void LuxPowerInverter::parse_luxpower_response_packet(const std::vector<uint8_t> &data) {
  this->rx_count_++; // Increment the received packet counter

  // Sanity check: Ensure the packet is at least the minimum expected size
  if (data.size() < LUXPOWER_A1_MIN_LENGTH) {
    ESP_LOGW(TAG, "A1 response too short (%zu bytes)! Min expected: %u.", data.size(), LUXPOWER_A1_MIN_LENGTH);
    this->status_set_warning();
    return;
  }

  // Extract `frame_length` from the packet itself for consistent length verification
  uint16_t frame_length = (data[5] << 8) | data[4];
  uint16_t expected_packet_length = frame_length + 6;

  // Final check: Ensure the received packet size matches the size indicated in its header
  if (data.size() != expected_packet_length) {
    ESP_LOGW(TAG, "A1 response length mismatch! Header says %u, received %zu. Dropping packet.",
             expected_packet_length, data.size());
    this->status_set_warning();
    return;
  }

  // --- Extracting fields based on LXPPacket.py analysis ---
  // Byte 6: TCP Function Code (e.g., 0xC2 for TRANSLATED_DATA)
  uint8_t tcp_function_code = data[6];

  // Bytes 17-18: Data Length (total byte length of the Modbus payload)
  uint16_t modbus_data_length = (data[18] << 8) | data[17];

  // Byte 20: Modbus Function Code (e.g., 0x03 Read Holding Registers, 0x04 Read Input Registers)
  uint8_t modbus_function_code = data[20];

  // Bytes 31-32: Starting Modbus Register Address (little-endian)
  uint16_t modbus_start_register = (data[32] << 8) | data[31];

  // Bytes 33-34: Number of Modbus Registers (little-endian)
  uint16_t number_of_registers = (data[34] << 8) | data[33];

  // Pointer to the start of the actual Modbus data payload (register values)
  const uint8_t *modbus_payload_ptr = &data[35];

  // Consistency check: The `modbus_data_length` should be twice the `number_of_registers`
  if (modbus_data_length != (number_of_registers * 2)) {
      ESP_LOGW(TAG, "A1 response Modbus data length (%u) does not match expected (%u registers * 2 bytes). Dropping packet.",
               modbus_data_length, number_of_registers);
      this->status_set_warning();
      return;
  }

  // Process the packet based on the TCP function code and Modbus function code
  if (tcp_function_code == LUXPOWER_TCP_TRANSLATED_DATA) { // Ensure it's a data packet (0xC2)
      if (modbus_function_code == MODBUS_CMD_READ_INPUT_REGISTER) { // 0x04 - Read Input Registers
        ESP_LOGD(TAG, "Received A1 Read Input Registers response for registers %u-%u, count %u",
                 modbus_start_register, modbus_start_register + number_of_registers - 1, number_of_registers);

        // Iterate through the Modbus payload, extracting and processing each 16-bit register
        for (int i = 0; i < number_of_registers; ++i) {
          uint16_t reg_address = modbus_start_register + i;
          // Modbus register values are typically Big-Endian (most significant byte first)
          // The Python `struct.unpack(">H", ...)` confirms this.
          // In C++, for `[byte0, byte1]` where byte0 is LSB and byte1 is MSB (common in some UART/Modbus),
          // the value is (byte1 << 8) | byte0.
          // Python's ">H" implies MSB at first byte of pair, LSB at second.
          // If `modbus_payload_ptr[i*2]` is the MSB and `modbus_payload_ptr[i*2+1]` is the LSB:
          // uint16_t value = (modbus_payload_ptr[i * 2] << 8) | modbus_payload_ptr[(i * 2) + 1];
          // However, LXPPacket.py uses `packet[self.packet_length - 2: self.packet_length]` for CRC,
          // and `value = struct.unpack(">H", self.value[i:i + 2])[0]`.
          // This unpacks 2 bytes as Big Endian. Given `self.value` is just `packet[35:35+value_length]`,
          // it means the first byte of each 2-byte pair is the MSB, second is LSB.
          // So, for example, if data is `[A, B, C, D]`, it's parsing `AB` then `CD`.
          // Thus, modbus_payload_ptr[i*2] is MSB, modbus_payload_ptr[i*2+1] is LSB.
          uint16_t value = (modbus_payload_ptr[i * 2] << 8) | modbus_payload_ptr[(i * 2) + 1];
          this->parse_and_publish_register(reg_address, value);
        }
        this->status_set_ok();
      } else if (modbus_function_code == MODBUS_CMD_READ_HOLDING_REGISTER) { // 0x03 - Read Holding Registers
        ESP_LOGD(TAG, "Received A1 Read Holding Registers response for registers %u-%u, count %u",
                 modbus_start_register, modbus_start_register + number_of_registers - 1, number_of_registers);
        for (int i = 0; i < number_of_registers; ++i) {
          uint16_t reg_address = modbus_start_register + i;
          uint16_t value = (modbus_payload_ptr[i * 2] << 8) | modbus_payload_ptr[(i * 2) + 1];
          this->parse_and_publish_register(reg_address, value);
        }
        this->status_set_ok();
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

// --- Helper/Dummy Functions (You'll need to expand these based on your specific sensors) ---

// Placeholder for `parse_and_publish_register`.
// This is where you will map the decoded `reg_address` and `value` to your actual ESPHome `sensor` entities
// and publish their states.
void LuxPowerInverter::parse_and_publish_register(uint16_t reg_address, uint16_t value) {
  ESP_LOGV(TAG, "Parsed register 0x%04X: Value 0x%04X (decimal: %u)", reg_address, value, value);

  // Example: How you might publish to a sensor.
  // You would typically have `sensor::Sensor *my_sensor_ptr_` members in your class
  // and assign them via `set_my_sensor_ptr(sensor::Sensor *sensor)` in your YAML/C++ setup.
  /*
  if (reg_address == 100) { // Example for register 100
      if (this->some_voltage_sensor_ != nullptr) {
          this->some_voltage_sensor_->publish_state(static_cast<float>(value) / 10.0f); // Example: if value needs scaling
      }
  } else if (reg_address == 101) { // Example for register 101
      if (this->another_sensor_ != nullptr) {
          this->another_sensor_->publish_state(static_cast<float>(value));
      }
  }
  // ... continue for all registers you want to expose
  */
}

// Placeholder for `status_set_warning`.
void LuxPowerInverter::status_set_warning() {
  ESP_LOGW(TAG, "LuxPower Inverter communication status: WARNING (error detected)");
  // You might update a binary sensor or text sensor here to indicate a warning state.
}

// Placeholder for `status_set_ok`.
void LuxPowerInverter::status_set_ok() {
  ESP_LOGD(TAG, "LuxPower Inverter communication status: OK (last packet parsed successfully)");
  // You might update a binary sensor or text sensor here to indicate an OK state.
}

} // namespace luxpower_sna
} // namespace esphome
