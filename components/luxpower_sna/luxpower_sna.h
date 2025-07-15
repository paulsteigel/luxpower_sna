// luxpower_sna.h
#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/log.h"
#include <WiFiClient.h>
#include <cstring>

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

#pragma pack(push, 1)
struct LuxHeader {
  uint16_t prefix;
  uint16_t protocolVersion;
  uint16_t packetLength;
  uint8_t address;
  uint8_t function;
  char serialNumber[10];
  uint16_t dataLength;
};

struct LuxTranslatedData {
  uint8_t address;
  uint8_t deviceFunction;
  char serialNumber[10];
  uint16_t registerStart;
  uint8_t dataFieldLength;
};

struct LuxLogDataRawSection1 {
  uint16_t status;
  int16_t v_pv_1;
  int16_t v_pv_2;
  int16_t v_pv_3;
  int16_t v_bat;
  uint8_t soc;
  uint8_t soh;
  uint16_t internal_fault;
  int16_t p_pv_1;
  int16_t p_pv_2;
  int16_t p_pv_3;
  int16_t p_charge;
  int16_t p_discharge;
  int16_t v_ac_r;
  int16_t v_ac_s;
  int16_t v_ac_t;
  int16_t f_ac;
  int16_t p_inv;
  int16_t p_rec;
  int16_t rms_current;
  int16_t pf;
  int16_t v_eps_r;
  int16_t v_eps_s;
  int16_t v_eps_t;
  int16_t f_eps;
  int16_t p_to_eps;
  int16_t apparent_eps_power;
  int16_t p_to_grid;
  int16_t p_to_user;
  int16_t e_pv_1_day;
  int16_t e_pv_2_day;
  int16_t e_pv_3_day;
  int16_t e_inv_day;
  int16_t e_rec_day;
  int16_t e_chg_day;
  int16_t e_dischg_day;
  int16_t e_eps_day;
  int16_t e_to_grid_day;
  int16_t e_to_user_day;
  int16_t v_bus_1;
  int16_t v_bus_2;
};

struct LuxLogDataRawSection2 {
  int32_t e_pv_1_all;
  int32_t e_pv_2_all;
  int32_t e_pv_3_all;
  int32_t e_inv_all;
  int32_t e_rec_all;
  int32_t e_chg_all;
  int32_t e_dischg_all;
  int32_t e_eps_all;
  int32_t e_to_grid_all;
  int32_t e_to_user_all;
  uint32_t fault_code;
  uint32_t warning_code;
  int16_t t_inner;
  int16_t t_rad_1;
  int16_t t_rad_2;
  int16_t t_bat;
  uint16_t _reserved2;
  uint32_t uptime;
};

struct LuxLogDataRawSection3 {
  uint16_t _reserved3;
  int16_t max_chg_curr;
  int16_t max_dischg_curr;
  int16_t charge_volt_ref;
  int16_t dischg_cut_volt;
  uint8_t placeholder[20];
  int16_t bat_status_inv;
  int16_t bat_count;
  int16_t bat_capacity;
  int16_t bat_current;
  int16_t reg99;
  int16_t reg100;
  int16_t max_cell_volt;
  int16_t min_cell_volt;
  int16_t max_cell_temp;
  int16_t min_cell_temp;
  uint16_t _reserved4;
  int16_t bat_cycle_count;
  uint8_t _reserved5[14];
  int16_t p_load2;
};

struct LuxLogDataRawSection4 {
  uint16_t reg120;
  int16_t gen_input_volt;
  int16_t gen_input_freq;
  int16_t gen_power_watt;
  int16_t gen_power_day;
  int16_t gen_power_all;
  uint16_t reg126;
  int16_t eps_L1_volt;
  int16_t eps_L2_volt;
  int16_t eps_L1_watt;
  int16_t eps_L2_watt;
  uint8_t placeholder[50];
};

struct LuxLogDataRawSection5 {
  uint8_t _reserved7[20];
  int16_t p_load_ongrid;
  int16_t e_load_day;
  int16_t e_load_all_l;
  uint8_t _reserved8[54];
};
#pragma pack(pop)

class LuxpowerSNAComponent : public PollingComponent {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void update() override;
  
  void set_host(const std::string &host) { host_ = host; }
  void set_port(uint16_t port) { port_ = port; }
  void set_dongle_serial(const std::string &serial) { dongle_serial_ = serial; }
  void set_inverter_serial(const std::string &serial) { inverter_serial_ = serial; }

