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
