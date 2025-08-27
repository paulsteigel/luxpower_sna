// luxpower_sna.cpp
#include "luxpower_sna.h"
//#include "esphome/core/hal.h"

namespace esphome {
namespace luxpower_sna {

const char *LuxpowerSNAComponent::STATUS_TEXTS[193] = {
  "Standby", "Error", "Inverting", "", "Solar > Load - Surplus > Grid", "Float", "", "Charger Off", "Supporting", "Selling", "Pass Through", "Offsetting", "Solar > Battery Charging", "", "", "",
  "Battery Discharging > LOAD - Surplus > Grid", "Temperature Over Range", "", "", "Solar + Battery Discharging > LOAD - Surplus > Grid", "", "", "", "", "", "", "", "AC Battery Charging", "", "", "", "", "", "Solar + Grid > Battery Charging",
  "", "", "", "", "", "", "", "", "", "No Grid : Battery > EPS", "", "", "", "", "", "", "", "", "No Grid : Solar > EPS - Surplus > Battery Charging", "", "", "", "", "No Grid : Solar + Battery Discharging > EPS"
};

const char *LuxpowerSNAComponent::BATTERY_STATUS_TEXTS[17] = {
  "Charge Forbidden & Discharge Forbidden", "Unknown", "Charge Forbidden & Discharge Allowed", "Charge Allowed & Discharge Allowed",
  "", "", "", "", "", "", "", "", "", "", "", "", "Charge Allowed & Discharge Forbidden"
};

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Luxpower SNA component...");
  
  // Initialize state
  connection_state_ = ConnectionState::DISCONNECTED;
  bank_state_ = DataBankState::IDLE;
  current_bank_index_ = 0;
  last_connection_attempt_ = 0;
  state_start_time_ = 0;
  initialization_complete_ = false;
  expected_response_size_ = 0;
  
  // Reserve buffer space
  response_buffer_.reserve(512);

#ifdef USE_ESP_IDF
  socket_fd_ = -1;
  memset(&server_addr_, 0, sizeof(server_addr_));
#endif
  
  // Validate that required input components are configured
  if (host_input_ == nullptr) {
    ESP_LOGE(TAG, "Host input not configured - component will not function");
    mark_failed();
    return;
  }
  if (port_input_ == nullptr) {
    ESP_LOGE(TAG, "Port input not configured - component will not function");
    mark_failed();
    return;
  }
  if (dongle_serial_input_ == nullptr) {
    ESP_LOGE(TAG, "Dongle serial input not configured - component will not function");
    mark_failed();
    return;
  }
  if (inverter_serial_input_ == nullptr) {
    ESP_LOGE(TAG, "Inverter serial input not configured - component will not function");
    mark_failed();
    return;
  }
  
  ESP_LOGCONFIG(TAG, "Component initialized successfully");
  initialization_complete_ = true;
}

bool LuxpowerSNAComponent::validate_runtime_parameters_() {
  bool valid = true;
  
  // Get current values from template inputs
  std::string host = get_host_from_input_();
  uint16_t port = get_port_from_input_();
  std::string dongle_serial = get_dongle_serial_from_input_();
  std::string inverter_serial = get_inverter_serial_from_input_();
  
  if (host.empty()) {
    ESP_LOGV(TAG, "Host input is empty");
    valid = false;
  }
  
  if (port == 0) {
    ESP_LOGV(TAG, "Port input is 0");
    valid = false;
  }
  
  if (dongle_serial.empty()) {
    ESP_LOGV(TAG, "Dongle serial input is empty");
    valid = false;
  } else if (dongle_serial.length() != 10) {
    ESP_LOGW(TAG, "Dongle serial must be exactly 10 characters, got %zu: '%s'", 
             dongle_serial.length(), dongle_serial.c_str());
    valid = false;
  }
  
  if (inverter_serial.empty()) {
    ESP_LOGV(TAG, "Inverter serial input is empty");
    valid = false;
  } else if (inverter_serial.length() != 10) {
    ESP_LOGW(TAG, "Inverter serial must be exactly 10 characters, got %zu: '%s'", 
             inverter_serial.length(), inverter_serial.c_str());
    valid = false;
  }
  
  return valid;
}

std::string LuxpowerSNAComponent::get_host_from_input_() {
  if (host_input_ != nullptr && host_input_->has_state()) {
    return host_input_->state;
  }
  return "";
}

uint16_t LuxpowerSNAComponent::get_port_from_input_() {
  if (port_input_ != nullptr && port_input_->has_state()) {
    float port_val = port_input_->state;
    if (port_val > 0 && port_val <= 65535) {
      return static_cast<uint16_t>(port_val);
    }
  }
  return 0;
}

std::string LuxpowerSNAComponent::get_dongle_serial_from_input_() {
  if (dongle_serial_input_ != nullptr && dongle_serial_input_->has_state()) {
    return dongle_serial_input_->state;
  }
  return "";
}

std::string LuxpowerSNAComponent::get_inverter_serial_from_input_() {
  if (inverter_serial_input_ != nullptr && inverter_serial_input_->has_state()) {
    return inverter_serial_input_->state;
  }
  return "";
}

void LuxpowerSNAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Luxpower SNA:");
  
  if (!initialization_complete_) {
    ESP_LOGCONFIG(TAG, "  Status: FAILED - Component not initialized");
    return;
  }
  
