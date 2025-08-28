// luxpower_sna.h
#pragma once
#include "esphome/core/hal.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/template/text/template_text.h"
#include "esphome/components/template/number/template_number.h"

// Framework-specific includes
#ifdef USE_ESP_IDF
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/sys.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#else
#include <WiFiClient.h>
#ifdef USE_ESP32
#include <WiFi.h>
#elif USE_ESP8266
#include <ESP8266WiFi.h>
#endif
#endif

#include <vector>
#include <queue>
#include <cstring>
#include <functional>
#include <map>

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

enum class ConnectionState {
  DISCONNECTED,
  CONNECTING,
  CONNECTED,
  REQUESTING_DATA,
  WAITING_RESPONSE,
  PROCESSING_RESPONSE,
  ERROR
};

enum class DataBankState {
  IDLE,
  SENDING_REQUEST,
  WAITING_RESPONSE,
  PROCESSING_DATA,
  COMPLETED,
  ERROR
};

#pragma pack(push, 1)
struct LuxHeader {
  uint16_t prefix; uint16_t protocolVersion; uint16_t packetLength; uint8_t address;
  uint8_t function; char serialNumber[10]; uint16_t dataLength;
};
struct LuxTranslatedData {
  uint8_t address; uint8_t deviceFunction; char serialNumber[10];
  uint16_t registerStart; uint8_t dataFieldLength;
};
struct LuxLogDataRawSection1 {
  uint16_t status;
  int16_t v_pv_1, v_pv_2, v_pv_3, v_bat;
  uint8_t soc, soh;
  uint16_t internal_fault;
  int16_t p_pv_1, p_pv_2, p_pv_3, p_charge, p_discharge;
  int16_t v_ac_r, v_ac_s, v_ac_t, f_ac, p_inv, p_rec;
  int16_t rms_current, pf;
  int16_t v_eps_r, v_eps_s, v_eps_t, f_eps, p_to_eps, apparent_eps_power;
  int16_t p_to_grid, p_to_user;
  int16_t e_pv_1_day, e_pv_2_day, e_pv_3_day, e_inv_day, e_rec_day;
  int16_t e_chg_day, e_dischg_day, e_eps_day, e_to_grid_day, e_to_user_day;
  int16_t v_bus_1, v_bus_2;
};
struct LuxLogDataRawSection2 {
  int32_t e_pv_1_all, e_pv_2_all, e_pv_3_all, e_inv_all, e_rec_all;
  int32_t e_chg_all, e_dischg_all, e_eps_all, e_to_grid_all, e_to_user_all;
  uint32_t fault_code, warning_code;
  int16_t t_inner, t_rad_1, t_rad_2, t_bat;
  uint16_t _reserved2;
  uint32_t uptime;
};
struct LuxLogDataRawSection3 {
  uint16_t _reserved3;
  int16_t max_chg_curr, max_dischg_curr, charge_volt_ref, dischg_cut_volt;
  uint8_t placeholder[20];
  int16_t bat_status_inv, bat_count, bat_capacity, bat_current;
  int16_t reg99, reg100;
  int16_t max_cell_volt, min_cell_volt, max_cell_temp, min_cell_temp;
  uint16_t _reserved4;
  int16_t bat_cycle_count;
  uint8_t _reserved5[14];
  int16_t p_load2;
};
struct LuxLogDataRawSection4 {
  uint16_t reg120; int16_t gen_input_volt, gen_input_freq, gen_power_watt; int16_t gen_power_day, gen_power_all;
  uint16_t reg126; int16_t eps_L1_volt, eps_L2_volt, eps_L1_watt, eps_L2_watt; uint8_t placeholder[50];
};
struct LuxLogDataRawSection5 {
  uint8_t _reserved7[20]; 
  int16_t p_load_ongrid, e_load_day, e_load_all_l; 
  uint8_t _reserved8[54];
};
#pragma pack(pop)

class LuxpowerSNAComponent : public PollingComponent {
 public:
  void setup() override;
  void dump_config() override;
  void update() override;
  void loop() override;  // Non-blocking processing

