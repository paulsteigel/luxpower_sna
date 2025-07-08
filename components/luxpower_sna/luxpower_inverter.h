// components/luxpower_sna/luxpower_inverter.h
#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <ESPAsyncTCP.h> // Include the ESPAsyncTCP library
#include <vector>
#include <string>
#include <map> // For storing register values

#include "consts.h" // Include the new constants file
#include "sensors.h"    // Include the new sensor header

namespace esphome {
namespace luxpower_sna {

// Forward declaration of LuxpowerPacket helper class
class LuxpowerPacket;

// CRC16 calculation function (Modbus CRC)
uint16_t calculate_crc16(const uint8_t *data, size_t length);

// Define the LuxpowerInverterComponent class, inheriting from PollingComponent
// PollingComponent provides the update() method that will be called periodically
class LuxpowerInverterComponent : public PollingComponent { // Class name changed
  // Grant LuxpowerPacket access to protected/private members of this class
  friend class LuxpowerPacket; // Added friend declaration

 public:
  // Constructor: Initializes the component with a default update interval.
  // The update interval can be overridden by the YAML configuration.
  LuxpowerInverterComponent(); // Constructor name changed

  // Setters for the configuration parameters from YAML
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::string &serial) { this->dongle_serial_ = serial; }
  void set_inverter_serial_number(const std::string &serial) { this->inverter_serial_number_ = serial; }

  // Add a sensor to be managed by this component
  void add_sensor(LuxpowerSnaSensor *sensor) { this->sensors_.push_back(sensor); }

  // setup() is called once when the ESPHome device starts up.
  void setup() override;

  // update() is called periodically based on the update_interval.
  void update() override;

  // dump_config() is used for logging the component's configuration.
  void dump_config() override;

 protected:
  std::string host_;
  uint16_t port_;
  std::string dongle_serial_;
  std::string inverter_serial_number_;

  AsyncClient client_; // The TCP client for communication
  bool connected_ = false; // Flag to track connection status
  std::vector<uint8_t> receive_buffer_; // Buffer to accumulate incoming data
  std::map<uint16_t, uint16_t> register_values_; // Map to store parsed register values (address -> value)

  std::vector<LuxpowerSnaSensor *> sensors_; // List of sensors managed by this component

  // Callback for when the client connects
  static void onConnect(void *arg, AsyncClient *client);
  // Callback for when the client disconnects
  static void onDisconnect(void *arg, AsyncClient *client);
  // Callback for when data is received
  static void onData(void *arg, AsyncClient *client, void *data, size_t len);
  // Callback for when data is sent
  static void onAck(void *arg, AsyncClient *client, size_t len, uint32_t time);
  // Callback for error
  static void onError(void *arg, AsyncClient *client, int8_t error);

  // Helper method to connect to the inverter
  void connect_to_inverter_();

  // Helper method to send a raw packet
  void send_packet_(const std::vector<uint8_t>& packet);

  // Helper method to process received data
  void process_received_data_();

  // Helper method to parse Modbus RTU data within a Luxpower packet
  // This will extract register values and update the register_values_ map
  void parse_modbus_response_(const std::vector<uint8_t>& data_payload, uint8_t function_code, uint16_t start_address);

  // Custom helper for logging byte buffers as hex dump
  void log_buffer_hexdump_(const char* tag, const uint8_t* buffer, size_t len, esphome::LogLevel level = esphome::LogLevel::LOG_LEVEL_VERBOSE);
};

// --- LuxpowerPacket Helper Class ---
// This class will encapsulate the logic for building and parsing Luxpower packets.
// It will be a C++ equivalent of the LXPPacket.py functionality.
class LuxpowerPacket {
public:
    // Builds a read holding registers command packet
    // target_serial: Inverter serial number (10 bytes)
    // dongle_serial: Dongle serial number (10 bytes)
    // start_address: Starting register address
    // num_registers: Number of registers to read
    static std::vector<uint8_t> build_read_holding_command(
        const std::string& target_serial,
        const std::string& dongle_serial,
        uint16_t start_address,
        uint16_t num_registers
    );

    // Builds a read input registers command packet
    // target_serial: Inverter serial number (10 bytes)
    // dongle_serial: Dongle serial number (10 bytes)
    // start_address: Starting register address
    // num_registers: Number of registers to read
    static std::vector<uint8_t> build_read_input_command(
        const std::string& target_serial,
        const std::string& dongle_serial,
        uint16_t start_address,
        uint16_t num_registers
    );

    // Decodes a received Luxpower packet
    // raw_data: The raw byte vector received from the inverter
    // comp: Pointer to the main component to update register values
    // Returns true if decoding was successful, false otherwise.
    static bool decode_packet(const std::vector<uint8_t>& raw_data, LuxpowerInverterComponent* comp);

private:
    // Helper to convert string serial to byte vector
    static std::vector<uint8_t> serial_string_to_bytes(const std::string& serial_str);
};

} // namespace luxpower_sna
} // namespace esphome
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