  // Show current input values
  std::string host = get_host_from_input_();
  uint16_t port = get_port_from_input_();
  std::string dongle_serial = get_dongle_serial_from_input_();
  std::string inverter_serial = get_inverter_serial_from_input_();
  
  ESP_LOGCONFIG(TAG, "  Host: %s", host.empty() ? "Not set" : host.c_str());
  ESP_LOGCONFIG(TAG, "  Port: %u", port);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", dongle_serial.empty() ? "Not set" : dongle_serial.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter Serial: %s", inverter_serial.empty() ? "Not set" : inverter_serial.c_str());
  ESP_LOGCONFIG(TAG, "  Update Interval: %.1fs", this->get_update_interval() / 1000.0f);
  ESP_LOGCONFIG(TAG, "  Current State: %s", get_state_name_(connection_state_));
#ifdef USE_ESP_IDF
  ESP_LOGCONFIG(TAG, "  Framework: ESP-IDF");
#else
  ESP_LOGCONFIG(TAG, "  Framework: Arduino");
#endif
}

void LuxpowerSNAComponent::update() {
  if (!initialization_complete_) {
    ESP_LOGW(TAG, "Skipping update - component not properly initialized");
    return;
  }
  
  // Only trigger new data collection if we're disconnected
  if (connection_state_ == ConnectionState::DISCONNECTED) {
    // Validate runtime parameters from template inputs
    if (!validate_runtime_parameters_()) {
      ESP_LOGV(TAG, "Skipping update - input parameters not ready or invalid");
      return;
    }
    
    ESP_LOGD(TAG, "Starting new data collection cycle");
    connection_state_ = ConnectionState::DISCONNECTED; // Will be handled in loop()
  } else {
    ESP_LOGV(TAG, "Data collection already in progress, state: %s", get_state_name_(connection_state_));
  }
}

void LuxpowerSNAComponent::loop() {
  if (!initialization_complete_) {
    return;
  }
  
  // Non-blocking state machine - runs every loop iteration
  switch (connection_state_) {
    case ConnectionState::DISCONNECTED:
      handle_disconnected_state_();
      break;
      
    case ConnectionState::CONNECTING:
      handle_connecting_state_();
      break;
      
    case ConnectionState::CONNECTED:
      handle_connected_state_();
      break;
      
    case ConnectionState::REQUESTING_DATA:
      handle_requesting_data_state_();
      break;
      
    case ConnectionState::WAITING_RESPONSE:
      handle_waiting_response_state_();
      break;
      
    case ConnectionState::PROCESSING_RESPONSE:
      handle_processing_response_state_();
      break;
      
    case ConnectionState::ERROR:
      handle_error_state_();
      break;
  }

  // ADD THIS: Process async requests when connected and not busy with main data collection
  if (is_connection_ready() && !async_requests_.empty() && 
      (connection_state_ == ConnectionState::CONNECTED || connection_state_ == ConnectionState::DISCONNECTED)) {
    process_async_requests_();
  }
}

void LuxpowerSNAComponent::handle_disconnected_state_() {
  uint32_t now = millis();
  
  // Prevent too frequent connection attempts
  if (now - last_connection_attempt_ < 5000) {
    return;
  }
  
  if (!validate_runtime_parameters_()) {
    return;
  }
  
  if (start_connection_attempt_()) {
    connection_state_ = ConnectionState::CONNECTING;
    state_start_time_ = now;
    last_connection_attempt_ = now;
  }
}

void LuxpowerSNAComponent::handle_connecting_state_() {
  uint32_t now = millis();
  
  if (check_connection_ready_()) {
    std::string host = get_host_from_input_();
    uint16_t port = get_port_from_input_();
    ESP_LOGI(TAG, "Successfully connected to %s:%u", host.c_str(), port);
    connection_state_ = ConnectionState::CONNECTED;
    current_bank_index_ = 0;
    return;
  }
  
  // Check for timeout
  if (now - state_start_time_ > 10000) {
    ESP_LOGW(TAG, "Connection timeout after 10 seconds");
    connection_state_ = ConnectionState::ERROR;
  }
}

