#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"

#include <vector>
#include <string>
#include <optional>
#include <memory> // Required for std::unique_ptr
// Platform-specific WiFi includes
#ifdef USE_ESP32
#include <WiFi.h>
#elif USE_ESP8266
#include <ESP8266WiFi.h>
#endif

// Forward-declare the Mutex class to avoid including heavy headers.
namespace esphome {
class Mutex;
}

namespace esphome {
namespace luxclient {

class LuxClient : public Component {
 public:
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::string &serial) { this->dongle_serial_ = serial; }
  void set_inverter_serial(const std::string &serial) { this->inverter_serial_ = serial; }
  void set_read_timeout(uint32_t timeout) { this->read_timeout_ = timeout; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override;

  std::optional<std::vector<uint8_t>> read_holding_registers(uint16_t reg_address, uint8_t reg_count);
  bool write_holding_register(uint16_t reg_address, uint16_t value);

 protected:
  std::vector<uint8_t> build_request_packet(uint8_t function_code, uint16_t start_reg, uint16_t reg_count_or_value);
  std::optional<std::vector<uint8_t>> execute_transaction(const std::vector<uint8_t> &request);

  std::string host_;
  uint16_t port_{8000};
  std::string dongle_serial_;
  std::string inverter_serial_;
  uint32_t read_timeout_{1000};

  // Using a unique_ptr to the Mutex. This only requires the forward
  // declaration above, keeping this header file clean.
  std::unique_ptr<Mutex> client_mutex_;
};

}  // namespace luxclient
}  // namespace esphome
