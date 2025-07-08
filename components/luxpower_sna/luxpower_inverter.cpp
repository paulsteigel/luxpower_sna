// src/esphome/components/luxpower_sna/luxpower_inverter.cpp

#include "luxpower_inverter.h" // Must be first, brings in all declarations
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"     // For format_hex_pretty
#include "esphome/core/log.h"         // For ESP_LOGx macros
#include "esphome/core/hal.h"         // For millis()
#include "esphome/components/sensor/sensor.h" // For sensor::Sensor base class and LOG_SENSOR macro
#include "luxpower_sna_sensor.h"      // For LuxpowerSnaSensor class definition
#include "luxpower_sna_constants.h"   // For LuxpowerRegType enum and other constants

#include <functional> // Included for general C++ features, but specific bind usage is simplified
#include <algorithm>  // For std::min, std::max (if used in future logic)
#include <cmath>      // For NAN (Not-a-Number)
#include <deque>      // For std::deque (rx_buffer_)

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna.inverter"; // Tag for logging in this component

// Constructor implementation
LuxPowerInverterComponent::LuxPowerInverterComponent() : Component() {
  this->client_ = new AsyncClient();
  this->client_connected_ = false;
  this->last_request_time_ = 0;
  // --- START FIX for 'last_connect_attempt_' ---
  this->last_connection_attempt_time_ = 0;
  // --- END FIX ---
  // Default values for host/port/serials, will be overridden by YAML config
  this->inverter_host_ = "";
  this->inverter_port_ = 0;
  this->dongle_serial_ = "";
  this->inverter_serial_number_ = "";
  this->update_interval_ = std::chrono::milliseconds(5000); // Default 5 seconds
}

// setup() implementation: Called once when the component is initialized
void LuxPowerInverterComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxPower Inverter Component...");
  ESP_LOGCONFIG(TAG, "  Host: %s", this->inverter_host_.c_str());
  ESP_LOGCONFIG(TAG, "  Port: %u", this->inverter_port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", this->dongle_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter Serial: %s", this->inverter_serial_number_.c_str());
  ESP_LOGCONFIG(TAG, "  Update Interval: %u ms", (uint32_t) this->update_interval_.count());

  // Bind callbacks for AsyncClient events - CORRECTED BINDING FOR STATIC MEMBERS
  // --- START FIX for AsyncClient callbacks ---
  this->client_->onConnect(&LuxPowerInverterComponent::on_connect_cb, this);
  this->client_->onDisconnect(&LuxPowerInverterComponent::on_disconnect_cb, this);
  this->client_->onData(&LuxPowerInverterComponent::on_data_cb, this);
  this->client_->onError(&LuxPowerInverterComponent::on_error_cb, this);
  // --- END FIX ---

  // Attempt initial connection
  this->connect_to_inverter();
}

// In luxpower_inverter.cpp

void LuxPowerInverter::loop() {
  const uint32_t now = millis();
  if ((now - this->last_byte_received_) > this->read_timeout_) {
    if (this->data_buffer_.size() > 0) {
      ESP_LOGW(TAG, "Buffer timeout! Discarding %zu bytes.", this->data_buffer_.size());
      this->data_buffer_.clear();
    }
  }

  while (this->available()) {
    uint8_t byte;
    this->read_byte(&byte);
    this->data_buffer_.push_back(byte);
    this->last_byte_received_ = now;

    // Check for A1 1A prefix
    if (this->data_buffer_.size() >= 2) {
      if (this->data_buffer_[0] == 0xA1 && this->data_buffer_[1] == 0x1A) {
        // It's a potential A1 packet
        // Minimum A1 packet length is 37 bytes
        if (this->data_buffer_.size() >= LUXPOWER_A1_MIN_LENGTH) {
          // Frame Length is at bytes 4 and 5 (little-endian)
          uint16_t frame_length = (this->data_buffer_[5] << 8) | this->data_buffer_[4];
          uint16_t calculated_packet_length = frame_length + 6; // 6 bytes for prefix (2), protocol (2), frame_length (2)

          if (this->data_buffer_.size() >= calculated_packet_length) {
            // We have a full A1 packet
            std::vector<uint8_t> packet(this->data_buffer_.begin(), this->data_buffer_.begin() + calculated_packet_length);
            this->parse_luxpower_response_packet(packet);

            // Remove the processed packet from the buffer
            this->data_buffer_.erase(this->data_buffer_.begin(), this->data_buffer_.begin() + calculated_packet_length);
            // Continue processing if there's more data in the buffer
            continue;
          }
        }
      } else {
        // Not an A1 1A prefix, discard the first byte and continue
        ESP_LOGW(TAG, "Unknown prefix 0x%02X. Discarding byte.", this->data_buffer_[0]);
        this->data_buffer_.erase(this->data_buffer_.begin());
        // Continue from the new first byte in the next iteration of the while loop
        continue;
      }
    }
  }
}