void LuxpowerSNAComponent::handle_connected_state_() {
#ifdef USE_ESP_IDF
  if (socket_fd_ < 0) {
#else
  if (!client_.connected()) {
#endif
    ESP_LOGW(TAG, "Connection lost");
    connection_state_ = ConnectionState::ERROR;
    return;
  }
  
  // Start data collection
  connection_state_ = ConnectionState::REQUESTING_DATA;
  bank_state_ = DataBankState::IDLE;
  current_bank_index_ = 0;
}

void LuxpowerSNAComponent::handle_requesting_data_state_() {
#ifdef USE_ESP_IDF
  if (socket_fd_ < 0) {
#else
  if (!client_.connected()) {
#endif
    ESP_LOGW(TAG, "Connection lost during data request");
    connection_state_ = ConnectionState::ERROR;
    return;
  }
  
  if (current_bank_index_ >= 5) {
    // All banks processed successfully
    ESP_LOGI(TAG, "All data banks processed successfully");
    disconnect_client_();
    connection_state_ = ConnectionState::DISCONNECTED;
    current_bank_index_ = 0;
    return;
  }
  
  if (send_bank_request_()) {
    connection_state_ = ConnectionState::WAITING_RESPONSE;
    state_start_time_ = millis();
    response_buffer_.clear();
    expected_response_size_ = 200; // Approximate expected size
  } else {
    ESP_LOGW(TAG, "Failed to send bank request");
    connection_state_ = ConnectionState::ERROR;
  }
}

void LuxpowerSNAComponent::handle_waiting_response_state_() {
#ifdef USE_ESP_IDF
  if (socket_fd_ < 0) {
#else
  if (!client_.connected()) {
#endif
    ESP_LOGW(TAG, "Connection lost while waiting for response");
    connection_state_ = ConnectionState::ERROR;
    return;
  }
  
  // Check for timeout
  uint32_t now = millis();
  if (now - state_start_time_ > 5000) {
    ESP_LOGW(TAG, "Response timeout for bank %d", banks_[current_bank_index_]);
    connection_state_ = ConnectionState::ERROR;
    return;
  }
  
  // Try to read available data
  if (read_available_data_()) {
    // Check if we have enough data to process
    if (response_buffer_.size() >= sizeof(LuxHeader) + sizeof(LuxTranslatedData) + 2) {
      connection_state_ = ConnectionState::PROCESSING_RESPONSE;
    }
  }
}

void LuxpowerSNAComponent::handle_processing_response_state_() {
  if (process_received_data_()) {
    ESP_LOGV(TAG, "Successfully processed bank %d (%d/5)", 
             banks_[current_bank_index_], current_bank_index_ + 1);
    current_bank_index_++;
    connection_state_ = ConnectionState::REQUESTING_DATA;
  } else {
    ESP_LOGW(TAG, "Failed to process response for bank %d", banks_[current_bank_index_]);
    connection_state_ = ConnectionState::ERROR;
  }
}

void LuxpowerSNAComponent::handle_error_state_() {
  ESP_LOGV(TAG, "Handling error recovery");
  
  disconnect_client_();
  response_buffer_.clear();
  connection_state_ = ConnectionState::DISCONNECTED;
  current_bank_index_ = 0;
  bank_state_ = DataBankState::IDLE;
}

bool LuxpowerSNAComponent::start_connection_attempt_() {
  std::string host = get_host_from_input_();
  uint16_t port = get_port_from_input_();
  
  ESP_LOGD(TAG, "Attempting to connect to %s:%u", host.c_str(), port);

#ifdef USE_ESP_IDF
  // ESP-IDF socket implementation
  if (socket_fd_ >= 0) {
    close(socket_fd_);
  }

  socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd_ < 0) {
    ESP_LOGE(TAG, "Failed to create socket: %d", errno);
    return false;
  }

  // Set socket to non-blocking for connection
  int flags = fcntl(socket_fd_, F_GETFL, 0);
  fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);

  // Setup server address
  server_addr_.sin_family = AF_INET;
  server_addr_.sin_port = htons(port);
  
  if (inet_pton(AF_INET, host.c_str(), &server_addr_.sin_addr) <= 0) {
    ESP_LOGE(TAG, "Invalid IP address: %s", host.c_str());
    close(socket_fd_);
    socket_fd_ = -1;
    return false;
  }

  // Start non-blocking connection
  int result = connect(socket_fd_, (struct sockaddr*)&server_addr_, sizeof(server_addr_));
  if (result == 0) {
    // Connected immediately (unlikely but possible)
    return true;
  } else if (errno == EINPROGRESS) {
    // Connection in progress - this is expected for non-blocking
    return true;
  } else {
    ESP_LOGE(TAG, "Failed to start connection: %d", errno);
    close(socket_fd_);
    socket_fd_ = -1;
    return false;
  }

#else
  // Arduino WiFiClient implementation
  return client_.connect(host.c_str(), port);
#endif
}

bool LuxpowerSNAComponent::check_connection_ready_() {
#ifdef USE_ESP_IDF
  if (socket_fd_ < 0) {
    return false;
  }

  // Check if connection is ready using select
  fd_set write_fds;
  fd_set error_fds;
  struct timeval timeout = {0, 0}; // Non-blocking
  
  FD_ZERO(&write_fds);
  FD_ZERO(&error_fds);
  FD_SET(socket_fd_, &write_fds);
  FD_SET(socket_fd_, &error_fds);
  
  int result = select(socket_fd_ + 1, nullptr, &write_fds, &error_fds, &timeout);
  
  if (result > 0) {
    if (FD_ISSET(socket_fd_, &error_fds)) {
      // Connection failed
      int error;
      socklen_t len = sizeof(error);
      getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, &error, &len);
      ESP_LOGE(TAG, "Connection failed with error: %d", error);
      close(socket_fd_);
      socket_fd_ = -1;
      return false;
    }
    
    if (FD_ISSET(socket_fd_, &write_fds)) {
      // Connection successful, set socket back to blocking
      int flags = fcntl(socket_fd_, F_GETFL, 0);
      fcntl(socket_fd_, F_SETFL, flags & ~O_NONBLOCK);
      
      // Set socket timeouts
      struct timeval tv;
      tv.tv_sec = 10;
      tv.tv_usec = 0;
      setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
      setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
      
      return true;
    }
  }
  
  return false; // Still connecting

#else
  return client_.connected();
#endif
}

