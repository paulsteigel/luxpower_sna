#include "luxpower_inverter.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"
#include <deque>
#include <functional>
#include <map>
#include <AsyncTCP.h> // Ensure this is included

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

LuxPowerInverterComponent::LuxPowerInverterComponent() {
  this->client_ = new AsyncClient();
}

void LuxPowerInverterComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxPower Inverter Component...");
  ESP_LOGCONFIG(TAG, "  Host: %s", this->inverter_host_.c_str());
  ESP_LOGCONFIG(TAG, "  Port: %u", this->inverter_port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", this->dongle_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter Serial: %s", this->inverter_serial_number_.c_str());
  ESP_LOGCONFIG(TAG, "  Update Interval: %u ms", (uint32_t) this->update_interval_.count());

  this->client_->onConnect(std::bind(&LuxPowerInverterComponent::on_connect_cb, this, std::placeholders::_1, std::placeholders::_2));
  this->client_->onDisconnect(std::bind(&LuxPowerInverterComponent::on_disconnect_cb, this, std::placeholders::_1, std::placeholders::_2));
  this->client_->onData(std::bind(&LuxPowerInverterComponent::on_data_cb, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
  this->client_->onError(std::bind(&LuxPowerInverterComponent::on_error_cb, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

  this->connect_to_inverter();
}

void LuxPowerInverterComponent::loop() {
  if (!this->is_connected()) {
    if (millis() - this->last_connect_attempt_ > this->connect_retry_interval_) {
      ESP_LOGD(TAG, "Client not connected, attempting reconnect to %s:%u", this->inverter_host_.c_str(), this->inverter_port_);
      this->connect_to_inverter();
    }
  } else {
    // 1. Send data request if update interval has passed
    if (millis() - this->last_request_time_ > this->update_interval_.count()) {
      ESP_LOGD(TAG, "Sending LuxPower Inverter data request (Bank 0, Regs 0-125).");
      this->last_request_time_ = millis();

      // NEW: Use the custom LuxPower packet builder
      // Function Code 0x03 for Read Holding Registers
      // Read 126 registers starting from address 0
      std::vector<uint8_t> request_packet = build_luxpower_request_packet(0x03, 0, 126); 
      if (!this->send_data(request_packet)) {
        ESP_LOGE(TAG, "Failed to send LuxPower read request.");
        this->disconnect_from_inverter();
        return; 
      }
    }

    // 2. Process received data from rx_buffer_
    // Minimum LuxPower packet length is 29 bytes
    while (this->rx_buffer_.size() >= LUXPOWER_PACKET_MIN_TOTAL_LENGTH) {
      // Check for start byte 0x68
      if (this->rx_buffer_.front() != LUXPOWER_START_BYTE) {
        ESP_LOGW(TAG, "Received malformed LuxPower packet: missing start byte 0x68. Flushing one byte.");
        this->rx_buffer_.pop_front(); // Discard malformed byte
        continue;
      }

      // Get expected total packet length from bytes 1 and 2
      // This length field (2 bytes) includes the payload, CRC, and excludes the start byte and the length field itself, but includes the end byte.
      // So, total_packet_length = Start Byte (1) + Length Field (2) + Reported Length (from bytes 1,2)
      uint16_t reported_length = (this->rx_buffer_[1] << 8) | this->rx_buffer_[2];
      size_t total_packet_length = 3 + reported_length; // 1 (Start Byte) + 2 (Length Field) + reported_length

      if (this->rx_buffer_.size() < total_packet_length) {
        // Not enough data for a complete packet yet, wait for more
        break; 
      }

      // We have a full packet, extract it
      std::vector<uint8_t> packet_data(total_packet_length);
      for (size_t i = 0; i < total_packet_length; ++i) {
        packet_data[i] = this->rx_buffer_.front();
        this->rx_buffer_.pop_front();
      }

      // NEW: Parse the LuxPower proprietary response packet
      std::vector<uint8_t> luxpower_payload;
      // Assuming we're always expecting a Read Holding Registers (0x03) response for this loop
      if (this->parse_luxpower_response_packet(packet_data, luxpower_payload)) {
        // Now interpret the Modbus-like payload (which starts with Function Code)
        if (luxpower_payload.empty() || luxpower_payload[0] != 0x03) {
            ESP_LOGW(TAG, "LuxPower response payload does not contain expected function code 0x03. Got 0x%02X", luxpower_payload.empty() ? 0xFF : luxpower_payload[0]);
            continue; // Skip to next packet if any
        }

        // The payload for FC 0x03 is FC (1) + Byte Count (1) + Data (N bytes)
        // Adjust indices for `interpret_modbus_read_holding_registers_payload`
        if (this->interpret_modbus_read_holding_registers_payload(luxpower_payload, 0, 126)) {
          // Successfully parsed data, now update sensors
          for (LuxpowerSnaSensor *s : this->luxpower_sensors_) {
            if (s->get_bank() == 0) { // Only process Bank 0 sensors for this request
              auto it = this->current_raw_registers_.find(s->get_register_address());
              if (it != this->current_raw_registers_.end()) {
                uint16_t raw_value = it->second;

                if (s->get_reg_type() == LUX_REG_TYPE_FIRMWARE) {
                  // Firmware is at registers 114-118 (5 regs total)
                  if (s->get_register_address() == 114 && this->current_raw_registers_.count(118)) {
                    std::vector<uint16_t> firmware_regs;
                    for (int i = 0; i < 5; ++i) firmware_regs.push_back(this->current_raw_registers_[114 + i]);
                    std::string firmware_version = this->get_firmware_version_(firmware_regs);
                    ESP_LOGD(TAG, "Firmware Version: %s", firmware_version.c_str());
                    s->publish_state(0.0f); // Or NAN, or for a TextSensor
                  } else {
                    s->publish_state(NAN);
                  }
                } else if (s->get_reg_type() == LUX_REG_TYPE_MODEL) {
                  // Model is at registers 119-122 (4 regs total)
                  if (s->get_register_address() == 119 && this->current_raw_registers_.count(122)) {
                    std::vector<uint16_t> model_regs;
                    for (int i = 0; i < 4; ++i) model_regs.push_back(this->current_raw_registers_[119 + i]);
                    std::string model_name = this->get_model_name_(model_regs);
                    ESP_LOGD(TAG, "Model Name: %s", model_name.c_str());
                    s->publish_state(0.0f); // Or NAN
                  } else {
                    s->publish_state(NAN);
                  }
                } else {
                  float value = this->get_sensor_value_(raw_value, s->get_reg_type());
                  s->publish_state(value);
                }
              } else {
                ESP_LOGW(TAG, "Sensor %s (reg 0x%04X, bank %u) not found in received data. Publishing NAN.", 
                         s->get_name().c_str(), s->get_register_address(), s->get_bank());
                s->publish_state(NAN);
              }
            }
          }
        } else {
          ESP_LOGW(TAG, "Failed to interpret Modbus FC 0x03 payload from LuxPower response.");
        }
      } else {
        ESP_LOGW(TAG, "Failed to parse LuxPower proprietary packet. Likely corrupt or invalid.");
      }
    }
  }
}

void LuxPowerInverterComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "LuxPower Inverter:");
  ESP_LOGCONFIG(TAG, "  Host: %s", this->inverter_host_.c_str());
  ESP_LOGCONFIG(TAG, "  Port: %u", this->inverter_port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", this->dongle_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter Serial: %s", this->inverter_serial_number_.c_str());
  ESP_LOGCONFIG(TAG, "  Update Interval: %u ms", (uint32_t) this->update_interval_.count());
  if (this->is_connected()) {
    ESP_LOGCONFIG(TAG, "  Status: Connected");
  } else {
    ESP_LOGCONFIG(TAG, "  Status: Disconnected");
  }
  for (auto *s : this->luxpower_sensors_) {
    LOG_SENSOR("  ", "Sensor", s); 
  }
}

float LuxPowerInverterComponent::get_setup_priority() const { 
  return esphome::setup_priority::AFTER_CONNECTION; 
}

bool LuxPowerInverterComponent::is_connected() {
  return this->client_connected_ && this->client_->connected();
}

bool LuxPowerInverterComponent::connect_to_inverter() {
  if (this->is_connected()) {
    ESP_LOGD(TAG, "Already connected to %s:%u", this->inverter_host_.c_str(), this->inverter_port_);
    return true;
  }

  this->last_connect_attempt_ = millis();
  ESP_LOGI(TAG, "Connecting to LuxPower Inverter at %s:%u...", this->inverter_host_.c_str(), this->inverter_port_);

  if (!this->client_->connect(this->inverter_host_.c_str(), this->inverter_port_)) {
    ESP_LOGE(TAG, "Failed to initiate connection to %s:%u", this->inverter_host_.c_str(), this->inverter_port_);
    return false;
  }
  return true;
}

void LuxPowerInverterComponent::disconnect_from_inverter() {
  if (this->client_) {
    this->client_->stop();
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

// --- AsyncTCP Callbacks --- (unchanged)

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

// --- Luxpower Sensor Management --- (unchanged, just mentioning here)
void LuxPowerInverterComponent::add_luxpower_sensor(LuxpowerSnaSensor *obj, const std::string &name, uint16_t reg_addr, LuxpowerRegType reg_type, uint8_t bank) {
  obj->set_name(name);
  obj->set_register_address(reg_addr);
  obj->set_reg_type(reg_type);
  obj->set_bank(bank);
  obj->set_parent(this);
  this->luxpower_sensors_.push_back(obj);
  App.register_component(obj);
}

// --- NEW: Luxpower Proprietary Protocol Helper Implementations ---

uint16_t LuxPowerInverterComponent::calculate_luxpower_crc16(const std::vector<uint8_t>& data) {
    uint16_t crc = 0xFFFF;
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ LUXPOWER_CRC16_POLY;
            } else {
                crc = crc >> 1;
            }
        }
    }
    return crc;
}