// dump_config() implementation: For logging component configuration at startup
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
    // Use LOG_SENSOR for standard ESPHome sensor config output
    LOG_SENSOR("  ", "Sensor", s);
  }
}

// get_setup_priority() implementation
float LuxPowerInverterComponent::get_setup_priority() const {
  return esphome::setup_priority::AFTER_CONNECTION;
}

// is_connected() implementation
bool LuxPowerInverterComponent::is_connected() {
  return this->client_connected_ && this->client_ && this->client_->connected();
}

// connect_to_inverter() implementation
bool LuxPowerInverterComponent::connect_to_inverter() {
  if (this->is_connected()) {
    ESP_LOGD(TAG, "Already connected to %s:%u", this->inverter_host_.c_str(), this->inverter_port_);
    return true;
  }

  this->last_connection_attempt_time_ = millis();
  ESP_LOGI(TAG, "Connecting to LuxPower Inverter at %s:%u...", this->inverter_host_.c_str(), this->inverter_port_);

  if (!this->client_->connect(this->inverter_host_.c_str(), this->inverter_port_)) {
    ESP_LOGE(TAG, "Failed to initiate connection to %s:%u", this->inverter_host_.c_str(), this->inverter_port_);
    return false;
  }
  return true; // Connection initiated, not necessarily established yet
}

// disconnect_from_inverter() implementation
void LuxPowerInverterComponent::disconnect_from_inverter() {
  if (this->client_) {
    this->client_->stop(); // This will trigger on_disconnect_cb
  }
  this->client_connected_ = false;
  this->rx_buffer_.clear(); // Clear buffer on disconnect
  // Mark all associated sensors as unavailable
  for (auto *s : this->luxpower_sensors_) {
    s->publish_state(NAN);
  }
}

// send_data() implementation
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

  // AsyncClient's write returns the number of bytes written, 0 if nothing sent.
  return this->client_->write(reinterpret_cast<const char*>(data.data()), data.size()) > 0;
}

// --- AsyncTCP Callbacks ---
// Ensure these functions are STATIC as declared in header
void LuxPowerInverterComponent::on_connect_cb(void *arg, AsyncClient *client) {
  LuxPowerInverterComponent *comp = static_cast<LuxPowerInverterComponent*>(arg);
  ESP_LOGI(TAG, "Connected to LuxPower Inverter!");
  comp->client_connected_ = true;
  comp->rx_buffer_.clear(); // Clear buffer on new connection
  comp->last_request_time_ = 0; // Trigger immediate first request
}

void LuxPowerInverterComponent::on_disconnect_cb(void *arg, AsyncClient *client) {
  LuxPowerInverterComponent *comp = static_cast<LuxPowerInverterComponent*>(arg);
  ESP_LOGW(TAG, "Disconnected from LuxPower Inverter!");
  comp->client_connected_ = false;
  comp->rx_buffer_.clear(); // Clear buffer on disconnect
  // Mark all associated sensors as unavailable
  for (auto *s : comp->luxpower_sensors_) { // Use comp-> here
    s->publish_state(NAN);
  }
}

