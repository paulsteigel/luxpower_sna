#include "luxpower_sna.h"
#include "esphome/core/log.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// ... (get_u16, get_s16, publish_state helpers remain the same as before)
static uint16_t get_u16(const uint8_t *ptr) { return (ptr[0] << 8) | ptr[1]; }
static int16_t get_s16(const uint8_t *ptr) { return (int16_t) get_u16(ptr); }
template<typename T>
void publish_state(T *sensor, float value) {
    if (sensor && (!sensor->has_state() || sensor->get_raw_state() != value)) {
      sensor->publish_state(value);
    }
}
void publish_state(text_sensor::TextSensor *sensor, const std::string &value) {
    if (sensor && (!sensor->has_state() || sensor->get_state() != value)) {
      sensor->publish_state(value);
    }
}


void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Luxpower SNA...");
}

void LuxpowerSNAComponent::on_shutdown() {
  if (this->socket_ != nullptr) {
    this->socket_->close();
  }
}

float LuxpowerSNAComponent::get_setup_priority() const { return setup_priority::AFTER_WIFI; }

void LuxpowerSNAComponent::update() {
  if (this->socket_ != nullptr && !this->socket_->is_connected()) {
    ESP_LOGW(TAG, "Connection lost. Closing socket.");
    this->socket_->close();
    this->socket_.reset();
  }

  if (this->socket_ == nullptr) {
    ESP_LOGD(TAG, "Connecting to %s:%d", this->host_.c_str(), this->port_);
    this->socket_ = socket::Socket::create_unique(this->host_, this->port_);
    if (this->socket_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create socket.");
        this->mark_failed();
        return;
    }
  }

  this->request_data_();

  uint8_t raw[512];
  int len = this->socket_->read_array(raw, sizeof(raw));
  if (len > 0) {
    this->parse_lux_packet_(raw, len);
  } else if (len < 0) {
    ESP_LOGW(TAG, "Socket read error: %d", len);
    this->socket_->close();
    this->socket_.reset();
    this->status_set_warning();
  }
}

// ... (request_data_, parse_lux_packet_, get_status_text_, calculate_crc_ remain the same as before)
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
  if (len < 20) {
    ESP_LOGW(TAG, "Packet too short for header: %d bytes", len);
    return;
  }
  
  const uint8_t *payload = raw + 20;
  uint16_t data_len = (raw[18] | (raw[19] << 8));
  
  if (len < 20 + data_len + 2) {
    ESP_LOGW(TAG, "Packet shorter than expected. Len: %d, Expected: %d", len, 20 + data_len + 2);
    return;
  }
  
  uint16_t received_crc = (raw[20 + data_len + 1] << 8) | raw[20 + data_len];
  uint16_t calculated_crc = this->calculate_crc_(payload, data_len);

  if (calculated_crc != received_crc) {
    ESP_LOGW(TAG, "Invalid CRC. Expected=%04X, Calculated=%04X", received_crc, calculated_crc);
    return;
  }
  ESP_LOGD(TAG, "CRC OK");
  this->status_clear_warning();

  // --- PARSE SENSORS ---
  uint16_t status_code = get_u16(payload + 0);
  publish_state(this->status_code_sensor_, (float)status_code);
  publish_state(this->status_text_sensor_, this->get_status_text_(status_code));
  publish_state(this->pv1_voltage_sensor_, get_u16(payload + 2) / 10.0f);
  publish_state(this->pv2_voltage_sensor_, get_u16(payload + 4) / 10.0f);
  publish_state(this->battery_voltage_sensor_, get_u16(payload + 8) / 10.0f);
  publish_state(this->soc_sensor_, (float)(payload[11] & 0xFF)); 
  float p_pv1 = get_u16(payload + 14);
  float p_pv2 = get_u16(payload + 16);
  publish_state(this->pv1_power_sensor_, p_pv1);
  publish_state(this->pv2_power_sensor_, p_pv2);
  publish_state(this->pv_power_sensor_, p_pv1 + p_pv2);
  float p_charge = get_u16(payload + 20);
  float p_discharge = get_u16(payload + 22);
  publish_state(this->charge_power_sensor_, p_charge);
  publish_state(this->discharge_power_sensor_, p_discharge);
  publish_state(this->battery_power_sensor_, p_discharge - p_charge);
  publish_state(this->grid_voltage_sensor_, get_u16(payload + 24) / 10.0f);
  publish_state(this->grid_frequency_sensor_, get_u16(payload + 30) / 100.0f);
  publish_state(this->inverter_power_sensor_, (float)get_s16(payload + 32));
  float pf_raw = get_u16(payload + 38);
  float pf = (pf_raw <= 1000) ? (pf_raw / 1000.0f) : ((1000.0f - pf_raw) / 1000.0f);
  publish_state(this->power_factor_sensor_, pf);
  publish_state(this->eps_voltage_sensor_, get_u16(payload + 40) / 10.0f);
  publish_state(this->eps_frequency_sensor_, get_u16(payload + 46) / 100.0f);
  publish_state(this->eps_power_sensor_, (float)get_u16(payload + 48));
  float p_to_grid = get_s16(payload + 52);
  float p_to_user = get_u16(payload + 54);
  publish_state(this->grid_power_sensor_, p_to_user - p_to_grid);
  float e_pv1_day = get_u16(payload + 56) / 10.0f;
  float e_pv2_day = get_u16(payload + 58) / 10.0f;
  publish_state(this->pv_today_sensor_, e_pv1_day + e_pv2_day);
  publish_state(this->inverter_today_sensor_, get_u16(payload + 62) / 10.0f);
  publish_state(this->charge_today_sensor_, get_u16(payload + 66) / 10.0f);
  publish_state(this->discharge_today_sensor_, get_u16(payload + 68) / 10.0f);
  publish_state(this->eps_today_sensor_, get_u16(payload + 70) / 10.0f);
  publish_state(this->grid_export_today_sensor_, get_u16(payload + 72) / 10.0f);
  publish_state(this->grid_import_today_sensor_, get_u16(payload + 74) / 10.0f);
  publish_state(this->inverter_temp_sensor_, (float)get_s16(payload + 128));
  publish_state(this->radiator_temp_sensor_, (float)get_s16(payload + 130));
  publish_state(this->battery_temp_sensor_, (float)get_s16(payload + 134));
  publish_state(this->load_power_sensor_, (float)get_u16(payload + 340));
  publish_state(this->load_today_sensor_, get_u16(payload + 342) / 10.0f);
}

const char *LuxpowerSNAComponent::get_status_text_(uint16_t status_code) {
  switch (status_code) {
    case 0x00: return "Standby"; case 0x01: return "Fault"; case 0x02: return "Programming";
    case 0x04: return "PV On-Grid"; case 0x08: return "PV Charging"; case 0x0C: return "PV Charging & On-Grid";
    case 0x10: return "Battery On-Grid"; case 0x14: return "PV & Battery On-Grid"; case 0x20: return "AC Charging";
    case 0x28: return "PV & AC Charging"; case 0x40: return "Battery Off-Grid"; case 0x60: return "AC Coupled Charging Off-Grid";
    case 0x80: return "PV Off-Grid (Unstable)"; case 0xC0: return "PV & Battery Off-Grid"; case 0x88: return "PV Charging & Off-Grid";
    default: return "Unknown";
  }
}

uint16_t LuxpowerSNAComponent::calculate_crc_(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001) { crc = (crc >> 1) ^ 0xA001; } else { crc >>= 1; }
    }
  }
  return crc;
}

}  // namespace luxpower_sna
}  // namespace esphome
