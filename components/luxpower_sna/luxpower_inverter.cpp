// custom_components/luxpower_inverter/luxpower_inverter.cpp
#include "luxpower_inverter.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h" // For format_hex_pretty
#include "sensor/sensor.h" // NEW: Include the renamed sensor header

// For Modbus CRC calculation (standard library, not specific to ESPHome)
#define CRC16_POLY 0xA001 // Modbus polynomial

namespace esphome {
namespace luxpower_inverter {

LuxPowerInverterComponent::LuxPowerInverterComponent() {
  this->client_ = new AsyncClient();
}

void LuxPowerInverterComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxPower Inverter Component...");
  ESP_LOGCONFIG(TAG, "  Host: %s", this->inverter_host_.c_str());
  ESP_LOGCONFIG(TAG, "  Port: %d", this->inverter_port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", this->dongle_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter Serial: %s", this->inverter_serial_number_.c_str());
  ESP_LOGCONFIG(TAG, "  Update Interval: %u ms", this->update_interval_);

  this->client_->onConnect(std::bind(&LuxPowerInverterComponent::on_connect_cb, this, std::placeholders::_1, std::placeholders::_2));
  this->client_->onDisconnect(std::bind(&LuxPowerInverterComponent::on_disconnect_cb, this, std::placeholders::_1, std::placeholders::_2));
  this->client_->onData(std::bind(&LuxPowerInverterComponent::on_data_cb, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
  this->client_->onError(std::bind(&LuxPowerInverterComponent::on_error_cb, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

  this->connect_to_inverter();
}

void LuxPowerInverterComponent::loop() {
  if (!this->is_connected()) {
    if (millis() - this->last_connect_attempt_ > this->connect_retry_interval_) {
      ESP_LOGD(TAG, "Client not connected, attempting reconnect to %s:%d", this->inverter_host_.c_str(), this->inverter_port_);
      this->connect_to_inverter();
    }
  } else {
    const size_t MODBUS_TCP_HEADER_LENGTH = 7;

    while (this->rx_buffer_.size() >= MODBUS_TCP_HEADER_LENGTH) {
      uint16_t pdu_length = (this->rx_buffer_[4] << 8) | this->rx_buffer_[5];
      size_t total_packet_length = MODBUS_TCP_HEADER_LENGTH + pdu_length;

      if (this->rx_buffer_.size() < total_packet_length) {
        break;
      }

      std::vector<uint8_t> packet_data(total_packet_length);
      for (size_t i = 0; i < total_packet_length; ++i) {
        packet_data[i] = this->rx_buffer_.front();
        this->rx_buffer_.pop_front();
      }

      if (this->parse_modbus_read_holding_registers_response(packet_data, 1, 0, 20)) {
          for (auto *s : this->luxpower_sensors_) {
              auto it = this->current_raw_registers_.find(s->get_register_address());
              if (it != this->current_raw_registers_.end()) {
                  s->update_state(it->second);
              } else {
                  ESP_LOGW(TAG, "Sensor %s (reg 0x%04X) not found in received data. Publishing NAN.", s->get_name().c_str(), s->get_register_address());
                  s->publish_state(NAN);
              }
          }
      } else {
          ESP_LOGW(TAG, "Failed to parse Modbus TCP response.");
      }
    }
  }
}

void LuxPowerInverterComponent::update() {
  if (!this->is_connected()) {
    ESP_LOGW(TAG, "Not connected to inverter, skipping update request.");
    return;
  }

  if (millis() - this->last_request_time_ < this->update_interval_) {
    return;
  }

  ESP_LOGD(TAG, "Sending LuxPower Inverter data request.");
  this->last_request_time_ = millis();

  std::vector<uint8_t> request_packet = create_modbus_read_holding_registers_request(1, 0, 20);
  if (!this->send_data(request_packet)) {
    ESP_LOGE(TAG, "Failed to send Modbus read request.");
  }
}

void LuxPowerInverterComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "LuxPower Inverter:");
  ESP_LOGCONFIG(TAG, "  Host: %s", this->inverter_host_.c_str());
  ESP_LOGCONFIG(TAG, "  Port: %d", this->inverter_port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", this->dongle_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter Serial: %s", this->inverter_serial_number_.c_str());
  ESP_LOGCONFIG(TAG, "  Update Interval: %u ms", this->update_interval_);
  if (this->is_connected()) {
    ESP_LOGCONFIG(TAG, "  Status: Connected");
  } else {
    ESP_LOGCONFIG(TAG, "  Status: Disconnected");
  }
  for (auto *s : this->luxpower_sensors_) {
    ESP_LOGCONFIG(TAG, "  Sensor:");
    ESP_LOGCONFIG(TAG, "    Name: %s", s->get_name().c_str());
    ESP_LOGCONFIG(TAG, "    Register: 0x%04X", s->get_register_address());
    ESP_LOGCONFIG(TAG, "    Bank: %u", s->get_bank());
  }
}

bool LuxPowerInverterComponent::connect_to_inverter() {
  if (this->is_connected()) {
    ESP_LOGD(TAG, "Already connected to %s:%d", this->inverter_host_.c_str(), this->inverter_port_);
    return true;
  }

  this->last_connect_attempt_ = millis();
  ESP_LOGI(TAG, "Connecting to LuxPower Inverter at %s:%d...", this->inverter_host_.c_str(), this->inverter_port_);

  if (!this->client_->connect(this->inverter_host_.c_str(), this->inverter_port_)) {
    ESP_LOGE(TAG, "Failed to initiate connection to %s:%d", this->inverter_host_.c_str(), this->inverter_port_);
    return false;
  }
  return true;
}

void LuxPowerInverterComponent::disconnect_from_inverter() {
  if (this->client_) {
    this->client_->stop();
    ESP_LOGI(TAG, "Disconnected from LuxPower Inverter.");
  }
  this->client_connected_ = false;
  this->rx_buffer_.clear();
  for (auto *s : this->luxpower_sensors_) {
    s->publish_state(NAN);
  }
}

bool LuxPowerInverterComponent::send_data(const std::vector<uint8_t>& data) {
  if (!this->is_connected()) {
    ESP_LOGW(TAG, "Not connected, cannot send data.");
    return false;
  }
  if (data.empty()) {
    ESP_LOGW(TAG, "Attempted to send empty data.");
    return false;
  }

  ESP_LOGD(TAG, "Sending %u bytes: %s", data.size(), format_hex_pretty(data.data(), data.size()).c_str());

  return this->client_->write(reinterpret_cast<const char*>(data.data()), data.size()) > 0;
}

// --- AsyncTCP Callbacks ---

void LuxPowerInverterComponent::on_connect_cb(void *arg, AsyncClient *client) {
  ESP_LOGI(TAG, "Connected to LuxPower Inverter!");
  this->client_connected_ = true;
  this->rx_buffer_.clear();
  this->last_request_time_ = 0;
}

void LuxPowerInverterComponent::on_disconnect_cb(void *arg, AsyncClient *client) {
  ESP_LOGW(TAG, "Disconnected from LuxPower Inverter!");
  this->client_connected_ = false;
  this->rx_buffer_.clear();
  for (auto *s : this->luxpower_sensors_) {
    s->publish_state(NAN);
  }
}

void LuxPowerInverterComponent::on_data_cb(void *arg, AsyncClient *client, void *data, size_t len) {
  ESP_LOGV(TAG, "Received %u bytes: %s", len, format_hex_pretty(data, len).c_str());

  uint8_t *byte_data = reinterpret_cast<uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) {
    this->rx_buffer_.push_back(byte_data[i]);
  }
}

void LuxPowerInverterComponent::on_error_cb(void *arg, AsyncClient *client, int error) {
  ESP_LOGE(TAG, "TCP client error: %d (%s)", error, client->errorToString(error));
  this->client_connected_ = false;
  this->client_->stop();
}

// --- Luxpower Sensor Management ---
void LuxPowerInverterComponent::add_luxpower_sensor(esphome::components::sensor::Sensor *obj, const std::string &name, uint16_t reg_addr, LuxpowerRegType reg_type, uint8_t bank) {
  auto *s = new LuxpowerSensor();
  s->set_parent(this);
  s->set_register_address(reg_addr);
  s->set_reg_type(reg_type);
  s->set_bank(bank);
  s->set_name(name);
  s->set_unit_of_measurement(obj->get_unit_of_measurement());
  s->set_device_class(obj->get_device_class());
  s->set_state_class(obj->get_state_class());
  s->set_icon(obj->get_icon());
  s->set_accuracy_decimals(obj->get_accuracy_decimals());
  s->set_unique_id(obj->get_unique_id());
  this->luxpower_sensors_.push_back(s);
  App.register_component(s);
}

// --- Luxpower Protocol Helpers (from LXPPacket.py and Modbus standard) ---

uint16_t LuxPowerInverterComponent::calculate_crc16_modbus(const std::vector<uint8_t>& data) {
    uint16_t crc = 0xFFFF;
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ CRC16_POLY;
            } else {
                crc = crc >> 1;
            }
        }
    }
    return crc;
}

