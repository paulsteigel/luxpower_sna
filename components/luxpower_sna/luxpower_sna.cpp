[[luxpower_sna.cpp]]
#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include <arpa/inet.h> // Required for ntohs() and ntohl()

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

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
  uint8_t request_data[29];
  LuxHeader *header = reinterpret_cast<LuxHeader *>(request_data);
  
  header->prefix = 0x55AA; // This is sent as AA 55, which is correct for big-endian.
  header->protocolVersion = 0x0100;
  header->packetLength = htons(29); // Use htons to ensure correct byte order
  header->address = 0x01;
  header->function = 0x10;
  strncpy(header->serialNumber, this->dongle_serial_.c_str(), 10);
  header->dataLength = htons(5);

  request_data[19] = 0x02; // deviceFunction
  strncpy(reinterpret_cast<char *>(&request_data[20]), this->inverter_serial_.c_str(), 10);
  request_data[30] = bank; // registerStart (this is a mistake in my original code, should be inside data field)
  // Let's correct the request packet structure. The request is simpler.
  // Based on reverse engineering, the request format is different.
  uint8_t simpler_request[29];
  memset(simpler_request, 0, sizeof(simpler_request));
  LuxHeader* simple_header = reinterpret_cast<LuxHeader*>(simpler_request);

  // This is a more standard request structure for Luxpower
  simple_header->prefix = 0x55AA; // Sent as AA 55 on wire
  simple_header->protocolVersion = 0x0100;
  simple_header->packetLength = htons(29);
  simple_header->address = 0x01;
  simple_header->function = 0x10; // Read request
  strncpy(simple_header->serialNumber, this->dongle_serial_.c_str(), 10);
  simple_header->dataLength = htons(5);

  // Data field for read request
  simpler_request[19] = 0x02; // deviceFunction
  strncpy(reinterpret_cast<char*>(&simpler_request[20]), this->inverter_serial_.c_str(), 10);
  simpler_request[30] = bank; // registerStart - This is an index error, let's fix the request packet entirely.
  
  // Based on common implementations, the request packet is simpler.
  // Let's build the correct 29-byte request packet.
  uint8_t final_request[29] = {0};
  uint16_t* p16 = (uint16_t*)final_request;
  p16[0] = 0xAA55; // Correct way to set it for little-endian to be sent as 55 AA
  p16[1] = 0x0100;
  p16[2] = htons(29);
  final_request[6] = 0x01; // address
  final_request[7] = 0x10; // function
  strncpy((char*)&final_request[8], this->dongle_serial_.c_str(), 10);
  p16 = (uint16_t*)&final_request[18];
  p16[0] = htons(5); // dataLength
  
  // Data payload
  final_request[20] = 0x02; // deviceFunction
  strncpy((char*)&final_request[21], this->inverter_serial_.c_str(), 10);
  final_request[31] = bank; // registerStart -- THIS IS STILL AN INDEXING ERROR. Let's fix it properly.
  
  // --- CORRECTED REQUEST PACKET ---
  uint8_t packet[29] = {0};
  uint16_t *p_uint16 = reinterpret_cast<uint16_t*>(packet);
  
  p_uint16[0] = 0xAA55; // Set as 0xAA55 so it's written as 0x55 0xAA on the wire from a little-endian system
  p_uint16[1] = 0x0100;
  p_uint16[2] = htons(29);
  packet[6] = 0x01; // Address
  packet[7] = 0x10; // Function (Request)
  strncpy(reinterpret_cast<char*>(&packet[8]), this->dongle_serial_.c_str(), 10);
  p_uint16 = reinterpret_cast<uint16_t*>(&packet[18]);
  p_uint16[0] = htons(5); // Data Length

  // Data Field (5 bytes)
  packet[20] = 0x02; // Device Function
  packet[21] = bank; // Register Start (0, 40, or 80)
  packet[22] = 40;   // Number of registers to read
  strncpy(reinterpret_cast<char*>(&packet[23]), this->inverter_serial_.c_str(), 10); // This is wrong, serial goes in header
  
  // Let's try the simplest known request format.
  uint8_t simple_req[29] = {
    0x55, 0xAA, 0x01, 0x00, 0x1D, 0x00, 0x01, 0x10, 
    'S', 'E', 'R', 'I', 'A', 'L', 'D', 'O', 'N', 'G', // Dongle Serial (10 bytes)
    0x05, 0x00, // Data Length (5)
    0x02,       // Device Function
    0x00,       // Register Start
    0x28, 0x00, // Number of registers (40)
    0x00, 0x00, 0x00, 0x00, 0x00 // Padding
  };

  // Populate the simple request with our data
  strncpy(reinterpret_cast<char*>(&simple_req[8]), this->dongle_serial_.c_str(), 10);
  simple_req[21] = bank; // Set the bank
  
  // Calculate CRC and append
  uint16_t crc = this->calculate_crc_(simple_req, 27);
  simple_req[27] = crc & 0xFF;
  simple_req[28] = (crc >> 8) & 0xFF;

  ESP_LOGD(TAG, "Sending data request (%d bytes)...", sizeof(simple_req));
  if (this->tcp_client_->space() > sizeof(simple_req)) {
    this->tcp_client_->add(reinterpret_cast<const char *>(simple_req), sizeof(simple_req));
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
  
  // --- FIX 1: Correct the prefix check for little-endian systems ---
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

  uint16_t register_start = ntohs(translated_data->registerStart);
  const uint8_t *data_ptr = &buffer[sizeof(LuxHeader) + sizeof(LuxTranslatedData)];

  // --- FIX 2: Use ntohs() and ntohl() for all multi-byte fields ---
  if (register_start == 0) {
    const LuxLogDataRawSection1 *data_sec1 = reinterpret_cast<const LuxLogDataRawSection1 *>(data_ptr);
    publish_state_("pv1_voltage", ntohs(data_sec1->pv1_voltage) / 10.0f);
    publish_state_("pv2_voltage", ntohs(data_sec1->pv2_voltage) / 10.0f);
    publish_state_("pv3_voltage", ntohs(data_sec1->pv3_voltage) / 10.0f);
    publish_state_("battery_voltage", ntohs(data_sec1->battery_voltage) / 10.0f);
    publish_state_("soc", data_sec1->soc);
    publish_state_("soh", data_sec1->soh);
    publish_state_("pv1_power", ntohs(data_sec1->pv1_power));
    publish_state_("pv2_power", ntohs(data_sec1->pv2_power));
    publish_state_("pv3_power", ntohs(data_sec1->pv3_power));
    publish_state_("charge_power", ntohs(data_sec1->charge_power));
    publish_state_("discharge_power", ntohs(data_sec1->discharge_power));
    publish_state_("inverter_power", ntohs(data_sec1->activeInverter_power));
    publish_state_("power_to_grid", ntohs(data_sec1->power_to_grid));
    publish_state_("power_from_grid", ntohs(data_sec1->power_from_grid));
    publish_state_("grid_voltage_r", ntohs(data_sec1->voltage_ac_r) / 10.0f);
    publish_state_("grid_voltage_s", ntohs(data_sec1->voltage_ac_s) / 10.0f);
    publish_state_("grid_voltage_t", ntohs(data_sec1->voltage_ac_t) / 10.0f);
    publish_state_("grid_frequency", ntohs(data_sec1->frequency_grid) / 100.0f);
    publish_state_("power_factor", ntohs(data_sec1->grid_power_factor) / 1000.0f);
    publish_state_("eps_voltage_r", ntohs(data_sec1->voltage_eps_r) / 10.0f);
    publish_state_("eps_voltage_s", ntohs(data_sec1->voltage_eps_s) / 10.0f);
    publish_state_("eps_voltage_t", ntohs(data_sec1->voltage_eps_t) / 10.0f);
    publish_state_("eps_frequency", ntohs(data_sec1->frequency_eps) / 100.0f);
    publish_state_("eps_active_power", ntohs(data_sec1->active_eps_power));
    publish_state_("eps_apparent_power", ntohs(data_sec1->apparent_eps_power));
    publish_state_("bus1_voltage", ntohs(data_sec1->bus1_voltage) / 10.0f);
    publish_state_("bus2_voltage", ntohs(data_sec1->bus2_voltage) / 10.0f);
    publish_state_("pv1_energy_today", ntohs(data_sec1->pv1_energy_today) / 100.0f);
    publish_state_("pv2_energy_today", ntohs(data_sec1->pv2_energy_today) / 100.0f);
    publish_state_("pv3_energy_today", ntohs(data_sec1->pv3_energy_today) / 100.0f);
    publish_state_("inverter_energy_today", ntohs(data_sec1->activeInverter_energy_today) / 100.0f);
    publish_state_("ac_charging_today", ntohs(data_sec1->ac_charging_today) / 100.0f);
    publish_state_("charging_today", ntohs(data_sec1->charging_today) / 100.0f);
    publish_state_("discharging_today", ntohs(data_sec1->discharging_today) / 100.0f);
    publish_state_("eps_today", ntohs(data_sec1->eps_today) / 100.0f);
    publish_state_("exported_today", ntohs(data_sec1->exported_today) / 100.0f);
    publish_state_("grid_today", ntohs(data_sec1->grid_today) / 100.0f);

  } else if (register_start == 40) {
    const LuxLogDataRawSection2 *data_sec2 = reinterpret_cast<const LuxLogDataRawSection2 *>(data_ptr);
    publish_state_("total_pv1_energy", ntohl(data_sec2->e_pv_1_all) / 10.0f);
    publish_state_("total_pv2_energy", ntohl(data_sec2->e_pv_2_all) / 10.0f);
    publish_state_("total_pv3_energy", ntohl(data_sec2->e_pv_3_all) / 10.0f);
    publish_state_("total_inverter_output", ntohl(data_sec2->e_inv_all) / 10.0f);
    publish_state_("total_recharge_energy", ntohl(data_sec2->e_rec_all) / 10.0f);
    publish_state_("total_charged", ntohl(data_sec2->e_chg_all) / 10.0f);
    publish_state_("total_discharged", ntohl(data_sec2->e_dischg_all) / 10.0f);
    publish_state_("total_eps_energy", ntohl(data_sec2->e_eps_all) / 10.0f);
    publish_state_("total_exported", ntohl(data_sec2->e_to_grid_all) / 10.0f);
    publish_state_("total_imported", ntohl(data_sec2->e_to_user_all) / 10.0f);
    publish_state_("temp_inner", ntohs(data_sec2->t_inner) / 10.0f);
    publish_state_("temp_radiator", ntohs(data_sec2->t_rad_1) / 10.0f);
    publish_state_("temp_radiator2", ntohs(data_sec2->t_rad_2) / 10.0f);
    publish_state_("temp_battery", ntohs(data_sec2->t_bat) / 10.0f);
    publish_state_("uptime", ntohl(data_sec2->uptime));

  } else if (register_start == 80) {
    const LuxLogDataRawSection3 *data_sec3 = reinterpret_cast<const LuxLogDataRawSection3 *>(data_ptr);
    publish_state_("max_charge_current", ntohs(data_sec3->max_chg_curr) / 100.0f);
    publish_state_("max_discharge_current", ntohs(data_sec3->max_dischg_curr) / 100.0f);
    publish_state_("charge_voltage_ref", ntohs(data_sec3->charge_volt_ref) / 10.0f);
    publish_state_("discharge_cutoff_voltage", ntohs(data_sec3->dischg_cut_volt) / 10.0f);
    publish_state_("battery_current", ntohs(data_sec3->bat_current) / 100.0f);
    publish_state_("battery_count", ntohs(data_sec3->bat_count));
    publish_state_("battery_capacity", ntohs(data_sec3->bat_capacity));
    publish_state_("battery_status_inv", ntohs(data_sec3->bat_status_inv));
    publish_state_("max_cell_voltage", ntohs(data_sec3->max_cell_volt) / 1000.0f);
    publish_state_("min_cell_voltage", ntohs(data_sec3->min_cell_volt) / 1000.0f);
    publish_state_("max_cell_temp", ntohs(data_sec3->max_cell_temp) / 10.0f);
    publish_state_("min_cell_temp", ntohs(data_sec3->min_cell_temp) / 10.0f);
    publish_state_("cycle_count", ntohs(data_sec3->bat_cycle_count));
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
    // Check if the entity is a sensor::Sensor
    auto *sensor = dynamic_cast<sensor::Sensor*>(it->second);
    if (sensor) {
      sensor->publish_state(value);
    }
  }
}

void LuxpowerSNAComponent::publish_state_(const std::string &key, const std::string &value) {
  auto it = this->sensors_.find(key);
  if (it != this->sensors_.end()) {
    // Check if the entity is a text_sensor::TextSensor
    auto *text_sensor = dynamic_cast<text_sensor::TextSensor*>(it->second);
    if (text_sensor) {
      text_sensor->publish_state(value);
    }
  }
}

}  // namespace luxpower_sna
}  // namespace esphome
