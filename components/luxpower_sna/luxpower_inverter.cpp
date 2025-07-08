#include "luxpower_inverter.h" // Includes its own header
#include "esphome/core/log.h"  // For ESP_LOGx macros
#include "esphome/core/hal.h" // For millis()
#include <arpa/inet.h>         // For inet_pton
#include <sys/socket.h>        // For socket, connect, send, recv
#include <unistd.h>            // For close
#include <cstring>             // For memset
#include <algorithm>           // For std::min (though not explicitly used in current read logic, good to have for string manipulation)
#include <map>                 // For std::map

namespace esphome {
namespace luxpower_sna { // CORRECT NAMESPACE: Must match the .h file

static const char *const TAG = "luxpower_sna"; // TAG defined within the correct namespace

// Constructor definition
LuxPowerInverterComponent::LuxPowerInverterComponent() {
  // No AsyncClient to initialize here anymore, using direct socket.
}

void LuxPowerInverterComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxPower Inverter Component...");
  ESP_LOGCONFIG(TAG, "  Host: %s", this->inverter_host_.c_str());
  ESP_LOGCONFIG(TAG, "  Port: %u", this->inverter_port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", this->dongle_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter Serial: %s", this->inverter_serial_number_.c_str());
  ESP_LOGCONFIG(TAG, "  Update Interval: %u ms", (uint32_t) this->update_interval_.count());
  
  this->last_update_time_ = std::chrono::steady_clock::now(); // Initialize last update time
}

void LuxPowerInverterComponent::loop() {
  // Check if it's time for an update based on the configured interval
  if (std::chrono::steady_clock::now() - this->last_update_time_ >= this->update_interval_) {
    this->last_update_time_ = std::chrono::steady_clock::now(); // Reset timer for next update
    ESP_LOGD(TAG, "Updating sensor values...");

    // Example of reading data (adjust ranges as per actual inverter registers and banks)
    // Read Bank 0 registers (e.g., from address 0, 126 registers)
    std::vector<uint16_t> bank0_data;
    // Assuming 126 registers for Bank 0 from address 0 to 125
    if (this->read_registers_(0, 126, bank0_data)) { 
      // Iterate through all configured sensors and update their states if data is available
      for (LuxpowerSnaSensor *sens : this->luxpower_sensors_) {
        // Only process sensors belonging to Bank 0 for this read cycle
        if (sens->get_bank() == 0) { // Use getter for bank
          // Check if the register address is within the received data range
          if (sens->get_register_address() < bank0_data.size()) { // Use getter for register_address
            // Get the raw register value
            uint16_t raw_value = bank0_data[sens->get_register_address()];
            
            // Handle special string types (Firmware, Model)
            if (sens->get_reg_type() == LUX_REG_TYPE_FIRMWARE) { // Use getter for reg_type
              // Firmware is usually spanned across multiple registers
              if (sens->get_register_address() == 114 && bank0_data.size() >= 119) { // Start of firmware block
                  std::vector<uint16_t> firmware_regs(bank0_data.begin() + 114, bank0_data.begin() + 119); // 5 registers (114-118)
                  std::string firmware_version = this->get_firmware_version_(firmware_regs);
                  ESP_LOGD(TAG, "Firmware Version: %s", firmware_version.c_str());
                  sens->publish_state(0.0f); // Publish a dummy float, actual value would be in text_sensor
              }
            } else if (sens->get_reg_type() == LUX_REG_TYPE_MODEL) { // Use getter for reg_type
              // Model is usually spanned across multiple registers
              if (sens->get_register_address() == 119 && bank0_data.size() >= 123) { // Start of model block
                  std::vector<uint16_t> model_regs(bank0_data.begin() + 119, bank0_data.begin() + 123); // 4 registers (119-122)
                  std::string model_name = this->get_model_name_(model_regs);
                  ESP_LOGD(TAG, "Model Name: %s", model_name.c_str());
                  sens->publish_state(0.0f); // Publish a dummy float, actual value would be in text_sensor
              }
            } else {
              // For numeric sensor types, convert and publish
              float value = this->get_sensor_value_(raw_value, sens->get_reg_type()); // Use getter for reg_type
              sens->publish_state(value);
            }
          } else {
            ESP_LOGW(TAG, "Sensor %s (reg 0x%04X) is outside received Bank 0 data range.", sens->get_name().c_str(), sens->get_register_address());
            sens->publish_state(NAN); // Mark as unavailable
          }
        }
      }
    } else {
      ESP_LOGW(TAG, "Failed to read Bank 0 registers from %s:%u", this->inverter_host_.c_str(), this->inverter_port_);
      // Mark all sensors as unavailable on communication failure
      for (LuxpowerSnaSensor *sens : this->luxpower_sensors_) {
        sens->publish_state(NAN);
      }
    }
    // Add similar logic for other banks (e.g., Bank 1, Bank 2, etc.) if needed.
  }
}

void LuxPowerInverterComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "LuxPower Inverter Component:");
  ESP_LOGCONFIG(TAG, "  Host: %s", this->inverter_host_.c_str());
  ESP_LOGCONFIG(TAG, "  Port: %u", this->inverter_port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", this->dongle_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter Serial: %s", this->inverter_serial_number_.c_str());
  ESP_LOGCONFIG(TAG, "  Update Interval: %u ms", (uint32_t) this->update_interval_.count());
  for (LuxpowerSnaSensor *sens : this->luxpower_sensors_) {
    LOG_SENSOR("  ", "Sensor", sens); // Helper macro to log sensor configuration
  }
}

