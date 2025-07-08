// src/esphome/components/luxpower_sna/luxpower_inverter.cpp

#include "luxpower_inverter.h" // Keep this first - it brings in all declarations
#include "esphome/core/log.h"
#include "esphome/core/helpers.h" // For format_hex_pretty and other utilities
#include "esphome/components/sensor/sensor.h" // Needed for sensor-related functions like publish_state
#include "luxpower_sna_constants.h" // <--- UNCOMMENTED: Needed for constants like LUXPOWER_END_BYTE_LENGTH and LuxpowerRegType definition
#include "luxpower_sna_sensor.h"

#include <functional> // For std::bind and std::placeholders
#include <algorithm> // For std::min, std::max (if used in your logic)
#include <cmath> // For NAN (Not-a-Number)

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_inverter"; // Tag for logging in this component

// Constructor implementation
// Initializes member variables and the AsyncClient
LuxPowerInverterComponent::LuxPowerInverterComponent() : Component() {
  this->client_ = new AsyncClient(); // Initialize the AsyncClient
  this->client_connected_ = false;
  this->request_pending_ = false;
  this->waiting_for_response_ = false;
  this->last_connection_attempt_time_ = 0;
  this->last_request_time_ = 0;
  this->inverter_port_ = 0; // Initialize port to 0
}

