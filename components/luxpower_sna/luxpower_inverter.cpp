// components/luxpower_sna/luxpower_inverter.cpp
#include "luxpower_inverter.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
// #include <arpa/inet.h> // Removed: This header is not available on ESP32

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// --- CRC16 Calculation (Modbus CRC) ---
// This function is a direct translation of common Modbus CRC16 implementations.
uint16_t calculate_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001; // Polynomial for Modbus CRC16
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// --- LuxpowerPacket Helper Class Implementation ---

std::vector<uint8_t> LuxpowerPacket::serial_string_to_bytes(const std::string& serial_str) {
    std::vector<uint8_t> bytes;
    bytes.reserve(10); // Serial numbers are 10 characters

    // The Python `serial.encode()` converts string characters to their ASCII byte values.
    // We will replicate that here.
    for (char c : serial_str) {
        bytes.push_back(static_cast<uint8_t>(c));
    }

    // Pad with zeros if the serial string is shorter than 10, or truncate if longer.
    // This ensures the byte vector is always 10 bytes long.
    bytes.resize(10, 0x00);
    return bytes;
}

std::vector<uint8_t> LuxpowerPacket::build_read_holding_command(
    const std::string& target_serial,
    const std::string& dongle_serial,
    uint16_t start_address,
    uint16_t num_registers
) {
    std::vector<uint8_t> packet;
    packet.reserve(30); // Pre-allocate for efficiency

    // Luxpower Header (17 bytes)
    // Byte 0-1: Start (0xAA 0x55)
    packet.push_back(LUX_START_BYTE_1);
    packet.push_back(LUX_START_BYTE_2);

    // Byte 2-3: Length (placeholder for now, will be filled later)
    packet.push_back(0x00);
    packet.push_back(0x00);

    // Byte 4-13: Target Inverter Serial (10 bytes)
    std::vector<uint8_t> target_serial_bytes = serial_string_to_bytes(target_serial);
    packet.insert(packet.end(), target_serial_bytes.begin(), target_serial_bytes.end());

    // Byte 14: Command (0xC3 for READ_PARAM)
    packet.push_back(LUX_READ_PARAM);
    // Byte 15: Modbus Function Code (0x03 for READ_HOLDING_REGISTERS)
    packet.push_back(MODBUS_READ_HOLDING_REGISTERS);

    // Modbus PDU for Read Holding Registers (Function Code 0x03)
    // Start Address (2 bytes, Big Endian)
    packet.push_back(static_cast<uint8_t>((start_address >> 8) & 0xFF));
    packet.push_back(static_cast<uint8_t>(start_address & 0xFF));

    // Number of Registers (2 bytes, Big Endian)
    packet.push_back(static_cast<uint8_t>((num_registers >> 8) & 0xFF));
    packet.push_back(static_cast<uint8_t>(num_registers & 0xFF));

    // Dongle Serial (10 bytes) - Appended after the Modbus PDU in Luxpower protocol
    std::vector<uint8_t> dongle_serial_bytes = serial_string_to_bytes(dongle_serial);
    packet.insert(packet.end(), dongle_serial_bytes.begin(), dongle_serial_bytes.end());

    // CRC16 (2 bytes) - Calculated over the entire packet *excluding* the initial AA 55 and Length bytes.
    // The Python code calculates CRC over bytes from index 4 onwards.
    size_t crc_data_start_index = 4;
    uint16_t crc = calculate_crc16(&packet[crc_data_start_index], packet.size() - crc_data_start_index);

    // Append CRC (LSB first, then MSB, as per Modbus convention)
    packet.push_back(static_cast<uint8_t>(crc & 0xFF));
    packet.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));

    // Update Length (bytes 2-3)
    // Length is the total length of the packet *excluding* the initial AA 55 and Length bytes themselves.
    // So, it's the size of the packet from index 4 to the end.
    uint16_t total_len_payload = packet.size() - 4; // Total packet size minus AA 55 and Length bytes
    packet[2] = static_cast<uint8_t>((total_len_payload >> 8) & 0xFF);
    packet[3] = static_cast<uint8_t>(total_len_payload & 0xFF);

    return packet;
}

