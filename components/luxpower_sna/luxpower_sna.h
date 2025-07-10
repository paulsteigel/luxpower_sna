#ifndef LUXPOWER_SNA_H
#define LUXPOWER_SNA_H

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/hal.h"
#include <AsyncTCP.h>
#include <map>
#include <string>
#include <vector>

namespace esphome {
namespace luxpower_sna {

// --- Data Structures for Parsing ---
#pragma pack(push, 1)
struct LuxHeader {
  uint16_t prefix;
  uint16_t length;
  uint8_t protocol;
  uint8_t function;
  uint8_t direction;
};

struct LuxTranslatedData {
  char serialNumber[10];
  uint8_t deviceFunction;
  uint8_t registerStart;
  uint8_t registerCount;
};

struct LuxLogDataRawSection1 {
  uint16_t pv1_voltage;
  uint16_t pv2_voltage;
  uint16_t pv3_voltage;
  uint16_t unk1;
  uint16_t battery_voltage;
  uint16_t soc;
  uint16_t soh;
  uint16_t pv1_power;
  uint16_t pv2_power;
  uint16_t pv3_power;
  uint16_t charge_power;
  uint16_t discharge_power;
  uint16_t activeInverter_power;
  uint16_t power_to_grid;
  uint16_t power_from_grid;
  uint16_t voltage_ac_r;
  uint16_t voltage_ac_s;
  uint16_t voltage_ac_t;
  uint16_t frequency_grid;
  uint16_t grid_power_factor;
  uint16_t voltage_eps_r;
  uint16_t voltage_eps_s;
  uint16_t voltage_eps_t;
  uint16_t frequency_eps;
  uint16_t active_eps_power;
  uint16_t apparent_eps_power;
  uint16_t bus1_voltage;
  uint16_t bus2_voltage;
  uint16_t pv1_energy_today;
  uint16_t pv2_energy_today;
  uint16_t pv3_energy_today;
  uint16_t activeInverter_energy_today;
  uint16_t ac_charging_today;
  uint16_t charging_today;
  uint16_t discharging_today;
  uint16_t eps_today;
  uint16_t exported_today;
  uint16_t grid_today;
};

struct LuxLogDataRawSection2 {
  uint32_t e_pv_1_all;
  uint32_t e_pv_2_all;
  uint32_t e_pv_3_all;
  uint32_t e_inv_all;
  uint32_t e_rec_all;
  uint32_t e_chg_all;
  uint32_t e_dischg_all;
  uint32_t e_eps_all;
  uint32_t e_to_grid_all;
  uint32_t e_to_user_all;
  uint16_t t_inner;
  uint16_t t_rad_1;
  uint16_t t_rad_2;
  uint16_t t_bat;
  uint32_t uptime;
};

struct LuxLogDataRawSection3 {
  uint16_t max_chg_curr;
  uint16_t max_dischg_curr;
  uint16_t charge_volt_ref;
  uint16_t dischg_cut_volt;
  int16_t bat_current;
  uint16_t bat_count;
  uint16_t bat_capacity;
  uint16_t bat_status_inv;
  uint16_t max_cell_volt;
  uint16_t min_cell_volt;
  uint16_t max_cell_temp;
  uint16_t min_cell_temp;
  uint16_t bat_cycle_count;
};
#pragma pack(pop)

// Inherit from PollingComponent to get the loop() and update() methods
class LuxpowerSNAComponent : public PollingComponent, public Component {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;

  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::string &serial) { this->dongle_serial_ = serial; }
  void set_inverter_serial(const std::string &serial) { this->inverter_serial_ = serial; }
  
  // Setter methods for each sensor
  #define LUX_FLOAT_SENSOR(name) \
    void set_##name##_sensor(sensor::Sensor *s) { this->float_sensors_[#name] = s; }
  #define LUX_STRING_SENSOR(name) \
    void set_##name##_sensor(text_sensor::TextSensor *s) { this->string_sensors_[#name] = s; }