std::vector<uint8_t> LuxPowerInverterComponent::build_luxpower_request_packet(uint8_t function_code, uint16_t register_address, 
                                                                               uint16_t num_registers_or_data, const std::vector<uint8_t>& additional_data) {
    std::vector<uint8_t> packet_payload; // This will contain InverterSerial + DongleSerial + FC + Reg/NumReg/Data + CRC

    // Inverter Serial Number (10 bytes)
    for (char c : this->inverter_serial_number_) packet_payload.push_back(c);
    if (this->inverter_serial_number_.length() < 10) { // Pad with zeros if shorter than 10 bytes
        for (size_t i = 0; i < (10 - this->inverter_serial_number_.length()); ++i) packet_payload.push_back(0x00);
    }
    // Dongle Serial Number (10 bytes)
    for (char c : this->dongle_serial_) packet_payload.push_back(c);
    if (this->dongle_serial_.length() < 10) { // Pad with zeros if shorter than 10 bytes
        for (size_t i = 0; i < (10 - this->dongle_serial_.length()); ++i) packet_payload.push_back(0x00);
    }

    // Function Code (1 byte)
    packet_payload.push_back(function_code);

    // Register / Data (2 bytes) - depends on function code
    packet_payload.push_back((register_address >> 8) & 0xFF);
    packet_payload.push_back(register_address & 0xFF);

    // Num Registers / Data for Modbus Read (2 bytes) or other data
    // For Read Holding Registers (FC 0x03), this is the quantity of registers
    packet_payload.push_back((num_registers_or_data >> 8) & 0xFF);
    packet_payload.push_back(num_registers_or_data & 0xFF);

    // Append additional data if provided (e.g., for Write Multiple Registers)
    packet_payload.insert(packet_payload.end(), additional_data.begin(), additional_data.end());

    // Calculate CRC16 over the packet_payload (excluding Start Byte, Length, End Byte)
    uint16_t crc = calculate_luxpower_crc16(packet_payload);

    // Append CRC to payload
    packet_payload.push_back(crc & 0xFF);       // CRC Low Byte
    packet_payload.push_back((crc >> 8) & 0xFF); // CRC High Byte

    // Construct the final packet with header and footer
    std::vector<uint8_t> final_packet;
    final_packet.push_back(LUXPOWER_START_BYTE); // 0x68

    // Length field (2 bytes): Length of bytes from Inverter Serial to CRC (inclusive) + End Byte (1 byte)
    // This is `len(packet_payload) + 1 (for END_BYTE)`
    uint16_t total_payload_and_crc_len = packet_payload.size() + 1; // +1 for the END_BYTE
    final_packet.push_back((total_payload_and_crc_len >> 8) & 0xFF);
    final_packet.push_back(total_payload_and_crc_len & 0xFF);

    // Append the constructed payload (InverterSerial through CRC)
    final_packet.insert(final_packet.end(), packet_payload.begin(), packet_payload.end());

    final_packet.push_back(LUXPOWER_END_BYTE); // 0x16

    return final_packet;
}

