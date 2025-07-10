#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include <cstring>

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// THE MISSING PIECE: The key used to decode the data payload.
static const char *DECODE_KEY = "lux_2021_umb";
static const size_t DECODE_KEY_LEN = 12;

void log_hex_buffer(const char* title, const uint8_t *buffer, size_t len) {
  if (len == 0) {
    return;
  }
  char str[len * 3 + 1];
  for (size_t i = 0; i < len; i++) {
    sprintf(str + i * 3, "%02X ", buffer[i]);
  }
  str[len * 3 - 1] = '\0';
  ESP_LOGD(TAG, "%s (%d bytes): %s", title, len, str);
}

// NEW FUNCTION: Decodes the data payload in place.
void decode_data(uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    data[i] ^= DECODE_KEY[i % DECODE_KEY_LEN];
  }
}

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxpowerSNA...");
  this->tcp_client_ = new AsyncClient();
  this->tcp_client_->setRxTimeout(15000);

  this->tcp_client_->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
    if (this->data_ready_to_process_) {
      ESP_LOGW(TAG, "New data arrived before previous was processed. Overwriting.");
    }
    this->rx_buffer_.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + len);
    this->data_ready_to_process_ = true;
    client->close();
  });

  this->tcp_client_->onConnect([this](void *arg, AsyncClient *client) {
    ESP_LOGD(TAG, "Successfully connected. Sending request for bank %d...", this->next_bank_to_request_);
    this->request_bank_(this->next_bank_to_request_);
  });

  this->tcp_client_->onError([this](void *arg, AsyncClient *client, int8_t error) {
    ESP_LOGW(TAG, "Connection error: %s", client->errorToString(error));
    client->close();
  });

  this->tcp_client_->onDisconnect([this](void *arg, AsyncClient *client) { ESP_LOGD(TAG, "Disconnected from host."); });
  this->tcp_client_->onTimeout([this](void *arg, AsyncClient *client, uint32_t time) {
    ESP_LOGW(TAG, "Connection timeout after %d ms.", time);
    client->close();
  });
}

void LuxpowerSNAComponent::loop() {
  if (this->data_ready_to_process_) {
    this->data_ready_to_process_ = false;
    log_hex_buffer("<- Processing Raw Received", this->rx_buffer_.data(), this->rx_buffer_.size());
    this->handle_response_();
  }
}

void LuxpowerSNAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Luxpower SNA Component:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%u", this->host_.c_str(), this->port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", this->dongle_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter Serial: %s", this->inverter_serial_.c_str());
}

void LuxpowerSNAComponent::update() {
  if (this->tcp_client_->connected()) {
    ESP_LOGD(TAG, "Update requested, but a connection is already in progress. Skipping.");
    return;
  }
  ESP_LOGD(TAG, "Connecting to %s:%u...", this->host_.c_str(), this->port_);
  this->tcp_client_->connect(this->host_.c_str(), this->port_);
}

void LuxpowerSNAComponent::request_bank_(uint8_t bank) {
  uint8_t pkt[29] = {0xAA, 0x55, 0x12, 0x00, 0x01, 0xC2, 0x14};
  memcpy(pkt + 7, this->dongle_serial_.c_str(), 10);
  pkt[17] = bank;
  pkt[18] = 0x00;
  memcpy(pkt + 19, this->inverter_serial_.c_str(), 10);
  uint16_t crc = calculate_crc_(pkt + 2, 25);
  pkt[27] = crc & 0xFF;
  pkt[28] = crc >> 8;

  log_hex_buffer("-> Sending Request", pkt, sizeof(pkt));

  if (this->tcp_client_->space() > sizeof(pkt)) {
    this->tcp_client_->add(reinterpret_cast<const char *>(pkt), sizeof(pkt));
    this->tcp_client_->send();
  } else {
    ESP_LOGW(TAG, "Not enough space in TCP buffer to send request.");
    this->tcp_client_->close();
  }
}

