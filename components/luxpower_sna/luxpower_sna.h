// esphome_config/custom_components/luxpower_sna/luxpower_sna.h

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include <vector>

#include "lwip/err.h"

struct tcp_pcb;
struct pbuf;

namespace esphome {
namespace luxpower_sna {

class LuxpowerSNAComponent : public PollingComponent {
 public:
  void setup() override;
  void dump_config() override;
  void update() override;

  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::string &serial) { this->dongle_serial_ = serial; }

  // --- Setters for our sensors (called by the generated code) ---
  void set_v_pv1_sensor(sensor::Sensor *s) { this->v_pv1_sensor_ = s; }
  void set_p_pv1_sensor(sensor::Sensor *s) { this->p_pv1_sensor_ = s; }
  void set_v_bat_sensor(sensor::Sensor *s) { this->v_bat_sensor_ = s; }
  void set_p_charge_sensor(sensor::Sensor *s) { this->p_charge_sensor_ = s; }
  void set_p_discharge_sensor(sensor::Sensor *s) { this->p_discharge_sensor_ = s; }
  void set_p_inv_sensor(sensor::Sensor *s) { this->p_inv_sensor_ = s; }

  // Public members/methods for LwIP callbacks
  void close_connection();
  void parse_response(const std::vector<uint8_t> &data);
  struct tcp_pcb *pcb_ = nullptr;
  std::vector<uint8_t> rx_buffer_;

  std::vector<uint8_t> build_request_packet(uint8_t function_code, uint8_t start_reg, uint8_t reg_count);
  uint16_t calculate_lux_checksum(const std::vector<uint8_t> &data);

 private:
  uint16_t get_register_value(const std::vector<uint8_t> &data, int offset);

  // Configuration variables
  std::string host_;
  uint16_t port_;
  std::string dongle_serial_;
  
  // Pointers to our sensor objects
  sensor::Sensor *v_pv1_sensor_{nullptr};
  sensor::Sensor *p_pv1_sensor_{nullptr};
  sensor::Sensor *v_bat_sensor_{nullptr};
  sensor::Sensor *p_charge_sensor_{nullptr};
  sensor::Sensor *p_discharge_sensor_{nullptr};
  sensor::Sensor *p_inv_sensor_{nullptr};
};

}  // namespace luxpower_sna
}  // namespace esphome

