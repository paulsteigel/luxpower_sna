#include "luxpower_inverter.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

void LuxpowerInverterComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Luxpower SNA Component...");
  this->client_ = new AsyncClient();
  this->rx_buffer_.reserve(256); // Pre-allocate some memory

  this->client_->onConnect([this](void *arg, AsyncClient *client) {
    ESP_LOGI(TAG, "Connected to inverter at %s:%d", this->host_.c_str(), this->port_);
    this->is_connected_ = true;
    this->update();
  });

  this->client_->onDisconnect([this](void *arg, AsyncClient *client) {
    ESP_LOGW(TAG, "Disconnected from inverter.");
    this->is_connected_ = false;
  });

  this->client_->onError([this](void *arg, AsyncClient *client, int8_t error) {
    ESP_LOGE(TAG, "Connection error: %s", client->errorToString(error));
    this->is_connected_ = false;
  });

  this->client_->onData([this](void *arg, AsyncClient *c, void *data, size_t len) {
    this->on_data(data, len);
  });
}

void LuxpowerInverterComponent::on_data(void *data, size_t len) {
    this->rx_buffer_.insert(this->rx_buffer_.end(), (uint8_t*)data, (uint8_t*)data + len);

    while (true) {
        if (this->rx_buffer_.size() < 6) {
            return; // Not enough data for a header
        }
        
        if (this->rx_buffer_[0] != 0xA1 || this->rx_buffer_[1] != 0x1A) {
            ESP_LOGW(TAG, "Invalid start of packet found, discarding one byte.");
            this->rx_buffer_.erase(this->rx_buffer_.begin());
            continue; // Try again with the next byte
        }

        uint16_t packet_len = (uint16_t(this->rx_buffer_[5]) << 8) | this->rx_buffer_[4];

        if (this->rx_buffer_.size() < packet_len) {
            return; // Incomplete packet, wait for more data
        }

        // We have a full packet
        std::vector<uint8_t> packet(this->rx_buffer_.begin(), this->rx_buffer_.begin() + packet_len);
        this->handle_packet(packet);

        // Remove the processed packet from the buffer
        this->rx_buffer_.erase(this->rx_buffer_.begin(), this->rx_buffer_.begin() + packet_len);
    }
}

void LuxpowerInverterComponent::handle_packet(const std::vector<uint8_t> &packet) {
    // Basic validation
    if (packet[7] != 0xC2) {
        ESP_LOGD(TAG, "Received non-data packet, ignoring.");
        return;
    }

    const int DATA_FRAME_START = 35;
    if (packet.size() < DATA_FRAME_START + 2) {
        ESP_LOGW(TAG, "Received packet is too short to contain data.");
        return;
    }

    // Extract just the register values
    std::vector<uint8_t> d(packet.begin() + DATA_FRAME_START, packet.end() - 2);

    // --- PARSE AND PUBLISH ---
    // Each register is 2 bytes wide.
    float battery_voltage = get_16bit_unsigned(d, 4) / 10.0f;
    float battery_current = get_16bit_signed(d, 5) / 10.0f;
    float power_from_grid = get_16bit_signed(d, 16);
    float battery_capacity_ah = get_16bit_unsigned(d, 21);
    float daily_solar_generation = get_16bit_unsigned(d, 25) / 10.0f;

    ESP_LOGD(TAG, "Parsed: V:%.1f, A:%.1f, Grid:%.0fW, Cap:%.0fAh, Solar:%.1fkWh", 
             battery_voltage, battery_current, power_from_grid, battery_capacity_ah, daily_solar_generation);

    if (this->battery_voltage_sensor_ != nullptr) this->battery_voltage_sensor_->publish_state(battery_voltage);
    if (this->battery_current_sensor_ != nullptr) this->battery_current_sensor_->publish_state(battery_current);
    if (this->power_from_grid_sensor_ != nullptr) this->power_from_grid_sensor_->publish_state(power_from_grid);
    if (this->battery_capacity_ah_sensor_ != nullptr) this->battery_capacity_ah_sensor_->publish_state(battery_capacity_ah);
    if (this->daily_solar_generation_sensor_ != nullptr) this->daily_solar_generation_sensor_->publish_state(daily_solar_generation);
}


void LuxpowerInverterComponent::update() {
  if (!this->is_connected_) {
    this->connect_to_inverter();
    return;
  }
  this->request_inverter_data();
}

void LuxpowerInverterComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Luxpower SNA Component:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%d", this->host_.c_str(), this->port_);
  LOG_SENSOR("  ", "Battery Voltage", this->battery_voltage_sensor_);
  LOG_SENSOR("  ", "Battery Current", this->battery_current_sensor_);
  LOG_SENSOR("  ", "Battery Capacity AH", this->battery_capacity_ah_sensor_);
  LOG_SENSOR("  ", "Power from Grid", this->power_from_grid_sensor_);
  LOG_SENSOR("  ", "Daily Solar", this->daily_solar_generation_sensor_);
}

void LuxpowerInverterComponent::request_inverter_data() {
  if (!this->is_connected_) return;
  ESP_LOGD(TAG, "Polling update: Requesting data from inverter.");
  
  uint16_t start_register = 0;
  uint16_t num_registers = 40;
  uint8_t function = 4; // READ_INPUT
  
  auto packet = this->prepare_packet_for_read(start_register, num_registers, function);
  
  if (this->client_->space() > packet.size() && this->client_->canSend()) {
    this->client_->write((const char*)packet.data(), packet.size());
  } else {
    ESP_LOGW(TAG, "Cannot send data request, client buffer full or not ready.");
  }
}

// --- Helper Functions and Setters ---
uint16_t LuxpowerInverterComponent::get_16bit_unsigned(const std::vector<uint8_t> &data, int offset_reg) {
    int offset_bytes = offset_reg * 2;
    if (data.size() < offset_bytes + 2) return 0;
    return (uint16_t(data[offset_bytes + 1]) << 8) | data[offset_bytes];
}

int16_t LuxpowerInverterComponent::get_16bit_signed(const std::vector<uint8_t> &data, int offset_reg) {
    int offset_bytes = offset_reg * 2;
    if (data.size() < offset_bytes + 2) return 0;
    return (int16_t)((uint16_t(data[offset_bytes + 1]) << 8) | data[offset_bytes]);
}

void LuxpowerInverterComponent::set_host(const std::string &host) { this->host_ = host; }
void LuxpowerInverterComponent::set_port(uint16_t port) { this->port_ = port; }
void LuxpowerInverterComponent::set_dongle_serial(const std::vector<uint8_t> &serial) { this->dongle_serial_ = serial; }
void LuxpowerInverterComponent::set_inverter_serial_number(const std::vector<uint8_t> &serial) { this->inverter_serial_ = serial; }

void LuxpowerInverterComponent::connect_to_inverter() {
  if (this->is_connected_ || this->client_->connected()) return;
  ESP_LOGD(TAG, "Attempting connection to %s:%d", this->host_.c_str(), this->port_);
  this->client_->connect(this->host_.c_str(), this->port_);
}

std::vector<uint8_t> LuxpowerInverterComponent::prepare_packet_for_read(uint16_t start, uint16_t count, uint8_t func) {
  std::vector<uint8_t> packet, data_frame;
  data_frame.push_back(0); data_frame.push_back(func);
  data_frame.insert(data_frame.end(), this->inverter_serial_.begin(), this->inverter_serial_.end());
  data_frame.push_back(start & 0xFF); data_frame.push_back(start >> 8);
  data_frame.push_back(count & 0xFF); data_frame.push_back(count >> 8);
  uint16_t crc = compute_crc(data_frame);
  packet.push_back(0xA1); packet.push_back(0x1A);
  packet.push_back(0x02); packet.push_back(0x00);
  uint16_t frame_len = 14 + data_frame.size();
  packet.push_back(frame_len & 0xFF); packet.push_back(frame_len >> 8);
  packet.push_back(0x01); packet.push_back(194);
  packet.insert(packet.end(), this->dongle_serial_.begin(), this->dongle_serial_.end());
  uint16_t data_len = 2 + data_frame.size();
  packet.push_back(data_len & 0xFF); packet.push_back(data_len >> 8);
  packet.insert(packet.end(), data_frame.begin(), data_frame.end());
  packet.push_back(crc & 0xFF); packet.push_back(crc >> 8);
  return packet;
}

uint16_t LuxpowerInverterComponent::compute_crc(const std::vector<uint8_t> &data) {
  uint16_t crc = 0xFFFF;
  for (uint8_t byte : data) {
    crc ^= byte;
    for (int i = 0; i < 8; i++) {
      if (crc & 1) { crc >>= 1; crc ^= 0xA001; } else { crc >>= 1; }
    }
  }
  return crc;
}

}  // namespace luxpower_sna
}  // namespace esphome