float LuxPowerInverterComponent::get_setup_priority() const { 
  // Set priority after network connection is established
  return esphome::setup_priority::AFTER_CONNECTION; 
}

// Constructor definition (must be outside setup/loop)
LuxPowerInverterComponent::LuxPowerInverterComponent() {
  // Initialization of member variables if needed
}

void LuxPowerInverterComponent::add_luxpower_sensor(LuxpowerSnaSensor *obj, const std::string &name, uint16_t register_address, LuxpowerRegType reg_type, uint8_t bank) {
  obj->set_name(name); // Set the sensor's name
  obj->set_register_address(register_address); // Use setter
  obj->set_reg_type(reg_type); // Use setter
  obj->set_bank(bank); // Use setter
  obj->set_parent(this); // Set this component as the parent of the sensor
  this->luxpower_sensors_.push_back(obj); // Add sensor to the list
}

// Private methods implementations:

bool LuxPowerInverterComponent::read_registers_(uint16_t start_address, uint16_t num_registers, std::vector<uint16_t>& out_data) {
  if (num_registers == 0 || num_registers > 125) { // Common Modbus limit, check Luxpower specific limits
    ESP_LOGE(TAG, "Invalid number of registers to read: %u", num_registers);
    return false;
  }

  // Construct Modbus TCP request ADU (Application Data Unit)
  std::vector<uint8_t> command;
  // MBAP Header (7 bytes)
  command.push_back(0x00); command.push_back(0x01); // Transaction ID (arbitrary)
  command.push_back(0x00); command.push_back(0x00); // Protocol ID (Modbus TCP)
  command.push_back(0x00); command.push_back(0x06); // Length (6 bytes for PDU)
  // Modbus PDU (Function Code + Data)
  command.push_back(0x01); // Unit ID (typically 1 for inverters)
  command.push_back(0x03); // Function Code: Read Holding Registers
  command.push_back(static_cast<uint8_t>((start_address >> 8) & 0xFF)); // Starting Address High Byte
  command.push_back(static_cast<uint8_t>(start_address & 0xFF));       // Starting Address Low Byte
  command.push_back(static_cast<uint8_t>((num_registers >> 8) & 0xFF)); // Quantity of Registers High Byte
  command.push_back(static_cast<uint8_t>(num_registers & 0xFF));       // Quantity of Registers Low Byte

  std::vector<uint8_t> response;
  if (!this->send_modbus_command_(command, response)) {
    return false; // Failed to send or receive response
  }

  // Parse Modbus TCP response
  // Expected: MBAP Header (7 bytes) + Unit ID (1) + FC (1) + Byte Count (1) + Data (N bytes)
  // Minimum response size: 7 (MBAP) + 1 (Unit ID) + 1 (FC) + 1 (Byte Count) = 10 bytes
  if (response.size() < 9 || response[7] != 0x03) { // Check size and Function Code in response
    ESP_LOGE(TAG, "Invalid Modbus response header. Size: %u, FC: 0x%02X", response.size(), response.size() > 7 ? response[7] : 0);
    return false;
  }

  uint8_t byte_count = response[8]; // Number of data bytes in the response
  if (response.size() < 9 + byte_count || byte_count != num_registers * 2) {
    ESP_LOGE(TAG, "Modbus response byte count mismatch. Expected %u, got %u", num_registers * 2, byte_count);
    return false;
  }

  out_data.clear(); // Clear previous data
  for (size_t i = 0; i < byte_count; i += 2) {
    // Combine two bytes into a 16-bit register value (Big-endian)
    uint16_t value = (static_cast<uint16_t>(response[9 + i]) << 8) | response[10 + i];
    out_data.push_back(value);
  }
  return true;
}


