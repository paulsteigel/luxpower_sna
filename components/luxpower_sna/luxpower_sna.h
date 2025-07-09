#ifndef LUXPOWER_SNA_H
#define LUXPOWER_SNA_H

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/socket/socket.h"
#include <vector>

namespace esphome {
namespace luxpower_sna {

class LuxpowerSNAComponent : public PollingComponent {
public:
  void setup() override;
  void update() override;
  void dump_config() override;

  // Setters for configuration
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::string &serial) { this->dongle_serial_ = serial; }
  void set_num_banks_to_request(int num) { this->num_banks_to_request_ = num; }

  // Setters for sensors
  void set_soc_sensor(sensor::Sensor *sensor) { this->soc_sensor_ = sensor; }
  void set_pv1_power_sensor(sensor::Sensor *sensor) { this->pv1_power_sensor_ = sensor; }
  // ... add setters for all your other sensors ...
  void set_status_text_sensor(text_sensor::TextSensor *sensor) { this->status_text_sensor_ = sensor; }

private:
  // Helper functions
  void start_update_cycle_();
  void send_next_request_();
  void handle_response_(void *data, size_t len);
  void parse_and_publish_();
  void end_update_cycle_(bool success);
  uint16_t get_register_value_(int register_index);

  // NEW: Packet building and utility functions
  std::vector<uint8_t> build_request_packet_(uint16_t start_register, uint16_t num_registers);
  void log_hex_buffer(const std::string& prefix, const std::vector<uint8_t>& buffer);

  // Configuration
  std::string host_;
  uint16_t port_{8000};
  std::string dongle_serial_;
  int num_banks_to_request_{1};

  // State machine
  std::unique_ptr<socket::Socket> socket_;
  bool is_updating_{false};
  int current_bank_to_request_{0};
  
  // Data buffer (sized for 4 banks: 4 banks * 40 registers/bank * 2 bytes/register)
  std::vector<uint8_t> data_buffer_;

  // Sensors
  sensor::Sensor *soc_sensor_{nullptr};
  sensor::Sensor *pv1_power_sensor_{nullptr};
  // ... other sensors ...
  text_sensor::TextSensor *status_text_sensor_{nullptr};
};

} // namespace luxpower_sna
} // namespace esphome

#endif // LUXPOWER_SNA_H