void LuxPowerInverterComponent::on_data_cb(void *arg, AsyncClient *client, void *data, size_t len) {
  LuxPowerInverterComponent *comp = static_cast<LuxPowerInverterComponent*>(arg);
  uint8_t *byte_data = reinterpret_cast<uint8_t*>(data); // This line already exists and is correct!

  // --- START FIX: Use byte_data for format_hex_pretty ---
  ESP_LOGV(TAG, "Received %u bytes: %s", len, format_hex_pretty(byte_data, len).c_str());
  // --- END FIX ---

  for (size_t i = 0; i < len; ++i) {
    comp->rx_buffer_.push_back(byte_data[i]); // This line already uses byte_data
  }
  // Data will be processed in loop()
}

void LuxPowerInverterComponent::on_error_cb(void *arg, AsyncClient *client, int error) {
  LuxPowerInverterComponent *comp = static_cast<LuxPowerInverterComponent*>(arg);
  ESP_LOGE(TAG, "TCP client error: %d (%s)", error, client->errorToString(error));
  comp->client_connected_ = false; // Mark as disconnected
  comp->client_->stop(); // Explicitly stop to ensure clean state
}

// --- Luxpower Sensor Management ---
// Corrected implementation: obj is already a LuxpowerSnaSensor created by YAML
void LuxPowerInverterComponent::add_luxpower_sensor(LuxpowerSnaSensor *obj, const std::string &name, uint16_t reg_addr, LuxpowerRegType reg_type, uint8_t bank) {
  obj->set_name(name.c_str()); // Set name using c_str()
  obj->set_register_address(reg_addr);
  obj->set_reg_type(reg_type);
  obj->set_bank(bank);
  obj->set_parent(this); // Set this component as the parent
  this->luxpower_sensors_.push_back(obj);
  App.register_component(obj); // Register the sensor component with ESPHome
  ESP_LOGD(TAG, "Added LuxPower SNA Sensor: %s (Reg: 0x%04X, Type: %u, Bank: %u)",
           name.c_str(), reg_addr, reg_type, bank);
}

// --- Luxpower Proprietary Protocol Helper Implementations ---

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
    uint16_t total_payload_and_crc_len = packet_payload.size() + LUXPOWER_END_BYTE_LENGTH;
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
        ESP_LOGW(TAG, "LuxPower response missing start byte (0x%02X). Got 0x%02X", LUXPOWER_START_BYTE, response_packet[0]);
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
        ESP_LOGW(TAG, "LuxPower response missing end byte (0x%02X). Got 0x%02X", LUXPOWER_END_BYTE, response_packet[actual_total_length - 1]);
        return false;
    }

    // 4. Extract Payload for CRC (from Inverter Serial to CRC, excluding Start Byte, Length, and End Byte)
    size_t data_for_crc_len = reported_length - LUXPOWER_END_BYTE_LENGTH; // Reported length includes CRC and End Byte

    if (data_for_crc_len < (LUXPOWER_MIN_PAYLOAD_FOR_CRC_LENGTH  - 2)) { // -2 for CRC itself
        ESP_LOGW(TAG, "LuxPower payload for CRC is too short after length check.");
        return false;
    }

    std::vector<uint8_t> payload_for_crc;
    // Copy bytes from response_packet[3] up to [3 + data_for_crc_len - 1]
    for (size_t i = 0; i < data_for_crc_len; ++i) {
        payload_for_crc.push_back(response_packet[3 + i]);
    }

    // 5. Verify CRC
    // CRC is 2 bytes, but the order is usually LSB then MSB in Modbus/LuxPower
    uint16_t received_crc = (static_cast<uint16_t>(payload_for_crc[payload_for_crc.size() - 1]) << 8) | payload_for_crc[payload_for_crc.size() - 2];
    uint16_t calculated_crc = calculate_luxpower_crc16(payload_for_crc);

    if (received_crc != calculated_crc) {
        ESP_LOGW(TAG, "LuxPower response CRC mismatch. Received 0x%04X, Calculated 0x%04X", received_crc, calculated_crc);
        return false;
    }

    // 6. Extract the actual "Modbus-like" payload (Function Code onwards)
    // This starts after the 10 bytes Inverter Serial + 10 bytes Dongle Serial
    if (payload_for_crc.size() < 21) { // 20 bytes for serials + 1 for FC
        ESP_LOGW(TAG, "LuxPower payload after CRC check too short to extract function code.");
        return false;
    }

    // Copy bytes from Function Code onwards (Function code is at index 20 within `payload_for_crc`)
    for(size_t i = 20; i < payload_for_crc.size(); ++i) {
        out_payload.push_back(payload_for_crc[i]);
    }

    return true;
}