  // System Sensor Setters
  void set_lux_firmware_version_sensor(text_sensor::TextSensor *s) { lux_firmware_version_sensor_ = s; }
  void set_lux_inverter_model_sensor(text_sensor::TextSensor *s) { lux_inverter_model_sensor_ = s; }
  void set_lux_status_text_sensor(text_sensor::TextSensor *s) { lux_status_text_sensor_ = s; }
  void set_lux_battery_status_text_sensor(text_sensor::TextSensor *s) { lux_battery_status_text_sensor_ = s; }
  //void set_inverter_serial_number_sensor(text_sensor::TextSensor *s) { inverter_serial_number_sensor_ = s; }

  // Section1 Sensor Setters
  void set_lux_current_solar_voltage_1_sensor(sensor::Sensor *s) { lux_current_solar_voltage_1_sensor_ = s; }
  void set_lux_current_solar_voltage_2_sensor(sensor::Sensor *s) { lux_current_solar_voltage_2_sensor_ = s; }
  void set_lux_current_solar_voltage_3_sensor(sensor::Sensor *s) { lux_current_solar_voltage_3_sensor_ = s; }
  void set_lux_battery_voltage_sensor(sensor::Sensor *s) { lux_battery_voltage_sensor_ = s; }
  void set_lux_battery_percent_sensor(sensor::Sensor *s) { lux_battery_percent_sensor_ = s; }
  void set_soh_sensor(sensor::Sensor *s) { soh_sensor_ = s; }
  void set_lux_internal_fault_sensor(sensor::Sensor *s) { lux_internal_fault_sensor_ = s; }
  void set_lux_current_solar_output_1_sensor(sensor::Sensor *s) { lux_current_solar_output_1_sensor_ = s; }
  void set_lux_current_solar_output_2_sensor(sensor::Sensor *s) { lux_current_solar_output_2_sensor_ = s; }
  void set_lux_current_solar_output_3_sensor(sensor::Sensor *s) { lux_current_solar_output_3_sensor_ = s; }
  void set_lux_battery_charge_sensor(sensor::Sensor *s) { lux_battery_charge_sensor_ = s; }
  void set_lux_battery_discharge_sensor(sensor::Sensor *s) { lux_battery_discharge_sensor_ = s; }
  void set_lux_grid_voltage_r_sensor(sensor::Sensor *s) { lux_grid_voltage_r_sensor_ = s; }
  void set_lux_grid_voltage_s_sensor(sensor::Sensor *s) { lux_grid_voltage_s_sensor_ = s; }
  void set_lux_grid_voltage_t_sensor(sensor::Sensor *s) { lux_grid_voltage_t_sensor_ = s; }
  void set_lux_grid_frequency_live_sensor(sensor::Sensor *s) { lux_grid_frequency_live_sensor_ = s; }
  void set_lux_grid_voltage_live_sensor(sensor::Sensor *s) { lux_grid_voltage_live_sensor_ = s; }
  void set_lux_power_from_inverter_live_sensor(sensor::Sensor *s) { lux_power_from_inverter_live_sensor_ = s; }
  void set_lux_power_to_inverter_live_sensor(sensor::Sensor *s) { lux_power_to_inverter_live_sensor_ = s; }
  void set_lux_power_current_clamp_sensor(sensor::Sensor *s) { lux_power_current_clamp_sensor_ = s; }
  void set_grid_power_factor_sensor(sensor::Sensor *s) { grid_power_factor_sensor_ = s; }
  void set_eps_voltage_r_sensor(sensor::Sensor *s) { eps_voltage_r_sensor_ = s; }
  void set_eps_voltage_s_sensor(sensor::Sensor *s) { eps_voltage_s_sensor_ = s; }
  void set_eps_voltage_t_sensor(sensor::Sensor *s) { eps_voltage_t_sensor_ = s; }
  void set_eps_frequency_sensor(sensor::Sensor *s) { eps_frequency_sensor_ = s; }
  void set_lux_power_to_eps_sensor(sensor::Sensor *s) { lux_power_to_eps_sensor_ = s; }
  void set_lux_power_to_grid_live_sensor(sensor::Sensor *s) { lux_power_to_grid_live_sensor_ = s; }
  void set_lux_power_from_grid_live_sensor(sensor::Sensor *s) { lux_power_from_grid_live_sensor_ = s; }
  void set_lux_daily_solar_array_1_sensor(sensor::Sensor *s) { lux_daily_solar_array_1_sensor_ = s; }
  void set_lux_daily_solar_array_2_sensor(sensor::Sensor *s) { lux_daily_solar_array_2_sensor_ = s; }
  void set_lux_daily_solar_array_3_sensor(sensor::Sensor *s) { lux_daily_solar_array_3_sensor_ = s; }
  void set_lux_power_from_inverter_daily_sensor(sensor::Sensor *s) { lux_power_from_inverter_daily_sensor_ = s; }
  void set_lux_power_to_inverter_daily_sensor(sensor::Sensor *s) { lux_power_to_inverter_daily_sensor_ = s; }
  void set_lux_daily_battery_charge_sensor(sensor::Sensor *s) { lux_daily_battery_charge_sensor_ = s; }
  void set_lux_daily_battery_discharge_sensor(sensor::Sensor *s) { lux_daily_battery_discharge_sensor_ = s; }
  void set_lux_power_to_eps_daily_sensor(sensor::Sensor *s) { lux_power_to_eps_daily_sensor_ = s; }
  void set_lux_power_to_grid_daily_sensor(sensor::Sensor *s) { lux_power_to_grid_daily_sensor_ = s; }
  void set_lux_power_from_grid_daily_sensor(sensor::Sensor *s) { lux_power_from_grid_daily_sensor_ = s; }
  void set_bus1_voltage_sensor(sensor::Sensor *s) { bus1_voltage_sensor_ = s; }
  void set_bus2_voltage_sensor(sensor::Sensor *s) { bus2_voltage_sensor_ = s; }
  void set_lux_current_solar_output_sensor(sensor::Sensor *s) { lux_current_solar_output_sensor_ = s; }
  void set_lux_daily_solar_sensor(sensor::Sensor *s) { lux_daily_solar_sensor_ = s; }
  void set_lux_power_to_home_sensor(sensor::Sensor *s) { lux_power_to_home_sensor_ = s; }
  void set_lux_battery_flow_sensor(sensor::Sensor *s) { lux_battery_flow_sensor_ = s; }
  void set_lux_grid_flow_sensor(sensor::Sensor *s) { lux_grid_flow_sensor_ = s; }
  void set_lux_home_consumption_live_sensor(sensor::Sensor *s) { lux_home_consumption_live_sensor_ = s; }
  void set_lux_home_consumption_sensor(sensor::Sensor *s) { lux_home_consumption_sensor_ = s; }