std::vector<uint8_t> LuxpowerPacket::build_read_input_command(
    const std::string& target_serial,
    const std::string& dongle_serial,
    uint16_t start_address,
    uint16_t num_registers
) {
    std::vector<uint8_t> packet;
    packet.reserve(30); // Pre-allocate for efficiency

    // Luxpower Header (17 bytes)
    // Byte 0-1: Start (0xAA 0x55)
    packet.push_back(LUX_START_BYTE_1);
    packet.push_back(LUX_START_BYTE_2);

    // Byte 2-3: Length (placeholder for now, will be filled later)
    packet.push_back(0x00);
    packet.push_back(0x00);

    // Byte 4-13: Target Inverter Serial (10 bytes)
    std::vector<uint8_t> target_serial_bytes = serial_string_to_bytes(target_serial);
    packet.insert(packet.end(), target_serial_bytes.begin(), target_serial_bytes.end());

    // Byte 14: Command (0xC3 for READ_PARAM)
    packet.push_back(LUX_READ_PARAM);
    // Byte 15: Modbus Function Code (0x04 for READ_INPUT_REGISTERS)
    packet.push_back(MODBUS_READ_INPUT_REGISTERS);

    // Modbus PDU for Read Input Registers (Function Code 0x04)
    // Start Address (2 bytes, Big Endian)
    packet.push_back(static_cast<uint8_t>((start_address >> 8) & 0xFF));
    packet.push_back(static_cast<uint8_t>(start_address & 0xFF));

    // Number of Registers (2 bytes, Big Endian)
    packet.push_back(static_cast<uint8_t>((num_registers >> 8) & 0xFF));
    packet.push_back(static_cast<uint8_t>(num_registers & 0xFF));

    // Dongle Serial (10 bytes) - Appended after the Modbus PDU in Luxpower protocol
    std::vector<uint8_t> dongle_serial_bytes = serial_string_to_bytes(dongle_serial);
    packet.insert(packet.end(), dongle_serial_bytes.begin(), dongle_serial_bytes.end());

    // CRC16 (2 bytes) - Calculated over the entire packet *excluding* the initial AA 55 and Length bytes.
    size_t crc_data_start_index = 4;
    uint16_t crc = calculate_crc16(&packet[crc_data_start_index], packet.size() - crc_data_start_index);

    // Append CRC (LSB first, then MSB, as per Modbus convention)
    packet.push_back(static_cast<uint8_t>(crc & 0xFF));
    packet.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));

    // Update Length (bytes 2-3)
    uint16_t total_len_payload = packet.size() - 4;
    packet[2] = static_cast<uint8_t>((total_len_payload >> 8) & 0xFF);
    packet[3] = static_cast<uint8_t>(total_len_payload & 0xFF);

    return packet;
}


bool LuxpowerPacket::decode_packet(const std::vector<uint8_t>& raw_data, LuxpowerInverterComponent* comp) {
    if (raw_data.size() < 6) { // Minimum packet size (AA 55 Len1 Len2 Cmd1 Cmd2)
        ESP_LOGW(TAG, "Received packet too short for basic header.");
        return false;
    }

    if (raw_data[0] != LUX_START_BYTE_1 || raw_data[1] != LUX_START_BYTE_2) {
        ESP_LOGW(TAG, "Invalid start bytes in buffer. Dropping byte.");
        return false;
    }

    uint16_t declared_len = (static_cast<uint16_t>(raw_data[2]) << 8) | raw_data[3];
    if (raw_data.size() - 4 < declared_len) { // raw_data.size() - 4 is the actual payload length
        ESP_LOGW(TAG, "Received packet length mismatch. Declared: %u, Actual: %u", declared_len, raw_data.size() - 4);
        return false;
    }

    // Verify CRC (CRC is calculated over the payload, excluding AA 55 and Length bytes)
    // The CRC bytes are the last two bytes of the declared length payload.
    if (declared_len < 2) { // Need at least 2 bytes for CRC
        ESP_LOGW(TAG, "Packet too short to contain CRC.");
        return false;
    }
    // CRC is LSB then MSB in the packet, so reverse for 16-bit value
    uint16_t received_crc = (static_cast<uint16_t>(raw_data[declared_len + 3]) << 8) | raw_data[declared_len + 2];
    uint16_t calculated_crc = calculate_crc16(&raw_data[4], declared_len - 2); // Calculate CRC over payload excluding CRC itself

    if (received_crc != calculated_crc) {
        ESP_LOGW(TAG, "CRC mismatch! Received: 0x%04X, Calculated: 0x%04X", received_crc, calculated_crc);
        return false;
    }

    // Packet seems valid, now parse the content based on command
    uint8_t command_code = raw_data[14]; // Assuming command is at byte 14 (after 10-byte serial + 2 header bytes)
    uint8_t function_code = raw_data[15]; // Modbus function code

    ESP_LOGD(TAG, "Packet decoded: Command=0x%02X, Function=0x%02X", command_code, function_code);

    if (command_code == LUX_TRANSLATED_DATA) { // This is a data response
        if (function_code == MODBUS_READ_HOLDING_REGISTERS || function_code == MODBUS_READ_INPUT_REGISTERS) {
            uint8_t byte_count = raw_data[16]; // Number of data bytes following
            ESP_LOGD(TAG, "Modbus Read Response. Function: 0x%02X, Byte Count: %u", function_code, byte_count);

            // Ensure we have enough bytes for the data payload + dongle serial + CRC
            // Header (16 bytes) + Byte Count (1 byte) + Data (byte_count) + Dongle Serial (10 bytes) + CRC (2 bytes)
            // Total expected length: 16 (header) + 1 (byte_count) + byte_count + 10 (dongle) + 2 (crc)
            // declared_len = (10 serial) + (1 cmd) + (1 func) + (1 byte_count) + (data bytes) + (10 dongle serial) + (2 crc)
            // So, data starts at index 17.
            if (declared_len < (1 + 1 + 1 + byte_count + 10 + 2)) { // Min payload for Modbus RTU response
                ESP_LOGW(TAG, "Incomplete Modbus Read response payload. Declared len: %u, Byte count: %u", declared_len, byte_count);
                return false;
            }

            // Extract the data payload (Modbus RTU data)
            std::vector<uint8_t> data_payload_bytes;
            data_payload_bytes.reserve(byte_count);
            for (size_t i = 0; i < byte_count; ++i) {
                data_payload_bytes.push_back(raw_data[17 + i]);
            }

            // The start address of the response is not explicitly in the response packet.
            // It's implicitly linked to the request. For now, we assume the component
            // knows which request this response belongs to.
            // A more robust solution would involve tracking outstanding requests.
            // For simplicity, let's assume the component will know the start address.
            // For now, we'll pass a dummy 0x0000 and let parse_modbus_response_ figure it out.
            comp->parse_modbus_response_(data_payload_bytes, function_code, 0x0000);

        } else {
            ESP_LOGI(TAG, "Unhandled TRANSLATED_DATA function code: 0x%02X", function_code);
        }
    } else {
        ESP_LOGI(TAG, "Unhandled command code: 0x%02X", command_code);
    }

    return true;
}

