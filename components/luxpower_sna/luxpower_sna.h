// esphome_config/custom_components/luxpower_sna/luxpower_sna.h

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include <vector>

// --- FIX 1: Include LwIP header for err_t type ---
#include "lwip/err.h"

// Forward-declare LwIP structs to keep includes minimal
struct tcp_pcb;
struct pbuf;

namespace esphome {
namespace luxpower_sna {

class LuxpowerSNAComponent : public PollingComponent {
 public:
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::string &serial) { this->dongle_serial_ = serial; }
  // --- FIX 3: Re-add the missing setter ---
  void set_num_banks_to_request(int num) { this->num_banks_to_request_ = num; }
  
  void setup() override;
  void dump_config() override;
  void update() override;

  // Public members/methods for LwIP callbacks
  void close_connection();
  struct tcp_pcb *pcb_ = nullptr;
  std::vector<uint8_t> rx_buffer_;

  // --- FIX 2: Move helper functions to public so callbacks can access them ---
  std::vector<uint8_t> build_request_packet(uint8_t function_code, uint8_t start_reg, uint8_t reg_count);
  uint16_t calculate_lux_checksum(const std::vector<uint8_t> &data);

 private:
  // Configuration variables
  std::string host_;
  uint16_t port_;
  std::string dongle_serial_;
  // --- FIX 3: Re-add the member variable ---
  int num_banks_to_request_{1}; 
};

// Declaration for our static receive callback function
static err_t tcp_receive_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

}  // namespace luxpower_sna
}  // namespace esphome
