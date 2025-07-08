#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
//#include "sensor/luxpower_sna_sensor.h"
#include <vector>
#include <string>
#include <chrono>
#include <deque>
#include <map>

#include <AsyncTCP.h>

namespace esphome {
namespace luxpower_sna {

// Forward declaration of LuxpowerSnaSensor class
class LuxpowerSnaSensor; 

// Define the enum for Luxpower register types (unchanged)
enum LuxpowerRegType {
    LUX_REG_TYPE_INT = 0,
    LUX_REG_TYPE_FLOAT_DIV10 = 1,
    LUX_REG_TYPE_SIGNED_INT = 2,
    LUX_REG_TYPE_FIRMWARE = 3,
    LUX_REG_TYPE_MODEL = 4,
    LUX_REG_TYPE_BITMASK = 5,
    LUX_REG_TYPE_TIME_MINUTES = 6,
};

// LuxPower specific packet structure constants (from LXPPacket.py analysis)
// Modbus CRC polynomial for LuxPower's CRC (from LXPPacket.py, likely standard Modbus polynomial)
#define LUXPOWER_CRC16_POLY 0xA001 

#define LUXPOWER_START_BYTE 0x68
#define LUXPOWER_END_BYTE   0x16

// Length of fixed fields within the packet payload (used for Length calculation)
// Inverter Serial (10 bytes) + Dongle Serial (10 bytes) + Function Code (1 byte) + Register/Data (2 bytes) + CRC (2 bytes)
#define LUXPOWER_FIXED_PAYLOAD_LEN 25 // (10 + 10 + 1 + 2 + 2)

// Total min packet length = Start Byte (1) + Length Field (2) + Fixed Payload (25) + End Byte (1) = 29 bytes
#define LUXPOWER_PACKET_MIN_TOTAL_LENGTH 29

class LuxPowerInverterComponent : public Component {
 public:
  LuxPowerInverterComponent();

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_inverter_host(const std::string &host) { this->inverter_host_ = host; }
  void set_inverter_port(uint16_t port) { this->inverter_port_ = port; }
  void set_dongle_serial(const std::string &serial) { this->dongle_serial_ = serial; }
  void set_inverter_serial_number(const std::string &serial) { this->inverter_serial_number_ = serial; }
  void set_update_interval(uint32_t update_interval_ms) { this->update_interval_ = std::chrono::milliseconds(update_interval_ms); }

  void add_luxpower_sensor(LuxpowerSnaSensor *obj, const std::string &name, uint16_t register_address, LuxpowerRegType reg_type, uint8_t bank);

 protected:
  std::string inverter_host_;
  uint16_t inverter_port_;
  std::string dongle_serial_; // Your dongle serial for the request
  std::string inverter_serial_number_; // The inverter's serial number for the request
  std::chrono::milliseconds update_interval_;

  AsyncClient *client_;
  bool client_connected_ = false;
  std::deque<uint8_t> rx_buffer_; 

  uint32_t last_request_time_ = 0;
  uint32_t last_connect_attempt_ = 0;
  const uint32_t connect_retry_interval_ = 5000;

  std::vector<LuxpowerSnaSensor *> luxpower_sensors_;
  std::map<uint16_t, uint16_t> current_raw_registers_;

  // AsyncTCP Callbacks (unchanged)
  void on_connect_cb(void *arg, AsyncClient *client);
  void on_disconnect_cb(void *arg, AsyncClient *client);
  void on_data_cb(void *arg, AsyncClient *client, void *data, size_t len);
  void on_error_cb(void *arg, AsyncClient *client, int error);

  // Connection management (unchanged)
  bool is_connected();
  bool connect_to_inverter();
  void disconnect_from_inverter();
  bool send_data(const std::vector<uint8_t>& data);

  // --- NEW: Luxpower Proprietary Protocol Helpers ---
  // Calculates CRC16 for the LuxPower packet's payload (excluding header, length, and footer)
  static uint16_t calculate_luxpower_crc16(const std::vector<uint8_t>& data);

  // Builds a full LuxPower request packet (including 0x68 header, serials, CRC, and 0x16 footer)
  std::vector<uint8_t> build_luxpower_request_packet(uint8_t function_code, uint16_t register_address, 
                                                     uint16_t num_registers_or_data, const std::vector<uint8_t>& additional_data = {});
  
  // Parses a full LuxPower response packet, validates CRC, and extracts payload
  // Returns true on success, populates out_payload with the extracted data (Function Code onwards)
  bool parse_luxpower_response_packet(const std::vector<uint8_t>& response_packet, std::vector<uint8_t>& out_payload);

  // Helper to interpret the extracted payload from a Read Holding Registers (FC 0x03) response
  bool interpret_modbus_read_holding_registers_payload(const std::vector<uint8_t>& payload, uint16_t expected_start_address, uint16_t expected_num_registers);


  // Sensor value decoding (unchanged)
  float get_sensor_value_(uint16_t register_value, LuxpowerRegType reg_type);
  std::string get_firmware_version_(const std::vector<uint16_t>& data);
  std::string get_model_name_(const std::vector<uint16_t>& data);
};

} // namespace luxpower_sna
} // namespace esphome