// --- LuxpowerInverterComponent Implementation ---

LuxpowerInverterComponent::LuxpowerInverterComponent() : PollingComponent(20000) {
  this->client_.onConnect(LuxpowerInverterComponent::onConnect, this);
  this->client_.onDisconnect(LuxpowerInverterComponent::onDisconnect, this);
  this->client_.onData(LuxpowerInverterComponent::onData, this);
  this->client_.onAck(LuxpowerInverterComponent::onAck, this);
  this->client_.onError(LuxpowerInverterComponent::onError, this);
}

void LuxpowerInverterComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Luxpower SNA Component...");
  this->connect_to_inverter_();
}

void LuxpowerInverterComponent::update() {
  ESP_LOGD(TAG, "Luxpower SNA Component updating...");
  if (!this->connected_) {
    ESP_LOGW(TAG, "Not connected to inverter. Attempting to reconnect...");
    this->connect_to_inverter_();
  } else {
    ESP_LOGD(TAG, "Connected to inverter. Sending read holding registers command...");
    // We need to request the correct register bank that contains battery data.
    // Based on LXPPacket.py, battery data is often in a bank starting around register 100.
    // Let's request a block that covers these.
    // Assuming REG_BATTERY_VOLTAGE (100) and REG_BATTERY_DISCHARGE_POWER (107) are in the same block.
    // A block of 10 registers from 100 (0x64) to 109 (0x6D) should cover these.
    std::vector<uint8_t> command_packet = LuxpowerPacket::build_read_input_command(
        this->inverter_serial_number_, this->dongle_serial_, REG_BATTERY_VOLTAGE, 10); // Request 10 registers from 0x64
    this->send_packet_(command_packet);
  }
}

void LuxpowerInverterComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Luxpower SNA Component:");
  ESP_LOGCONFIG(TAG, "  Host: %s", this->host_.c_str());
  ESP_LOGCONFIG(TAG, "  Port: %u", this->port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", this->dongle_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter Serial Number: %s", this->inverter_serial_number_.c_str());
  ESP_LOGCONFIG(TAG, "  Update Interval: %lu ms", this->get_update_interval());

  for (auto *sensor : this->sensors_) {
    LOG_SENSOR("  ", "Sensor", sensor);
  }
}

