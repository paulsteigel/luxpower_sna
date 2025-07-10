#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include <cstring>

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// --- Helper to log byte arrays in HEX format ---
void log_hex_buffer(const uint8_t *buffer, size_t len) {
  if (len == 0) {
    return;
  }
  char str[len * 3 + 1];
  for (size_t i = 0; i < len; i++) {
    sprintf(str + i * 3, "%02X ", buffer[i]);
  }
  str[len * 3 - 1] = '\0'; // Remove last space
  ESP_LOGD(TAG, "%s", str);
}


// --- Byte Swap Helpers (Inverter is Big Endian, ESP is Little Endian) ---
uint16_t LuxpowerSNAComponent::swap_uint16(uint16_t val) { return (val << 8) | (val >> 8); }
uint32_t LuxpowerSNAComponent::swap_uint32(uint32_t val) {
  return ((val << 24) & 0xFF000000) | ((val << 8) & 0x00FF0000) | ((val >> 8) & 0x0000FF00) | ((val >> 24) & 0x000000FF);
}

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxpowerSNA...");
  this->tcp_client_ = new AsyncClient();
  // Set a 15 second timeout for all operations
  this->tcp_client_->setRxTimeout(15000);

  this->tcp_client_->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
    ESP_LOGD(TAG, "<- Received %d bytes of data:", len);
    log_hex_buffer(static_cast<uint8_t *>(data), len);
    this->handle_response_(static_cast<uint8_t *>(data), len);
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

  ESP_LOGD(TAG, "-> Sending Request (%d bytes):", sizeof(pkt));
  log_hex_buffer(pkt, sizeof(pkt));

  if (this->tcp_client_->space() > sizeof(pkt)) {
    this->tcp_client_->add(reinterpret_cast<const char *>(pkt), sizeof(pkt));
    this->tcp_client_->send();
  } else {
    ESP_LOGW(TAG, "Not enough space in TCP buffer to send request.");
    this->tcp_client_->close();
  }
}

