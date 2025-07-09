// esphome_config/custom_components/luxpower_sna/luxpower_sna.h

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

// Forward-declare the LwIP struct
struct tcp_pcb;

namespace esphome {
namespace luxpower_sna {

class LuxpowerSNAComponent : public PollingComponent {
 public:
  // --- ADD THESE SETTER FUNCTIONS ---
  // These are called by ESPHome to configure the component from your YAML
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::string &serial) { this->dongle_serial_ = serial; }
  void set_num_banks_to_request(int num_banks) { this->num_banks_to_request_ = num_banks; }

  // Core component functions
  void setup() override;
  void dump_config() override;
  void update() override;

  // Public members for LwIP callbacks
  void close_connection();
  struct tcp_pcb *pcb_ = nullptr;

 private:
  // Configuration variables
  std::string host_{}; // Default is now empty, will be set from YAML
  uint16_t port_{8000};
  std::string dongle_serial_{};
  int num_banks_to_request_{1}; // Default to 1 if not specified in YAML
};

}  // namespace luxpower_sna
}  // namespace esphome
