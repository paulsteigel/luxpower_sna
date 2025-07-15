#pragma once

#include "esphome.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <vector>
#include <string>
#include <memory>
#include "lxp_packet.h"

namespace esphome {
namespace luxpower {

using namespace esphome;

class LuxPowerClient : public Component, public async_tcp::AsyncTCPClient {
 public:
  // Configuration
  void set_dongle_serial(const std::string &serial) { dongle_serial_ = serial; }
  void set_inverter_serial(const std::string &serial) { inverter_serial_ = serial; }
  void set_respond_to_heartbeat(bool respond) { respond_to_heartbeat_ = respond; }
  
  // Lifecycle
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  
  // Register Operations
  bool write_holding_register(uint16_t reg, uint16_t value);
  std::optional<uint16_t> read_holding_register(uint16_t reg);
  void request_data_bank(uint8_t bank);
  void request_hold_bank(uint8_t bank);
  
  // System Commands
  void restart_inverter();
  void reset_settings();
  void sync_time(bool force = false);

 protected:
  void on_connect() override;
  void on_disconnect() override;
  void on_data(std::vector<uint8_t> &data) override;
  void parse_incoming_data(const std::vector<uint8_t> &data);
  void process_packet(const std::vector<uint8_t> &packet);
  void send_packet(const std::vector<uint8_t> &packet);
  
  std::string dongle_serial_;
  std::string inverter_serial_;
  bool respond_to_heartbeat_{true};
  Mutex data_mutex_;
  std::unique_ptr<LxpPacket> lxp_packet_;
  
  // Timing
  uint32_t last_heartbeat_{0};
  uint32_t last_data_refresh_{0};
  uint32_t last_hold_refresh_{0};
};

}  // namespace luxpower
}  // namespace esphome