void LuxpowerSNAComponent::handle_response_(const uint8_t *buffer, size_t length) {
  const uint16_t RESPONSE_HEADER_SIZE = sizeof(LuxHeader) + sizeof(LuxTranslatedData);
  if (length < RESPONSE_HEADER_SIZE) {
    ESP_LOGW(TAG, "Packet too small for headers. Length: %d", length);
    return;
  }

  LuxHeader header;
  LuxTranslatedData trans;
  memcpy(&header, buffer, sizeof(LuxHeader));
  memcpy(&trans, buffer + sizeof(LuxHeader), sizeof(LuxTranslatedData));

  uint16_t prefix_swapped = swap_uint16(header.prefix);
  uint16_t reg_start_swapped = swap_uint16(trans.registerStart);

  // *********************************************************************************
  // *** CRITICAL FIX: Check for 0x1AA1 as per your working Arduino project      ***
  // *********************************************************************************
  if (prefix_swapped != 0x1AA1 || header.function != 0xC2 || trans.deviceFunction != 0x04) {
    ESP_LOGW(TAG, "Invalid header/function. Prefix: 0x%04X, Func: 0x%02X, DevFunc: 0x%02X", prefix_swapped, header.function, trans.deviceFunction);
    return;
  }
  
  ESP_LOGD(TAG, "✓ Decoded Header OK. Register Bank: %d", reg_start_swapped);

  static bool serial_published = false;
  if (!serial_published) {
    std::string inv_serial(trans.serialNumber, 10);
    publish_state_("inverter_serial", inv_serial);
    serial_published = true;
  }

  const uint8_t *data_ptr = buffer + RESPONSE_HEADER_SIZE;
  size_t data_payload_length = length - RESPONSE_HEADER_SIZE - 2; // Subtract 2 for CRC

  if (reg_start_swapped == 0 && data_payload_length >= sizeof(LuxLogDataRawSection1)) {
    ESP_LOGD(TAG, "Parsing data for bank 0...");
    const auto *raw = reinterpret_cast<const LuxLogDataRawSection1 *>(data_ptr);
    publish_state_("pv1_voltage", swap_uint16(raw->pv1_voltage) / 10.0f);
    publish_state_("pv2_voltage", swap_uint16(raw->pv2_voltage) / 10.0f);
    publish_state_("pv3_voltage", swap_uint16(raw->pv3_voltage) / 10.0f);
    publish_state_("battery_voltage", swap_uint16(raw->battery_voltage) / 10.0f);
    publish_state_("soc", (float)raw->soc);
    publish_state_("soh", (float)raw->soh);
    publish_state_("pv1_power", (float)swap_uint16(raw->pv1_power));
    publish_state_("pv2_power", (float)swap_uint16(raw->pv2_power));
    publish_state_("pv3_power", (float)swap_uint16(raw->pv3_power));
    publish_state_("charge_power", (float)swap_uint16(raw->charge_power));
    publish_state_("discharge_power", (float)swap_uint16(raw->discharge_power));
    publish_state_("inverter_power", (float)swap_uint16(raw->activeInverter_power));
    publish_state_("power_to_grid", (float)swap_uint16(raw->power_to_grid));
    publish_state_("power_from_grid", (float)swap_uint16(raw->power_from_grid));
    publish_state_("grid_voltage_r", swap_uint16(raw->voltage_ac_r) / 10.0f);
    publish_state_("grid_voltage_s", swap_uint16(raw->voltage_ac_s) / 10.0f);
    publish_state_("grid_voltage_t", swap_uint16(raw->voltage_ac_t) / 10.0f);
    publish_state_("grid_frequency", swap_uint16(raw->frequency_grid) / 100.0f);
    publish_state_("power_factor", swap_uint16(raw->grid_power_factor) / 1000.0f);
    publish_state_("eps_voltage_r", swap_uint16(raw->voltage_eps_r) / 10.0f);
    publish_state_("eps_voltage_s", swap_uint16(raw->voltage_eps_s) / 10.0f);
    publish_state_("eps_voltage_t", swap_uint16(raw->voltage_eps_t) / 10.0f);
    publish_state_("eps_frequency", swap_uint16(raw->frequency_eps) / 100.0f);
    publish_state_("eps_active_power", (float)swap_uint16(raw->active_eps_power));
    publish_state_("eps_apparent_power", (float)swap_uint16(raw->apparent_eps_power));
    publish_state_("bus1_voltage", swap_uint16(raw->bus1_voltage) / 10.0f);
    publish_state_("bus2_voltage", swap_uint16(raw->bus2_voltage) / 10.0f);
    publish_state_("pv1_energy_today", swap_uint16(raw->pv1_energy_today) / 10.0f);
    publish_state_("pv2_energy_today", swap_uint16(raw->pv2_energy_today) / 10.0f);
    publish_state_("pv3_energy_today", swap_uint16(raw->pv3_energy_today) / 10.0f);
    publish_state_("inverter_energy_today", swap_uint16(raw->activeInverter_energy_today) / 10.0f);
    publish_state_("ac_charging_today", swap_uint16(raw->ac_charging_today) / 10.0f);
    publish_state_("charging_today", swap_uint16(raw->charging_today) / 10.0f);
    publish_state_("discharging_today", swap_uint16(raw->discharging_today) / 10.0f);
    publish_state_("eps_today", swap_uint16(raw->eps_today) / 10.0f);
    publish_state_("exported_today", swap_uint16(raw->exported_today) / 10.0f);
    publish_state_("grid_today", swap_uint16(raw->grid_today) / 10.0f);

  } else if (reg_start_swapped == 40 && data_payload_length >= sizeof(LuxLogDataRawSection2)) {
    ESP_LOGD(TAG, "Parsing data for bank 40...");
    const auto *raw = reinterpret_cast<const LuxLogDataRawSection2 *>(data_ptr);
    publish_state_("total_pv1_energy", swap_uint32(raw->e_pv_1_all) / 10.0f);
    publish_state_("total_pv2_energy", swap_uint32(raw->e_pv_2_all) / 10.0f);
    publish_state_("total_pv3_energy", swap_uint32(raw->e_pv_3_all) / 10.0f);
    publish_state_("total_inverter_output", swap_uint32(raw->e_inv_all) / 10.0f);
    publish_state_("total_recharge_energy", swap_uint32(raw->e_rec_all) / 10.0f);
    publish_state_("total_charged", swap_uint32(raw->e_chg_all) / 10.0f);
    publish_state_("total_discharged", swap_uint32(raw->e_dischg_all) / 10.0f);
    publish_state_("total_eps_energy", swap_uint32(raw->e_eps_all) / 10.0f);
    publish_state_("total_exported", swap_uint32(raw->e_to_grid_all) / 10.0f);
    publish_state_("total_imported", swap_uint32(raw->e_to_user_all) / 10.0f);
    publish_state_("temp_inner", swap_uint16(raw->t_inner) / 10.0f);
    publish_state_("temp_radiator", swap_uint16(raw->t_rad_1) / 10.0f);
    publish_state_("temp_radiator2", swap_uint16(raw->t_rad_2) / 10.0f);
    publish_state_("temp_battery", swap_uint16(raw->t_bat) / 10.0f);
    publish_state_("uptime", (float)swap_uint32(raw->uptime));

  } else if (reg_start_swapped == 80 && data_payload_length >= sizeof(LuxLogDataRawSection3)) {
    ESP_LOGD(TAG, "Parsing data for bank 80...");
    const auto *raw = reinterpret_cast<const LuxLogDataRawSection3 *>(data_ptr);
    publish_state_("max_charge_current", swap_uint16(raw->max_chg_curr) / 100.0f);
    publish_state_("max_discharge_current", swap_uint16(raw->max_dischg_curr) / 100.0f);
    publish_state_("charge_voltage_ref", swap_uint16(raw->charge_volt_ref) / 10.0f);
    publish_state_("discharge_cutoff_voltage", swap_uint16(raw->dischg_cut_volt) / 10.0f);
    publish_state_("battery_current", swap_uint16(raw->bat_current) / 100.0f);
    publish_state_("battery_count", (float)swap_uint16(raw->bat_count));
    publish_state_("battery_capacity", (float)swap_uint16(raw->bat_capacity));
    publish_state_("battery_status_inv", (float)swap_uint16(raw->bat_status_inv));
    publish_state_("max_cell_voltage", swap_uint16(raw->max_cell_volt) / 1000.0f);
    publish_state_("min_cell_voltage", swap_uint16(raw->min_cell_volt) / 1000.0f);
    publish_state_("max_cell_temp", swap_uint16(raw->max_cell_temp) / 10.0f);
    publish_state_("min_cell_temp", swap_uint16(raw->min_cell_temp) / 10.0f);
    publish_state_("cycle_count", (float)swap_uint16(raw->bat_cycle_count));
  } else {
    ESP_LOGW(TAG, "Unrecognized register %d or insufficient data length %d for parsing", reg_start_swapped, data_payload_length);
    return; // Do not advance to next bank if parsing failed
  }

  // Cycle to the next bank for the next successful update
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
  } else {
    ESP_LOGW(TAG, "Sensor with key '%s' not found for publishing.", key.c_str());
  }
}

void LuxpowerSNAComponent::publish_state_(const std::string &key, const std::string &value) {
  auto it = this->string_sensors_.find(key);
  if (it != this->string_sensors_.end()) {
    it->second->publish_state(value);
  } else {
    ESP_LOGW(TAG, "Text sensor with key '%s' not found for publishing.", key.c_str());
  }
}

}  // namespace luxpower_sna
}  // namespace esphome