void LuxpowerInverterComponent::connect_to_inverter_() {
  if (this->client_.connected()) {
    ESP_LOGI(TAG, "Already connected to %s:%u", this->host_.c_str(), this->port_);
    this->connected_ = true;
    return;
  }

  ESP_LOGI(TAG, "Attempting to connect to %s:%u...", this->host_.c_str(), this->port_);
  IPAddress ip_address;
  if (!ip_address.fromString(this->host_.c_str())) {
    ESP_LOGE(TAG, "Invalid host IP address: %s", this->host_.c_str());
    this->connected_ = false;
    return;
  }

  if (this->client_.connect(ip_address, this->port_)) {
    ESP_LOGI(TAG, "Connection initiated to %s:%u", this->host_.c_str(), this->port_);
    // Connection is asynchronous, onConnect will be called if successful
  } else {
    ESP_LOGE(TAG, "Failed to initiate connection to %s:%u", this->host_.c_str(), this->port_);
    this->connected_ = false;
  }
}

void LuxpowerInverterComponent::send_packet_(const std::vector<uint8_t>& packet) {
    if (!this->client_.connected()) {
        ESP_LOGW(TAG, "Cannot send packet: client not connected.");
        return;
    }
    if (packet.empty()) {
        ESP_LOGW(TAG, "Cannot send empty packet.");
        return;
    }

    ESP_LOGD(TAG, "Sending packet of %u bytes.", packet.size());
    this->log_buffer_hexdump_(TAG, packet.data(), packet.size(), esphome::log::LOG_LEVEL_VERBOSE); // Use custom hexdump

    this->client_.write(reinterpret_cast<const char*>(packet.data()), packet.size());
}

void LuxpowerInverterComponent::process_received_data_() {
    // This function will be called when new data arrives in the buffer.
    // It should attempt to parse complete Luxpower packets from the buffer.

    // Log the current state of the receive buffer before processing
    if (!this->receive_buffer_.empty()) {
        ESP_LOGV(TAG, "Receive buffer before processing (%u bytes):", this->receive_buffer_.size());
        this->log_buffer_hexdump_(TAG, this->receive_buffer_.data(), this->receive_buffer_.size(), esphome::log::LOG_LEVEL_VERBOSE); // Use custom hexdump
    }

    while (this->receive_buffer_.size() >= 4) { // Minimum size for header (AA 55 Len1 Len2)
        if (this->receive_buffer_[0] != LUX_START_BYTE_1 || this->receive_buffer_[1] != LUX_START_BYTE_2) {
            ESP_LOGW(TAG, "Invalid start bytes in buffer. Dropping byte.");
            // If header is invalid, shift buffer to find next potential start
            this->receive_buffer_.erase(this->receive_buffer_.begin());
            continue;
        }

        uint16_t declared_len = (static_cast<uint16_t>(this->receive_buffer_[2]) << 8) | this->receive_buffer_[3];
        uint16_t total_packet_len = 4 + declared_len; // AA 55 Len1 Len2 + declared_len

        if (this->receive_buffer_.size() < total_packet_len) {
            // Not enough data for a complete packet yet
            ESP_LOGV(TAG, "Incomplete packet in buffer. Need %u, have %u.", total_packet_len, this->receive_buffer_.size());
            break; // Wait for more data
        }

        // We have a complete packet!
        std::vector<uint8_t> complete_packet(this->receive_buffer_.begin(), this->receive_buffer_.begin() + total_packet_len);

        // Attempt to decode the packet
        if (LuxpowerPacket::decode_packet(complete_packet, this)) {
            ESP_LOGD(TAG, "Successfully decoded a Luxpower packet.");
            // Data has been parsed and stored in register_values_ map by decode_packet
            // Now, update the ESPHome sensors
            for (auto *sensor : this->sensors_) {
                sensor->update_value(this->register_values_);
            }
        } else {
            ESP_LOGW(TAG, "Failed to decode Luxpower packet.");
        }

        // Remove the processed packet from the buffer
        this->receive_buffer_.erase(this->receive_buffer_.begin(), this->receive_buffer_.begin() + total_packet_len);
    }
}