  // Section2 Sensor Setters
  void set_lux_total_solar_array_1_sensor(sensor::Sensor *s) { lux_total_solar_array_1_sensor_ = s; }
  void set_lux_total_solar_array_2_sensor(sensor::Sensor *s) { lux_total_solar_array_2_sensor_ = s; }
  void set_lux_total_solar_array_3_sensor(sensor::Sensor *s) { lux_total_solar_array_3_sensor_ = s; }
  void set_lux_power_from_inverter_total_sensor(sensor::Sensor *s) { lux_power_from_inverter_total_sensor_ = s; }
  void set_lux_power_to_inverter_total_sensor(sensor::Sensor *s) { lux_power_to_inverter_total_sensor_ = s; }
  void set_lux_total_battery_charge_sensor(sensor::Sensor *s) { lux_total_battery_charge_sensor_ = s; }
  void set_lux_total_battery_discharge_sensor(sensor::Sensor *s) { lux_total_battery_discharge_sensor_ = s; }
  void set_lux_power_to_eps_total_sensor(sensor::Sensor *s) { lux_power_to_eps_total_sensor_ = s; }
  void set_lux_power_to_grid_total_sensor(sensor::Sensor *s) { lux_power_to_grid_total_sensor_ = s; }
  void set_lux_power_from_grid_total_sensor(sensor::Sensor *s) { lux_power_from_grid_total_sensor_ = s; }
  void set_lux_fault_code_sensor(sensor::Sensor *s) { lux_fault_code_sensor_ = s; }
  void set_lux_warning_code_sensor(sensor::Sensor *s) { lux_warning_code_sensor_ = s; }
  void set_lux_internal_temp_sensor(sensor::Sensor *s) { lux_internal_temp_sensor_ = s; }
  void set_lux_radiator1_temp_sensor(sensor::Sensor *s) { lux_radiator1_temp_sensor_ = s; }
  void set_lux_radiator2_temp_sensor(sensor::Sensor *s) { lux_radiator2_temp_sensor_ = s; }
  void set_lux_battery_temperature_live_sensor(sensor::Sensor *s) { lux_battery_temperature_live_sensor_ = s; }
  void set_lux_uptime_sensor(sensor::Sensor *s) { lux_uptime_sensor_ = s; }
  void set_lux_total_solar_sensor(sensor::Sensor *s) { lux_total_solar_sensor_ = s; }
  void set_lux_home_consumption_total_sensor(sensor::Sensor *s) { lux_home_consumption_total_sensor_ = s; }

