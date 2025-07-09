#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// Helper to get a 16-bit unsigned value from a big-endian byte array
static uint16_t get_u16(const uint8_t *ptr) { return (ptr[0] << 8) | ptr[1]; }
// Helper to get a 16-bit signed value from a big-endian byte array
static int16_t get_s16(const uint8_t *ptr) { return (int16_t) get_u16(ptr); }

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Luxpower SNA...");
  this->last_poll_ = -this->get_update_interval(); // Poll immediately on first run
}

void LuxpowerSNAComponent::on_shutdown() {
  if (this->socket_ != nullptr) {
    this->socket_->close();
    this->socket_ = nullptr;
  }
}

float LuxpowerSNAComponent::get_setup_priority() const { return setup_priority::AFTER_WIFI; }

void LuxpowerSNAComponent::update() {
  // Check connection and poll interval
  if (this->socket_ != nullptr && !this->socket_->is_connected()) {
    ESP_LOGW(TAG, "Connection lost. Closing socket.");
    this->socket_->close();
    this->socket_ = nullptr;
  }

  if (this->socket_ == nullptr) {
    ESP_LOGI(TAG, "Connecting to %s:%d", this->address_.c_str(), this->port_);
    this->socket_ = socket::Socket::create_tcp(this->address_, this->port_);
    if (this->socket_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create socket.");
        return;
    }
  }

  if (millis() - this->last_poll_ > this->get_update_interval()) {
    this->last_poll_ = millis();
    this->request_data_();
  }

  // Read data if available
  if (this->socket_->is_connected() && this->socket_->available() > 0) {
    uint8_t raw[256];
    int len = this->socket_->read(raw, sizeof(raw));
    if (len > 0) {
      this->parse_lux_packet_(raw, len);
    }
  }
}

void LuxpowerSNAComponent::request_data_() {
  if (this->socket_ == nullptr || !this->socket_->is_connected()) {
    ESP_LOGW(TAG, "Not connected, cannot send request.");
    return;
  }

  uint8_t request[29];
  request[0] = 0xAA; request[1] = 0x55;      // Header
  request[2] = 0x01; request[3] = 0x1A;      // Protocol & Frame Length
  request[4] = 0x01; request[5] = 0x02;      // Command: Request All Data
  request[6] = 20;                          // Data Length
  memcpy(request + 7, this->dongle_serial_.data(), 10);
  memcpy(request + 17, this->inverter_serial_.data(), 10);
  
  uint16_t crc = this->calculate_crc_(request + 2, 25);
  request[27] = crc & 0xFF;
  request[28] = (crc >> 8) & 0xFF;

  this->socket_->write(request, sizeof(request));
  ESP_LOGD(TAG, "Data request sent.");
}