void LuxpowerSNAComponent::disconnect_client_() {
#ifdef USE_ESP_IDF
  if (socket_fd_ >= 0) {
    close(socket_fd_);
    socket_fd_ = -1;
  }
#else
  if (client_.connected()) {
    client_.stop();
  }
#endif
}

bool LuxpowerSNAComponent::send_bank_request_() {
  uint8_t bank = banks_[current_bank_index_];
  
  // Get current serial numbers from template inputs
  std::string dongle_serial = get_dongle_serial_from_input_();
  std::string inverter_serial = get_inverter_serial_from_input_();
  
  uint8_t pkt[38] = {
    0xA1, 0x1A, 0x02, 0x00, 0x20, 0x00, 0x01, 0xC2,
    0,0,0,0,0,0,0,0,0,0, 0x12, 0x00, 0x00, 0x04,
    0,0,0,0,0,0,0,0,0,0, static_cast<uint8_t>(bank), 0x00, 0x28, 0x00, 0x00, 0x00
  };
  
  // Copy serial numbers (ensure they're exactly 10 chars)
  memset(pkt + 8, 0, 10);
  memset(pkt + 22, 0, 10);
  memcpy(pkt + 8, dongle_serial.c_str(), std::min(dongle_serial.length(), size_t(10)));
  memcpy(pkt + 22, inverter_serial.c_str(), std::min(inverter_serial.length(), size_t(10)));
  
  uint16_t crc = calculate_crc_(pkt + 20, 16);
  pkt[36] = crc & 0xFF;
  pkt[37] = crc >> 8;

  ESP_LOGV(TAG, "Sending request for bank %d with dongle: %s, inverter: %s", 
           bank, dongle_serial.c_str(), inverter_serial.c_str());

#ifdef USE_ESP_IDF
  if (socket_fd_ < 0) {
    return false;
  }
  
  ssize_t written = send(socket_fd_, pkt, sizeof(pkt), 0);
  return written == sizeof(pkt);

#else
  size_t written = client_.write(pkt, sizeof(pkt));
  client_.flush();
  return written == sizeof(pkt);
#endif
}

bool LuxpowerSNAComponent::read_available_data_() {
  bool data_received = false;

#ifdef USE_ESP_IDF
  if (socket_fd_ < 0) {
    return false;
  }
  
  uint8_t buffer[256];
  ssize_t received = recv(socket_fd_, buffer, sizeof(buffer), MSG_DONTWAIT);
  
  if (received > 0) {
    response_buffer_.insert(response_buffer_.end(), buffer, buffer + received);
    data_received = true;
  } else if (received == 0) {
    ESP_LOGW(TAG, "Connection closed by peer");
    return false;
  } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
    ESP_LOGE(TAG, "Receive error: %d", errno);
    return false;
  }

#else
  while (client_.available()) {
    uint8_t byte = client_.read();
    response_buffer_.push_back(byte);
    data_received = true;
    
    // Prevent buffer overflow
    if (response_buffer_.size() > 1024) {
      ESP_LOGW(TAG, "Response buffer overflow, resetting");
      response_buffer_.clear();
      return false;
    }
  }
#endif

  return data_received;
}

bool LuxpowerSNAComponent::process_received_data_() {
  if (response_buffer_.size() < sizeof(LuxHeader)) {
    ESP_LOGW(TAG, "Response too short: %zu bytes", response_buffer_.size());
    return false;
  }
  
  LuxHeader *header = reinterpret_cast<LuxHeader *>(response_buffer_.data());
  if (header->prefix != 0x1AA1) {
    ESP_LOGW(TAG, "Invalid header prefix: 0x%04X", header->prefix);
    return false;
  }

  if (response_buffer_.size() < sizeof(LuxHeader) + sizeof(LuxTranslatedData)) {
    ESP_LOGW(TAG, "Response missing translated data section");
    return false;
  }

  LuxTranslatedData *trans = reinterpret_cast<LuxTranslatedData *>(response_buffer_.data() + sizeof(LuxHeader));
  if (trans->deviceFunction != 0x04) {
    ESP_LOGW(TAG, "Invalid device function: 0x%02X", trans->deviceFunction);
    return false;
  }

  size_t total_size = response_buffer_.size();
  uint16_t crc_calc = calculate_crc_(response_buffer_.data() + sizeof(LuxHeader), total_size - sizeof(LuxHeader) - 2);
  uint16_t crc_received = response_buffer_[total_size - 2] | (response_buffer_[total_size - 1] << 8);
  if (crc_calc != crc_received) {
    ESP_LOGW(TAG, "CRC mismatch: calc=0x%04X, recv=0x%04X", crc_calc, crc_received);
    return false;
  }

  size_t data_offset = sizeof(LuxHeader) + sizeof(LuxTranslatedData);
  size_t data_size = total_size - data_offset - 2;
  uint8_t bank = banks_[current_bank_index_];
  
  switch (bank) {
    case 0: 
      if (data_size >= sizeof(LuxLogDataRawSection1)) {
        process_section1_(*reinterpret_cast<LuxLogDataRawSection1 *>(response_buffer_.data() + data_offset));
      } else {
        ESP_LOGW(TAG, "Insufficient data for section 1: %zu < %zu", data_size, sizeof(LuxLogDataRawSection1));
        return false;
      }
      break;
    case 40: 
      if (data_size >= sizeof(LuxLogDataRawSection2)) {
        process_section2_(*reinterpret_cast<LuxLogDataRawSection2 *>(response_buffer_.data() + data_offset));
      } else {
        ESP_LOGW(TAG, "Insufficient data for section 2: %zu < %zu", data_size, sizeof(LuxLogDataRawSection2));
        return false;
      }
      break;
    case 80: 
      if (data_size >= sizeof(LuxLogDataRawSection3)) {
        process_section3_(*reinterpret_cast<LuxLogDataRawSection3 *>(response_buffer_.data() + data_offset));
      } else {
        ESP_LOGW(TAG, "Insufficient data for section 3: %zu < %zu", data_size, sizeof(LuxLogDataRawSection3));
        return false;
      }
      break;
    case 120: 
      if (data_size >= sizeof(LuxLogDataRawSection4)) {
        process_section4_(*reinterpret_cast<LuxLogDataRawSection4 *>(response_buffer_.data() + data_offset));
      } else {
        ESP_LOGW(TAG, "Insufficient data for section 4: %zu < %zu", data_size, sizeof(LuxLogDataRawSection4));
        return false;
      }
      break;
    case 160: 
      if (data_size >= sizeof(LuxLogDataRawSection5)) {
        process_section5_(*reinterpret_cast<LuxLogDataRawSection5 *>(response_buffer_.data() + data_offset));
      } else {
        ESP_LOGW(TAG, "Insufficient data for section 5: %zu < %zu", data_size, sizeof(LuxLogDataRawSection5));
        return false;
      }
      break;
    default: 
      ESP_LOGW(TAG, "Unknown bank: %d", bank);
      return false;
  }
  
  return true;
}