// This function interprets the *extracted payload* of a LuxPower response
// which starts with the Function Code, then Byte Count, then data.
bool LuxPowerInverterComponent::interpret_modbus_read_holding_registers_payload(const std::vector<uint8_t>& payload, uint16_t expected_start_address, uint16_t expected_num_registers) {
    // Payload for FC 0x03 is: Function Code (1) + Byte Count (1) + Data (N bytes)
    if (payload.size() < 2) { // Min 1 FC + 1 Byte Count (even for 0 registers, byte count is 0)
        ESP_LOGW(TAG, "Modbus FC 0x03 payload too short: %u bytes", payload.size());
        return false;
    }

    uint8_t function_code = payload[0];
    if (function_code == 0x83) { // Exception response
        if (payload.size() < 2) {
            ESP_LOGE(TAG, "Modbus exception response payload too short.");
            return false;
        }
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

// get_sensor_value_() implementation: Converts raw register values to sensor values
float LuxPowerInverterComponent::get_sensor_value_(uint16_t raw_value, LuxpowerRegType reg_type) {
  switch (reg_type) {
    case LUX_REG_TYPE_INT:
      return static_cast<float>(raw_value);
    case LUX_REG_TYPE_FLOAT_DIV10:
      return static_cast<float>(raw_value) / 10.0f;
    case LUX_REG_TYPE_SIGNED_INT:
      return static_cast<float>(static_cast<int16_t>(raw_value)); // Cast to signed 16-bit
    case LUX_REG_TYPE_FIRMWARE: {
      // For firmware, assuming raw_value is part of a larger sequence or needs special handling.
      // This function typically decodes a single register. Firmware is multi-register.
      // Returning NAN here is appropriate, as the string decoding happens elsewhere.
      return NAN;
    }
    case LUX_REG_TYPE_MODEL: {
      // Similar to firmware, model is multi-register and string-based.
      return NAN;
    }
    case LUX_REG_TYPE_BITMASK:
    case LUX_REG_TYPE_TIME_MINUTES:
      return static_cast<float>(raw_value);
    default:
      ESP_LOGW(TAG, "Unknown LuxpowerRegType: %d. Returning NAN.", static_cast<int>(reg_type));
      return NAN;
  }
}

// get_firmware_version_() implementation
std::string LuxPowerInverterComponent::get_firmware_version_(const std::vector<uint16_t>& data) {
  // Expects 5 registers for firmware (10 characters)
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

// get_model_name_() implementation
std::string LuxPowerInverterComponent::get_model_name_(const std::vector<uint16_t>& data) {
  // Expects 4 registers for model (8 characters)
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

// In luxpower_inverter.cpp

void LuxPowerInverter::parse_luxpower_response_packet(
    const std::vector<uint8_t> &data) {
  this->rx_count_++;
  if (data.size() < LUXPOWER_A1_MIN_LENGTH) {
    ESP_LOGW(TAG, "A1 response too short (%zu bytes)!", data.size());
    this->status_set_warning();
    return;
  }

  // A1 prefix already checked by the loop() function caller

  // uint16_t protocol_number = (data[3] << 8) | data[2]; // Bytes 2-3: Protocol number (always 0x012C or 300)
  uint16_t frame_length = (data[5] << 8) | data[4]; // Bytes 4-5: Frame Length (length from byte 6 to CRC)

  // Verify calculated packet length against actual received length
  uint16_t calculated_packet_length = frame_length + 6; // 6 bytes for prefix (2), protocol (2), frame_length (2)
  if (data.size() != calculated_packet_length) {
    ESP_LOGW(TAG, "A1 response length mismatch! Expected %u, got %zu.",
             calculated_packet_length, data.size());
    this->status_set_warning();
    return;
  }

  uint8_t tcp_function_code = data[6]; // Byte 6: TCP Function Code (0xC2 or 194 for TRANSLATED_DATA)
  // std::vector<uint8_t> dongle_serial(data.begin() + 7, data.begin() + 17); // Bytes 7-16: Dongle Serial (10 bytes)

  uint16_t modbus_data_length = (data[18] << 8) | data[17]; // Bytes 17-18: Data Length (byte length of actual Modbus payload)
  uint8_t address_action = data[19]; // Byte 19: Address Action (0x01 for read)
  uint8_t modbus_function_code = data[20]; // Byte 20: Modbus Function Code (e.g., 0x04 Read Input, 0x03 Read Holding)
  // std::vector<uint8_t> inverter_serial(data.begin() + 21, data.begin() + 31); // Bytes 21-30: Inverter Serial (10 bytes)
  uint16_t modbus_start_register = (data[32] << 8) | data[31]; // Bytes 31-32: Starting Register (Modbus address)
  uint16_t number_of_registers = (data[34] << 8) | data[33]; // Bytes 33-34: Number of Registers (count of 16-bit registers)

  // The actual Modbus payload starts at offset 35
  const uint8_t *modbus_payload_ptr = &data[35];

  if (modbus_data_length != (number_of_registers * 2)) {
      ESP_LOGW(TAG, "A1 response Modbus data length mismatch! Expected %u, got %u.",
               number_of_registers * 2, modbus_data_length);
      this->status_set_warning();
      return;
  }

  // Handle different Modbus function codes
  if (modbus_function_code == MODBUS_CMD_READ_INPUT_REGISTER) { // 0x04
    // Process input registers
    ESP_LOGD(TAG, "Received A1 Read Input Registers response for registers %u-%u, length %u",
             modbus_start_register, modbus_start_register + number_of_registers - 1, number_of_registers);

    for (int i = 0; i < number_of_registers; ++i) {
      uint16_t reg_address = modbus_start_register + i;
      // Modbus values are typically big-endian, matching LXPPacket.py's struct.unpack(">H")
      uint16_t value = (modbus_payload_ptr[(i * 2) + 1] << 8) | modbus_payload_ptr[i * 2];
      this->parse_and_publish_register(reg_address, value);
    }
    this->status_set_ok();
  } else if (modbus_function_code == MODBUS_CMD_READ_HOLDING_REGISTER) { // 0x03
    // Process holding registers
    ESP_LOGD(TAG, "Received A1 Read Holding Registers response for registers %u-%u, length %u",
             modbus_start_register, modbus_start_register + number_of_registers - 1, number_of_registers);
    for (int i = 0; i < number_of_registers; ++i) {
      uint16_t reg_address = modbus_start_register + i;
      // Modbus values are typically big-endian, matching LXPPacket.py's struct.unpack(">H")
      uint16_t value = (modbus_payload_ptr[(i * 2) + 1] << 8) | modbus_payload_ptr[i * 2];
      this->parse_and_publish_register(reg_address, value);
    }
    this->status_set_ok();
  } else {
    ESP_LOGW(TAG, "Unsupported Modbus function code in A1 packet: 0x%02X", modbus_function_code);
    this->status_set_warning();
  }
}

} // namespace luxpower_sna
} // namespace esphome