// setup() implementation: Called once when the component is initialized
void LuxPowerInverterComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxPower Inverter component...");

  // You will need to set inverter_host_ and inverter_port_ based on your ESPHome YAML configuration.
  // For example, if you have these as options in your custom component's schema:
  // this->inverter_host_ = get_host_from_config(); // You'd need a helper or direct variable assignment
  // this->inverter_port_ = get_port_from_config(); // based on how you expose these in YAML
  // For now, these will be empty/0 unless set in your config
  ESP_LOGCONFIG(TAG, "  Inverter Host: %s", this->inverter_host_.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter Port: %u", this->inverter_port_);

  // Bind callbacks for AsyncClient events
  // 'this' is passed as 'arg' to the static callback, then cast back to LuxPowerInverterComponent*
  this->client_->onConnect(std::bind(&LuxPowerInverterComponent::on_connect_cb, this, std::placeholders::_1, std::placeholders::_2));
  this->client_->onDisconnect(std::bind(&LuxPowerInverterComponent::on_disconnect_cb, this, std::placeholders::_1, std::placeholders::_2));
  this->client_->onData(std::bind(&LuxPowerInverterComponent::on_data_cb, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
  this->client_->onError(std::bind(&LuxPowerInverterComponent::on_error_cb, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

  // Attempt initial connection
  this->connect_to_inverter();
}

// loop() implementation: Called repeatedly by ESPHome
void LuxPowerInverterComponent::loop() {
  // Periodically attempt to reconnect if not currently connected
  if (!this->client_connected_ && (millis() - this->last_connection_attempt_time_ > this->reconnect_interval_ms_)) {
    ESP_LOGD(TAG, "Attempting to reconnect to inverter at %s:%u...", this->inverter_host_.c_str(), this->inverter_port_);
    this->connect_to_inverter();
  }

  // Example logic to send a request periodically if connected and no request is pending
  if (this->client_connected_ && !this->request_pending_ && (millis() - this->last_request_time_ > 5000)) { // Example: send a request every 5 seconds
    this->request_pending_ = true;
    this->last_request_time_ = millis();
    // TODO: Implement the actual data packet generation for your request
    // Example: std::vector<unsigned char> request_packet = generate_luxpower_request();
    // this->send_data(request_packet);
    ESP_LOGD(TAG, "Sending placeholder request to inverter...");
    // For now, let's simulate a sent packet and wait for response
    this->waiting_for_response_ = true;
  }

  // Process received data in receive_buffer_ if a full packet is available
  // This part is crucial for your protocol; adjust packet length checks as needed.
  if (this->receive_buffer_.size() >= LUXPOWER_MIN_PACKET_LENGTH && this->waiting_for_response_) {
      std::vector<unsigned char> parsed_data; // This might hold simplified data after parsing
      if (this->parse_luxpower_response_packet(this->receive_buffer_, parsed_data)) {
          ESP_LOGD(TAG, "Successfully parsed inverter response. Updating sensors...");
          this->waiting_for_response_ = false;
          this->request_pending_ = false; // Request handled

          // Iterate through registered sensors and publish their states
          for (auto* s : this->luxpower_sensors_) {
            // Your logic here to match sensor to parsed data
            // Example based on previous errors:
            // if (s->get_bank() == 0) { // Only process Bank 0 sensors for this request
                auto it = this->current_raw_registers_.find(s->get_register_address());
                if (it != this->current_raw_registers_.end()) {
                    uint16_t raw_value = it->second;
                    float value = this->get_sensor_value_(raw_value, s->get_reg_type());
                    s->publish_state(value);
                } else {
                    ESP_LOGW(TAG, "Sensor %s (reg 0x%04X, bank %u) not found in received data. Publishing NAN.",
                             s->get_name().c_str(), s->get_register_address(), s->get_bank());
                    s->publish_state(NAN);
                }
            // }
          }
      } else if (this->receive_buffer_.size() > LUXPOWER_MAX_PACKET_LENGTH) { // Prevent buffer overflow for malformed packets
          ESP_LOGW(TAG, "Received malformed or oversized packet. Clearing buffer.");
          this->receive_buffer_.clear();
          this->waiting_for_response_ = false;
          this->request_pending_ = false;
      }
      // If packet is not yet complete (size < min or size < reported_length), do nothing and wait for more data in next loop iteration
  }
}

// dump_config() implementation: For logging component configuration at startup
void LuxPowerInverterComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "LuxPower Inverter Component:");
  ESP_LOGCONFIG(TAG, "  Host: %s", this->inverter_host_.c_str());
  ESP_LOGCONFIG(TAG, "  Port: %u", this->inverter_port_);
  ESP_LOGCONFIG(TAG, "  Reconnect Interval: %ums", this->reconnect_interval_ms_);

  if (!this->luxpower_sensors_.empty()) {
    ESP_LOGCONFIG(TAG, "  Registered Sensors:");
    for (auto* s : this->luxpower_sensors_) {
      // Assuming LuxpowerSnaSensor has its own dump_config() or you print details here.
      // Call sensor's dump_config for more detailed logging if it exists
      // s->dump_config();
      ESP_LOGCONFIG(TAG, "    - Name: %s (Reg: 0x%04X, Type: %u, Bank: %u)",
                    s->get_name().c_str(), s->get_register_address(), s->get_reg_type(), s->get_bank());
    }
  } else {
    ESP_LOGCONFIG(TAG, "  No LuxPower SNA sensors registered yet.");
  }
}

// is_connected() implementation
bool LuxPowerInverterComponent::is_connected() {
  return this->client_connected_ && this->client_ && this->client_->connected();
}

// connect_to_inverter() implementation
bool LuxPowerInverterComponent::connect_to_inverter() {
  if (this->is_connected()) {
    ESP_LOGD(TAG, "Already connected to inverter.");
    return true; // Already connected
  }

  // Ensure host and port are set from your YAML configuration
  // For demonstration, let's hardcode for now if not set by config schema
  if (this->inverter_host_.empty()) {
      this->inverter_host_ = "192.168.1.100"; // TODO: Replace with your actual inverter IP/hostname
      ESP_LOGW(TAG, "Inverter host not set in config, using default: %s", this->inverter_host_.c_str());
  }
  if (this->inverter_port_ == 0) {
      this->inverter_port_ = 8000; // TODO: Replace with your actual inverter port
      ESP_LOGW(TAG, "Inverter port not set in config, using default: %u", this->inverter_port_);
  }


  this->last_connection_attempt_time_ = millis();
  ESP_LOGI(TAG, "Initiating TCP connection to %s:%u...", this->inverter_host_.c_str(), this->inverter_port_);
  if (!this->client_->connect(this->inverter_host_.c_str(), this->inverter_port_)) {
    ESP_LOGE(TAG, "Failed to start TCP connection process.");
    return false;
  }
  return true;
}

// disconnect_from_inverter() implementation
void LuxPowerInverterComponent::disconnect_from_inverter() {
  if (this->client_) {
    ESP_LOGI(TAG, "Stopping TCP client and disconnecting from inverter.");
    this->client_->stop(); // This will trigger onDisconnect_cb if not already disconnected
  }
  this->client_connected_ = false;
  this->receive_buffer_.clear(); // Clear any partial data
  this->request_pending_ = false;
  this->waiting_for_response_ = false;

  // Optionally set all associated sensors to NAN on disconnect
  for (auto* s : this->luxpower_sensors_) {
    if (s->has_state()) { // Only publish if the sensor actually reports a state
      s->publish_state(NAN);
    }
  }
}

// send_data() implementation
bool LuxPowerInverterComponent::send_data(const std::vector<unsigned char>& data) {
  if (!this->is_connected()) {
    ESP_LOGE(TAG, "Not connected to inverter. Cannot send data.");
    return false;
  }
  if (data.empty()) {
    ESP_LOGW(TAG, "Attempted to send empty data packet.");
    return true; // Consider as success if no data to send
  }

  ESP_LOGD(TAG, "Sending %u bytes to inverter:", data.size());
  // The format_hex_pretty helper requires #include "esphome/core/helpers.h"
  ESP_LOGD(TAG, "  %s", format_hex_pretty(data).c_str());

  size_t written_bytes = this->client_->write(reinterpret_cast<const char*>(data.data()), data.size());
  if (written_bytes != data.size()) {
    ESP_LOGE(TAG, "Failed to send all data (%u/%u bytes sent).", written_bytes, data.size());
    return false;
  }
  this->waiting_for_response_ = true; // Set flag to expect a response
  return true;
}

// on_connect_cb implementation: Called when TCP connection is established
void LuxPowerInverterComponent::on_connect_cb(void *arg, AsyncClient *client) {
  LuxPowerInverterComponent *comp = static_cast<LuxPowerInverterComponent*>(arg);
  ESP_LOGI(TAG, "Successfully connected to inverter!");
  comp->client_connected_ = true;
  comp->receive_buffer_.clear(); // Clear buffer on new connection
  comp->request_pending_ = false;
  comp->waiting_for_response_ = false;
}

// on_disconnect_cb implementation: Called when TCP connection is lost
void LuxPowerInverterComponent::on_disconnect_cb(void *arg, AsyncClient *client) {
  LuxPowerInverterComponent *comp = static_cast<LuxPowerInverterComponent*>(arg);
  ESP_LOGW(TAG, "Disconnected from inverter!");
  comp->disconnect_from_inverter(); // Use common disconnect logic
}

// on_data_cb implementation: Called when data is received
void LuxPowerInverterComponent::on_data_cb(void *arg, AsyncClient *client, void *data, size_t len) {
  LuxPowerInverterComponent *comp = static_cast<LuxPowerInverterComponent*>(arg);
  ESP_LOGD(TAG, "Received %u bytes from inverter.", len);
  // ESP_LOGD(TAG, "  %s", format_hex_pretty(data, len).c_str()); // Requires format_hex_pretty helper

  // Append received data to the receive buffer
  const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);
  comp->receive_buffer_.insert(comp->receive_buffer_.end(), bytes, bytes + len);
}

// on_error_cb implementation: Called when a TCP error occurs
void LuxPowerInverterComponent::on_error_cb(void *arg, AsyncClient *client, int error) {
  LuxPowerInverterComponent *comp = static_cast<LuxPowerInverterComponent*>(arg);
  ESP_LOGE(TAG, "TCP client error: %d (%s)", error, client->errorToString(error)); // AsyncTCP provides errorToString
  comp->disconnect_from_inverter(); // Disconnect on error
}

// add_luxpower_sensor() implementation: Adds a sensor to the component
void LuxPowerInverterComponent::add_luxpower_sensor(LuxpowerSnaSensor* obj, const std::string& name, uint16_t reg_addr, LuxpowerRegType reg_type, uint8_t bank) {
  obj->set_name(name);
  obj->set_register_address(reg_addr);
  obj->set_reg_type(reg_type);
  obj->set_bank(bank);
  obj->set_parent(this); // Allow sensor to access its parent component if needed
  this->luxpower_sensors_.push_back(obj);
  App.register_component(obj); // Register the sensor with ESPHome's framework
  ESP_LOGD(TAG, "Added sensor: %s (Reg: 0x%04X, Type: %u, Bank: %u)",
           name.c_str(), reg_addr, reg_type, bank);
}

// parse_luxpower_response_packet() implementation: Parses raw data into meaningful info
bool LuxPowerInverterComponent::parse_luxpower_response_packet(const std::vector<unsigned char>& raw_data, std::vector<unsigned char>& parsed_data) {
  // TODO: This is where you implement the core of your LuxPower protocol parsing.
  // You need to:
  // 1. Validate packet length.
  // 2. Check header/start bytes.
  // 3. Perform CRC or checksum validation (crucial for data integrity).
  // 4. Extract register addresses and their corresponding values.
  // 5. Populate this->current_raw_registers_ map (e.g., this->current_raw_registers_[address] = value;).
  // 6. Clear the receive_buffer_ or remove the parsed packet from it.

  // Example placeholders (you must replace with your actual protocol logic):
  const size_t LUXPOWER_MIN_PACKET_LENGTH = 10; // Placeholder: Minimum expected packet size
  const size_t LUXPOWER_MAX_PACKET_LENGTH = 200; // Placeholder: Max expected packet size
  // You'll need to define these in luxpower_sna_constants.h or similar

  if (raw_data.size() < LUXPOWER_MIN_PACKET_LENGTH) {
    // Not enough data for a full packet, wait for more
    return false;
  }

  // --- EXAMPLE PLACEHOLDER PROTOCOL PARSING LOGIC ---
  // This needs to be replaced with your actual LuxPower protocol.
  // Assuming a simple structure: [START_BYTE] [LENGTH_MSB] [LENGTH_LSB] [DATA...] [CRC_MSB] [CRC_LSB] [END_BYTE]

  // Placeholder: Check for a specific start byte (e.g., 0xAA)
  if (raw_data[0] != 0xAA) { // Replace 0xAA with your actual start byte
      ESP_LOGW(TAG, "Invalid start byte in packet. Clearing buffer.");
      this->receive_buffer_.clear(); // Clear malformed data
      return false;
  }

  // Placeholder: Extract reported length (assuming 2 bytes after start byte)
  // uint16_t reported_length = (raw_data[1] << 8) | raw_data[2];
  // if (raw_data.size() < reported_length) {
  //     return false; // Packet not fully received yet
  // }

  // Placeholder: CRC calculation and validation
  // uint16_t expected_crc = calculate_crc16_from_packet(raw_data); // You need to implement this
  // uint16_t actual_crc_in_packet = (raw_data[reported_length - 3] << 8) | raw_data[reported_length - 2];
  // if (expected_crc != actual_crc_in_packet) {
  //     ESP_LOGW(TAG, "CRC mismatch! Expected 0x%04X, Got 0x%04X. Clearing buffer.", expected_crc, actual_crc_in_packet);
  //     this->receive_buffer_.clear();
  //     return false;
  // }

  // Placeholder: Extracting data and populating current_raw_registers_
  // You'll need to know the structure of the data payload (e.g., address-value pairs)
  this->current_raw_registers_.clear(); // Clear previous readings
  // For example, if your data payload starts at index 3 and ends before the CRC/End Byte
  // for (size_t i = 3; i < raw_data.size() - 3; i += 4) { // Assuming each entry is 4 bytes (2 for addr, 2 for value)
  //     uint16_t reg_address = (raw_data[i] << 8) | raw_data[i+1];
  //     uint16_t reg_value = (raw_data[i+2] << 8) | raw_data[i+3];
  //     this->current_raw_registers_[reg_address] = reg_value;
  // }
  // --- END EXAMPLE PLACEHOLDER ---

  // If parsing is successful, clear the buffer or remove the parsed packet
  this->receive_buffer_.clear();
  return true; // Packet successfully parsed
}

// get_sensor_value_() implementation: Converts raw register values to sensor values
float LuxPowerInverterComponent::get_sensor_value_(uint16_t raw_value, LuxpowerRegType reg_type) {
  // TODO: Implement the actual conversion logic based on your LuxPower protocol's register types.
  // This is a critical function for getting correct sensor readings.

  switch (reg_type) {
    case LUX_REG_TYPE_INT:
      return static_cast<float>(raw_value);
    case LUX_REG_TYPE_FLOAT_DIV10:
      return static_cast<float>(raw_value) / 10.0f;
    case LUX_REG_TYPE_SIGNED_INT:
      // Handle signed 16-bit integer conversion
      return static_cast<float>(static_cast<int16_t>(raw_value));
    case LUX_REG_TYPE_FIRMWARE: {
      // Example: Convert two bytes to version like X.YY
      uint8_t major = (raw_value >> 8) & 0xFF;
      uint8_t minor = raw_value & 0xFF;
      return static_cast<float>(major) + (static_cast<float>(minor) / 100.0f);
    }
    case LUX_REG_TYPE_MODEL:
        // Model numbers are often strings, so this might be better handled by a TextSensor.
        // For a float sensor, you might return the raw value or a derived numerical ID.
        return static_cast<float>(raw_value);
    case LUX_REG_TYPE_BITMASK:
        // Bitmask values often represent flags or states. Return as float or convert to binary.
        return static_cast<float>(raw_value);
    case LUX_REG_TYPE_TIME_MINUTES:
        // Convert minutes to hours for a float sensor
        return static_cast<float>(raw_value) / 60.0f;
    default:
      ESP_LOGW(TAG, "Unknown register type %u. Returning NAN.", reg_type);
      return NAN;
  }
}

} // namespace luxpower_sna
} // namespace esphome