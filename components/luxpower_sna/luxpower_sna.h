#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <map>
#include <string>
#include <queue>

#ifdef USE_ESP32
#include <AsyncTCP.h>
#elif USE_ESP8266
#include <ESPAsyncTCP.h>
#endif

namespace esphome {
namespace luxpower_sna {

#pragma pack(push, 1)

struct LuxHeader {
  uint16_t prefix;
  uint16_t protocolVersion;
  uint16_t packetLength;
  uint8_t  address;
  uint8_t  function;
  char     serialNumber[10];
  uint16_t dataLength;
};

struct LuxTranslatedData {
  uint8_t  address;
  uint8_t  deviceFunction;
  char     serialNumber[10];
  uint16_t registerStart;
  uint8_t  dataFieldLength;
};

struct LuxLogDataRawSection1 {
  uint16_t status;
  int16_t  pv1_voltage; int16_t  pv2_voltage; int16_t  pv3_voltage;
  int16_t  battery_voltage; uint8_t  soc; uint8_t  soh;
  uint16_t _reserved1;
  int16_t  pv1_power; int16_t  pv2_power; int16_t  pv3_power;
  int16_t  charge_power; int16_t  discharge_power;
  int16_t  voltage_ac_r; int16_t  voltage_ac_s; int16_t  voltage_ac_t;
  int16_t  frequency_grid; int16_t  activeInverter_power;
  int16_t  activeCharge_power; int16_t  inductor_current;
  int16_t  grid_power_factor;
  int16_t  voltage_eps_r; int16_t  voltage_eps_s; int16_t  voltage_eps_t;
  int16_t  frequency_eps; int16_t  active_eps_power; int16_t  apparent_eps_power;
  int16_t  power_to_grid; int16_t  power_from_grid;
  int16_t  pv1_energy_today; int16_t  pv2_energy_today; int16_t  pv3_energy_today;
  int16_t  activeInverter_energy_today; int16_t  ac_charging_today;
  int16_t  charging_today; int16_t  discharging_today; int16_t  eps_today;
  int16_t  exported_today; int16_t  grid_today;
  int16_t  bus1_voltage; int16_t  bus2_voltage;
};

struct LuxLogDataRawSection2 {
  int32_t  e_pv_1_all; int32_t  e_pv_2_all; int32_t  e_pv_3_all;
  int32_t  e_inv_all; int32_t  e_rec_all; int32_t  e_chg_all;
  int32_t  e_dischg_all; int32_t  e_eps_all; int32_t  e_to_grid_all;
  int32_t  e_to_user_all;
  uint32_t fault_code; uint32_t warning_code;
  int16_t  t_inner; int16_t  t_rad_1; int16_t  t_rad_2; int16_t  t_bat;
  uint16_t _reserved2; uint32_t uptime;
};

struct LuxLogDataRawSection3 {
  uint16_t _reserved3;
  int16_t  max_chg_curr; int16_t  max_dischg_curr;
  int16_t  charge_volt_ref; int16_t  dischg_cut_volt;
  uint8_t  placeholder[20];
  int16_t  bat_status_inv; int16_t  bat_count; int16_t  bat_capacity;
  int16_t  bat_current;
  uint8_t  placeholder2[6];
  int16_t  max_cell_volt; int16_t  min_cell_volt;
  int16_t  max_cell_temp; int16_t  min_cell_temp;
  uint16_t _reserved4; int16_t  bat_cycle_count;
  uint8_t  _reserved5[14];
  int16_t  p_load2;
};

struct LuxLogDataRawSection4 {
  uint16_t reg120;
  int16_t  gen_input_volt;
  int16_t  gen_input_freq;
  int16_t  gen_power_watt;
  int16_t  gen_power_day;
  int16_t  gen_power_all;
  uint16_t reg126;
  int16_t  eps_L1_volt;
  int16_t  eps_L2_volt;
  int16_t  eps_L1_watt;
  int16_t  eps_L2_watt;
  uint8_t  placeholder[50];
};

#pragma pack(pop)

class LuxpowerSNAComponent : public PollingComponent {
 public:
  void setup() override;
  void dump_config() override;
  void update() override;

  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_dongle_serial(const std::string &serial) { this->dongle_serial_ = serial; }
  void set_inverter_serial_number(const std::string &serial) { this->inverter_serial_ = serial; }