const char* LuxpowerSNAComponent::get_state_name_(ConnectionState state) {
  switch (state) {
    case ConnectionState::DISCONNECTED: return "DISCONNECTED";
    case ConnectionState::CONNECTING: return "CONNECTING";
    case ConnectionState::CONNECTED: return "CONNECTED";
    case ConnectionState::REQUESTING_DATA: return "REQUESTING_DATA";
    case ConnectionState::WAITING_RESPONSE: return "WAITING_RESPONSE";
    case ConnectionState::PROCESSING_RESPONSE: return "PROCESSING_RESPONSE";
    case ConnectionState::ERROR: return "ERROR";
    default: return "UNKNOWN";
  }
}

uint16_t LuxpowerSNAComponent::calculate_crc_(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; ++i)
      crc = (crc >> 1) ^ (crc & 1 ? 0xA001 : 0);
  }
  return crc;
}

void LuxpowerSNAComponent::publish_sensor_(sensor::Sensor *sensor, float value) {
  if (sensor != nullptr) sensor->publish_state(value);
}

void LuxpowerSNAComponent::publish_text_sensor_(text_sensor::TextSensor *sensor, const std::string &value) {
  if (sensor != nullptr) sensor->publish_state(value);
}

// Process_sectionX_ methods for publishing data to sensors 
void LuxpowerSNAComponent::process_section1_(const LuxLogDataRawSection1 &data) {
  publish_sensor_(lux_current_solar_voltage_1_sensor_, data.v_pv_1 / 10.0f);
  publish_sensor_(lux_current_solar_voltage_2_sensor_, data.v_pv_2 / 10.0f);
  publish_sensor_(lux_current_solar_voltage_3_sensor_, data.v_pv_3 / 10.0f);
  publish_sensor_(lux_battery_voltage_sensor_, data.v_bat / 10.0f);
  publish_sensor_(lux_battery_percent_sensor_, data.soc);
  publish_sensor_(soh_sensor_, data.soh);
  publish_sensor_(lux_internal_fault_sensor_, data.internal_fault);
  publish_sensor_(lux_current_solar_output_1_sensor_, data.p_pv_1);
  publish_sensor_(lux_current_solar_output_2_sensor_, data.p_pv_2);
  publish_sensor_(lux_current_solar_output_3_sensor_, data.p_pv_3);
  publish_sensor_(lux_battery_charge_sensor_, data.p_charge);
  publish_sensor_(lux_battery_discharge_sensor_, data.p_discharge);
  publish_sensor_(lux_grid_voltage_r_sensor_, data.v_ac_r / 10.0f);
  publish_sensor_(lux_grid_voltage_s_sensor_, data.v_ac_s / 10.0f);
  publish_sensor_(lux_grid_voltage_t_sensor_, data.v_ac_t / 10.0f);
  publish_sensor_(lux_grid_frequency_live_sensor_, data.f_ac / 100.0f);
  publish_sensor_(lux_power_from_inverter_live_sensor_, data.p_inv);
  publish_sensor_(lux_power_to_inverter_live_sensor_, data.p_rec);
  publish_sensor_(lux_power_current_clamp_sensor_, data.rms_current / 100.0f);
  publish_sensor_(grid_power_factor_sensor_, data.pf / 1000.0f);
  publish_sensor_(eps_voltage_r_sensor_, data.v_eps_r / 10.0f);
  publish_sensor_(eps_voltage_s_sensor_, data.v_eps_s / 10.0f);
  publish_sensor_(eps_voltage_t_sensor_, data.v_eps_t / 10.0f);
  publish_sensor_(eps_frequency_sensor_, data.f_eps / 100.0f);
  publish_sensor_(lux_power_to_eps_sensor_, data.p_to_eps);
  publish_sensor_(lux_power_to_grid_live_sensor_, data.p_to_grid);
  publish_sensor_(lux_power_from_grid_live_sensor_, data.p_to_user);
  publish_sensor_(lux_daily_solar_array_1_sensor_, data.e_pv_1_day / 10.0f);
  publish_sensor_(lux_daily_solar_array_2_sensor_, data.e_pv_2_day / 10.0f);
  publish_sensor_(lux_daily_solar_array_3_sensor_, data.e_pv_3_day / 10.0f);
  publish_sensor_(lux_power_from_inverter_daily_sensor_, data.e_inv_day / 10.0f);
  publish_sensor_(lux_power_to_inverter_daily_sensor_, data.e_rec_day / 10.0f);
  publish_sensor_(lux_daily_battery_charge_sensor_, data.e_chg_day / 10.0f);
  publish_sensor_(lux_daily_battery_discharge_sensor_, data.e_dischg_day / 10.0f);
  publish_sensor_(lux_power_to_eps_daily_sensor_, data.e_eps_day / 10.0f);
  publish_sensor_(lux_power_to_grid_daily_sensor_, data.e_to_grid_day / 10.0f);
  publish_sensor_(lux_power_from_grid_daily_sensor_, data.e_to_user_day / 10.0f);
  publish_sensor_(bus1_voltage_sensor_, data.v_bus_1 / 10.0f);
  publish_sensor_(bus2_voltage_sensor_, data.v_bus_2 / 10.0f);
    
  float lux_grid_voltage_live = data.v_ac_r / 10.0f;
  int16_t lux_current_solar_output = data.p_pv_1 + data.p_pv_2 + data.p_pv_3;
  float lux_daily_solar = (data.e_pv_1_day + data.e_pv_2_day + data.e_pv_3_day) / 10.0f;
  int16_t lux_power_to_home = data.p_to_user - data.p_rec;
  float lux_battery_flow = (data.p_discharge > 0) ? -data.p_discharge : data.p_charge;
  float lux_grid_flow = (data.p_to_user > 0) ? -data.p_to_user : data.p_to_grid;
  float lux_home_consumption_live = data.p_to_user - data.p_rec + data.p_inv - data.p_to_grid;
  float lux_home_consumption = (data.e_to_user_day - data.e_rec_day + data.e_inv_day - data.e_to_grid_day) / 10.0f;

  publish_sensor_(lux_grid_voltage_live_sensor_, lux_grid_voltage_live);
  publish_sensor_(lux_current_solar_output_sensor_, lux_current_solar_output);
  publish_sensor_(lux_daily_solar_sensor_, lux_daily_solar);
  publish_sensor_(lux_power_to_home_sensor_, lux_power_to_home);
  publish_sensor_(lux_battery_flow_sensor_, lux_battery_flow);
  publish_sensor_(lux_grid_flow_sensor_, lux_grid_flow);
  publish_sensor_(lux_home_consumption_live_sensor_, lux_home_consumption_live);
  publish_sensor_(lux_home_consumption_sensor_, lux_home_consumption);

  if (data.status < 193 && STATUS_TEXTS[data.status] != nullptr && strlen(STATUS_TEXTS[data.status]) > 0) {
    publish_text_sensor_(lux_status_text_sensor_, STATUS_TEXTS[data.status]);
  } else {
    publish_text_sensor_(lux_status_text_sensor_, "Unknown Status");
  }
}

