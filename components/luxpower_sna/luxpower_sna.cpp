// esphome_config/custom_components/luxpower_sna/luxpower_sna.cpp

#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <numeric>

#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "lwip/dns.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

static err_t tcp_receive_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

// --- PARSING LOGIC ---
const int DATA_START_OFFSET = 35; // The actual register data starts at byte 35

// Helper function to extract a 16-bit little-endian value from the data buffer
uint16_t LuxpowerSNAComponent::get_register_value(const std::vector<uint8_t> &data, int reg_index) {
    int byte_pos = DATA_START_OFFSET + (reg_index * 2);
    if (byte_pos + 1 >= data.size()) {
        ESP_LOGW(TAG, "Not enough data to read register %d", reg_index);
        return 0;
    }
    return (data[byte_pos + 1] << 8) | data[byte_pos];
}

void LuxpowerSNAComponent::parse_response(const std::vector<uint8_t> &data) {
    ESP_LOGD(TAG, "Parsing response of size %d", data.size());
    
    // Basic validation: The response for 40 registers should be 117 bytes long
    if (data.size() < 117) {
        ESP_LOGW(TAG, "Response is too short to parse: %d bytes", data.size());
        return;
    }

    // --- Register Mapping (based on common Luxpower SNA registers) ---
    // Register 0: Vpv1 (0.1V)
    if (this->v_pv1_sensor_) {
        float value = get_register_value(data, 0) * 0.1f;
        this->v_pv1_sensor_->publish_state(value);
    }
    // Register 2: Ppv1 (W)
    if (this->p_pv1_sensor_) {
        float value = get_register_value(data, 2);
        this->p_pv1_sensor_->publish_state(value);
    }
    // Register 14: Vbat (0.1V)
    if (this->v_bat_sensor_) {
        float value = get_register_value(data, 14) * 0.1f;
        this->v_bat_sensor_->publish_state(value);
    }
    // Register 18: Pcharge (W)
    if (this->p_charge_sensor_) {
        float value = get_register_value(data, 18);
        this->p_charge_sensor_->publish_state(value);
    }
    // Register 19: Pdischarge (W)
    if (this->p_discharge_sensor_) {
        float value = get_register_value(data, 19);
        this->p_discharge_sensor_->publish_state(value);
    }
    // Register 23: Pinv (W)
    if (this->p_inv_sensor_) {
        float value = get_register_value(data, 23);
        this->p_inv_sensor_->publish_state(value);
    }

    ESP_LOGI(TAG, "Successfully parsed inverter data.");
}

// --- LwIP Callbacks ---
err_t tcp_receive_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
  LuxpowerSNAComponent *component = static_cast<LuxpowerSNAComponent *>(arg);

  if (p == nullptr) {
    ESP_LOGD(TAG, "Connection closed by remote host.");
    // --- MODIFIED: Parse data before closing ---
    if (!component->rx_buffer_.empty()) {
        component->parse_response(component->rx_buffer_);
    }
    component->close_connection();
    return ERR_OK;
  }

  if (err == ERR_OK) {
    for (struct pbuf *q = p; q != nullptr; q = q->next) {
      component->rx_buffer_.insert(component->rx_buffer_.end(), (uint8_t *)q->payload, (uint8_t *)q->payload + q->len);
    }
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
  } else {
    ESP_LOGW(TAG, "Receive error: %d", err);
    component->close_connection();
  }
  return ERR_OK;
}

// ... The rest of the file is mostly the same ...
static err_t tcp_connected_callback(void *arg, struct tcp_pcb *tpcb, err_t err) {
  LuxpowerSNAComponent *component = static_cast<LuxpowerSNAComponent *>(arg);
  if (err == ERR_OK) {
    ESP_LOGD(TAG, "Connection successful! Sending request...");
    component->rx_buffer_.clear();
    tcp_recv(tpcb, tcp_receive_callback);
    std::vector<uint8_t> request = component->build_request_packet(0x10, 0, 40);
    err_t write_err = tcp_write(tpcb, request.data(), request.size(), TCP_WRITE_FLAG_COPY);
    if (write_err != ERR_OK) {
      ESP_LOGW(TAG, "Error writing to TCP stream: %d", write_err);
      component->close_connection();
      return ERR_OK;
    }
    tcp_output(tpcb);
    ESP_LOGD(TAG, "Request packet sent (%d bytes):", request.size());
    ESP_LOGD(TAG, "  %s", format_hex_pretty(request).c_str());
    component->status_clear_warning();
  } else {
    ESP_LOGW(TAG, "Connection failed. Error: %d", err);
    component->close_connection();
  }
  return ERR_OK;
}