  // Section3 Sensor Setters
  void set_lux_bms_limit_charge_sensor(sensor::Sensor *s) { lux_bms_limit_charge_sensor_ = s; }
  void set_lux_bms_limit_discharge_sensor(sensor::Sensor *s) { lux_bms_limit_discharge_sensor_ = s; }
  void set_charge_voltage_ref_sensor(sensor::Sensor *s) { charge_voltage_ref_sensor_ = s; }
  void set_discharge_cutoff_voltage_sensor(sensor::Sensor *s) { discharge_cutoff_voltage_sensor_ = s; }
  void set_battery_status_inv_sensor(sensor::Sensor *s) { battery_status_inv_sensor_ = s; }
  void set_lux_battery_count_sensor(sensor::Sensor *s) { lux_battery_count_sensor_ = s; }
  void set_lux_battery_capacity_ah_sensor(sensor::Sensor *s) { lux_battery_capacity_ah_sensor_ = s; }
  void set_lux_battery_current_sensor(sensor::Sensor *s) { lux_battery_current_sensor_ = s; }
  void set_max_cell_volt_sensor(sensor::Sensor *s) { max_cell_volt_sensor_ = s; }
  void set_min_cell_volt_sensor(sensor::Sensor *s) { min_cell_volt_sensor_ = s; }
  void set_max_cell_temp_sensor(sensor::Sensor *s) { max_cell_temp_sensor_ = s; }
  void set_min_cell_temp_sensor(sensor::Sensor *s) { min_cell_temp_sensor_ = s; }
  void set_lux_battery_cycle_count_sensor(sensor::Sensor *s) { lux_battery_cycle_count_sensor_ = s; }
  void set_lux_home_consumption_2_live_sensor(sensor::Sensor *s) { lux_home_consumption_2_live_sensor_ = s; }

  // Section4 Sensor Setters
  void set_lux_current_generator_voltage_sensor(sensor::Sensor *s) { lux_current_generator_voltage_sensor_ = s; }
  void set_lux_current_generator_frequency_sensor(sensor::Sensor *s) { lux_current_generator_frequency_sensor_ = s; }
  void set_lux_current_generator_power_sensor(sensor::Sensor *s) { lux_current_generator_power_sensor_ = s; }
  void set_lux_current_generator_power_daily_sensor(sensor::Sensor *s) { lux_current_generator_power_daily_sensor_ = s; }
  void set_lux_current_generator_power_all_sensor(sensor::Sensor *s) { lux_current_generator_power_all_sensor_ = s; }
  void set_lux_current_eps_L1_voltage_sensor(sensor::Sensor *s) { lux_current_eps_L1_voltage_sensor_ = s; }
  void set_lux_current_eps_L2_voltage_sensor(sensor::Sensor *s) { lux_current_eps_L2_voltage_sensor_ = s; }
  void set_lux_current_eps_L1_watt_sensor(sensor::Sensor *s) { lux_current_eps_L1_watt_sensor_ = s; }
  void set_lux_current_eps_L2_watt_sensor(sensor::Sensor *s) { lux_current_eps_L2_watt_sensor_ = s; }

  // Section5 Sensor Setters
  void set_p_load_ongrid_sensor(sensor::Sensor *s) { p_load_ongrid_sensor_ = s; }
  void set_e_load_day_sensor(sensor::Sensor *s) { e_load_day_sensor_ = s; }
  void set_e_load_all_l_sensor(sensor::Sensor *s) { e_load_all_l_sensor_ = s; }

 private:
  WiFiClient client_;
  uint8_t next_bank_index_{0};
  //const uint8_t banks_[5] = {0, 40, 80, 120, 160};

  // added 15/7 for connection handling
  bool connected_{false};
  uint32_t last_heartbeat_{0};
  uint8_t current_bank_{0};
  uint8_t banks_[5] = {0, 40, 80, 120, 160};
  std::vector<uint8_t> packet_buffer_;