void LuxpowerSNAComponent::process_section2_(const LuxLogDataRawSection2 &data) {
  publish_sensor_(lux_total_solar_array_1_sensor_, data.e_pv_1_all / 10.0f);
  publish_sensor_(lux_total_solar_array_2_sensor_, data.e_pv_2_all / 10.0f);
  publish_sensor_(lux_total_solar_array_3_sensor_, data.e_pv_3_all / 10.0f);
  publish_sensor_(lux_power_from_inverter_total_sensor_, data.e_inv_all / 10.0f);
  publish_sensor_(lux_power_to_inverter_total_sensor_, data.e_rec_all / 10.0f);
  publish_sensor_(lux_total_battery_charge_sensor_, data.e_chg_all / 10.0f);
  publish_sensor_(lux_total_battery_discharge_sensor_, data.e_dischg_all / 10.0f);
  publish_sensor_(lux_power_to_eps_total_sensor_, data.e_eps_all / 10.0f);
  publish_sensor_(lux_power_to_grid_total_sensor_, data.e_to_grid_all / 10.0f);
  publish_sensor_(lux_power_from_grid_total_sensor_, data.e_to_user_all / 10.0f);
  publish_sensor_(lux_fault_code_sensor_, data.fault_code);
  publish_sensor_(lux_warning_code_sensor_, data.warning_code);
  publish_sensor_(lux_internal_temp_sensor_, data.t_inner / 10.0f);
  publish_sensor_(lux_radiator1_temp_sensor_, data.t_rad_1);
  publish_sensor_(lux_radiator2_temp_sensor_, data.t_rad_2);
  publish_sensor_(lux_battery_temperature_live_sensor_, data.t_bat / 10.0f);
  publish_sensor_(lux_uptime_sensor_, data.uptime);

  float lux_total_solar = (data.e_pv_1_all + data.e_pv_2_all + data.e_pv_3_all) / 10.0f;
  float lux_home_consumption_total = (data.e_to_user_all - data.e_rec_all + data.e_inv_all - data.e_to_grid_all) / 10.0f;
  
  publish_sensor_(lux_total_solar_sensor_, lux_total_solar);
  publish_sensor_(lux_home_consumption_total_sensor_, lux_home_consumption_total);
}

