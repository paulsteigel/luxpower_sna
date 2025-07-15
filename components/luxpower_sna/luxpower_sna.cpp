```cpp
#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome {
namespace luxpower_sna {

void LuxpowerSNAComponent::setup() {
  this->current_bank_ = 0;
  this->connected_ = false;
  this->last_request_ = 0;
  this->last_heartbeat_ = 0;
  this->request_in_progress_ = false; // Lock-like mechanism
}

void LuxpowerSNAComponent::update() {
  uint32_t now = millis();
  
  // Prevent overlapping requests (mimic Python's lock)
  if (request_in_progress_) {
    ESP_LOGD(TAG, "Request in progress, skipping update");
    return;
  }

  // Ensure at least 20 seconds between requests to match Python polling
  if (now - last_request_ < 20000) {
    ESP_LOGD(TAG, "Too soon since last request, skipping update");
    return;
  }

  check_connection_();
  if (!connected_) {
    ESP_LOGE(TAG, "Not connected, cannot update");
    return;
  }

  request_in_progress_ = true;
  request_bank_(banks_[current_bank_]);
  last_request_ = now;
}

bool LuxpowerSNAComponent::is_heartbeat_packet_(const uint8_t *data) {
  LuxHeader *header = reinterpret_cast<LuxHeader*>(const_cast<uint8_t*>(data));
  return (header->prefix == 0x1AA1 && header->function == 193); // HEARTBEAT = 193 from LXPPacket.py
}

void LuxpowerSNAComponent::handle_heartbeat_(const uint8_t *data, size_t len) {
  if (len != 19 && len != 21) { // Heartbeat packets are 19 or 21 bytes in LXPPacket.py
    ESP_LOGE(TAG, "Invalid heartbeat packet length: %zu", len);
    return;
  }
  LuxHeader *header = reinterpret_cast<LuxHeader*>(const_cast<uint8_t*>(data));
  if (header->prefix != 0x1AA1 || header->function != 193) {
    ESP_LOGE(TAG, "Invalid heartbeat packet: prefix=0x%04X, function=0x%02X", header->prefix, header->function);
    return;
  }
  if (client_.connected()) {
    client_.write(data, len);
    ESP_LOGD(TAG, "Responded to heartbeat packet of length %zu", len);
    last_heartbeat_ = millis();
  } else {
    ESP_LOGW(TAG, "Cannot respond to heartbeat: not connected");
  }
}

bool LuxpowerSNAComponent::receive_response_(uint8_t bank) {
  uint8_t buffer[512];
  uint32_t start = millis();
  size_t total_read = 0;

  // Wait up to 15 seconds for response (matches inverter's 10-15s prep time)
  while (millis() - start < 15000) {
    if (client_.available()) {
      size_t bytes_read = client_.readBytes(buffer + total_read, sizeof(buffer) - total_read);
      if (bytes_read > 0) {
        total_read += bytes_read;
        packet_buffer_.insert(packet_buffer_.end(), buffer, buffer + bytes_read);
        if (process_packet_buffer_(bank)) {
          request_in_progress_ = false; // Release lock
          current_bank_ = (current_bank_ + 1) % 5; // Move to next bank
          return true;
        }
      }
    }
    delay(10);
  }

  if (total_read == 0) {
    ESP_LOGE(TAG, "No data received for bank %d after 15s", bank);
    request_in_progress_ = false; // Release lock on timeout
    return false;
  }
  request_in_progress_ = false; // Release lock
  return false;
}

bool LuxpowerSNAComponent::process_packet_buffer_(uint8_t bank) {
  while (packet_buffer_.size() >= sizeof(LuxHeader)) {
    LuxHeader *header = reinterpret_cast<LuxHeader*>(packet_buffer_.data());
    if (header->prefix != 0x1AA1) {
      ESP_LOGE(TAG, "Invalid packet prefix: 0x%04X", header->prefix);
      packet_buffer_.erase(packet_buffer_.begin(), packet_buffer_.begin() + 1);
      continue;
    }

    uint16_t total_length = header->packetLength + 6;
    if (packet_buffer_.size() < total_length) {
      ESP_LOGD(TAG, "Incomplete packet, waiting for more data (%zu/%d bytes)", packet_buffer_.size(), total_length);
      return false;
    }

    if (is_heartbeat_packet_(packet_buffer_.data())) {
      handle_heartbeat_(packet_buffer_.data(), total_length);
      packet_buffer_.erase(packet_buffer_.begin(), packet_buffer_.begin() + total_length);
      continue;
    }

    // Validate packet fields
    if (header->protocolVersion != 2 || header->function != 194) { // TRANSLATED_DATA = 194
      ESP_LOGE(TAG, "Invalid protocol or function: protocol=0x%04X, function=0x%02X", 
               header->protocolVersion, header->function);
      packet_buffer_.erase(packet_buffer_.begin(), packet_buffer_.begin() + total_length);
      continue;
    }

    // Validate CRC
    uint16_t crc_calc = calculate_crc_(packet_buffer_.data() + sizeof(LuxHeader), 
                                      total_length - sizeof(LuxHeader) - 2);
    uint16_t crc_received = (packet_buffer_[total_length - 1] << 8) | packet_buffer_[total_length - 2];
    if (crc_calc != crc_received) {
      ESP_LOGE(TAG, "CRC mismatch: calc=0x%04X, recv=0x%04X", crc_calc, crc_received);
      packet_buffer_.erase(packet_buffer_.begin(), packet_buffer_.begin() + total_length);
      continue;
    }

    // Validate TranslatedData
    LuxTranslatedData *trans = reinterpret_cast<LuxTranslatedData*>(packet_buffer_.data() + sizeof(LuxHeader));
    if (trans->deviceFunction != 0x04) { // READ_INPUT = 4
      ESP_LOGE(TAG, "Invalid device function: 0x%02X", trans->deviceFunction);
      packet_buffer_.erase(packet_buffer_.begin(), packet_buffer_.begin() + total_length);
      continue;
    }

    // Validate register start
    uint16_t expected_register = bank * 40;
    if (trans->registerStart != expected_register) {
      ESP_LOGW(TAG, "Unexpected register start: expected=%d, received=%d", 
               expected_register, trans->registerStart);
      packet_buffer_.erase(packet_buffer_.begin(), packet_buffer_.begin() + total_length);
      continue;
    }

    // Process data packet
    size_t data_offset = sizeof(LuxHeader) + sizeof(LuxTranslatedData);
    uint8_t* payload = packet_buffer_.data() + data_offset;
    size_t payload_size = total_length - data_offset - 2;

    switch (bank) {
      case 0:
        if (payload_size >= sizeof(LuxLogDataRawSection1)) {
          process_section1_(*reinterpret_cast<const LuxLogDataRawSection1*>(payload));
        } else {
          ESP_LOGE(TAG, "Payload too small for bank 0: %zu bytes", payload_size);
        }
        break;
      case 40:
        if (payload_size >= sizeof(LuxLogDataRawSection2)) {
          process_section2_(*reinterpret_cast<const LuxLogDataRawSection2*>(payload));
        } else {
          ESP_LOGE(TAG, "Payload too small for bank 40: %zu bytes", payload_size);
        }
        break;
      case 80:
        if (payload_size >= sizeof(LuxLogDataRawSection3)) {
          process_section3_(*reinterpret_cast<const LuxLogDataRawSection3*>(payload));
        } else {
          ESP_LOGE(TAG, "Payload too small for bank 80: %zu bytes", payload_size);
        }
        break;
      case 120:
        if (payload_size >= sizeof(LuxLogDataRawSection4)) {
          process_section4_(*reinterpret_cast<const LuxLogDataRawSection4*>(payload));
        } else {
          ESP_LOGE(TAG, "Payload too small for bank 120: %zu bytes", payload_size);
        }
        break;
      case 160:
        if (payload_size >= sizeof(LuxLogDataRawSection5)) {
          process_section5_(*reinterpret_cast<const LuxLogDataRawSection5*>(payload));
        } else {
          ESP_LOGE(TAG, "Payload too small for bank 160: %zu bytes", payload_size);
        }
        break;
      default:
        ESP_LOGW(TAG, "Unknown bank: %d", bank);
    }

    packet_buffer_.erase(packet_buffer_.begin(), packet_buffer_.begin() + total_length);
    return true;
  }
  return false;
}

void LuxpowerSNAComponent::request_bank_(uint8_t bank) {
  if (!client_.connected()) {
    ESP_LOGE(TAG, "Cannot send request for bank %d: not connected", bank);
    request_in_progress_ = false;
    return;
  }

  uint8_t pkt[38] = {
    0xA1, 0x1A,       // Prefix
    0x02, 0x00,       // Protocol version 2
    0x20, 0x00,       // Frame length (32)
    0x01,             // Address
    0xC2,             // Function (TRANSLATED_DATA = 194)
    // Dongle serial (10 bytes)
    0,0,0,0,0,0,0,0,0,0,
    0x12, 0x00,       // Data length (18)
    // Data frame starts here
    0x01,             // Address action (ACTION_READ = 1)
    0x04,             // Device function (READ_INPUT = 4)
    // Inverter serial (10 bytes)
    0,0,0,0,0,0,0,0,0,0,
    // Register and value
    static_cast<uint8_t>(bank * 40), 0x00, // Register (low, high)
    0x28, 0x00        // Value (40 registers)
  };

  // Copy serial numbers
  memcpy(pkt + 8, dongle_serial_.c_str(), std::min(dongle_serial_.length(), size_t(10)));
  memcpy(pkt + 22, inverter_serial_.c_str(), std::min(inverter_serial_.length(), size_t(10)));

  // Calculate CRC for data frame portion only (16 bytes)
  uint16_t crc = calculate_crc_(pkt + 20, 16);
  pkt[36] = crc & 0xFF;
  pkt[37] = crc >> 8;

  ESP_LOGV(TAG, "Sending request for bank %d", bank);
  client_.write(pkt, sizeof(pkt));
}

void LuxpowerSNAComponent::check_connection_() {
  if (!client_.connected()) {
    if (connected_) {
      ESP_LOGW(TAG, "Connection lost, attempting to reconnect");
      safe_disconnect_();
    }
    if (client_.connect(host_.c_str(), port_)) {
      connected_ = true;
      packet_buffer_.clear();
      ESP_LOGI(TAG, "Reconnected to inverter");
      last_heartbeat_ = millis();
    } else {
      ESP_LOGE(TAG, "Reconnection failed");
      connected_ = false;
      request_in_progress_ = false; // Release lock on failed connection
    }
  }
}

void LuxpowerSNAComponent::safe_disconnect_() {
  client_.stop();
  packet_buffer_.clear();
  connected_ = false;
  request_in_progress_ = false; // Release lock
}

void LuxpowerSNAComponent::process_section1_(const LuxLogDataRawSection1 &data) {
  if (this->pv_power_1_ != nullptr)
    this->pv_power_1_->publish_state(data.pvPower1);
  if (this->pv_power_2_ != nullptr)
    this->pv_power_2_->publish_state(data.pvPower2);
  if (this->battery_power_ != nullptr)
    this->battery_power_->publish_state(data.batteryPower);
  if (this->soc_ != nullptr)
    this->soc_->publish_state(data.SOC);
  if (this->grid_power_ != nullptr)
    this->grid_power_->publish_state(data.gridPower);
  if (this->load_power_ != nullptr)
    this->load_power_->publish_state(data.loadPower);
}

void LuxpowerSNAComponent::process_section2_(const LuxLogDataRawSection2 &data) {
  if (this->pv_voltage_1_ != nullptr)
    this->pv_voltage_1_->publish_state(data.pvVoltage1 / 10.0f);
  if (this->pv_voltage_2_ != nullptr)
    this->pv_voltage_2_->publish_state(data.pvVoltage2 / 10.0f);
  if (this->pv_current_1_ != nullptr)
    this->pv_current_1_->publish_state(data.pvCurrent1 / 10.0f);
  if (this->pv_current_2_ != nullptr)
    this->pv_current_2_->publish_state(data.pvCurrent2 / 10.0f);
  if (this->battery_voltage_ != nullptr)
    this->battery_voltage_->publish_state(data.batteryVoltage / 10.0f);
  if (this->battery_current_ != nullptr)
    this->battery_current_->publish_state(data.batteryCurrent / 10.0f);
  if (this->battery_temp_ != nullptr)
    this->battery_temp_->publish_state(data.batteryTemp / 10.0f);
}

void LuxpowerSNAComponent::process_section3_(const LuxLogDataRawSection3 &data) {
  if (this->grid_voltage_ != nullptr)
    this->grid_voltage_->publish_state(data.gridVoltage / 10.0f);
  if (this->grid_current_ != nullptr)
    this->grid_current_->publish_state(data.gridCurrent / 10.0f);
  if (this->grid_freq_ != nullptr)
    this->grid_freq_->publish_state(data.gridFreq / 100.0f);
  if (this->load_voltage_ != nullptr)
    this->load_voltage_->publish_state(data.loadVoltage / 10.0f);
  if (this->load_current_ != nullptr)
    this->load_current_->publish_state(data.loadCurrent / 10.0f);
  if (this->load_freq_ != nullptr)
    this->load_freq_->publish_state(data.loadFreq / 100.0f);
}

void LuxpowerSNAComponent::process_section4_(const LuxLogDataRawSection4 &data) {
  if (this->inverter_temp_ != nullptr)
    this->inverter_temp_->publish_state(data.inverterTemp / 10.0f);
  if (this->inverter_status_ != nullptr)
    this->inverter_status_->publish_state(data.inverterStatus);
}

void LuxpowerSNAComponent::process_section5_(const LuxLogDataRawSection5 &data) {
  if (this->pv_energy_1_ != nullptr)
    this->pv_energy_1_->publish_state(data.pvEnergy1);
  if (this->pv_energy_2_ != nullptr)
    this->pv_energy_2_->publish_state(data.pvEnergy2);
  if (this->battery_charge_energy_ != nullptr)
    this->battery_charge_energy_->publish_state(data.batteryChargeEnergy);
  if (this->battery_discharge_energy_ != nullptr)
    this->battery_discharge_energy_->publish_state(data.batteryDischargeEnergy);
  if (this->grid_import_energy_ != nullptr)
    this->grid_import_energy_->publish_state(data.gridImportEnergy);
  if (this->grid_export_energy_ != nullptr)
    this->grid_export_energy_->publish_state(data.gridExportEnergy);
  if (this->load_energy_ != nullptr)
    this->load_energy_->publish_state(data.loadEnergy);
}

uint16_t LuxpowerSNAComponent::calculate_crc_(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t pos = 0; pos < len; pos++) {
    crc ^= data[pos];
    for (uint8_t i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) {
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
```