static void tcp_error_callback(void *arg, err_t err) {
  LuxpowerSNAComponent *component = static_cast<LuxpowerSNAComponent *>(arg);
  ESP_LOGW(TAG, "TCP Error. Code: %d. Closing connection.", err);
  component->pcb_ = nullptr;
  component->status_set_warning();
}

uint16_t LuxpowerSNAComponent::calculate_lux_checksum(const std::vector<uint8_t> &data) {
    return std::accumulate(data.begin() + 4, data.end(), 0);
}

std::vector<uint8_t> LuxpowerSNAComponent::build_request_packet(uint8_t function_code, uint8_t start_reg, uint8_t reg_count) {
    std::vector<uint8_t> packet;
    packet.resize(17);
    packet[0] = 0xA8; packet[1] = 0x10;
    uint16_t data_len = 11;
    packet[2] = data_len & 0xFF; packet[3] = (data_len >> 8) & 0xFF;
    packet[4] = 0x01; packet[5] = 0x00;
    memcpy(&packet[6], this->dongle_serial_.c_str(), 10);
    packet[16] = function_code;
    packet.push_back(start_reg);
    packet.push_back(reg_count);
    packet.resize(21);
    uint16_t checksum = this->calculate_lux_checksum(packet);
    packet[19] = checksum & 0xFF; packet[20] = (checksum >> 8) & 0xFF;
    return packet;
}

void LuxpowerSNAComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up LuxpowerSNAComponent...");
}

void LuxpowerSNAComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "LuxpowerSNAComponent:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%u", this->host_.c_str(), this->port_);
  ESP_LOGCONFIG(TAG, "  Dongle Serial: %s", this->dongle_serial_.c_str());
  LOG_UPDATE_INTERVAL(this);
}

void LuxpowerSNAComponent::update() {
  ESP_LOGD(TAG, "Starting update...");
  if (this->pcb_ != nullptr) {
    ESP_LOGD(TAG, "Update called but a connection is already active. Aborting old one.");
    this->close_connection();
  }
  this->pcb_ = tcp_new();
  if (this->pcb_ == nullptr) {
    ESP_LOGW(TAG, "Could not create TCP PCB");
    this->status_set_warning();
    return;
  }
  tcp_arg(this->pcb_, this);
  tcp_err(this->pcb_, tcp_error_callback);
  ip_addr_t remote_ip;
  err_t err = dns_gethostbyname(this->host_.c_str(), &remote_ip, nullptr, nullptr);
  if (err != ERR_OK && err != ERR_INPROGRESS) {
      ESP_LOGW(TAG, "Could not resolve host %s. Error: %d", this->host_.c_str(), err);
      this->close_connection();
      return;
  }
  ESP_LOGD(TAG, "Connecting to %s:%u...", this->host_.c_str(), this->port_);
  err = tcp_connect(this->pcb_, &remote_ip, this->port_, tcp_connected_callback);
  if (err != ERR_OK) {
    ESP_LOGW(TAG, "Could not initiate TCP connection. Error: %d", err);
    this->close_connection();
    return;
  }
}

void LuxpowerSNAComponent::close_connection() {
  if (this->pcb_ != nullptr) {
    tcp_err(this->pcb_, nullptr);
    tcp_sent(this->pcb_, nullptr);
    tcp_recv(this->pcb_, nullptr);
    tcp_arg(this->pcb_, nullptr);
    err_t err = tcp_close(this->pcb_);
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "Error closing TCP connection: %d. Aborting.", err);
        tcp_abort(this->pcb_);
    }
    this->pcb_ = nullptr;
    ESP_LOGD(TAG, "Connection closed.");
  }
}

}  // namespace luxpower_sna
}  // namespace esphome