bool LuxPowerInverterComponent::parse_luxpower_response_packet(const std::vector<uint8_t>& response_packet, std::vector<uint8_t>& out_payload) {
    out_payload.clear(); // Clear output payload before processing

    if (response_packet.size() < LUXPOWER_PACKET_MIN_TOTAL_LENGTH) {
        ESP_LOGW(TAG, "LuxPower response too short: %u bytes", response_packet.size());
        return false;
    }

    // 1. Check Start Byte
    if (response_packet[0] != LUXPOWER_START_BYTE) {
        ESP_LOGW(TAG, "LuxPower response missing start byte (0x68). Got 0x%02X", response_packet[0]);
        return false;
    }

    // 2. Extract Length Field
    uint16_t reported_length = (response_packet[1] << 8) | response_packet[2];
    size_t actual_total_length = response_packet.size();
    size_t expected_total_length = 3 + reported_length; // 1 Start Byte + 2 Length Field + reported_length

    if (actual_total_length != expected_total_length) {
        ESP_LOGW(TAG, "LuxPower response length mismatch. Reported %u, Actual %u, Expected %u", reported_length, actual_total_length, expected_total_length);
        return false;
    }

    // 3. Check End Byte
    if (response_packet[actual_total_length - 1] != LUXPOWER_END_BYTE) {
        ESP_LOGW(TAG, "LuxPower response missing end byte (0x16). Got 0x%02X", response_packet[actual_total_length - 1]);
        return false;
    }

    // 4. Extract Payload (from Inverter Serial to CRC, excluding Start Byte, Length, and End Byte)
    // The payload data for CRC check is from byte 3 up to (actual_total_length - 1 - 2 - 1)
    // Which is from LUXPOWER_OFFSET_INV_SERIAL up to (actual_total_length - 1 - LUXPOWER_END_BYTE_LENGTH - CRC_LENGTH)
    // Or, more simply: from byte 3 up to `actual_total_length - 1 (end byte) - 2 (crc bytes)`
    std::vector<uint8_t> payload_for_crc;
    // Data for CRC is everything between length field and CRC itself.
    // It is `reported_length - 1 (end byte)`.
    size_t data_for_crc_len = reported_length - LUXPOWER_END_BYTE_LENGTH; // Reported length includes CRC and End Byte
    
    if (data_for_crc_len < (LUXPOWER_FIXED_PAYLOAD_LEN - 2)) { // -2 for CRC itself
        ESP_LOGW(TAG, "LuxPower payload for CRC is too short after length check.");
        return false;
    }
    
    // Copy bytes from response_packet[3] up to [3 + data_for_crc_len - 1]
    for (size_t i = 0; i < data_for_crc_len; ++i) {
        payload_for_crc.push_back(response_packet[3 + i]);
    }

    // 5. Verify CRC
    uint16_t received_crc = (static_cast<uint16_t>(response_packet[actual_total_length - 2]) << 8) | response_packet[actual_total_length - 3];
    uint16_t calculated_crc = calculate_luxpower_crc16(payload_for_crc);

    if (received_crc != calculated_crc) {
        ESP_LOGW(TAG, "LuxPower response CRC mismatch. Received 0x%04X, Calculated 0x%04X", received_crc, calculated_crc);
        return false;
    }

    // 6. Extract the actual "Modbus-like" payload (Inverter Serial + Dongle Serial + FC + Register/Data + Modbus-specific data)
    // This is `payload_for_crc` itself, because CRC was calculated over it.
    // We just need to remove the serial numbers part if that's not needed by the caller.
    // For `interpret_modbus_read_holding_registers_payload`, it expects FC onwards.
    // So, copy from function code onwards (offset 20 within payload_for_crc)
    if (payload_for_crc.size() < 21) { // 20 bytes for serials + 1 for FC
        ESP_LOGW(TAG, "LuxPower payload after CRC check too short to extract function code.");
        return false;
    }
    
    // Copy bytes from Function Code onwards
    for(size_t i = 20; i < payload_for_crc.size(); ++i) { // Function code is at index 20 within `payload_for_crc`
        out_payload.push_back(payload_for_crc[i]);
    }

    return true;
}

