#include "luxpower_sna.h"
#include "esphome/core/log.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// --- HELPER FUNCTIONS FOR ENDIANNESS ---
// The inverter sends data in Big-Endian format. The ESP32 is Little-Endian.
// These functions swap the byte order to correctly interpret the numbers.

// Swaps a 16-bit unsigned integer (equivalent to ntohs)
uint16_t swap_uint16(uint16_t val) {
  return (val << 8) | (val >> 8);
}

// Swaps a 32-bit unsigned integer (equivalent to ntohl)
uint32_t swap_uint32(uint32_t val) {
  return ((val << 24) & 0xFF000000) |
         ((val <<  8) & 0x00FF0000) |
         ((val >>  8) & 0x0000FF00) |
         ((val >> 24) & 0x000000FF);
}


void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxpowerSNA...");
  this->tcp_client_ = new AsyncClient();

  // --- TCP Client Event Handlers ---
  this->tcp_client_->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
    ESP_LOGD(TAG, "Received %d bytes of data.", len);
    this->handle_response_(static_cast<uint8_t *>(data), len);
    client->close();
  });

  this->tcp_client_->onConnect([this](void *arg, AsyncClient *client) {
    ESP_LOGD(TAG, "Successfully connected. Sending request for bank %d...", this->next_bank_to_request_);
    this->request_bank_(this->next_bank_to_request_);
  });

  this->tcp_client_->onError([this](void *arg, AsyncClient *client, int8_t error) {
    ESP_LOGW(TAG, "Connection error: %s", client->errorToString(error));
  });

  this->tcp_client_->onDisconnect([this](void *arg, AsyncClient *client) {
    ESP_LOGD(TAG, "Disconnected from host.");
  });

  this->tcp_client_->onTimeout([this](void *arg, AsyncClient *client, uint32_t time) {
    ESP_LOGW(TAG, "Connection timeout.");
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
  ESP_LOGD(TAG, "Connecting to %s:%u to request bank %d", this->host_.c_str(), this->port_, this->next_bank_to_request_);
  this->tcp_client_->connect(this->host_.c_str(), this->port_);
}

void LuxpowerSNAComponent::request_bank_(uint8_t bank) {
  // This is the known-good request format for Luxpower inverters
  uint8_t request_packet[29] = {
    0x55, 0xAA, 0x01, 0x00, 0x1D, 0x00, 0x01, 0x10, 
    'S', 'E', 'R', 'I', 'A', 'L', 'D', 'O', 'N', 'G', // Placeholder for Dongle Serial (10 bytes)
    0x05, 0x00, // Data Length (5)
    0x02,       // Device Function
    0x00,       // Register Start
    0x28, 0x00, // Number of registers (40)
    0x00, 0x00, 0x00, 0x00, 0x00 // Padding
  };

  // Populate the request with our specific data
  strncpy(reinterpret_cast<char*>(&request_packet[8]), this->dongle_serial_.c_str(), 10);
  request_packet[21] = bank; // Set the bank (0, 40, or 80)
  
  // Calculate CRC and append it to the packet
  uint16_t crc = this->calculate_crc_(request_packet, 27);
  request_packet[27] = crc & 0xFF;
  request_packet[28] = (crc >> 8) & 0xFF;

  ESP_LOGD(TAG, "Sending data request (%d bytes)...", sizeof(request_packet));
  if (this->tcp_client_->space() > sizeof(request_packet)) {
    this->tcp_client_->add(reinterpret_cast<const char *>(request_packet), sizeof(request_packet));
    this->tcp_client_->send();
  } else {
    ESP_LOGW(TAG, "Not enough space in TCP buffer to send request.");
    this->tcp_client_->close();
  }
}


void LuxpowerSNAComponent::handle_response_(const uint8_t *buffer, size_t length) {
  if (length < sizeof(LuxHeader) + sizeof(LuxTranslatedData)) {
    ESP_LOGW(TAG, "Received packet is too short. Length: %d", length);
    return;
  }

  const LuxHeader *header = reinterpret_cast<const LuxHeader *>(buffer);
  const LuxTranslatedData *translated_data = reinterpret_cast<const LuxTranslatedData *>(&buffer[sizeof(LuxHeader)]);
  
  if (header->prefix != 0xAA55 || header->function != 0xC2 || translated_data->deviceFunction != 0x04) {
    ESP_LOGW(TAG, "Invalid packet header/function. Prefix: 0x%X, Func: 0x%X, DevFunc: 0x%X",
             header->prefix, header->function, translated_data->deviceFunction);
    return;
  }

  // Publish inverter serial number once
  static bool serial_published = false;
  if (!serial_published) {
    std::string inv_serial(translated_data->serialNumber, 10);
    publish_state_("inverter_serial", inv_serial);
    serial_published = true;
  }

  uint16_t register_start = swap_uint16(translated_data->registerStart);
  const uint8_t *data_ptr = &buffer[sizeof(LuxHeader) + sizeof(LuxTranslatedData)];

  if (register_start == 0) {
    const LuxLogDataRawSection1 *data_sec1 = reinterpret_cast<const LuxLogDataRawSection1 *>(data_ptr);
    publish_state_("pv1_voltage", swap_uint16(data_sec1->pv1_voltage) / 10.0f);
    publish_state_("pv2_voltage", swap_uint16(data_sec1->pv2_voltage) / 10.0f);
    publish_state_("pv3_voltage", swap_uint16(data_sec1->pv3_voltage) / 10.0f);
    publish_state_("battery_voltage", swap_uint16(data_sec1->battery_voltage) / 10.0f);
    publish_state_("soc", data_sec1->soc);
    publish_state_("soh", data_sec1->soh);
    publish_state_("pv1_power", swap_uint16(data_sec1->pv1_power));
    publish_state_("pv2_power", swap_uint16(data_sec1->pv2_power));
    publish_state_("pv3_power", swap_uint16(data_sec1->pv3_power));
    publish_state_("charge_power", swap_uint16(data_sec1->charge_power));
    publish_state_("discharge_power", swap_uint16(data_sec1->discharge_power));
    publish_state_("inverter_power", swap_uint16(data_sec1->activeInverter_power));
    publish_state_("power_to_grid", swap_uint16(data_sec1->power_to_grid));
    publish_state_("power_from_grid", swap_uint16(data_sec1->power_from_grid));
    publish_state_("grid_voltage_r", swap_uint16(data_sec1->voltage_ac_r) / 10.0f);
    publish_state_("grid_frequency", swap_uint16(data_sec1->frequency_grid) / 100.0f);
    publish_state_("eps_active_power", swap_uint16(data_sec1->active_eps_power));
    publish_state_("bus1_voltage", swap_uint16(data_sec1->bus1_voltage) / 10.0f);
    publish_state_("pv1_energy_today", swap_uint16(data_sec1->pv1_energy_today) / 100.0f);
    publish_state_("pv2_energy_today", swap_uint16(data_sec1->pv2_energy_today) / 100.0f);
    publish_state_("pv3_energy_today", swap_uint16(data_sec1->pv3_energy_today) / 100.0f);
    publish_state_("inverter_energy_today", swap_uint16(data_sec1->activeInverter_energy_today) / 100.0f);
    publish_state_("charging_today", swap_uint16(data_sec1->charging_today) / 100.0f);
    publish_state_("discharging_today", swap_uint16(data_sec1->discharging_today) / 100.0f);
    publish_state_("eps_today", swap_uint16(data_sec1->eps_today) / 100.0f);
    publish_state_("exported_today", swap_uint16(data_sec1->exported_today) / 100.0f);
    publish_state_("grid_today", swap_uint16(data_sec1->grid_today) / 100.0f);

  } else if (register_start == 40) {
    const LuxLogDataRawSection2 *data_sec2 = reinterpret_cast<const LuxLogDataRawSection2 *>(data_ptr);
    publish_state_("total_pv1_energy", swap_uint32(data_sec2->e_pv_1_all) / 10.0f);
    publish_state_("total_pv2_energy", swap_uint32(data_sec2->e_pv_2_all) / 10.0f);
    publish_state_("total_pv3_energy", swap_uint32(data_sec2->e_pv_3_all) / 10.0f);
    publish_state_("total_inverter_output", swap_uint32(data_sec2->e_inv_all) / 10.0f);
    publish_state_("total_charged", swap_uint32(data_sec2->e_chg_all) / 10.0f);
    publish_state_("total_discharged", swap_uint32(data_sec2->e_dischg_all) / 10.0f);
    publish_state_("total_eps_energy", swap_uint32(data_sec2->e_eps_all) / 10.0f);
    publish_state_("total_exported", swap_uint32(data_sec2->e_to_grid_all) / 10.0f);
    publish_state_("total_imported", swap_uint32(data_sec2->e_to_user_all) / 10.0f);
    publish_state_("temp_inner", swap_uint16(data_sec2->t_inner) / 10.0f);
    publish_state_("temp_radiator", swap_uint16(data_sec2->t_rad_1) / 10.0f);
    publish_state_("temp_battery", swap_uint16(data_sec2->t_bat) / 10.0f);
    publish_state_("uptime", swap_uint32(data_sec2->uptime));

  } else if (register_start == 80) {
    const LuxLogDataRawSection3 *data_sec3 = reinterpret_cast<const LuxLogDataRawSection3 *>(data_ptr);
    publish_state_("max_charge_current", swap_uint16(data_sec3->max_chg_curr) / 100.0f);
    publish_state_("max_discharge_current", swap_uint16(data_sec3->max_dischg_curr) / 100.0f);
    publish_state_("charge_voltage_ref", swap_uint16(data_sec3->charge_volt_ref) / 10.0f);
    publish_state_("discharge_cutoff_voltage", swap_uint16(data_sec3->dischg_cut_volt) / 10.0f);
    publish_state_("battery_current", swap_uint16(data_sec3->bat_current) / 100.0f);
    publish_state_("battery_count", swap_uint16(data_sec3->bat_count));
    publish_state_("battery_capacity", swap_uint16(data_sec3->bat_capacity));
    publish_state_("max_cell_voltage", swap_uint16(data_sec3->max_cell_volt) / 1000.0f);
    publish_state_("min_cell_voltage", swap_uint16(data_sec3->min_cell_volt) / 1000.0f);
    publish_state_("max_cell_temp", swap_uint16(data_sec3->max_cell_temp) / 10.0f);
    publish_state_("min_cell_temp", swap_uint16(data_sec3->min_cell_temp) / 10.0f);
    publish_state_("cycle_count", swap_uint16(data_sec3->bat_cycle_count));
  }

  // Cycle to the next bank for the next update
  if (this->next_bank_to_request_ == 0) {
    this->next_bank_to_request_ = 40;
  } else if (this->next_bank_to_request_ == 40) {
    this->next_bank_to_request_ = 80;
  } else {
    this->next_bank_to_request_ = 0;
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

void LuxpowerSNAComponent::publish_state_(const std::string &key, float value) {
  auto it = this->sensors_.find(key);
  if (it != this->sensors_.end()) {
    auto *sensor = dynamic_cast<sensor::Sensor*>(it->second);
    if (sensor) {
      sensor->publish_state(value);
    }
  }
}

void LuxpowerSNAComponent::publish_state_(const std::string &key, const std::string &value) {
  auto it = this->sensors_.find(key);
  if (it != this->sensors_.end()) {
    auto *text_sensor = dynamic_cast<text_sensor::TextSensor*>(it->second);
    if (text_sensor) {
      text_sensor->publish_state(value);
    }
  }
}

}  // namespace luxpower_sna
}  // namespace esphome
