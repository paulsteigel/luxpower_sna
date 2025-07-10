#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// Helper to publish to a regular sensor
void LuxpowerSNAComponent::publish_state_(const std::string &key, float value) {
    if (this->sensors_.count(key)) {
        auto *sensor = (sensor::Sensor *)this->sensors_[key];
        sensor->publish_state(value);
    }
}

// Helper to publish to a text sensor
void LuxpowerSNAComponent::publish_state_(const std::string &key, const std::string &value) {
    if (this->sensors_.count(key)) {
        auto *text_sensor = (text_sensor::TextSensor *)this->sensors_[key];
        text_sensor->publish_state(value);
    }
}

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxpowerSNA Hub...");
}

void LuxpowerSNAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "LuxpowerSNA Hub:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%d", this->host_.c_str(), this->port_);
  ESP_LOGCONFIG(TAG, "  Dongle S/N: %s", this->dongle_serial_.c_str());
  ESP_LOGCONFIG(TAG, "  Inverter S/N: %s", this->inverter_serial_.c_str());
  LOG_UPDATE_INTERVAL(this);
  
  ESP_LOGCONFIG(TAG, "  Configured Sensors:");
  for (auto const& [key, val] : this->sensors_) {
    ESP_LOGCONFIG(TAG, "    - %s", key.c_str());
  }
}

// This is called by the PollingComponent scheduler.
void LuxpowerSNAComponent::update() {
  // It will trigger a request for the next bank in the sequence.
  this->request_bank_(this->next_bank_to_request_);
  
  // Cycle to the next bank for the subsequent poll.
  this->next_bank_to_request_ = (this->next_bank_to_request_ == 80) ? 0 : this->next_bank_to_request_ + 40;
}

// CRC-16 (Modbus) calculation, identical to your Arduino sketch
uint16_t LuxpowerSNAComponent::calculate_crc_(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; ++i)
      crc = (crc >> 1) ^ (crc & 1 ? 0xA001 : 0);
  }
  return crc;
}

void LuxpowerSNAComponent::request_bank_(uint8_t bank) {
  if (this->tcp_client_ != nullptr) {
    ESP_LOGD(TAG, "Request for bank %d skipped, connection already in progress.", bank);
    return;
  }
  
  ESP_LOGD(TAG, "Connecting to %s:%d to request bank %d", this->host_.c_str(), this->port_, bank);
  this->tcp_client_ = new AsyncClient();

  // Set a 15-second timeout for the entire connection process
  this->tcp_client_->setRxTimeout(15000);

  this->tcp_client_->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
    ESP_LOGD(TAG, "Received %d bytes of data.", len);
    this->handle_response_((uint8_t*)data, len);
    client->close();
  }, nullptr);

  this->tcp_client_->onConnect([this, bank](void *arg, AsyncClient *client) {
    ESP_LOGD(TAG, "Successfully connected. Sending request for bank %d...", bank);
    
    // --- Build the request packet (matches your Arduino sketch) ---
    uint8_t pkt[29] = {0xAA, 0x55, 0x12, 0x00, 0x01, 0xC2, 0x14};
    memcpy(pkt + 7, this->dongle_serial_.data(), 10);
    pkt[17] = bank; // The register bank to read (0, 40, or 80)
    pkt[18] = 0x00;
    memcpy(pkt + 19, this->inverter_serial_.data(), 10);
    uint16_t crc = this->calculate_crc_(pkt + 2, 25);
    pkt[27] = crc & 0xFF;
    pkt[28] = crc >> 8;
    
    ESP_LOGD(TAG, "Sending data request (29 bytes)...");
    client->write((const char*)pkt, 29);
  }, nullptr);

  auto cleanup = [this]() {
    if (this->tcp_client_ != nullptr) {
      delete this->tcp_client_;
      this->tcp_client_ = nullptr;
    }
  };

  this->tcp_client_->onError([this, cleanup](void *arg, AsyncClient *client, int8_t error) {
    ESP_LOGW(TAG, "Connection error: %s", client->errorToString(error));
    cleanup();
  }, nullptr);

  this->tcp_client_->onTimeout([this, cleanup](void *arg, AsyncClient *client, uint32_t time) {
    ESP_LOGW(TAG, "Connection timeout after %d ms", time);
    cleanup();
  }, nullptr);

  this->tcp_client_->onDisconnect([this, cleanup](void *arg, AsyncClient *client) {
    ESP_LOGD(TAG, "Disconnected from host.");
    cleanup();
  }, nullptr);

  if (!this->tcp_client_->connect(this->host_.c_str(), this->port_)) {
    ESP_LOGW(TAG, "Failed to initiate connection.");
    cleanup();
  }
}