void LuxpowerSNAComponent::parse_lux_packet_(const uint8_t *raw, uint32_t len) {
  ESP_LOGD(TAG, "Received %d bytes", len);
  // The SNA response packet is a direct dump of the input registers.
  // The data payload starts after a 20-byte header.
  if (len < 175) { // Need at least enough data for Pload (reg 170)
    ESP_LOGW(TAG, "Packet too short to parse: %d bytes", len);
    return;
  }

  const uint8_t *payload = raw + 20; // Start of the register data
  uint16_t data_len = (raw[18] | (raw[19] << 8));
  
  // Validate CRC on the data payload
  uint16_t received_crc = (raw[20 + data_len + 1] << 8) | raw[20 + data_len];
  uint16_t calculated_crc = this->calculate_crc_(payload, data_len);

  if (calculated_crc != received_crc) {
    ESP_LOGW(TAG, "Invalid CRC. Expected=%04X, Calculated=%04X", received_crc, calculated_crc);
    return;
  }

  // --- PARSE SENSORS BASED ON PDF REGISTER MAP ---
  // Register addresses are 0-based. Offset = Register Address * 2.

  // Reg 0: Status
  uint16_t status_code = get_u16(payload + 0);
  this->publish_state_if_changed_(this->status_code_sensor_, status_code);
  this->publish_state_if_changed_(this->status_text_sensor_, this->get_status_text_(status_code));

  // Reg 1 & 2: PV Voltages
  this->publish_state_if_changed_(this->pv1_voltage_sensor_, get_u16(payload + 2) / 10.0f);
  this->publish_state_if_changed_(this->pv2_voltage_sensor_, get_u16(payload + 4) / 10.0f);

  // Reg 4 & 5: Battery
  this->publish_state_if_changed_(this->battery_voltage_sensor_, get_u16(payload + 8) / 10.0f);
  // SOC is the low byte of register 5
  this->publish_state_if_changed_(this->soc_sensor_, (payload[11] & 0xFF)); 

  // Reg 7 & 8: PV Powers
  float p_pv1 = get_u16(payload + 14);
  float p_pv2 = get_u16(payload + 16);
  this->publish_state_if_changed_(this->pv1_power_sensor_, p_pv1);
  this->publish_state_if_changed_(this->pv2_power_sensor_, p_pv2);
  this->publish_state_if_changed_(this->pv_power_sensor_, p_pv1 + p_pv2);

  // Reg 10 & 11: Battery Charge/Discharge Power
  float p_charge = get_u16(payload + 20);
  float p_discharge = get_u16(payload + 22);
  this->publish_state_if_changed_(this->charge_power_sensor_, p_charge);
  this->publish_state_if_changed_(this->discharge_power_sensor_, p_discharge);
  // Battery power: positive is discharge, negative is charge
  this->publish_state_if_changed_(this->battery_power_sensor_, p_discharge - p_charge);

  // Reg 12 & 15: Grid Voltage and Frequency
  this->publish_state_if_changed_(this->grid_voltage_sensor_, get_u16(payload + 24) / 10.0f);
  this->publish_state_if_changed_(this->grid_frequency_sensor_, get_u16(payload + 30) / 100.0f);

  // Reg 16: Inverter Power (On-grid)
  this->publish_state_if_changed_(this->inverter_power_sensor_, get_s16(payload + 32));

  // Reg 19: Power Factor
  float pf_raw = get_u16(payload + 38);
  float pf = 0.0f;
  if (pf_raw <= 1000) {
      pf = pf_raw / 1000.0f;
  } else if (pf_raw > 1000 && pf_raw <= 2000) {
      pf = (1000.0f - pf_raw) / 1000.0f; // Leading
  }
  this->publish_state_if_changed_(this->power_factor_sensor_, pf);

  // Reg 20, 23, 24: EPS Voltage, Frequency, Power
  this->publish_state_if_changed_(this->eps_voltage_sensor_, get_u16(payload + 40) / 10.0f);
  this->publish_state_if_changed_(this->eps_frequency_sensor_, get_u16(payload + 46) / 100.0f);
  this->publish_state_if_changed_(this->eps_power_sensor_, get_u16(payload + 48));

  // Reg 26 & 27: Grid Power (Export/Import)
  // Ptogrid is signed, positive is export. Ptouser is import.
  // We define Grid Power as positive for import, negative for export.
  float p_to_grid = get_s16(payload + 52);
  float p_to_user = get_u16(payload + 54);
  this->publish_state_if_changed_(this->grid_power_sensor_, p_to_user - p_to_grid);

  // --- Daily Energy ---
  // Reg 28 & 29: PV Energy Today
  float e_pv1_day = get_u16(payload + 56) / 10.0f;
  float e_pv2_day = get_u16(payload + 58) / 10.0f;
  this->publish_state_if_changed_(this->pv_today_sensor_, e_pv1_day + e_pv2_day);

  // Reg 31: Inverter Energy Today
  this->publish_state_if_changed_(this->inverter_today_sensor_, get_u16(payload + 62) / 10.0f);
  // Reg 33 & 34: Battery Energy Today
  this->publish_state_if_changed_(this->charge_today_sensor_, get_u16(payload + 66) / 10.0f);
  this->publish_state_if_changed_(this->discharge_today_sensor_, get_u16(payload + 68) / 10.0f);
  // Reg 35: EPS Energy Today
  this->publish_state_if_changed_(this->eps_today_sensor_, get_u16(payload + 70) / 10.0f);
  // Reg 36 & 37: Grid Energy Today
  this->publish_state_if_changed_(this->grid_export_today_sensor_, get_u16(payload + 72) / 10.0f);
  this->publish_state_if_changed_(this->grid_import_today_sensor_, get_u16(payload + 74) / 10.0f);

  // --- Temperatures ---
  // Reg 64, 65, 67
  this->publish_state_if_changed_(this->inverter_temp_sensor_, get_s16(payload + 128));
  this->publish_state_if_changed_(this->radiator_temp_sensor_, get_s16(payload + 130));
  this->publish_state_if_changed_(this->battery_temp_sensor_, get_s16(payload + 134));

  // --- ACCURATE LOAD POWER ---
  // Reg 170: Pload
  this->publish_state_if_changed_(this->load_power_sensor_, get_u16(payload + 340));
  // Reg 171: Eload_day
  this->publish_state_if_changed_(this->load_today_sensor_, get_u16(payload + 342) / 10.0f);
}

const char *LuxpowerSNAComponent::get_status_text_(uint16_t status_code) {
  switch (status_code) {
    case 0x00: return "Standby";
    case 0x01: return "Fault";
    case 0x02: return "Programming";
    case 0x04: return "PV On-Grid";
    case 0x08: return "PV Charging";
    case 0x0C: return "PV Charging & On-Grid";
    case 0x10: return "Battery On-Grid";
    case 0x14: return "PV & Battery On-Grid";
    case 0x20: return "AC Charging";
    case 0x28: return "PV & AC Charging";
    case 0x40: return "Battery Off-Grid";
    case 0x60: return "AC Coupled Charging Off-Grid";
    case 0x80: return "PV Off-Grid (Unstable)";
    case 0xC0: return "PV & Battery Off-Grid";
    case 0x88: return "PV Charging & Off-Grid";
    default: return "Unknown";
  }
}

uint16_t LuxpowerSNAComponent::calculate_crc_(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

}  // namespace luxpower_sna
}  // namespace esphome