bool LuxPowerInverterComponent::send_modbus_command_(const std::vector<uint8_t>& command, std::vector<uint8_t>& response) {
  int sock = -1;
  struct sockaddr_in serv_addr; // Structure to hold server address details

  // Create a socket
  sock = socket(AF_INET, SOCK_STREAM, 0); // AF_INET for IPv4, SOCK_STREAM for TCP
  if (sock < 0) {
    ESP_LOGE(TAG, "Socket creation error");
    return false;
  }

  // Set up server address structure
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(this->inverter_port_); // Convert port to network byte order

  // Convert IPv4 address from text to binary form
  if (inet_pton(AF_INET, this->inverter_host_.c_str(), &serv_addr.sin_addr) <= 0) {
    ESP_LOGE(TAG, "Invalid address/ Address not supported: %s", this->inverter_host_.c_str());
    close(sock); // Close socket on error
    return false;
  }

  // Connect to the server
  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    ESP_LOGW(TAG, "Connection Failed! Host: %s, Port: %u. Is the inverter online?", this->inverter_host_.c_str(), this->inverter_port_);
    close(sock); // Close socket on error
    return false;
  }

  // Send the Modbus command
  if (send(sock, command.data(), command.size(), 0) != command.size()) {
    ESP_LOGE(TAG, "Send failed");
    close(sock); // Close socket on error
    return false;
  }

  response.clear();
  uint8_t buffer[256]; // Buffer for receiving data (Modbus ADU max size approx 256 bytes)
  int bytes_read = 0;

  // Read Modbus TCP header first (7 bytes: Transaction ID (2), Protocol ID (2), Length (2), Unit ID (1))
  bytes_read = recv(sock, buffer, 7, 0); 
  if (bytes_read != 7) {
      ESP_LOGE(TAG, "Failed to read full Modbus TCP header. Read %d bytes.", bytes_read);
      close(sock);
      return false;
  }
  // Copy the header bytes to the response vector
  for(int i = 0; i < bytes_read; ++i) response.push_back(buffer[i]);

  // Extract the PDU Length from the MBAP header (bytes 4 and 5)
  uint16_t pdu_length = (static_cast<uint16_t>(response[4]) << 8) | response[5]; 
  
  // Read the remaining PDU data based on the extracted length
  bytes_read = recv(sock, buffer, pdu_length, 0); // Read pdu_length bytes (Unit ID + FC + Data)
  if (bytes_read != pdu_length) { // Check if we received the expected remaining bytes
    ESP_LOGE(TAG, "Failed to read expected Modbus PDU data. Expected: %u, Got: %u", pdu_length, bytes_read);
    close(sock);
    return false;
  }
  // Copy remaining bytes
  for(int i=0; i<bytes_read; ++i) response.push_back(buffer[i]);

  close(sock); // Close the socket after communication
  return true;
}

uint16_t LuxPowerInverterComponent::calculate_crc_(const std::vector<uint8_t>& data) {
  // This function is generally not needed for Modbus TCP as TCP handles error checking.
  // It would be used for Modbus RTU (Serial).
  return 0; // Return 0 as it's not applicable for TCP
}

float LuxPowerInverterComponent::get_sensor_value_(uint16_t register_value, LuxpowerRegType reg_type) {
  switch (reg_type) {
    case LUX_REG_TYPE_INT:
      return static_cast<float>(register_value);
    case LUX_REG_TYPE_FLOAT_DIV10:
      return static_cast<float>(register_value) / 10.0f;
    case LUX_REG_TYPE_SIGNED_INT:
      // Cast to signed 16-bit integer to handle negative values correctly
      return static_cast<float>(static_cast<int16_t>(register_value));
    case LUX_REG_TYPE_FIRMWARE:
    case LUX_REG_TYPE_MODEL:
      // These types are handled as strings; returning 0.0f as a placeholder for float sensor
      return 0.0f; 
    case LUX_REG_TYPE_BITMASK:
      return static_cast<float>(register_value); // Treat bitmask as integer value for now
    case LUX_REG_TYPE_TIME_MINUTES:
      return static_cast<float>(register_value); // Treat minutes as integer for now
    default:
      ESP_LOGW(TAG, "Unknown LuxpowerRegType: %d", static_cast<int>(reg_type));
      return NAN; // Not a Number for invalid type
  }
}

std::string LuxPowerInverterComponent::get_firmware_version_(const std::vector<uint16_t>& data) {
  // Firmware version is typically spread across multiple registers, e.g., 5 registers = 10 bytes
  // Example: 0x3132, 0x3334, 0x3536, 0x3738, 0x3941 -> "123456789A"
  if (data.size() < 5) return ""; // Ensure enough registers are provided
  char buffer[11]; // 10 characters + null terminator
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
  buffer[10] = '\0'; // Null-terminate the string
  return std::string(buffer);
}

std::string LuxPowerInverterComponent::get_model_name_(const std::vector<uint16_t>& data) {
  // Model name is also typically spread across registers, e.g., 4 registers = 8 bytes
  if (data.size() < 4) return ""; // Ensure enough registers are provided
  char buffer[9]; // 8 characters + null terminator
  buffer[0] = (data[0] >> 8) & 0xFF;
  buffer[1] = data[0] & 0xFF;
  buffer[2] = (data[1] >> 8) & 0xFF;
  buffer[3] = data[1] & 0xFF;
  buffer[4] = (data[2] >> 8) & 0xFF;
  buffer[5] = data[2] & 0xFF;
  buffer[6] = (data[3] >> 8) & 0xFF;
  buffer[7] = data[3] & 0xFF;
  buffer[8] = '\0'; // Null-terminate the string
  return std::string(buffer);
}

} // namespace luxpower_sna
} // namespace esphome