  // Methods for switch support
  void read_register_async(uint16_t reg, std::function<void(uint16_t)> callback);
  void write_register_async(uint16_t reg, uint16_t value, std::function<void(bool)> callback);
  
  bool is_connection_ready() const {
    return connection_state_ == ConnectionState::CONNECTED && !processing_async_request_;
  }
  
  // Alternative method names in case you're using different naming
  bool is_connected() const {
    return is_connection_ready();
  }

  // Template input setters
  void set_host_input(template_::TemplateText *input) { host_input_ = input; }
  void set_port_input(template_::TemplateNumber *input) { port_input_ = input; }
  void set_dongle_serial_input(template_::TemplateText *input) { dongle_serial_input_ = input; }
  void set_inverter_serial_input(template_::TemplateText *input) { inverter_serial_input_ = input; }

  // Sensor setters
  void set_lux_firmware_version_sensor(text_sensor::TextSensor *s) { lux_firmware_version_sensor_ = s; }
  void set_lux_inverter_model_sensor(text_sensor::TextSensor *s) { lux_inverter_model_sensor_ = s; }
  void set_lux_status_text_sensor(text_sensor::TextSensor *s) { lux_status_text_sensor_ = s; }
  void set_lux_battery_status_text_sensor(text_sensor::TextSensor *s) { lux_battery_status_text_sensor_ = s; }
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
  
  // Section2 sensors
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
  
  // Section3 sensors
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
  
  // Section4 sensors
  void set_lux_current_generator_voltage_sensor(sensor::Sensor *s) { lux_current_generator_voltage_sensor_ = s; }
  void set_lux_current_generator_frequency_sensor(sensor::Sensor *s) { lux_current_generator_frequency_sensor_ = s; }
  void set_lux_current_generator_power_sensor(sensor::Sensor *s) { lux_current_generator_power_sensor_ = s; }
  void set_lux_current_generator_power_daily_sensor(sensor::Sensor *s) { lux_current_generator_power_daily_sensor_ = s; }
  void set_lux_current_generator_power_all_sensor(sensor::Sensor *s) { lux_current_generator_power_all_sensor_ = s; }
  void set_lux_current_eps_L1_voltage_sensor(sensor::Sensor *s) { lux_current_eps_L1_voltage_sensor_ = s; }
  void set_lux_current_eps_L2_voltage_sensor(sensor::Sensor *s) { lux_current_eps_L2_voltage_sensor_ = s; }
  void set_lux_current_eps_L1_watt_sensor(sensor::Sensor *s) { lux_current_eps_L1_watt_sensor_ = s; }
  void set_lux_current_eps_L2_watt_sensor(sensor::Sensor *s) { lux_current_eps_L2_watt_sensor_ = s; }
  
  // Section5 sensors
  void set_p_load_ongrid_sensor(sensor::Sensor *s) { p_load_ongrid_sensor_ = s; }
  void set_e_load_day_sensor(sensor::Sensor *s) { e_load_day_sensor_ = s; }
  void set_e_load_all_l_sensor(sensor::Sensor *s) { e_load_all_l_sensor_ = s; }

 protected:
  // Non-blocking state machine methods
  void handle_disconnected_state_();
  void handle_connecting_state_();
  void handle_connected_state_();
  void handle_requesting_data_state_();
  void handle_waiting_response_state_();
  void handle_processing_response_state_();
  void handle_error_state_();
  
  // Helper methods
  bool validate_runtime_parameters_();
  std::string get_host_from_input_();
  uint16_t get_port_from_input_();
  std::string get_dongle_serial_from_input_();
  std::string get_inverter_serial_from_input_();
  const char* get_state_name_(ConnectionState state);
  
  // Framework-agnostic networking methods
  bool start_connection_attempt_();
  bool check_connection_ready_();
  bool send_bank_request_();
  bool read_available_data_();
  bool process_received_data_();
  void disconnect_client_();
  