  void request_bank_(uint8_t bank);
  bool receive_response_(uint8_t bank);
  uint16_t calculate_crc_(const uint8_t *data, size_t len);
  void process_section1_(const LuxLogDataRawSection1 &data);
  void process_section2_(const LuxLogDataRawSection2 &data);
  void process_section3_(const LuxLogDataRawSection3 &data);
  void process_section4_(const LuxLogDataRawSection4 &data);
  void process_section5_(const LuxLogDataRawSection5 &data);
  void publish_sensor_(sensor::Sensor *sensor, float value);
  void publish_text_sensor_(text_sensor::TextSensor *sensor, const std::string &value);

  std::string host_;
  uint16_t port_;
  std::string dongle_serial_;
  std::string inverter_serial_;
  
  // Status text mappings
  static const char *STATUS_TEXTS[193];
  static const char *BATTERY_STATUS_TEXTS[17];

  // System Sensors
  text_sensor::TextSensor *lux_firmware_version_sensor_{nullptr};
  text_sensor::TextSensor *lux_inverter_model_sensor_{nullptr};
  text_sensor::TextSensor *lux_status_text_sensor_{nullptr};
  text_sensor::TextSensor *lux_battery_status_text_sensor_{nullptr};
  text_sensor::TextSensor *inverter_serial_number_sensor_{nullptr};

  // Section1 Sensors
  sensor::Sensor *lux_current_solar_voltage_1_sensor_{nullptr};
  sensor::Sensor *lux_current_solar_voltage_2_sensor_{nullptr};
  sensor::Sensor *lux_current_solar_voltage_3_sensor_{nullptr};
  sensor::Sensor *lux_battery_voltage_sensor_{nullptr};
  sensor::Sensor *lux_battery_percent_sensor_{nullptr};
  sensor::Sensor *soh_sensor_{nullptr};
  sensor::Sensor *lux_internal_fault_sensor_{nullptr};
  sensor::Sensor *lux_current_solar_output_1_sensor_{nullptr};
  sensor::Sensor *lux_current_solar_output_2_sensor_{nullptr};
  sensor::Sensor *lux_current_solar_output_3_sensor_{nullptr};
  sensor::Sensor *lux_battery_charge_sensor_{nullptr};
  sensor::Sensor *lux_battery_discharge_sensor_{nullptr};
  sensor::Sensor *lux_grid_voltage_r_sensor_{nullptr};
  sensor::Sensor *lux_grid_voltage_s_sensor_{nullptr};
  sensor::Sensor *lux_grid_voltage_t_sensor_{nullptr};
  sensor::Sensor *lux_grid_frequency_live_sensor_{nullptr};
  sensor::Sensor *lux_grid_voltage_live_sensor_{nullptr};
  sensor::Sensor *lux_power_from_inverter_live_sensor_{nullptr};
  sensor::Sensor *lux_power_to_inverter_live_sensor_{nullptr};
  sensor::Sensor *lux_power_current_clamp_sensor_{nullptr};
  sensor::Sensor *grid_power_factor_sensor_{nullptr};
  sensor::Sensor *eps_voltage_r_sensor_{nullptr};
  sensor::Sensor *eps_voltage_s_sensor_{nullptr};
  sensor::Sensor *eps_voltage_t_sensor_{nullptr};
  sensor::Sensor *eps_frequency_sensor_{nullptr};
  sensor::Sensor *lux_power_to_eps_sensor_{nullptr};
  sensor::Sensor *lux_power_to_grid_live_sensor_{nullptr};
  sensor::Sensor *lux_power_from_grid_live_sensor_{nullptr};
  sensor::Sensor *lux_daily_solar_array_1_sensor_{nullptr};
  sensor::Sensor *lux_daily_solar_array_2_sensor_{nullptr};
  sensor::Sensor *lux_daily_solar_array_3_sensor_{nullptr};
  sensor::Sensor *lux_power_from_inverter_daily_sensor_{nullptr};
  sensor::Sensor *lux_power_to_inverter_daily_sensor_{nullptr};
  sensor::Sensor *lux_daily_battery_charge_sensor_{nullptr};
  sensor::Sensor *lux_daily_battery_discharge_sensor_{nullptr};
  sensor::Sensor *lux_power_to_eps_daily_sensor_{nullptr};
  sensor::Sensor *lux_power_to_grid_daily_sensor_{nullptr};
  sensor::Sensor *lux_power_from_grid_daily_sensor_{nullptr};
  sensor::Sensor *bus1_voltage_sensor_{nullptr};
  sensor::Sensor *bus2_voltage_sensor_{nullptr};
  sensor::Sensor *lux_current_solar_output_sensor_{nullptr};
  sensor::Sensor *lux_daily_solar_sensor_{nullptr};
  sensor::Sensor *lux_power_to_home_sensor_{nullptr};
  sensor::Sensor *lux_battery_flow_sensor_{nullptr};
  sensor::Sensor *lux_grid_flow_sensor_{nullptr};
  sensor::Sensor *lux_home_consumption_live_sensor_{nullptr};
  sensor::Sensor *lux_home_consumption_sensor_{nullptr};

