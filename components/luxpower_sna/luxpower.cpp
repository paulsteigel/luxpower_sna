#include "luxpower.h"
#include "esphome/core/log.h"
#include <string>

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

void LuxpowerSnaComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Luxpower SNA Component...");
  this->client_ = new AsyncClient();

  // Set up callbacks for the TCP client
  this->client_->onConnect([this](void *arg, AsyncClient *client) {
    ESP_LOGI(TAG, "Connected to inverter at %s:%d", this->host_.c_str(), this->port_);
    this->is_connected_ = true;
    this->current_request_bank_ = 0; // Reset request sequence on connect
    this->update(); // Start data request immediately
  });

  this->client_->onDisconnect([this](void *arg, AsyncClient *client) {
    ESP_LOGW(TAG, "Disconnected from inverter.");
    this->is_connected_ = false;
  });

  this->client_->onError([this](void *arg, AsyncClient *client, int8_t error) {
    ESP_LOGE(TAG, "Connection error: %s", client->errorToString(error));
    this->is_connected_ = false;
  });

  this->client_->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
    std::vector<uint8_t> received_data;
    received_data.assign((uint8_t *) data, (uint8_t *) data + len);
    
    ESP_LOGD(TAG, "Received %d bytes from inverter", len);
    this->parse_inverter_data(received_data);
  });
  
  // Initial connection attempt
  this->connect_to_inverter();
}

void LuxpowerSnaComponent::update() {
  if (!this->is_connected_) {
    ESP_LOGW(TAG, "Not connected, attempting to reconnect...");
    this->connect_to_inverter();
    return;
  }
  
  ESP_LOGD(TAG, "Requesting data bank %d", this->current_request_bank_);
  this->request_inverter_data(this->current_request_bank_);

  // Cycle through the first 3 banks of data registers (0, 1, 2)
  this->current_request_bank_ = (this->current_request_bank_ + 1) % 3;
}

void LuxpowerSnaComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Luxpower SNA Component:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%d", this->host_.c_str(), this->port_);
  // Log sensors
  for (auto const& [key, val] : this->sensors_) {
      LOG_SENSOR("  ", "Sensor", val);
  }
}

void LuxpowerSnaComponent::register_sensor(const std::string &type, sensor::Sensor *sens) {
    this->sensors_[type] = sens;
}

void LuxpowerSnaComponent::connect_to_inverter() {
  if (this->is_connected_ || this->client_->connected()) {
    return;
  }
  ESP_LOGD(TAG, "Attempting to connect to %s:%d", this->host_.c_str(), this->port_);
  this->client_->connect(this->host_.c_str(), this->port_);
}

void LuxpowerSnaComponent::request_inverter_data(int bank) {
  if (!this->is_connected_) return;
  
  uint16_t start_register = bank * 40;
  uint16_t num_registers = 40;
  
  auto packet = this->prepare_packet_for_read(start_register, num_registers, DEVICE_FUNCTION_READ_INPUT);
  
  if (this->client_->space() > packet.size() && this->client_->canSend()) {
    this->client_->write((const char*)packet.data(), packet.size());
    ESP_LOGD(TAG, "Sent data request for bank %d", bank);
  } else {
    ESP_LOGW(TAG, "Cannot send data request, client buffer full.");
  }
}