  // Data processing
  uint16_t calculate_crc_(const uint8_t *data, size_t len);
  void process_section1_(const LuxLogDataRawSection1 &data);
  void process_section2_(const LuxLogDataRawSection2 &data);
  void process_section3_(const LuxLogDataRawSection3 &data);
  void process_section4_(const LuxLogDataRawSection4 &data);
  void process_section5_(const LuxLogDataRawSection5 &data);
  void publish_sensor_(sensor::Sensor *sensor, float value);
  void publish_text_sensor_(text_sensor::TextSensor *sensor, const std::string &value);

  // Methods for switch support
  void process_async_requests_();
  std::vector<uint8_t> prepare_single_register_read_packet_(uint16_t reg);
  std::vector<uint8_t> prepare_single_register_write_packet_(uint16_t reg, uint16_t value);
  bool send_packet_(const std::vector<uint8_t> &packet);

 private:
  // Template input references
  template_::TemplateText *host_input_{nullptr};
  template_::TemplateNumber *port_input_{nullptr};
  template_::TemplateText *dongle_serial_input_{nullptr};
  template_::TemplateText *inverter_serial_input_{nullptr};

  // Framework-specific connection management
#ifdef USE_ESP_IDF
  int socket_fd_{-1};
  struct sockaddr_in server_addr_{};
#else
  WiFiClient client_;
#endif

  // Non-blocking connection management
  ConnectionState connection_state_;
  DataBankState bank_state_;
  uint8_t current_bank_index_;
  uint32_t state_start_time_;
  uint32_t last_connection_attempt_;
  bool initialization_complete_;
  
  // Response buffer management
  std::vector<uint8_t> response_buffer_;
  size_t expected_response_size_;
  
  // Data banks to query
  const uint8_t banks_[5] = {0, 40, 80, 120, 160};
  
  // Status text arrays
  static const char *STATUS_TEXTS[193];
  static const char *BATTERY_STATUS_TEXTS[17];

  // For switch support
  struct AsyncRequest {
    enum Type { READ, WRITE };
    Type type;
    uint16_t reg;
    uint16_t value;  // for write operations
    std::function<void(uint16_t)> read_callback;
    std::function<void(bool)> write_callback;
    uint32_t timestamp;
  };
  std::vector<AsyncRequest> async_requests_;
  std::map<uint16_t, uint16_t> register_cache_;
  bool processing_async_request_{false};
  uint32_t async_request_start_time_{0};

  // Sensor pointer declarations
  text_sensor::TextSensor *lux_firmware_version_sensor_{nullptr};
  text_sensor::TextSensor *lux_inverter_model_sensor_{nullptr};
  text_sensor::TextSensor *lux_status_text_sensor_{nullptr};
  text_sensor::TextSensor *lux_battery_status_text_sensor_{nullptr};
  
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
  
  // Section2 sensors
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
  
  // Section3 sensors
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
  
  // Section4 sensors
  sensor::Sensor *lux_current_generator_voltage_sensor_{nullptr};
  sensor::Sensor *lux_current_generator_frequency_sensor_{nullptr};
  sensor::Sensor *lux_current_generator_power_sensor_{nullptr};
  sensor::Sensor *lux_current_generator_power_daily_sensor_{nullptr};
  sensor::Sensor *lux_current_generator_power_all_sensor_{nullptr};
  sensor::Sensor *lux_current_eps_L1_voltage_sensor_{nullptr};
  sensor::Sensor *lux_current_eps_L2_voltage_sensor_{nullptr};
  sensor::Sensor *lux_current_eps_L1_watt_sensor_{nullptr};
  sensor::Sensor *lux_current_eps_L2_watt_sensor_{nullptr};
  
  // Section5 sensors
  sensor::Sensor *p_load_ongrid_sensor_{nullptr};
  sensor::Sensor *e_load_day_sensor_{nullptr};
  sensor::Sensor *e_load_all_l_sensor_{nullptr};
};

}  // namespace luxpower_sna
}  // namespace esphome