void LuxpowerSNAComponent::process_section3_(const LuxLogDataRawSection3 &data) {
  int16_t raw_current = data.bat_current;
  if (raw_current & 0x8000) raw_current -= 0x10000;
  
  publish_sensor_(lux_bms_limit_charge_sensor_, data.max_chg_curr / 10.0f);
  publish_sensor_(lux_bms_limit_discharge_sensor_, data.max_dischg_curr / 10.0f);
  publish_sensor_(charge_voltage_ref_sensor_, data.charge_volt_ref / 10.0f);
  publish_sensor_(discharge_cutoff_voltage_sensor_, data.dischg_cut_volt / 10.0f);
  publish_sensor_(battery_status_inv_sensor_, data.bat_status_inv);
  publish_sensor_(lux_battery_count_sensor_, data.bat_count);
  publish_sensor_(lux_battery_capacity_ah_sensor_, data.bat_capacity);
  publish_sensor_(lux_battery_current_sensor_, raw_current / 10.0f);
  publish_sensor_(max_cell_volt_sensor_, data.max_cell_volt / 1000.0f);
  publish_sensor_(min_cell_volt_sensor_, data.min_cell_volt / 1000.0f);
  
  int16_t raw_max_temp = data.max_cell_temp;
  int16_t raw_min_temp = data.min_cell_temp;
  if (raw_max_temp & 0x8000) raw_max_temp -= 0x10000;
  if (raw_min_temp & 0x8000) raw_min_temp -= 0x10000;
  
  publish_sensor_(max_cell_temp_sensor_, raw_max_temp / 10.0f);
  publish_sensor_(min_cell_temp_sensor_, raw_min_temp / 10.0f);
  
  publish_sensor_(lux_battery_cycle_count_sensor_, data.bat_cycle_count);
  publish_sensor_(lux_home_consumption_2_live_sensor_, data.p_load2);
  
  if (data.bat_status_inv < 17 && BATTERY_STATUS_TEXTS[data.bat_status_inv] != nullptr && strlen(BATTERY_STATUS_TEXTS[data.bat_status_inv]) > 0) {
    publish_text_sensor_(lux_battery_status_text_sensor_, BATTERY_STATUS_TEXTS[data.bat_status_inv]);
  } else {
    publish_text_sensor_(lux_battery_status_text_sensor_, "Unknown Battery Status");
  }
}

void LuxpowerSNAComponent::process_section4_(const LuxLogDataRawSection4 &data) {
  publish_sensor_(lux_current_generator_voltage_sensor_, data.gen_input_volt / 10.0f);
  publish_sensor_(lux_current_generator_frequency_sensor_, data.gen_input_freq / 100.0f);
  
  int16_t gen_power = (data.gen_power_watt < 125) ? 0 : data.gen_power_watt;
  publish_sensor_(lux_current_generator_power_sensor_, gen_power);
  
  publish_sensor_(lux_current_generator_power_daily_sensor_, data.gen_power_day / 10.0f);
  publish_sensor_(lux_current_generator_power_all_sensor_, data.gen_power_all / 10.0f);
  publish_sensor_(lux_current_eps_L1_voltage_sensor_, data.eps_L1_volt / 10.0f);
  publish_sensor_(lux_current_eps_L2_voltage_sensor_, data.eps_L2_volt / 10.0f);
  publish_sensor_(lux_current_eps_L1_watt_sensor_, data.eps_L1_watt);
  publish_sensor_(lux_current_eps_L2_watt_sensor_, data.eps_L2_watt);
}

void LuxpowerSNAComponent::process_section5_(const LuxLogDataRawSection5 &data) {
  publish_sensor_(p_load_ongrid_sensor_, data.p_load_ongrid);
  publish_sensor_(e_load_day_sensor_, data.e_load_day / 10.0f);
  publish_sensor_(e_load_all_l_sensor_, data.e_load_all_l / 10.0f);
}

// ===============================
// NEW METHODS FOR SWITCH SUPPORT
// ===============================

bool LuxpowerSNAComponent::is_connection_ready() const {
#ifdef USE_ESP_IDF
  return socket_fd_ >= 0;
#else
  return client_.connected();
#endif
}

void LuxpowerSNAComponent::read_register_async(uint16_t reg, std::function<void(uint16_t)> callback) {
  // Check cache first
  auto it = register_cache_.find(reg);
  if (it != register_cache_.end()) {
    ESP_LOGD(TAG, "Returning cached register %d: 0x%04X", reg, it->second);
    if (callback) callback(it->second);
    return;
  }
  
  // Queue async read request
  AsyncRequest req;
  req.type = AsyncRequest::READ;
  req.reg = reg;
  req.read_callback = callback;
  async_requests_.push_back(req);
  
  ESP_LOGD(TAG, "Queued async read for register %d", reg);
}

