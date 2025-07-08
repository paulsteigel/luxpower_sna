// components/luxpower_sna/luxpower_sna.h
#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include <vector>
#include <map>
#include <string>

#ifdef USE_ESP32
#include <AsyncTCP.h>
#elif USE_ESP8266
#include <ESPAsyncTCP.h>
#endif

namespace esphome {
namespace luxpower_sna {

class LuxpowerSNAComponent : public PollingComponent {
 public:
  // --- Setters for configuration from __init__.py ---
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::vector<uint8_t> &serial) { this->dongle_serial_ = serial; }
  void set_inverter_serial_number(const std::vector<uint8_t> &serial) { this->inverter_serial_ = serial; }

  // --- Method called by sensor.py to register a sensor ---
  void add_sensor(const std::string &sensor_type, sensor::Sensor *sensor_obj);

  // --- Standard ESPHome methods ---
  void setup() override;
  void dump_config() override;
  void update() override;

 protected:
  // --- Communication methods ---
  void request_data_();
  void handle_packet_(void *arg, AsyncClient *client, void *data, size_t len);
  uint16_t calculate_crc_(const uint8_t *data, size_t len);

  // --- Member variables ---
  std::string host_;
  uint16_t port_;
  std::vector<uint8_t> dongle_serial_;
  std::vector<uint8_t> inverter_serial_;
  AsyncClient *tcp_client_{nullptr};

  // --- A map to store pointers to the registered sensors ---
  std::map<std::string, sensor::Sensor*> sensors_;
};

}  // namespace luxpower_sna
}  // namespace esphome