void LuxpowerSNAComponent::handle_response_() {
  uint8_t *buffer = this->rx_buffer_.data();
  size_t length = this->rx_buffer_.size();

  // --- CORRECTED PACKET VALIDATION SEQUENCE ---
  const size_t MIN_PACKET_SIZE = 16;
  if (length < MIN_PACKET_SIZE) {
    ESP_LOGW(TAG, "Packet too small. Received %d bytes, need at least %d.", length, MIN_PACKET_SIZE);
    return;
  }

  // 1. Check raw prefix
  if (buffer[0] != 0xA1 || buffer[1] != 0x1A) {
    ESP_LOGW(TAG, "Invalid response prefix. Expected 0xA1 0x1A, got 0x%02X 0x%02X.", buffer[0], buffer[1]);
    return;
  }

  // 2. Check raw length
  uint16_t payload_len = buffer[4] | (buffer[5] << 8);
  if (payload_len + 6 != length) {
    ESP_LOGW(TAG, "Packet length mismatch. Header says payload is %d bytes, but total packet is %d bytes.", payload_len, length);
    return;
  }

  // 3. ***** DECODE THE PAYLOAD *****
  // The payload starts after the header (8 bytes) and ends before the CRC (2 bytes)
  const size_t payload_offset = 8;
  const size_t payload_data_len = length - payload_offset - 2;
  decode_data(buffer + payload_offset, payload_data_len);
  log_hex_buffer("<- Decoded Full Packet", buffer, length);

  // 4. Verify function code on DECODED data
  if (buffer[7] != 0xC2) {
    ESP_LOGW(TAG, "Invalid function code. Expected 0xC2, got 0x%02X.", buffer[7]);
    return;
  }

  // 5. Verify CRC on DECODED data
  uint16_t received_crc = buffer[length - 2] | (buffer[length - 1] << 8);
  uint16_t calculated_crc = calculate_crc_(buffer + 2, length - 4);
  if (received_crc != calculated_crc) {
    ESP_LOGW(TAG, "CRC mismatch on decoded data. Received 0x%04X, calculated 0x%04X.", received_crc, calculated_crc);
    return;
  }
  
  ESP_LOGD(TAG, "✓ Packet validation successful (Prefix, Length, Decode, Function, CRC all OK)");

  // --- PARSE INNER DATA (from the now-decoded buffer) ---
  const uint8_t* inner_data_start = buffer + 8;
  LuxTranslatedData trans;
  memcpy(&trans, inner_data_start, sizeof(LuxTranslatedData));

  ESP_LOGD(TAG, "✓ Decoded Header OK. Register Bank: %d", trans.registerStart);

  static bool serial_published = false;
  if (!serial_published) {
    std::string inv_serial(trans.serialNumber, 10);
    publish_state_("inverter_serial", inv_serial);
    serial_published = true;
  }
  
  const uint8_t *data_ptr = inner_data_start + sizeof(LuxTranslatedData);
  size_t data_payload_length = payload_data_len - sizeof(LuxTranslatedData);

  // ... (The rest of the parsing logic is unchanged and should now work correctly) ...
  if (trans.registerStart == 0 && data_payload_length >= sizeof(LuxLogDataRawSection1)) {
    ESP_LOGD(TAG, "Parsing data for bank 0...");
    LuxLogDataRawSection1 raw;
    memcpy(&raw, data_ptr, sizeof(LuxLogDataRawSection1));
    
    publish_state_("pv1_voltage", raw.pv1_voltage / 10.0f);
    publish_state_("pv2_voltage", raw.pv2_voltage / 10.0f);
    publish_state_("pv3_voltage", raw.pv3_voltage / 10.0f);
    publish_state_("battery_voltage", raw.battery_voltage / 10.0f);
    publish_state_("soc", (float)raw.soc);
    publish_state_("soh", (float)raw.soh);
    publish_state_("pv1_power", (float)raw.pv1_power);
    publish_state_("pv2_power", (float)raw.pv2_power);
    publish_state_("pv3_power", (float)raw.pv3_power);
    publish_state_("charge_power", (float)raw.charge_power);
    publish_state_("discharge_power", (float)raw.discharge_power);
    publish_state_("inverter_power", (float)raw.activeInverter_power);
    publish_state_("power_to_grid", (float)raw.power_to_grid);
    publish_state_("power_from_grid", (float)raw.power_from_grid);
    publish_state_("grid_voltage_r", raw.voltage_ac_r / 10.0f);
    publish_state_("grid_voltage_s", raw.voltage_ac_s / 10.0f);
    publish_state_("grid_voltage_t", raw.voltage_ac_t / 10.0f);
    publish_state_("grid_frequency", raw.frequency_grid / 100.0f);
    publish_state_("power_factor", raw.grid_power_factor / 1000.0f);
    publish_state_("eps_voltage_r", raw.voltage_eps_r / 10.0f);
    publish_state_("eps_voltage_s", raw.voltage_eps_s / 10.0f);
    publish_state_("eps_voltage_t", raw.voltage_eps_t / 10.0f);
    publish_state_("eps_frequency", raw.frequency_eps / 100.0f);
    publish_state_("eps_active_power", (float)raw.active_eps_power);
    publish_state_("eps_apparent_power", (float)raw.apparent_eps_power);
    publish_state_("bus1_voltage", raw.bus1_voltage / 10.0f);
    publish_state_("bus2_voltage", raw.bus2_voltage / 10.0f);
    publish_state_("pv1_energy_today", raw.pv1_energy_today / 10.0f);
    publish_state_("pv2_energy_today", raw.pv2_energy_today / 10.0f);
    publish_state_("pv3_energy_today", raw.pv3_energy_today / 10.0f);
    publish_state_("inverter_energy_today", raw.activeInverter_energy_today / 10.0f);
    publish_state_("ac_charging_today", raw.ac_charging_today / 10.0f);
    publish_state_("charging_today", raw.charging_today / 10.0f);
    publish_state_("discharging_today", raw.discharging_today / 10.0f);
    publish_state_("eps_today", raw.eps_today / 10.0f);
    publish_state_("exported_today", raw.exported_today / 10.0f);
    publish_state_("grid_today", raw.grid_today / 10.0f);

  } else if (trans.registerStart == 40 && data_payload_length >= sizeof(LuxLogDataRawSection2)) {
    ESP_LOGD(TAG, "Parsing data for bank 40...");
    LuxLogDataRawSection2 raw;
    memcpy(&raw, data_ptr, sizeof(LuxLogDataRawSection2));
    
    publish_state_("total_pv1_energy", raw.e_pv_1_all / 10.0f);
    publish_state_("total_pv2_energy", raw.e_pv_2_all / 10.0f);
    publish_state_("total_pv3_energy", raw.e_pv_3_all / 10.0f);
    publish_state_("total_inverter_output", raw.e_inv_all / 10.0f);
    publish_state_("total_recharge_energy", raw.e_rec_all / 10.0f);
    publish_state_("total_charged", raw.e_chg_all / 10.0f);
    publish_state_("total_discharged", raw.e_dischg_all / 10.0f);
    publish_state_("total_eps_energy", raw.e_eps_all / 10.0f);
    publish_state_("total_exported", raw.e_to_grid_all / 10.0f);
    publish_state_("total_imported", raw.e_to_user_all / 10.0f);
    publish_state_("temp_inner", raw.t_inner / 10.0f);
    publish_state_("temp_radiator", raw.t_rad_1 / 10.0f);
    publish_state_("temp_radiator2", raw.t_rad_2 / 10.0f);
    publish_state_("temp_battery", raw.t_bat / 10.0f);
    publish_state_("uptime", (float)raw.uptime);

  } else if (trans.registerStart == 80 && data_payload_length >= sizeof(LuxLogDataRawSection3)) {
    ESP_LOGD(TAG, "Parsing data for bank 80...");
    LuxLogDataRawSection3 raw;
    memcpy(&raw, data_ptr, sizeof(LuxLogDataRawSection3));

    publish_state_("max_charge_current", raw.max_chg_curr / 100.0f);
    publish_state_("max_discharge_current", raw.max_dischg_curr / 100.0f);
    publish_state_("charge_voltage_ref", raw.charge_volt_ref / 10.0f);
    publish_state_("discharge_cutoff_voltage", raw.dischg_cut_volt / 10.0f);
    publish_state_("battery_current", raw.bat_current / 100.0f);
    publish_state_("battery_count", (float)raw.bat_count);
    publish_state_("battery_capacity", (float)raw.bat_capacity);
    publish_state_("battery_status_inv", (float)raw.bat_status_inv);
    publish_state_("max_cell_voltage", raw.max_cell_volt / 1000.0f);
    publish_state_("min_cell_voltage", raw.min_cell_volt / 1000.0f);
    publish_state_("max_cell_temp", raw.max_cell_temp / 10.0f);
    publish_state_("min_cell_temp", raw.min_cell_temp / 10.0f);
    publish_state_("cycle_count", (float)raw.bat_cycle_count);
  } else {
    ESP_LOGW(TAG, "Unrecognized register %d or insufficient data length %d for parsing", trans.registerStart, data_payload_length);
    return;
  }

  if (this->next_bank_to_request_ == 0) this->next_bank_to_request_ = 40;
  else if (this->next_bank_to_request_ == 40) this->next_bank_to_request_ = 80;
  else this->next_bank_to_request_ = 0;
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

void LuxpowerSNAComponent::publish_state_(const std::string &key, float value) {
  auto it = this->float_sensors_.find(key);
  if (it != this->float_sensors_.end()) {
    it->second->publish_state(value);
  }
}

void LuxpowerSNAComponent::publish_state_(const std::string &key, const std::string &value) {
  auto it = this->string_sensors_.find(key);
  if (it != this->string_sensors_.end()) {
    it->second->publish_state(value);
  }
}

}  // namespace luxpower_sna
}  // namespace esphome