  // Section2 Sensors
  sensor::Sensor *lux_total_solar_array_1_sensor_{nullptr};
  sensor::Sensor *lux_total_solar_array_2_sensor_{nullptr};
  sensor::Sensor *lux_total_solar_array_3_sensor_{nullptr};
  sensor::Sensor *lux_power_from_inverter_total_sensor_{nullptr};
  sensor::Sensor *lux_power_to_inverter_total_sensor_{nullptr};
  sensor::Sensor *lux_total_battery_charge_sensor_{nullptr};
  sensor::Sensor *lux_total_battery_discharge_sensor_{nullptr};
  sensor::Sensor *lux_power_to_eps_total_sensor_{nullptr};
  sensor::Sensor *lux_power_to_grid_total_sensor_{nullptr};
  sensor::Sensor *lux_power_from_grid_total_sensor_{nullptr};
  sensor::Sensor *lux_fault_code_sensor_{nullptr};
  sensor::Sensor *lux_warning_code_sensor_{nullptr};
  sensor::Sensor *lux_internal_temp_sensor_{nullptr};
  sensor::Sensor *lux_radiator1_temp_sensor_{nullptr};
  sensor::Sensor *lux_radiator2_temp_sensor_{nullptr};
  sensor::Sensor *lux_battery_temperature_live_sensor_{nullptr};
  sensor::Sensor *lux_uptime_sensor_{nullptr};
  sensor::Sensor *lux_total_solar_sensor_{nullptr};
  sensor::Sensor *lux_home_consumption_total_sensor_{nullptr};

  // Section3 Sensors
  sensor::Sensor *lux_bms_limit_charge_sensor_{nullptr};
  sensor::Sensor *lux_bms_limit_discharge_sensor_{nullptr};
  sensor::Sensor *charge_voltage_ref_sensor_{nullptr};
  sensor::Sensor *discharge_cutoff_voltage_sensor_{nullptr};
  sensor::Sensor *battery_status_inv_sensor_{nullptr};
  sensor::Sensor *lux_battery_count_sensor_{nullptr};
  sensor::Sensor *lux_battery_capacity_ah_sensor_{nullptr};
  sensor::Sensor *lux_battery_current_sensor_{nullptr};
  sensor::Sensor *max_cell_volt_sensor_{nullptr};
  sensor::Sensor *min_cell_volt_sensor_{nullptr};
  sensor::Sensor *max_cell_temp_sensor_{nullptr};
  sensor::Sensor *min_cell_temp_sensor_{nullptr};
  sensor::Sensor *lux_battery_cycle_count_sensor_{nullptr};
  sensor::Sensor *lux_home_consumption_2_live_sensor_{nullptr};

  // Section4 Sensors
  sensor::Sensor *lux_current_generator_voltage_sensor_{nullptr};
  sensor::Sensor *lux_current_generator_frequency_sensor_{nullptr};
  sensor::Sensor *lux_current_generator_power_sensor_{nullptr};
  sensor::Sensor *lux_current_generator_power_daily_sensor_{nullptr};
  sensor::Sensor *lux_current_generator_power_all_sensor_{nullptr};
  sensor::Sensor *lux_current_eps_L1_voltage_sensor_{nullptr};
  sensor::Sensor *lux_current_eps_L2_voltage_sensor_{nullptr};
  sensor::Sensor *lux_current_eps_L1_watt_sensor_{nullptr};
  sensor::Sensor *lux_current_eps_L2_watt_sensor_{nullptr};

  // Section5 Sensors
  sensor::Sensor *p_load_ongrid_sensor_{nullptr};
  sensor::Sensor *e_load_day_sensor_{nullptr};
  sensor::Sensor *e_load_all_l_sensor_{nullptr};
};

}  // namespace luxpower_sna
}  // namespace esphome
