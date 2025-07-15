#include "luxclient.h"

namespace esphome {
namespace luxpower {

static const char *const TAG = "luxpower.client";

void LuxPowerClient::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxPower Client");
  lxp_packet_ = std::make_unique<LxpPacket>(false, dongle_serial_, inverter_serial_);
  set_keepalive(15);  // 15s keepalive
}

void LuxPowerClient::on_connect() {
  ESP_LOGI(TAG, "Connected to LuxPower server");
  // Refresh data immediately after connection
  request_data_bank(0);
  request_hold_bank(0);
}

void LuxPowerClient::on_disconnect() {
  ESP_LOGW(TAG, "Disconnected from LuxPower server");
}

void LuxPowerClient::on_data(std::vector<uint8_t> &data) {
  parse_incoming_data(data);
}

void LuxPowerClient::parse_incoming_data(const std::vector<uint8_t> &data) {
  size_t pos = 0;
  while (pos < data.size()) {
    // Check minimum packet size
    if (data.size() - pos < 28) {
      ESP_LOGW(TAG, "Incomplete packet received");
      break;
    }

    // Extract packet length (big-endian)
    uint16_t pkt_len = (data[pos + 4] << 8) | data[pos + 5];
    uint16_t full_len = pkt_len + 6;
    
    if (pos + full_len > data.size()) {
      ESP_LOGW(TAG, "Partial packet received, waiting for more data");
      break;
    }
    
    // Extract complete packet
    std::vector<uint8_t> packet(data.begin() + pos, data.begin() + pos + full_len);
    pos += full_len;
    
    process_packet(packet);
  }
}

void LuxPowerClient::process_packet(const std::vector<uint8_t> &packet) {
  auto result = lxp_packet_->parse_packet(packet);
  
  if (result.packet_error) {
    ESP_LOGW(TAG, "Invalid packet received");
    return;
  }
  
  // Handle different packet types
  switch (result.tcp_function) {
    case LxpPacket::HEARTBEAT:
      if (respond_to_heartbeat_) {
        send_packet(lxp_packet_->prepare_heartbeat_response(packet));
      }
      last_heartbeat_ = millis();
      break;
      
    case LxpPacket::READ_INPUT:
      // Process data registers
      for (size_t i = 0; i < result.values.size(); i++) {
        uint16_t reg = result.register_addr + i;
        // Fire events or update sensors here
        ESP_LOGD(TAG, "Data Register %d: %d", reg, result.values[i]);
      }
      break;
      
    case LxpPacket::READ_HOLD:
    case LxpPacket::WRITE_SINGLE:
      // Process holding registers
      for (size_t i = 0; i < result.values.size(); i++) {
        uint16_t reg = result.register_addr + i;
        // Fire events or update sensors here
        ESP_LOGD(TAG, "Holding Register %d: %d", reg, result.values[i]);
      }
      break;
      
    default:
      ESP_LOGW(TAG, "Unknown function code: 0x%02X", result.tcp_function);
  }
}

void LuxPowerClient::send_packet(const std::vector<uint8_t> &packet) {
  if (!is_connected()) {
    ESP_LOGW(TAG, "Cannot send packet - not connected");
    return;
  }
  
  MutexLock lock(data_mutex_);
  write(packet.data(), packet.size());
  ESP_LOGD(TAG, "Sent %d bytes", packet.size());
}

void LuxPowerClient::loop() {
  AsyncTCPClient::loop();  // Handle base class operations
  
  // Periodic tasks
  uint32_t now = millis();
  
  // Refresh data registers every 60s
  if (now - last_data_refresh_ > 60000) {
    last_data_refresh_ = now;
    for (uint8_t bank = 0; bank < 3; bank++) {
      request_data_bank(bank);
    }
  }
  
  // Refresh holding registers every 300s
  if (now - last_hold_refresh_ > 300000) {
    last_hold_refresh_ = now;
    for (uint8_t bank = 0; bank < 5; bank++) {
      request_hold_bank(bank);
    }
  }
  
  // Heartbeat check
  if (last_heartbeat_ > 0 && now - last_heartbeat_ > 90000) {
    ESP_LOGW(TAG, "No heartbeat for 90s, reconnecting");
    disconnect();
  }
}

void LuxPowerClient::request_data_bank(uint8_t bank) {
  uint16_t start_reg = bank * 40;
  auto packet = lxp_packet_->prepare_read_packet(start_reg, 40, LxpPacket::READ_INPUT);
  send_packet(packet);
}

void LuxPowerClient::request_hold_bank(uint8_t bank) {
  uint16_t start_reg = bank * 40;
  if (bank == 5) start_reg = 200;
  if (bank == 6) start_reg = 560;
  
  auto packet = lxp_packet_->prepare_read_packet(start_reg, 40, LxpPacket::READ_HOLD);
  send_packet(packet);
}

bool LuxPowerClient::write_holding_register(uint16_t reg, uint16_t value) {
  auto packet = lxp_packet_->prepare_write_packet(reg, value);
  send_packet(packet);
  return true;
}

void LuxPowerClient::restart_inverter() {
  write_holding_register(11, 128);
  disconnect();
}

void LuxPowerClient::reset_settings() {
  write_holding_register(11, 2);
  disconnect();
}

void LuxPowerClient::sync_time(bool force) {
  // Time sync implementation similar to Python version
  // Would read current time registers and compare with system time
}

}  // namespace luxpower
}  // namespace esphome