std::vector<uint8_t> LuxPowerInverterComponent::create_modbus_read_holding_registers_request(uint8_t unit_id, uint16_t start_address, uint16_t num_registers) {
    std::vector<uint8_t> pdu;
    pdu.push_back(0x03);
    pdu.push_back(start_address >> 8);
    pdu.push_back(start_address & 0xFF);
    pdu.push_back(num_registers >> 8);
    pdu.push_back(num_registers & 0xFF);

    std::vector<uint8_t> adu;
    adu.push_back(0x00); adu.push_back(0x01);
    adu.push_back(0x00); adu.push_back(0x00);

    uint16_t length = pdu.size() + 1;
    adu.push_back(length >> 8); adu.push_back(length & 0xFF);

    adu.push_back(unit_id);
    adu.insert(adu.end(), pdu.begin(), pdu.end());

    return adu;
}

bool LuxPowerInverterComponent::parse_modbus_read_holding_registers_response(const std::vector<uint8_t>& response, uint8_t unit_id, uint16_t expected_start_address, uint16_t expected_num_registers) {
    if (response.size() < 9) {
        ESP_LOGW(TAG, "Modbus response too short: %u bytes", response.size());
        return false;
    }

    if (response[2] != 0x00 || response[3] != 0x00) {
        ESP_LOGW(TAG, "Modbus Protocol ID mismatch.");
        return false;
    }

    uint16_t received_length = (response[4] << 8) | response[5];
    if (received_length != (response.size() - 6)) {
        ESP_LOGW(TAG, "Modbus Length field mismatch. Expected %u, got %u", (response.size() - 6), received_length);
        return false;
    }

    if (response[6] != unit_id) {
        ESP_LOGW(TAG, "Modbus Unit ID mismatch. Expected %u, got %u", unit_id, response[6]);
        return false;
    }

    uint8_t function_code = response[7];
    if (function_code == 0x83) {
        uint8_t exception_code = response[8];
        ESP_LOGE(TAG, "Modbus exception response received: FC 0x%02X, Exception Code 0x%02X", function_code, exception_code);
        return false;
    }
    if (function_code != 0x03) {
        ESP_LOGW(TAG, "Modbus Function Code mismatch. Expected 0x03, got 0x%02X", function_code);
        return false;
    }

    uint8_t byte_count = response[8];
    if (byte_count != (expected_num_registers * 2)) {
        ESP_LOGW(TAG, "Modbus Byte Count mismatch. Expected %u, got %u", (expected_num_registers * 2), byte_count);
        return false;
    }
    if (response.size() < (7 + 2 + byte_count)) {
        ESP_LOGW(TAG, "Modbus response data truncated. Expected %u bytes, got %u", (7 + 2 + byte_count), response.size());
        return false;
    }

    this->current_raw_registers_.clear();
    for (uint16_t i = 0; i < expected_num_registers; ++i) {
        uint16_t reg_value = (response[9 + (i * 2)] << 8) | response[10 + (i * 2)];
        this->current_raw_registers_[expected_start_address + i] = reg_value;
    }

    ESP_LOGD(TAG, "Successfully parsed Modbus response for %u registers starting at 0x%04X", expected_num_registers, expected_start_address);
    return true;
}


float LuxPowerInverterComponent::decode_luxpower_value(uint16_t raw_value, LuxpowerRegType reg_type) {
    switch (reg_type) {
        case LUX_REG_TYPE_INT:
            return static_cast<float>(raw_value);
        case LUX_REG_TYPE_FLOAT_DIV10:
            return static_cast<float>(raw_value) / 10.0f;
        case LUX_REG_TYPE_SIGNED_INT:
            return static_cast<float>(static_cast<int16_t>(raw_value));
        case LUX_REG_TYPE_FIRMWARE:
        case LUX_REG_TYPE_MODEL:
            ESP_LOGW(TAG, "Attempted to decode firmware/model as float. Use TextSensor for these types.");
            return NAN;
        case LUX_REG_TYPE_BITMASK:
        case LUX_REG_TYPE_TIME_MINUTES:
            return static_cast<float>(raw_value);
        default:
            ESP_LOGW(TAG, "Unknown LuxpowerRegType: %d", reg_type);
            return NAN;
    }
}

} // namespace luxpower_inverter
} // namespace esphome