  LUX_STRING_SENSOR(inverter_serial)
  LUX_FLOAT_SENSOR(pv1_voltage)
  LUX_FLOAT_SENSOR(pv2_voltage)
  LUX_FLOAT_SENSOR(pv3_voltage)
  LUX_FLOAT_SENSOR(battery_voltage)
  LUX_FLOAT_SENSOR(soc)
  LUX_FLOAT_SENSOR(soh)
  LUX_FLOAT_SENSOR(pv1_power)
  LUX_FLOAT_SENSOR(pv2_power)
  LUX_FLOAT_SENSOR(pv3_power)
  LUX_FLOAT_SENSOR(charge_power)
  LUX_FLOAT_SENSOR(discharge_power)
  LUX_FLOAT_SENSOR(inverter_power)
  LUX_FLOAT_SENSOR(power_to_grid)
  LUX_FLOAT_SENSOR(power_from_grid)
  LUX_FLOAT_SENSOR(grid_voltage_r)
  LUX_FLOAT_SENSOR(grid_voltage_s)
  LUX_FLOAT_SENSOR(grid_voltage_t)
  LUX_FLOAT_SENSOR(grid_frequency)
  LUX_FLOAT_SENSOR(power_factor)
  LUX_FLOAT_SENSOR(eps_voltage_r)
  LUX_FLOAT_SENSOR(eps_voltage_s)
  LUX_FLOAT_SENSOR(eps_voltage_t)
  LUX_FLOAT_SENSOR(eps_frequency)
  LUX_FLOAT_SENSOR(eps_active_power)
  LUX_FLOAT_SENSOR(eps_apparent_power)
  LUX_FLOAT_SENSOR(bus1_voltage)
  LUX_FLOAT_SENSOR(bus2_voltage)
  LUX_FLOAT_SENSOR(pv1_energy_today)
  LUX_FLOAT_SENSOR(pv2_energy_today)
  LUX_FLOAT_SENSOR(pv3_energy_today)
  LUX_FLOAT_SENSOR(inverter_energy_today)
  LUX_FLOAT_SENSOR(ac_charging_today)
  LUX_FLOAT_SENSOR(charging_today)
  LUX_FLOAT_SENSOR(discharging_today)
  LUX_FLOAT_SENSOR(eps_today)
  LUX_FLOAT_SENSOR(exported_today)
  LUX_FLOAT_SENSOR(grid_today)
  LUX_FLOAT_SENSOR(total_pv1_energy)
  LUX_FLOAT_SENSOR(total_pv2_energy)
  LUX_FLOAT_SENSOR(total_pv3_energy)
  LUX_FLOAT_SENSOR(total_inverter_output)
  LUX_FLOAT_SENSOR(total_recharge_energy)
  LUX_FLOAT_SENSOR(total_charged)
  LUX_FLOAT_SENSOR(total_discharged)
  LUX_FLOAT_SENSOR(total_eps_energy)
  LUX_FLOAT_SENSOR(total_exported)
  LUX_FLOAT_SENSOR(total_imported)
  LUX_FLOAT_SENSOR(temp_inner)
  LUX_FLOAT_SENSOR(temp_radiator)
  LUX_FLOAT_SENSOR(temp_radiator2)
  LUX_FLOAT_SENSOR(temp_battery)
  LUX_FLOAT_SENSOR(uptime)
  LUX_FLOAT_SENSOR(max_charge_current)
  LUX_FLOAT_SENSOR(max_discharge_current)
  LUX_FLOAT_SENSOR(charge_voltage_ref)
  LUX_FLOAT_SENSOR(discharge_cutoff_voltage)
  LUX_FLOAT_SENSOR(battery_current)
  LUX_FLOAT_SENSOR(battery_count)
  LUX_FLOAT_SENSOR(battery_capacity)
  LUX_FLOAT_SENSOR(battery_status_inv)
  LUX_FLOAT_SENSOR(max_cell_voltage)
  LUX_FLOAT_SENSOR(min_cell_voltage)
  LUX_FLOAT_SENSOR(max_cell_temp)
  LUX_FLOAT_SENSOR(min_cell_temp)
  LUX_FLOAT_SENSOR(cycle_count)

 protected:
  void request_bank_(uint8_t bank);
  void handle_response_(); // Changed: no longer takes arguments
  uint16_t calculate_crc_(const uint8_t *data, size_t len);

  void publish_state_(const std::string &key, float value);
  void publish_state_(const std::string &key, const std::string &value);

  std::string host_;
  uint16_t port_;
  std::string dongle_serial_;
  std::string inverter_serial_;
  AsyncClient *tcp_client_{nullptr};
  uint8_t next_bank_to_request_{0};

  // New members for deferred processing
  std::vector<uint8_t> rx_buffer_;
  volatile bool data_ready_to_process_{false};

  std::map<std::string, sensor::Sensor *> float_sensors_;
  std::map<std::string, text_sensor::TextSensor *> string_sensors_;
};

}  // namespace luxpower_sna
}  // namespace esphome

#endif  // LUXPOWER_SNA_H