// This function now interprets the *extracted payload* of a LuxPower response
// which starts with the Function Code, then Byte Count, then data.
bool LuxPowerInverterComponent::interpret_modbus_read_holding_registers_payload(const std::vector<uint8_t>& payload, uint16_t expected_start_address, uint16_t expected_num_registers) {
    // Payload for FC 0x03 is: Function Code (1) + Byte Count (1) + Data (N bytes)
    if (payload.size() < 3) { // Min 1 FC + 1 Byte Count + 1 Data byte (even for 0 registers, byte count is 0, so payload size = 2)
        ESP_LOGW(TAG, "Modbus FC 0x03 payload too short: %u bytes", payload.size());
        return false;
    }

    uint8_t function_code = payload[0];
    if (function_code == 0x83) { // Exception response
        uint8_t exception_code = payload[1];
        ESP_LOGE(TAG, "Modbus exception response received in payload: FC 0x%02X, Exception Code 0x%02X", function_code, exception_code);
        return false;
    }
    if (function_code != 0x03) {
        ESP_LOGW(TAG, "Modbus Function Code mismatch in payload. Expected 0x03, got 0x%02X", function_code);
        return false;
    }

    uint8_t byte_count = payload[1];
    if (byte_count != (expected_num_registers * 2)) {
        ESP_LOGW(TAG, "Modbus Byte Count mismatch in payload. Expected %u, got %u", (expected_num_registers * 2), byte_count);
        return false;
    }
    if (payload.size() < (2 + byte_count)) { // 2 bytes header + byte_count for data
        ESP_LOGW(TAG, "Modbus response data truncated in payload. Expected %u bytes, got %u", (2 + byte_count), payload.size());
        return false;
    }

    this->current_raw_registers_.clear();
    for (uint16_t i = 0; i < expected_num_registers; ++i) {
        // Data starts from index 2 in the payload
        uint16_t reg_value = (static_cast<uint16_t>(payload[2 + (i * 2)]) << 8) | payload[3 + (i * 2)];
        this->current_raw_registers_[expected_start_address + i] = reg_value;
    }

    ESP_LOGD(TAG, "Successfully interpreted Modbus FC 0x03 payload for %u registers starting at 0x%04X", expected_num_registers, expected_start_address);
    return true;
}

