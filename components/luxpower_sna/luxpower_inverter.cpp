#include "luxpower_inverter.h"
#include "luxpower_sna_sensor.h" // Include the LuxpowerSnaSensor header
#include "esphome/core/log.h"
#include <string>

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

LuxPowerInverter::LuxPowerInverter(const std::string &host, uint16_t port, uint32_t update_interval,
                                   const std::string &dongle_serial, const std::string &inverter_serial_number)
    : host_(host), port_(port), update_interval_(update_interval),
      dongle_serial_(dongle_serial), inverter_serial_number_(inverter_serial_number) {}

void LuxPowerInverter::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxPower Inverter...");
  this->client_.stop();
  this->data_buffer_.reserve(256); // Reserve some space for the buffer
}

float LuxPowerInverter::get_setup_priority() const { return setup_priority::AFTER_WIFI; }

void LuxPowerInverter::loop() {
  const uint32_t now = millis();

  // Reconnect if not connected or if connection dropped
  if (!this->client_.connected()) {
    if (now - this->last_connect_attempt_ > 5000) { // Try to reconnect every 5 seconds
      this->client_.stop();
      ESP_LOGI(TAG, "Attempting to connect to LuxPower Inverter at %s:%u...", this->host_.c_str(), this->port_);
      if (this->client_.connect(this->host_.c_str(), this->port_)) {
        ESP_LOGI(TAG, "Successfully connected to LuxPower Inverter!");
        this->last_connect_attempt_ = now;
        this->status_set_ok();
      } else {
        ESP_LOGW(TAG, "Failed to connect to LuxPower Inverter. Client status: %d", this->client_.status());
        this->status_set_warning("Connection Failed");
        this->last_connect_attempt_ = now; // Update last attempt time even on failure
        return; // Don't proceed with data requests if not connected
      }
    } else {
      return; // Not yet time to reattempt connection
    }
  }

  // Check if it's time to send a request (update_interval_ is in milliseconds)
  if (now - this->last_request_time_ > this->update_interval_) {
    // This is a placeholder for sending a request.
    // You'll replace this with your actual Modbus/LuxPower request logic.
    ESP_LOGD(TAG, "Sending request to LuxPower Inverter (placeholder)...");

    // Example: send a simple Modbus request (e.g., read holding registers)
    // This assumes a Modbus RTU over TCP or similar protocol.
    // You will need to customize this based on the LuxPower protocol.
    uint8_t request_bytes[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD}; // Example: Read 10 registers from address 0
    this->client_.write(request_bytes, sizeof(request_bytes));
    this->last_request_time_ = now;
  }

  // Read available data from the client
  while (this->client_.available()) {
    uint8_t byte = this->client_.read();
    this->data_buffer_.push_back(byte);
    // In a real implementation, you would parse the buffer here
    // to check for complete packets.
  }

  // Example: simple check if buffer has some data
  if (!this->data_buffer_.empty()) {
    ESP_LOGV(TAG, "Received %d bytes: %s", this->data_buffer_.size(), format_hex_pretty(this->data_buffer_).c_str());
    // For now, we'll just clear it. In a real scenario, you'd parse it.
    this->parse_luxpower_response_packet(this->data_buffer_); // Call parsing function
    this->data_buffer_.clear();
  }
}

// Dummy implementation for now, replace with actual parsing
void LuxPowerInverter::parse_luxpower_response_packet(const std::vector<uint8_t> &data) {
    if (data.size() < 7) { // Minimum Modbus TCP ADU size for a response
        ESP_LOGW(TAG, "Received too short packet (%d bytes) to parse.", data.size());
        return;
    }
    // Assuming a simple Modbus read holding registers response for example
    // Transaction ID (2 bytes), Protocol ID (2 bytes), Length (2 bytes), Unit ID (1 byte), Function Code (1 byte), Byte Count (1 byte), Data, CRC (2 bytes if RTU)
    // For Modbus TCP, typically: Transaction ID (2), Protocol ID (2), Length (2), Unit ID (1), Function Code (1), Byte Count (1), Data...
    
    // For demonstration, let's assume a simple response format and extract some dummy data.
    // In a real scenario, you'd implement full LuxPower protocol parsing here.

    // Example: Assuming a response to reading register 0x00 from slave 0x01, value 0x1234
    // Typical Modbus TCP response structure:
    // TransID (2), ProtID (2), Length (2), UnitID (1), FuncCode (1), ByteCount (1), Data...
    // Example: 00 01 00 00 00 05 01 03 02 12 34 (Reads 1 register, value 0x1234)
    if (data.size() >= 9) { // Minimum for Modbus TCP response with 1 register (header 7 + func 1 + byte count 1 + 2 data bytes)
        uint8_t function_code = data[7];
        if (function_code == 0x03) { // Read Holding Registers
            uint8_t byte_count = data[8];
            if (data.size() >= (9 + byte_count)) {
                // For simplicity, let's just log and assume one register was read at address 0x00 and its value is the first two data bytes
                // You'll need to map this to actual LuxPower registers.
                if (byte_count >= 2) {
                    uint16_t value = (data[9] << 8) | data[10];
                    // This is a placeholder. You need to know the actual register address
                    // that this response corresponds to.
                    // For now, let's just publish a dummy value to a known sensor.
                    ESP_LOGD(TAG, "Parsed dummy register value: 0x%04X", value);
                    // If you have a sensor for a specific register (e.g., register 0)
                    parse_and_publish_register(0x00, value); // Example: publish to register 0x00
                }
            }
        }
    }
}