void LuxpowerSnaComponent::parse_inverter_data(const std::vector<uint8_t> &data) {
  // Basic validation from LXPPacket.py
  if (data.size() < 37) {
    ESP_LOGW(TAG, "Received packet is too small: %d bytes", data.size());
    return;
  }

  if (data[0] != 0xA1 || data[1] != 0x1A) {
    ESP_LOGW(TAG, "Invalid packet prefix");
    return;
  }
  
  uint16_t frame_length = get_16bit_unsigned(data, 4);
  if (data.size() != frame_length + 6) {
      ESP_LOGW(TAG, "Packet length mismatch. Expected %d, got %d", frame_length + 6, data.size());
      return;
  }

  uint8_t tcp_function = data[7];
  if (tcp_function != TCP_FUNCTION_TRANSLATED_DATA) {
    ESP_LOGD(TAG, "Ignoring non-data packet (function: %d)", tcp_function);
    return;
  }

  // Extract the data frame (inside the main packet)
  std::vector<uint8_t> data_frame;
  data_frame.assign(data.begin() + 20, data.end() - 2);

  // CRC Check
  uint16_t received_crc = get_16bit_unsigned(data, data.size() - 2);
  uint16_t calculated_crc = compute_crc(data_frame);

  if (received_crc != calculated_crc) {
    ESP_LOGW(TAG, "CRC mismatch. Received: %04X, Calculated: %04X", received_crc, calculated_crc);
    return;
  }

  uint8_t device_function = data_frame[1];
  if (device_function != DEVICE_FUNCTION_READ_INPUT) {
    ESP_LOGD(TAG, "Ignoring non-input-register packet (function: %d)", device_function);
    return;
  }

  uint16_t start_register = get_16bit_unsigned(data_frame, 12);
  uint16_t num_registers = data_frame[14] / 2;
  
  ESP_LOGD(TAG, "Parsing data for start register %d, count %d", start_register, num_registers);

  // This is where we extract values based on the register bank
  // This logic is a direct C++ translation of get_device_values_bankX from LXPPacket.py
  if (start_register == 0) { // Bank 0
    if (sensors_.count("status")) sensors_["status"]->publish_state(get_16bit_unsigned(data_frame, 15 + 0*2));
    if (sensors_.count("v_pv_1")) sensors_["v_pv_1"]->publish_state(get_16bit_unsigned(data_frame, 15 + 1*2) / 10.0);
    if (sensors_.count("v_pv_2")) sensors_["v_pv_2"]->publish_state(get_16bit_unsigned(data_frame, 15 + 2*2) / 10.0);
    if (sensors_.count("v_bat")) sensors_["v_bat"]->publish_state(get_16bit_unsigned(data_frame, 15 + 4*2) / 10.0);
    if (sensors_.count("soc")) sensors_["soc"]->publish_state(data_frame[15 + 5*2]); // This is a single byte
    if (sensors_.count("p_pv_1")) sensors_["p_pv_1"]->publish_state(get_16bit_unsigned(data_frame, 15 + 7*2));
    if (sensors_.count("p_pv_2")) sensors_["p_pv_2"]->publish_state(get_16bit_unsigned(data_frame, 15 + 8*2));
    if (sensors_.count("p_pv_total")) sensors_["p_pv_total"]->publish_state(get_16bit_unsigned(data_frame, 15 + 7*2) + get_16bit_unsigned(data_frame, 15 + 8*2));
    if (sensors_.count("p_charge")) sensors_["p_charge"]->publish_state(get_16bit_unsigned(data_frame, 15 + 10*2));
    if (sensors_.count("p_discharge")) sensors_["p_discharge"]->publish_state(get_16bit_unsigned(data_frame, 15 + 11*2));
    if (sensors_.count("p_inv")) sensors_["p_inv"]->publish_state(get_16bit_unsigned(data_frame, 15 + 16*2));
    if (sensors_.count("p_to_grid")) sensors_["p_to_grid"]->publish_state(get_16bit_unsigned(data_frame, 15 + 26*2));
    if (sensors_.count("p_to_user")) sensors_["p_to_user"]->publish_state(get_16bit_unsigned(data_frame, 15 + 27*2));
    float p_rec = get_16bit_unsigned(data_frame, 15 + 17*2);
    float p_load_calc = get_16bit_unsigned(data_frame, 15 + 27*2) - p_rec;
    if (sensors_.count("p_load")) sensors_["p_load"]->publish_state(p_load_calc > 0 ? p_load_calc : 0);
    float e_pv_1_day = get_16bit_unsigned(data_frame, 15 + 28*2) / 10.0;
    float e_pv_2_day = get_16bit_unsigned(data_frame, 15 + 29*2) / 10.0;
    if (sensors_.count("e_pv_total_day")) sensors_["e_pv_total_day"]->publish_state(e_pv_1_day + e_pv_2_day);
    if (sensors_.count("e_inv_day")) sensors_["e_inv_day"]->publish_state(get_16bit_unsigned(data_frame, 15 + 31*2) / 10.0);
    if (sensors_.count("e_chg_day")) sensors_["e_chg_day"]->publish_state(get_16bit_unsigned(data_frame, 15 + 33*2) / 10.0);
    if (sensors_.count("e_dischg_day")) sensors_["e_dischg_day"]->publish_state(get_16bit_unsigned(data_frame, 15 + 34*2) / 10.0);
    if (sensors_.count("e_to_grid_day")) sensors_["e_to_grid_day"]->publish_state(get_16bit_unsigned(data_frame, 15 + 36*2) / 10.0);
    if (sensors_.count("e_to_user_day")) sensors_["e_to_user_day"]->publish_state(get_16bit_unsigned(data_frame, 15 + 37*2) / 10.0);
  } else if (start_register == 40) { // Bank 1
    if (sensors_.count("t_inner")) sensors_["t_inner"]->publish_state(get_16bit_unsigned(data_frame, 15 + (64-40)*2));
    if (sensors_.count("t_rad_1")) sensors_["t_rad_1"]->publish_state(get_16bit_unsigned(data_frame, 15 + (65-40)*2));
    if (sensors_.count("t_bat")) sensors_["t_bat"]->publish_state(get_16bit_unsigned(data_frame, 15 + (67-40)*2));
  } else if (start_register == 80) { // Bank 2
    int16_t bat_current_raw = get_16bit_signed(data_frame, 15 + (98-80)*2);
    if (sensors_.count("bat_current")) sensors_["bat_current"]->publish_state(bat_current_raw / 10.0);
  }
}