void LuxpowerSNAComponent::write_register_async(uint16_t reg, uint16_t value, std::function<void(bool)> callback) {
  // Queue async write request
  AsyncRequest req;
  req.type = AsyncRequest::WRITE;
  req.reg = reg;
  req.value = value;
  req.write_callback = callback;
  async_requests_.push_back(req);
  
  ESP_LOGD(TAG, "Queued async write for register %d: 0x%04X", reg, value);
}

void LuxpowerSNAComponent::process_async_requests_() {
  if (async_requests_.empty()) return;
  
  // Process one request at a time
  auto req = async_requests_.front();
  async_requests_.erase(async_requests_.begin());
  
  if (req.type == AsyncRequest::READ) {
    ESP_LOGD(TAG, "Processing async read for register %d", req.reg);
    
    auto packet = prepare_single_register_read_packet_(req.reg);
    if (send_packet_(packet)) {
      // For now, simulate reading from cache or return 0
      uint16_t value = 0;
      auto it = register_cache_.find(req.reg);
      if (it != register_cache_.end()) {
        value = it->second;
      }
      if (req.read_callback) {
        req.read_callback(value);
      }
    } else {
      ESP_LOGW(TAG, "Failed to send read packet for register %d", req.reg);
      if (req.read_callback) {
        req.read_callback(0);
      }
    }
    
  } else if (req.type == AsyncRequest::WRITE) {
    ESP_LOGD(TAG, "Processing async write for register %d: 0x%04X", req.reg, req.value);
    
    auto packet = prepare_single_register_write_packet_(req.reg, req.value);
    bool success = send_packet_(packet);
    
    if (success) {
      // Update cache
      register_cache_[req.reg] = req.value;
    }
    
    if (req.write_callback) {
      req.write_callback(success);
    }
  }
}

std::vector<uint8_t> LuxpowerSNAComponent::prepare_single_register_read_packet_(uint16_t reg) {
  std::string dongle_serial = get_dongle_serial_from_input_();
  std::string inverter_serial = get_inverter_serial_from_input_();
  
  std::vector<uint8_t> packet = {
    0xA1, 0x1A, 0x02, 0x00, 0x20, 0x00, 0x01, 0xC2,
    0,0,0,0,0,0,0,0,0,0, // dongle serial (10 bytes)
    0x12, 0x00, 0x00, 0x03, // function code for single register read
    0,0,0,0,0,0,0,0,0,0, // inverter serial (10 bytes)
    static_cast<uint8_t>(reg & 0xFF), static_cast<uint8_t>(reg >> 8), // register address
    0x01, 0x00, // read 1 register
    0x00, 0x00  // CRC placeholder
  };
  
  // Copy serial numbers
  memset(packet.data() + 8, 0, 10);
  memset(packet.data() + 22, 0, 10);
  memcpy(packet.data() + 8, dongle_serial.c_str(), std::min(dongle_serial.length(), size_t(10)));
  memcpy(packet.data() + 22, inverter_serial.c_str(), std::min(inverter_serial.length(), size_t(10)));
  
  // Calculate and set CRC
  uint16_t crc = calculate_crc_(packet.data() + 20, packet.size() - 20 - 2);
  packet[packet.size() - 2] = crc & 0xFF;
  packet[packet.size() - 1] = crc >> 8;
  
  return packet;
}

std::vector<uint8_t> LuxpowerSNAComponent::prepare_single_register_write_packet_(uint16_t reg, uint16_t value) {
  std::string dongle_serial = get_dongle_serial_from_input_();
  std::string inverter_serial = get_inverter_serial_from_input_();
  
  std::vector<uint8_t> packet = {
    0xA1, 0x1A, 0x02, 0x00, 0x22, 0x00, 0x01, 0xC2,
    0,0,0,0,0,0,0,0,0,0, // dongle serial (10 bytes)
    0x14, 0x00, 0x00, 0x06, // function code for single register write
    0,0,0,0,0,0,0,0,0,0, // inverter serial (10 bytes)
    static_cast<uint8_t>(reg & 0xFF), static_cast<uint8_t>(reg >> 8), // register address
    static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8), // value to write
    0x00, 0x00  // CRC placeholder
  };
  
  // Copy serial numbers
  memset(packet.data() + 8, 0, 10);
  memset(packet.data() + 22, 0, 10);
  memcpy(packet.data() + 8, dongle_serial.c_str(), std::min(dongle_serial.length(), size_t(10)));
  memcpy(packet.data() + 22, inverter_serial.c_str(), std::min(inverter_serial.length(), size_t(10)));
  
  // Calculate and set CRC
  uint16_t crc = calculate_crc_(packet.data() + 20, packet.size() - 20 - 2);
  packet[packet.size() - 2] = crc & 0xFF;
  packet[packet.size() - 1] = crc >> 8;
  
  return packet;
}

bool LuxpowerSNAComponent::send_packet_(const std::vector<uint8_t> &packet) {
#ifdef USE_ESP_IDF
  if (socket_fd_ < 0) {
    return false;
  }
  
  ssize_t written = send(socket_fd_, packet.data(), packet.size(), 0);
  return written == static_cast<ssize_t>(packet.size());

#else
  size_t written = client_.write(packet.data(), packet.size());
  client_.flush();
  return written == packet.size();
#endif
}

}  // namespace luxpower_sna
}  // namespace esphome