void LuxPowerInverter::parse_and_publish_register(uint16_t reg_address, uint16_t value) {
  for (auto *sens : this->luxpower_sensors_) {
    if (sens->get_register_address() == reg_address) {
      float processed_value = value;
      switch (sens->get_reg_type()) {
        case LuxpowerRegType::LUX_REG_TYPE_INT:
          // No change for INT type
          break;
        case LuxpowerRegType::LUX_REG_TYPE_FLOAT_DIV10:
          processed_value /= 10.0f;
          break;
        case LuxpowerRegType::LUX_REG_TYPE_SIGNED_INT:
          // Convert to signed 16-bit integer
          processed_value = (int16_t) value;
          break;
        case LuxpowerRegType::LUX_REG_TYPE_FIRMWARE:
            // Assuming firmware is parsed as XX.YY, where value is 0xXXYY
            processed_value = ((value >> 8) & 0xFF) + (value & 0xFF) / 100.0f;
            break;
        case LuxpowerRegType::LUX_REG_TYPE_MODEL:
            // Model numbers are usually strings or specific codes.
            // This case needs special handling (e.g., mapping to a string sensor or text sensor).
            // For a numeric sensor, you might just publish the raw value or a part of it.
            ESP_LOGW(TAG, "Model register type 0x%04X is not directly numeric for sensor.", value);
            // Optionally, convert to float for publishing if a numeric sensor is used for model.
            break;
        case LuxpowerRegType::LUX_REG_TYPE_BITMASK:
            // Bitmask values need custom logic based on which bit represents what state.
            // For a numeric sensor, publish the raw value.
            break;
        case LuxpowerRegType::LUX_REG_TYPE_TIME_MINUTES:
            // Time in minutes needs conversion (e.g., 0-1439).
            // This might best be handled by a time component or a custom sensor.
            break;
        default:
          ESP_LOGW(TAG, "Unknown LuxpowerRegType encountered for register 0x%04X. Raw value: 0x%04X", reg_address, value);
          break;
      }
      sens->publish_state(processed_value);
      return;
    }
  }
  ESP_LOGV(TAG, "No sensor found for register 0x%04X. Value: 0x%04X", reg_address, value);
}

void LuxPowerInverter::add_luxpower_sensor(sensor::Sensor *obj, const std::string &name, uint16_t reg_address, int reg_type_int, uint8_t bank) {
    LuxpowerSnaSensor *lux_sens = static_cast<LuxpowerSnaSensor *>(obj);
    lux_sens->set_name(name);
    lux_sens->set_register_address(reg_address);
    lux_sens->set_reg_type(static_cast<LuxpowerRegType>(reg_type_int)); // Cast int to enum
    lux_sens->set_bank(bank);
    this->luxpower_sensors_.push_back(lux_sens);
    ESP_LOGCONFIG(TAG, "  Adding LuxPower Sensor: %s (Register: 0x%04X, Type: %d, Bank: %d)",
                  name.c_str(), reg_address, reg_type_int, bank);
}

void LuxPowerInverter::status_set_warning(const std::string &message) {
  if (message.empty()) {
    this->status_set_error(); // Set generic error if no specific message
  } else {
    this->status_set_error(message.c_str());
  }
}

void LuxPowerInverter::status_set_ok() {
  this->status_clear_error();
}

} // namespace luxpower_sna
} // namespace esphome