void LuxpowerSNAComponent::handle_response_(const uint8_t *buffer, size_t length) {
  const uint16_t RESPONSE_HEADER_SIZE = sizeof(LuxHeader) + sizeof(LuxTranslatedData);
  if (length < RESPONSE_HEADER_SIZE) {
    ESP_LOGW(TAG, "Packet too small for headers (%d bytes)", length);
    return;
  }

  LuxHeader header;
  LuxTranslatedData trans;
  memcpy(&header, buffer, sizeof(LuxHeader));
  memcpy(&trans, buffer + sizeof(LuxHeader), sizeof(LuxTranslatedData));

  // Validate packet headers
  if (header.prefix != 0x55AA || header.function != 0xC2 || trans.deviceFunction != 0x04) {
    ESP_LOGW(TAG, "Invalid packet header/function. Prefix: 0x%04X, Func: 0x%02X, DevFunc: 0x%02X",
             header.prefix, header.function, trans.deviceFunction);
    return;
  }

  // Publish inverter serial number from the response
  std::string inv_serial(trans.serialNumber, 10);
  this->publish_state_("inverter_serial", inv_serial);

  ESP_LOGI(TAG, "Decoding response for Inverter S/N: %s, Register Bank: %d",
           inv_serial.c_str(), trans.registerStart);

  uint16_t data_offset = RESPONSE_HEADER_SIZE;
  
  // --- Decode based on the register bank received ---
  if (trans.registerStart == 0 && length >= data_offset + sizeof(LuxLogDataRawSection1)) {
    LuxLogDataRawSection1 raw;
    memcpy(&raw, buffer + data_offset, sizeof(LuxLogDataRawSection1));
    
    // Real-time values
    publish_state_("pv1_voltage", raw.pv1_voltage / 10.0f);
    publish_state_("pv2_voltage", raw.pv2_voltage / 10.0f);
    publish_state_("pv3_voltage", raw.pv3_voltage / 10.0f);
    publish_state_("battery_voltage", raw.battery_voltage / 10.0f);
    publish_state_("soc", raw.soc);
    publish_state_("soh", raw.soh);
    publish_state_("pv1_power", raw.pv1_power);
    publish_state_("pv2_power", raw.pv2_power);
    publish_state_("pv3_power", raw.pv3_power);
    publish_state_("charge_power", raw.charge_power);
    publish_state_("discharge_power", raw.discharge_power);
    publish_state_("inverter_power", raw.activeInverter_power);
    publish_state_("power_to_grid", raw.power_to_grid);
    publish_state_("power_from_grid", raw.power_from_grid);
    publish_state_("grid_voltage_r", raw.voltage_ac_r / 10.0f);
    publish_state_("grid_voltage_s", raw.voltage_ac_s / 10.0f);
    publish_state_("grid_voltage_t", raw.voltage_ac_t / 10.0f);
    publish_state_("grid_frequency", raw.frequency_grid / 100.0f);
    publish_state_("power_factor", raw.grid_power_factor / 1000.0f);
    publish_state_("eps_voltage_r", raw.voltage_eps_r / 10.0f);
    publish_state_("eps_voltage_s", raw.voltage_eps_s / 10.0f);
    publish_state_("eps_voltage_t", raw.voltage_eps_t / 10.0f);
    publish_state_("eps_frequency", raw.frequency_eps / 100.0f);
    publish_state_("eps_active_power", raw.active_eps_power);
    publish_state_("eps_apparent_power", raw.apparent_eps_power);
    publish_state_("bus1_voltage", raw.bus1_voltage / 10.0f);
    publish_state_("bus2_voltage", raw.bus2_voltage / 10.0f);

    // Daily energy
    publish_state_("pv1_energy_today", raw.pv1_energy_today / 100.0f);
    publish_state_("pv2_energy_today", raw.pv2_energy_today / 100.0f);
    publish_state_("pv3_energy_today", raw.pv3_energy_today / 100.0f);
    publish_state_("inverter_energy_today", raw.activeInverter_energy_today / 100.0f);
    publish_state_("ac_charging_today", raw.ac_charging_today / 100.0f);
    publish_state_("charging_today", raw.charging_today / 100.0f);
    publish_state_("discharging_today", raw.discharging_today / 100.0f);
    publish_state_("eps_today", raw.eps_today / 100.0f);
    publish_state_("exported_today", raw.exported_today / 100.0f);
    publish_state_("grid_today", raw.grid_today / 100.0f);

  } else if (trans.registerStart == 40 && length >= data_offset + sizeof(LuxLogDataRawSection2)) {
    LuxLogDataRawSection2 raw;
    memcpy(&raw, buffer + data_offset, sizeof(LuxLogDataRawSection2));

    // Total Energy
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

    // Temperatures & Uptime
    publish_state_("temp_inner", raw.t_inner / 10.0f);
    publish_state_("temp_radiator", raw.t_rad_1 / 10.0f);
    publish_state_("temp_radiator2", raw.t_rad_2 / 10.0f);
    publish_state_("temp_battery", raw.t_bat / 10.0f);
    publish_state_("uptime", (float)raw.uptime);

  } else if (trans.registerStart == 80 && length >= data_offset + sizeof(LuxLogDataRawSection3)) {
    LuxLogDataRawSection3 raw;
    memcpy(&raw, buffer + data_offset, sizeof(LuxLogDataRawSection3));

    // BMS Details
    publish_state_("max_charge_current", raw.max_chg_curr / 100.0f);
    publish_state_("max_discharge_current", raw.max_dischg_curr / 100.0f);
    publish_state_("charge_voltage_ref", raw.charge_volt_ref / 10.0f);
    publish_state_("discharge_cutoff_voltage", raw.dischg_cut_volt / 10.0f);
    publish_state_("battery_current", raw.bat_current / 100.0f);
    publish_state_("battery_count", raw.bat_count);
    publish_state_("battery_capacity", raw.bat_capacity);
    publish_state_("battery_status_inv", raw.bat_status_inv);
    publish_state_("max_cell_voltage", raw.max_cell_volt / 1000.0f);
    publish_state_("min_cell_voltage", raw.min_cell_volt / 1000.0f);
    publish_state_("max_cell_temp", raw.max_cell_temp / 10.0f);
    publish_state_("min_cell_temp", raw.min_cell_temp / 10.0f);
    publish_state_("cycle_count", raw.bat_cycle_count);

  } else {
    ESP_LOGW(TAG, "Received packet for unknown bank %d or length mismatch. Length: %d", trans.registerStart, length);
  }
}

}  // namespace luxpower_sna
}  // namespace esphome
