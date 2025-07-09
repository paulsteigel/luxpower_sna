// esphome_config/custom_components/luxpower_sna/luxpower_sna.h

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include <vector> // --- NEW: Needed for std::vector ---

struct tcp_pcb;
struct pbuf;

namespace esphome {
namespace luxpower_sna {

class LuxpowerSNAComponent : public PollingComponent {
 public:
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::string &serial) { this->dongle_serial_ = serial; }
  
  void setup() override;
  void dump_config() override;
  void update() override;

  // Public members/methods for LwIP callbacks
  void close_connection();
  struct tcp_pcb *pcb_ = nullptr;
  // --- NEW: A buffer to store incoming data chunks ---
  std::vector<uint8_t> rx_buffer_;

 private:
  // --- NEW: Helper functions for building the packet ---
  std::vector<uint8_t> build_request_packet(uint8_t function_code, uint8_t start_reg, uint8_t reg_count);
  uint16_t calculate_lux_checksum(const std::vector<uint8_t> &data);

  // Configuration variables
  std::string host_;
  uint16_t port_;
  std::string dongle_serial_;
};

// --- NEW: Declaration for our static receive callback function ---
static err_t tcp_receive_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

}  // namespace luxpower_sna
}  // namespace esphome