float LuxPowerInverterComponent::get_sensor_value_(uint16_t register_value, LuxpowerRegType reg_type) {
  switch (reg_type) {
    case LUX_REG_TYPE_INT:
      return static_cast<float>(register_value);
    case LUX_REG_TYPE_FLOAT_DIV10:
      return static_cast<float>(register_value) / 10.0f;
    case LUX_REG_TYPE_SIGNED_INT:
      return static_cast<float>(static_cast<int16_t>(register_value));
    case LUX_REG_TYPE_FIRMWARE:
    case LUX_REG_TYPE_MODEL:
      ESP_LOGW(TAG, "Attempted to decode firmware/model as float. Use TextSensor for these types.");
      return NAN;
    case LUX_REG_TYPE_BITMASK:
    case LUX_REG_TYPE_TIME_MINUTES:
      return static_cast<float>(register_value);
    default:
      ESP_LOGW(TAG, "Unknown LuxpowerRegType: %d", static_cast<int>(reg_type));
      return NAN;
  }
}

std::string LuxPowerInverterComponent::get_firmware_version_(const std::vector<uint16_t>& data) {
  if (data.size() < 5) return "";
  char buffer[11]; // 10 chars + null terminator
  buffer[0] = (data[0] >> 8) & 0xFF;
  buffer[1] = data[0] & 0xFF;
  buffer[2] = (data[1] >> 8) & 0xFF;
  buffer[3] = data[1] & 0xFF;
  buffer[4] = (data[2] >> 8) & 0xFF;
  buffer[5] = data[2] & 0xFF;
  buffer[6] = (data[3] >> 8) & 0xFF;
  buffer[7] = data[3] & 0xFF;
  buffer[8] = (data[4] >> 8) & 0xFF;
  buffer[9] = data[4] & 0xFF;
  buffer[10] = '\0';
  return std::string(buffer);
}

std::string LuxPowerInverterComponent::get_model_name_(const std::vector<uint16_t>& data) {
  if (data.size() < 4) return "";
  char buffer[9]; // 8 chars + null terminator
  buffer[0] = (data[0] >> 8) & 0xFF;
  buffer[1] = data[0] & 0xFF;
  buffer[2] = (data[1] >> 8) & 0xFF;
  buffer[3] = data[1] & 0xFF;
  buffer[4] = (data[2] >> 8) & 0xFF;
  buffer[5] = data[2] & 0xFF;
  buffer[6] = (data[3] >> 8) & 0xFF;
  buffer[7] = data[3] & 0xFF;
  buffer[8] = '\0';
  return std::string(buffer);
}

} // namespace luxpower_sna
} // namespace esphome