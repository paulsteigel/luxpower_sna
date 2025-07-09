// esphome_config/custom_components/luxpower_sna/luxpower_sna.cpp

#include "luxpower_sna.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <numeric> // For std::accumulate

#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "lwip/dns.h"

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// --- LwIP Callback Functions ---

// Called when data is received from the inverter
err_t tcp_receive_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
  LuxpowerSNAComponent *component = static_cast<LuxpowerSNAComponent *>(arg);

  if (p == nullptr) {
    // This indicates the remote side has closed the connection
    ESP_LOGD(TAG, "Connection closed by remote host.");
    component->close_connection();
    return ERR_OK;
  }

  if (err == ERR_OK) {
    // Append all data from the pbuf chain to our buffer
    for (struct pbuf *q = p; q != nullptr; q = q->next) {
      component->rx_buffer_.insert(component->rx_buffer_.end(), (uint8_t *)q->payload, (uint8_t *)q->payload + q->len);
    }
    // Tell LwIP we have processed the data
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    // For now, just log the received data and close.
    // In the next step, we will parse this data.
    ESP_LOGD(TAG, "Received %d bytes. Full response:", component->rx_buffer_.size());
    ESP_LOGD(TAG, "  %s", format_hex_pretty(component->rx_buffer_).c_str());
    
    // TODO: Add full packet validation and parsing here.
    
    component->close_connection(); // We are done with this transaction
  } else {
    ESP_LOGW(TAG, "Receive error: %d", err);
    component->close_connection();
  }

  return ERR_OK;
}

// Called when the connection is successfully established.
static err_t tcp_connected_callback(void *arg, struct tcp_pcb *tpcb, err_t err) {
  LuxpowerSNAComponent *component = static_cast<LuxpowerSNAComponent *>(arg);
  if (err == ERR_OK) {
    ESP_LOGD(TAG, "Connection successful! Sending request...");

    // --- MODIFIED: This is the new logic ---
    // 1. Clear any old data from the receive buffer
    component->rx_buffer_.clear();

    // 2. Set up the receive callback to handle the inverter's response
    tcp_recv(tpcb, tcp_receive_callback);

    // 3. Build the request packet for the first block of registers
    // We are requesting 40 registers starting from address 0, function code 0x10 (read holding registers)
    std::vector<uint8_t> request = component->build_request_packet(0x10, 0, 40);

    // 4. Send the packet
    err_t write_err = tcp_write(tpcb, request.data(), request.size(), TCP_WRITE_FLAG_COPY);
    if (write_err != ERR_OK) {
      ESP_LOGW(TAG, "Error writing to TCP stream: %d", write_err);
      component->close_connection();
      return ERR_OK;
    }
    tcp_output(tpcb); // Send the data now

    ESP_LOGD(TAG, "Request packet sent (%d bytes):", request.size());
    ESP_LOGD(TAG, "  %s", format_hex_pretty(request).c_str());
    component->status_clear_warning();
    // --- End of new logic ---

  } else {
    ESP_LOGW(TAG, "Connection failed. Error: %d", err);
    component->close_connection();
  }
  return ERR_OK;
}

// Called when an error occurs on the connection.
static void tcp_error_callback(void *arg, err_t err) {
  LuxpowerSNAComponent *component = static_cast<LuxpowerSNAComponent *>(arg);
  ESP_LOGW(TAG, "TCP Error. Code: %d. Closing connection.", err);
  component->pcb_ = nullptr; // PCB is already freed by LwIP
  component->status_set_warning();
}

// --- NEW: Helper function to calculate the checksum ---
uint16_t LuxpowerSNAComponent::calculate_lux_checksum(const std::vector<uint8_t> &data) {
    // The checksum is a simple 16-bit sum of all bytes from address to the end of data
    return std::accumulate(data.begin() + 4, data.end(), 0);
}

// --- NEW: Helper function to build a request packet ---
std::vector<uint8_t> LuxpowerSNAComponent::build_request_packet(uint8_t function_code, uint8_t start_reg, uint8_t reg_count) {
    std::vector<uint8_t> packet;
    packet.resize(17);

    // 1. Header (2 bytes)
    packet[0] = 0xA8;
    packet[1] = 0x10;

    // 2. Data Length (2 bytes, little-endian)
    uint16_t data_len = 11; // Fixed for this request type
    packet[2] = data_len & 0xFF;
    packet[3] = (data_len >> 8) & 0xFF;

    // 4. Address (2 bytes, seems to be fixed)
    packet[4] = 0x01;
    packet[5] = 0x00;

    // 5. Dongle Serial Number (10 bytes)
    memcpy(&packet[6], this->dongle_serial_.c_str(), 10);

    // 6. Function Code (1 byte)
    packet[16] = function_code;

    // Now add the actual data payload
    // 7. Start Register (1 byte)
    packet.push_back(start_reg);
    // 8. Register Count (1 byte)
    packet.push_back(reg_count);
    
    // Resize the packet to its final size before checksum
    // Total size = 17 (header) + 2 (payload) + 2 (checksum) = 21
    packet.resize(21);

    // 9. Checksum (2 bytes, little-endian)
    uint16_t checksum = this->calculate_lux_checksum(packet);
    packet[19] = checksum & 0xFF;
    packet[20] = (checksum >> 8) & 0xFF;

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