  // Sensor Setters
  void set_pv1_voltage_sensor(sensor::Sensor *s) { this->float_sensors_["pv1_voltage"] = s; }
  void set_pv2_voltage_sensor(sensor::Sensor *s) { this->float_sensors_["pv2_voltage"] = s; }
  void set_pv3_voltage_sensor(sensor::Sensor *s) { this->float_sensors_["pv3_voltage"] = s; }
  void set_battery_voltage_sensor(sensor::Sensor *s) { this->float_sensors_["battery_voltage"] = s; }
  void set_soc_sensor(sensor::Sensor *s) { this->float_sensors_["soc"] = s; }
  void set_soh_sensor(sensor::Sensor *s) { this->float_sensors_["soh"] = s; }
  void set_pv1_power_sensor(sensor::Sensor *s) { this->float_sensors_["pv1_power"] = s; }
  void set_pv2_power_sensor(sensor::Sensor *s) { this->float_sensors_["pv2_power"] = s; }
  void set_pv3_power_sensor(sensor::Sensor *s) { this->float_sensors_["pv3_power"] = s; }
  void set_charge_power_sensor(sensor::Sensor *s) { this->float_sensors_["charge_power"] = s; }
  void set_discharge_power_sensor(sensor::Sensor *s) { this->float_sensors_["discharge_power"] = s; }
  void set_inverter_power_sensor(sensor::Sensor *s) { this->float_sensors_["inverter_power"] = s; }
  void set_power_to_grid_sensor(sensor::Sensor *s) { this->float_sensors_["power_to_grid"] = s; }
  void set_power_from_grid_sensor(sensor::Sensor *s) { this->float_sensors_["power_from_grid"] = s; }
  void set_grid_voltage_r_sensor(sensor::Sensor *s) { this->float_sensors_["grid_voltage_r"] = s; }
  void set_grid_voltage_s_sensor(sensor::Sensor *s) { this->float_sensors_["grid_voltage_s"] = s; }
  void set_grid_voltage_t_sensor(sensor::Sensor *s) { this->float_sensors_["grid_voltage_t"] = s; }
  void set_grid_frequency_sensor(sensor::Sensor *s) { this->float_sensors_["grid_frequency"] = s; }
  void set_power_factor_sensor(sensor::Sensor *s) { this->float_sensors_["power_factor"] = s; }
  void set_eps_voltage_r_sensor(sensor::Sensor *s) { this->float_sensors_["eps_voltage_r"] = s; }
  void set_eps_voltage_s_sensor(sensor::Sensor *s) { this->float_sensors_["eps_voltage_s"] = s; }
  void set_eps_voltage_t_sensor(sensor::Sensor *s) { this->float_sensors_["eps_voltage_t"] = s; }
  void set_eps_frequency_sensor(sensor::Sensor *s) { this->float_sensors_["eps_frequency"] = s; }
  void set_eps_active_power_sensor(sensor::Sensor *s) { this->float_sensors_["eps_active_power"] = s; }
  void set_eps_apparent_power_sensor(sensor::Sensor *s) { this->float_sensors_["eps_apparent_power"] = s; }
  void set_bus1_voltage_sensor(sensor::Sensor *s) { this->float_sensors_["bus1_voltage"] = s; }
  void set_bus2_voltage_sensor(sensor::Sensor *s) { this->float_sensors_["bus2_voltage"] = s; }
  void set_pv1_energy_today_sensor(sensor::Sensor *s) { this->float_sensors_["pv1_energy_today"] = s; }
  void set_pv2_energy_today_sensor(sensor::Sensor *s) { this->float_sensors_["pv2_energy_today"] = s; }
  void set_pv3_energy_today_sensor(sensor::Sensor *s) { this->float_sensors_["pv3_energy_today"] = s; }
  void set_inverter_energy_today_sensor(sensor::Sensor *s) { this->float_sensors_["inverter_energy_today"] = s; }
  void set_ac_charging_today_sensor(sensor::Sensor *s) { this->float_sensors_["ac_charging_today"] = s; }
  void set_charging_today_sensor(sensor::Sensor *s) { this->float_sensors_["charging_today"] = s; }
  void set_discharging_today_sensor(sensor::Sensor *s) { this->float_sensors_["discharging_today"] = s; }
  void set_eps_today_sensor(sensor::Sensor *s) { this->float_sensors_["eps_today"] = s; }
  void set_exported_today_sensor(sensor::Sensor *s) { this->float_sensors_["exported_today"] = s; }
  void set_grid_today_sensor(sensor::Sensor *s) { this->float_sensors_["grid_today"] = s; }
  void set_total_pv1_energy_sensor(sensor::Sensor *s) { this->float_sensors_["total_pv1_energy"] = s; }
  void set_total_pv2_energy_sensor(sensor::Sensor *s) { this->float_sensors_["total_pv2_energy"] = s; }
  void set_total_pv3_energy_sensor(sensor::Sensor *s) { this->float_sensors_["total_pv3_energy"] = s; }
  void set_total_inverter_output_sensor(sensor::Sensor *s) { this->float_sensors_["total_inverter_output"] = s; }
  void set_total_recharge_energy_sensor(sensor::Sensor *s) { this->float_sensors_["total_recharge_energy"] = s; }
  void set_total_charged_sensor(sensor::Sensor *s) { this->float_sensors_["total_charged"] = s; }
  void set_total_discharged_sensor(sensor::Sensor *s) { this->float_sensors_["total_discharged"] = s; }
  void set_total_eps_energy_sensor(sensor::Sensor *s) { this->float_sensors_["total_eps_energy"] = s; }
  void set_total_exported_sensor(sensor::Sensor *s) { this->float_sensors_["total_exported"] = s; }
  void set_total_imported_sensor(sensor::Sensor *s) { this->float_sensors_["total_imported"] = s; }
  void set_temp_inner_sensor(sensor::Sensor *s) { this->float_sensors_["temp_inner"] = s; }
  void set_temp_radiator_sensor(sensor::Sensor *s) { this->float_sensors_["temp_radiator"] = s; }
  void set_temp_radiator2_sensor(sensor::Sensor *s) { this->float_sensors_["temp_radiator2"] = s; }
  void set_temp_battery_sensor(sensor::Sensor *s) { this->float_sensors_["temp_battery"] = s; }
  void set_uptime_sensor(sensor::Sensor *s) { this->float_sensors_["uptime"] = s; }
  void set_max_charge_current_sensor(sensor::Sensor *s) { this->float_sensors_["max_charge_current"] = s; }
  void set_max_discharge_current_sensor(sensor::Sensor *s) { this->float_sensors_["max_discharge_current"] = s; }
  void set_charge_voltage_ref_sensor(sensor::Sensor *s) { this->float_sensors_["charge_voltage_ref"] = s; }
  void set_discharge_cutoff_voltage_sensor(sensor::Sensor *s) { this->float_sensors_["discharge_cutoff_voltage"] = s; }
  void set_battery_current_sensor(sensor::Sensor *s) { this->float_sensors_["battery_current"] = s; }
  void set_battery_count_sensor(sensor::Sensor *s) { this->float_sensors_["battery_count"] = s; }
  void set_battery_capacity_sensor(sensor::Sensor *s) { this->float_sensors_["battery_capacity"] = s; }
  void set_battery_status_inv_sensor(sensor::Sensor *s) { this->float_sensors_["battery_status_inv"] = s; }
  void set_max_cell_voltage_sensor(sensor::Sensor *s) { this->float_sensors_["max_cell_voltage"] = s; }
  void set_min_cell_voltage_sensor(sensor::Sensor *s) { this->float_sensors_["min_cell_voltage"] = s; }
  void set_max_cell_temp_sensor(sensor::Sensor *s) { this->float_sensors_["max_cell_temp"] = s; }
  void set_min_cell_temp_sensor(sensor::Sensor *s) { this->float_sensors_["min_cell_temp"] = s; }
  void set_cycle_count_sensor(sensor::Sensor *s) { this->float_sensors_["cycle_count"] = s; }
  void set_pload2_sensor(sensor::Sensor *s) { this->float_sensors_["p_load2"] = s; }
  void set_inverter_serial_sensor(text_sensor::TextSensor *s) { this->string_sensors_["inverter_serial"] = s; }
  void set_gen_input_volt_sensor(sensor::Sensor *s) { this->float_sensors_["gen_input_volt"] = s; }
  void set_gen_input_freq_sensor(sensor::Sensor *s) { this->float_sensors_["gen_input_freq"] = s; }
  void set_gen_power_watt_sensor(sensor::Sensor *s) { this->float_sensors_["gen_power_watt"] = s; }
  void set_gen_power_day_sensor(sensor::Sensor *s) { this->float_sensors_["gen_power_day"] = s; }
  void set_gen_power_all_sensor(sensor::Sensor *s) { this->float_sensors_["gen_power_all"] = s; }
  void set_eps_L1_volt_sensor(sensor::Sensor *s) { this->float_sensors_["eps_L1_volt"] = s; }
  void set_eps_L2_volt_sensor(sensor::Sensor *s) { this->float_sensors_["eps_L2_volt"] = s; }
  void set_eps_L1_watt_sensor(sensor::Sensor *s) { this->float_sensors_["eps_L1_watt"] = s; }
  void set_eps_L2_watt_sensor(sensor::Sensor *s) { this->float_sensors_["eps_L2_watt"] = s; }

 private:
  void process_next_float_();
  void process_next_string_();
  void request_bank_(uint8_t bank);
  void handle_response_(const uint8_t *buffer, size_t length);
  uint16_t calculate_crc_(const uint8_t *data, size_t len);
  void publish_state_(const std::string &key, float value);
  void publish_state_(const std::string &key, const std::string &value);
  void log_hex_buffer(const char* title, const uint8_t *buffer, size_t len);
  
  std::string host_;
  uint16_t port_;
  std::string dongle_serial_;
  std::string inverter_serial_;
  
  AsyncClient *tcp_client_{nullptr};
  std::map<std::string, sensor::Sensor *> float_sensors_;
  std::map<std::string, text_sensor::TextSensor *> string_sensors_;
  uint8_t next_bank_to_request_{0};

  // Internal queues for throttled updates
  std::queue<std::pair<std::string, float>> float_publish_queue_;
  std::queue<std::pair<std::string, std::string>> string_publish_queue_;
  bool float_publishing_{false};
  bool string_publishing_{false};
};

}  // namespace luxpower_sna
}  // namespace esphome
