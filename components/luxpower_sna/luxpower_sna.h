// esphome_config/custom_components/luxpower_sna/luxpower_sna.h

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

// --- FIX 1 ---
// Forward-declare the LwIP struct. This tells the compiler that a struct
// named "tcp_pcb" exists, so we can use pointers to it without including
// the full "lwip/tcp.h" header here.
struct tcp_pcb;

namespace esphome {
namespace luxpower_sna {

class LuxpowerSNAComponent : public PollingComponent {
 public:
  void setup() override;
  void dump_config() override;
  void update() override;

  // --- FIX 2 ---
  // These must be public so that our static C-style callback functions,
  // which are outside the class, can access them.
  void close_connection();
  struct tcp_pcb *pcb_ = nullptr;

 private:
  // You should already have these from the previous steps
  std::string host_{"192.168.10.100"};
  uint16_t port_{8000};
  std::string dongle_serial_{"DONGLE12345"};
};

}  // namespace luxpower_sna
}  // namespace esphome