void LuxpowerInverterComponent::parse_modbus_response_(const std::vector<uint8_t>& data_payload, uint8_t function_code, uint16_t start_address) {
    if (function_code == MODBUS_READ_HOLDING_REGISTERS || function_code == MODBUS_READ_INPUT_REGISTERS) {
        if (data_payload.size() % 2 != 0) {
            ESP_LOGW(TAG, "Modbus data payload has odd number of bytes. Expected even.");
            return;
        }

        uint16_t num_registers = data_payload.size() / 2;
        ESP_LOGD(TAG, "Parsing %u registers from Modbus response (Func: 0x%02X, Start: 0x%04X).", num_registers, function_code, start_address);

        uint16_t actual_start_address = REG_BATTERY_VOLTAGE; // Use the known start of our requested block

        for (uint16_t i = 0; i < num_registers; ++i) {
            uint16_t register_value = (static_cast<uint16_t>(data_payload[i * 2]) << 8) | data_payload[(i * 2) + 1];
            uint16_t current_register_address = actual_start_address + i;

            this->register_values_[current_register_address] = register_value;
            ESP_LOGV(TAG, "Register 0x%04X: Value = %u", current_register_address, register_value);
        }
    } else {
        ESP_LOGW(TAG, "Unsupported Modbus function code for parsing: 0x%02X", function_code);
    }
}

// --- AsyncClient Callbacks ---

void LuxpowerInverterComponent::onConnect(void *arg, AsyncClient *client) {
  LuxpowerInverterComponent *comp = static_cast<LuxpowerInverterComponent *>(arg);
  ESP_LOGI(TAG, "Successfully connected to inverter!");
  comp->connected_ = true;
  comp->receive_buffer_.clear(); // Clear buffer on new connection
}

void LuxpowerInverterComponent::onDisconnect(void *arg, AsyncClient *client) {
  LuxpowerInverterComponent *comp = static_cast<LuxpowerInverterComponent *>(arg);
  ESP_LOGW(TAG, "Disconnected from inverter.");
  comp->connected_ = false;
  comp->receive_buffer_.clear(); // Clear buffer on disconnect
}

void LuxpowerInverterComponent::onData(void *arg, AsyncClient *client, void *data, size_t len) {
  LuxpowerInverterComponent *comp = static_cast<LuxpowerInverterComponent *>(arg);
  ESP_LOGD(TAG, "Received %u bytes from inverter.", len);

  comp->log_buffer_hexdump_(TAG, static_cast<const uint8_t*>(data), len, esphome::log::LOG_LEVEL_VERBOSE); // Use custom hexdump

  const uint8_t* byte_data = static_cast<const uint8_t*>(data);
  comp->receive_buffer_.insert(comp->receive_buffer_.end(), byte_data, byte_data + len);

  comp->process_received_data_();
}

void LuxpowerInverterComponent::onAck(void *arg, AsyncClient *client, size_t len, uint32_t time) {
  ESP_LOGV(TAG, "Data acknowledged by inverter. Bytes: %u", len);
}

void LuxpowerInverterComponent::onError(void *arg, AsyncClient *client, int8_t error) {
  LuxpowerInverterComponent *comp = static_cast<LuxpowerInverterComponent *>(arg);
  ESP_LOGE(TAG, "AsyncClient error: %s", client->errorToString(error));
  comp->connected_ = false; // Mark as disconnected on error
  comp->receive_buffer_.clear(); // Clear buffer on error
}

// --- Custom Hexdump Logger Implementation ---
void LuxpowerInverterComponent::log_buffer_hexdump_(const char* tag, const uint8_t* buffer, size_t len, esphome::log::LogLevel level) {
    if (len == 0) {
        ESPHOME_LOG_LEVEL(level, tag, "Buffer is empty.");
        return;
    }

    const int BYTES_PER_LINE = 16;
    std::string hex_line;
    std::string ascii_line;
    char temp_str[4]; // Enough for "XX " or "."

    for (size_t i = 0; i < len; ++i) {
        sprintf(temp_str, "%02X ", buffer[i]);
        hex_line += temp_str;

        char c = (char)buffer[i];
        if (c >= ' ' && c <= '~') { // Printable ASCII
            sprintf(temp_str, "%c", c);
        } else {
            sprintf(temp_str, "."); // Non-printable
        }
        ascii_line += temp_str;

        if ((i + 1) % BYTES_PER_LINE == 0 || (i + 1) == len) {
            // End of line or end of buffer, print the accumulated line
            // Pad hex_line if it's the last, incomplete line
            if ((i + 1) == len && (i + 1) % BYTES_PER_LINE != 0) {
                int remaining_bytes = BYTES_PER_LINE - ((i + 1) % BYTES_PER_LINE);
                for (int j = 0; j < remaining_bytes; ++j) {
                    hex_line += "   "; // 3 spaces for each missing hex byte
                }
            }
            ESPHOME_LOG_LEVEL(level, tag, "%s %s", hex_line.c_str(), ascii_line.c_str());
            hex_line.clear();
            ascii_line.clear();
        }
    }
}

} // namespace luxpower_sna
} // namespace esphome
