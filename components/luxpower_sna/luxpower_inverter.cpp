#include "luxpower_inverter.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <WiFiClient.h>
#include "luxpower_sna_constants.h"
// Include the LuxpowerSnaSensor header as it's used directly now
#include "luxpower_sna_sensor.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// Updated constructor definition
LuxPowerInverter::LuxPowerInverter(const std::string &host, uint16_t port, uint32_t update_interval,
                                   const std::string &dongle_serial, const std::string &inverter_serial_number)
    : host_(host),
      port_(port),
      update_interval_(update_interval), // Already in milliseconds from __init__.py
      dongle_serial_(dongle_serial),
      inverter_serial_number_(inverter_serial_number) {
}

void LuxPowerInverter::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxPower Inverter...");
  ESP_LOGCONFIG(TAG, "Host: %s, Port: %u", host_.c_str(), port_);
  ESP_LOGCONFIG(TAG, "Dongle Serial: %s, Inverter Serial: %s", dongle_serial_.c_str(), inverter_serial_number_.c_str());
  ESP_LOGCONFIG(TAG, "Update Interval: %u ms", update_interval_);

  this->last_connect_attempt_ = 0;
  this->last_request_time_ = 0;
  this->client_.stop();
}

float LuxPowerInverter::get_setup_priority() const {
  return setup_priority::AFTER_WIFI;
}

void LuxPowerInverter::loop() {
  const uint32_t now = millis();

  // 1. Manage TCP Connection
  if (!this->client_.connected()) {
    if ((now - this->last_connect_attempt_) > RECONNECT_INTERVAL_MS) {
      ESP_LOGI(TAG, "Attempting to connect to %s:%u...", this->host_.c_str(), this->port_);
      this->client_.stop();
      if (this->client_.connect(this->host_.c_str(), this->port_)) {
        ESP_LOGI(TAG, "Successfully connected to LuxPower Inverter!");
        this->status_set_ok();
      } else {
        ESP_LOGW(TAG, "Failed to connect to LuxPower Inverter. Client status: %d", this->client_.status());
        this->status_set_warning("Connection Failed");
      }
      this->last_connect_attempt_ = now;
      this->data_buffer_.clear();
    }
    return;
  }

  // 2. Read incoming data
  while (this->client_.available()) {
    uint8_t byte = this->client_.read();
    this->data_buffer_.push_back(byte);
    if (this->data_buffer_.size() >= PACKET_MIN_LENGTH) {
      parse_luxpower_response_packet(this->data_buffer_);
      this->data_buffer_.clear();
    }
  }

  // 3. Periodically send requests
  if ((now - this->last_request_time_) > this->update_interval_) {
      ESP_LOGD(TAG, "Sending periodic request to inverter.");
      // Example: Send a heartbeat or data request (replace with actual LuxPower protocol)
      // This part needs actual LuxPower protocol implementation
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

    // Placeholder for actual LuxPower protocol parsing
    // This section should iterate through registers and update associated sensors
    if (data.size() >= 5) {
        uint16_t example_reg_address = 0x0001; // This should be extracted from the packet
        uint16_t example_value = (data[3] << 8) | data[4]; // This should be extracted from the packet
        parse_and_publish_register(example_reg_address, example_value);
    }
}

void LuxPowerInverter::parse_and_publish_register(uint16_t reg_address, uint16_t value) {
    ESP_LOGD(TAG, "Register 0x%04X: Value %u", reg_address, value);
    // This is where you would map the received register address and value to your ESPHome sensors
    // and publish them, e.g., this->voltage_sensor_->publish_state(value / 10.0f);

    // Iterate through registered sensors and update if address matches
    for (auto *sens : this->luxpower_sensors_) {
        if (sens->get_register_address() == reg_address) {
            float processed_value = value; // Default, adjust based on reg_type
            switch (sens->get_reg_type()) {
                case LuxpowerRegType::LUX_REG_TYPE_INT:
                    // Value is already an int, no conversion needed.
                    break;
                case LuxpowerRegType::LUX_REG_TYPE_FLOAT_DIV10:
                    processed_value /= 10.0f;
                    break;
                case LuxpowerRegType::LUX_REG_TYPE_SIGNED_INT:
                    // Assuming 16-bit signed int
                    if (value & 0x8000) { // Check if MSB is set
                        processed_value = -((~value + 1) & 0xFFFF);
                    }
                    break;
                // Add cases for FIRMWARE, MODEL, BITMASK, TIME_MINUTES later
                default:
                    ESP_LOGW(TAG, "Unknown register type for register 0x%04X", reg_address);
                    break;
            }
            sens->publish_state(processed_value);
            return; // Assuming one sensor per register address for simplicity
        }
    }
}


// Implementation of add_luxpower_sensor
void LuxPowerInverter::add_luxpower_sensor(sensor::Sensor *obj, const std::string &name, uint16_t reg_address, int reg_type, uint8_t bank) {
    auto *lux_sens = static_cast<LuxpowerSnaSensor *>(obj);
    lux_sens->set_register_address(reg_address);
    lux_sens->set_reg_type(static_cast<LuxpowerRegType>(reg_type)); // Cast int to enum
    lux_sens->set_bank(bank);
    this->luxpower_sensors_.push_back(lux_sens);
    ESP_LOGCONFIG(TAG, "Adding sensor: %s (Reg: 0x%04X, Type: %d, Bank: %d)", name.c_str(), reg_address, reg_type, bank);
}


void LuxPowerInverter::status_set_warning(const std::string &message) {
    Component::status_set_warning(message.c_str());
}

void LuxPowerInverter::status_set_ok() {
    this->status_clear_warning();
}

} // namespace luxpower_sna
} // namespace esphome