// Helper functions translated from Python
uint16_t LuxpowerSnaComponent::get_16bit_unsigned(const std::vector<uint8_t> &data, int offset) {
  return (uint16_t(data[offset+1]) << 8) | data[offset];
}

int16_t LuxpowerSnaComponent::get_16bit_signed(const std::vector<uint8_t> &data, int offset) {
  uint16_t u_val = (uint16_t(data[offset+1]) << 8) | data[offset];
  return (int16_t)u_val;
}

std::vector<uint8_t> LuxpowerSnaComponent::prepare_packet_for_read(uint16_t start_register, uint16_t num_registers, uint8_t function) {
  std::vector<uint8_t> packet;
  std::vector<uint8_t> data_frame;

  // Data frame first
  data_frame.push_back(ACTION_WRITE); // Python code uses ACTION_WRITE for reads
  data_frame.push_back(function);
  data_frame.insert(data_frame.end(), this->inverter_serial_.begin(), this->inverter_serial_.end());
  data_frame.push_back(start_register & 0xFF);
  data_frame.push_back(start_register >> 8);
  data_frame.push_back(num_registers & 0xFF);
  data_frame.push_back(num_registers >> 8);
  
  uint16_t crc = compute_crc(data_frame);

  // Main packet
  packet.push_back(0xA1); packet.push_back(0x1A); // Prefix
  packet.push_back(0x02); packet.push_back(0x00); // Protocol
  uint16_t frame_length = 14 + data_frame.size();
  packet.push_back(frame_length & 0xFF); packet.push_back(frame_length >> 8);
  packet.push_back(0x01); // Unknown byte
  packet.push_back(TCP_FUNCTION_TRANSLATED_DATA);
  packet.insert(packet.end(), this->dongle_serial_.begin(), this->dongle_serial_.end());
  uint16_t data_length = 2 + data_frame.size();
  packet.push_back(data_length & 0xFF); packet.push_back(data_length >> 8);
  
  // Add the data frame and CRC
  packet.insert(packet.end(), data_frame.begin(), data_frame.end());
  packet.push_back(crc & 0xFF);
  packet.push_back(crc >> 8);

  return packet;
}

uint16_t LuxpowerSnaComponent::compute_crc(const std::vector<uint8_t> &data) {
  uint16_t crc = 0xFFFF;
  for (uint8_t byte : data) {
    crc ^= byte;
    for (int i = 0; i < 8; i++) {
      if (crc & 1) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

}  // namespace luxpower_sna
}  // namespace esphome
