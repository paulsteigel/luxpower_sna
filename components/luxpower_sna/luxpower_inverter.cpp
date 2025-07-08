```cpp
// components/luxpower_sna/luxpower_inverter.cpp
#include "luxpower_inverter.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
// No longer need esphome/core/defines.h for ESP_LOG_BUFFER_HEXDUMP, as we're implementing our own
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

    // Packet seems valid, now parse the
