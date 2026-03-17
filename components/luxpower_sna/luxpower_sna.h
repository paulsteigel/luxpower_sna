#pragma once

// ---------------------------------------------------------------------------
// LuxPower SNA ESPHome Component – IDF-compatible (lwip sockets, no WiFiClient)
// Supports: sensors (READ_INPUT), switches (READ_HOLD / WRITE_SINGLE), numbers
// ---------------------------------------------------------------------------

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"         // millis()
#include "esphome/core/preferences.h"  // NVS host persistence
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"
#include "esphome/components/button/button.h"
#include "esphome/core/time.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

// for scanning dongle ip address
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ioctl(FIONBIO) is used instead of fcntl(O_NONBLOCK) for IDF socket compatibility
#include <queue>
#include <vector>
#include <cstring>

namespace esphome {
namespace luxpower_sna {

static const char *const TAG = "luxpower_sna";

// ---------------------------------------------------------------------------
// Protocol constants
// ---------------------------------------------------------------------------
static const uint8_t  LUX_TCP_TRANSLATED_DATA = 0xC2;  // 194
static const uint8_t  LUX_TCP_HEARTBEAT       = 0xC1;  // 193
static const uint8_t  LUX_FN_READ_HOLD        = 0x03;
static const uint8_t  LUX_FN_READ_INPUT       = 0x04;
static const uint8_t  LUX_FN_WRITE_SINGLE     = 0x06;
static const uint8_t  LUX_ACTION_WRITE        = 0x00;  // used for ALL requests per Python lib

// ---------------------------------------------------------------------------
// Packed structs for INPUT data banks
// ---------------------------------------------------------------------------
#pragma pack(push, 1)

struct LuxHeader {
    uint16_t prefix;
    uint16_t protocol;
    uint16_t frame_length;
    uint8_t  address;
    uint8_t  tcp_function;
    char     dongle[10];
    uint16_t data_length;
};  // 20 bytes

struct LuxTranslatedData {
    uint8_t  address;
    uint8_t  device_function;
    char     serial[10];
    uint16_t reg_start;
    uint8_t  value_length;
};  // 15 bytes

// Bank 0: registers 0-39
struct Bank0 {
    uint16_t status;
    int16_t  v_pv_1, v_pv_2, v_pv_3, v_bat;
    uint8_t  soc, soh;
    uint16_t internal_fault;
    int16_t  p_pv_1, p_pv_2, p_pv_3, p_charge, p_discharge;
    int16_t  v_ac_r, v_ac_s, v_ac_t, f_ac, p_inv, p_rec;
    int16_t  rms_current, pf;
    int16_t  v_eps_r, v_eps_s, v_eps_t, f_eps, p_to_eps, apparent_eps_power;
    int16_t  p_to_grid, p_to_user;
    int16_t  e_pv_1_day, e_pv_2_day, e_pv_3_day, e_inv_day, e_rec_day;
    int16_t  e_chg_day, e_dischg_day, e_eps_day, e_to_grid_day, e_to_user_day;
    int16_t  v_bus_1, v_bus_2;
};  // 80 bytes = 40 registers

// Bank 1: registers 40-79
struct Bank1 {
    int32_t  e_pv_1_all, e_pv_2_all, e_pv_3_all;
    int32_t  e_inv_all, e_rec_all, e_chg_all, e_dischg_all, e_eps_all;
    int32_t  e_to_grid_all, e_to_user_all;
    uint32_t fault_code, warning_code;
    int16_t  t_inner, t_rad_1, t_rad_2, t_bat;
    uint16_t _reserved68;
    uint32_t uptime;
    uint8_t  _tail[18];
};  // 80 bytes = 40 registers

// Bank 2: registers 80-119
struct Bank2 {
    uint16_t _r80;
    int16_t  max_chg_curr, max_dischg_curr, charge_volt_ref, dischg_cut_volt;
    uint8_t  _placeholder[20];
    int16_t  bat_status_inv;
    int16_t  bat_count;
    int16_t  bat_capacity;
    int16_t  bat_current;
    int16_t  _r99, _r100;
    int16_t  max_cell_volt;
    int16_t  min_cell_volt;
    int16_t  max_cell_temp;
    int16_t  min_cell_temp;
    uint16_t _r105;
    int16_t  bat_cycle_count;
    uint8_t  _r107_113[14];
    int16_t  p_load2;
    uint8_t  _r115_119[10];
};  // 80 bytes

// Bank 3: registers 120-159
struct Bank3 {
    uint16_t _r120;
    int16_t  gen_input_volt;
    int16_t  gen_input_freq;
    int16_t  gen_power_watt;
    int16_t  gen_power_day;
    int16_t  gen_power_all;
    uint16_t _r126;
    int16_t  eps_L1_volt;
    int16_t  eps_L2_volt;
    int16_t  eps_L1_watt;
    int16_t  eps_L2_watt;
    uint8_t  _r131_159[58];
};  // 80 bytes

// Bank 4: registers 160-199
struct Bank4 {
    uint8_t  _r160_169[20];
    int16_t  p_load_ongrid;
    int16_t  e_load_day;
    int16_t  e_load_all_l;
    uint8_t  _r173_199[54];
};  // 80 bytes

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Write command (from switches / numbers)
// ---------------------------------------------------------------------------
struct WriteCmd {
    uint16_t reg;
    uint16_t value;
};

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
class LuxpowerSNAComponent;

// ---------------------------------------------------------------------------
// Switch entity
// ---------------------------------------------------------------------------
class LuxpowerSNASwitch : public switch_::Switch, public Component {
 public:
    void set_parent(LuxpowerSNAComponent *parent) { parent_ = parent; }
    void set_register(uint16_t reg)  { register_addr_ = reg; }
    void set_bitmask(uint16_t mask)  { bitmask_ = mask; }
    uint16_t get_register() const    { return register_addr_; }
    uint16_t get_bitmask()  const    { return bitmask_; }
    void on_hold_update(const uint16_t *hold_regs);

 protected:
    void write_state(bool state) override;

 private:
    LuxpowerSNAComponent *parent_{nullptr};
    uint16_t register_addr_{0};
    uint16_t bitmask_{0};
};

// ---------------------------------------------------------------------------
// Number entity
// ---------------------------------------------------------------------------
class LuxpowerSNANumber : public number::Number, public Component {
 public:
    void set_parent(LuxpowerSNAComponent *parent) { parent_ = parent; }
    void set_register(uint16_t reg)   { register_addr_ = reg; }
    void set_bitmask(uint16_t mask)   { bitmask_ = mask; }
    void set_bitshift(uint8_t shift)  { bitshift_ = shift; }
    void set_divisor(uint16_t div)    { divisor_ = div; }
    void set_signed(bool s)           { is_signed_ = s; }
    uint16_t get_register() const     { return register_addr_; }
    void on_hold_update(const uint16_t *hold_regs);

 protected:
    void control(float value) override;

 private:
    LuxpowerSNAComponent *parent_{nullptr};
    uint16_t register_addr_{0};
    uint16_t bitmask_{0xFFFF};
    uint8_t  bitshift_{0};
    uint16_t divisor_{1};
    bool     is_signed_{false};

    static int16_t to_signed(uint16_t v)   { return (v & 0x8000) ? (int16_t)(v - 0x10000) : (int16_t)v; }
    static uint16_t to_unsigned(int16_t v) { return (uint16_t)((v + 0x10000) & 0xFFFF); }
};

// ---------------------------------------------------------------------------
// Button entity
// NOTE: Scan logic lives entirely in LuxpowerSNAComponent.
//       Button only holds the action enum and delegates via parent_.
//       Do NOT add scan_host_port_, try_probe_lux_dongle_, do_scan_,
//       or scan_task_fn_ here.
// ---------------------------------------------------------------------------
class LuxpowerSNAButton : public button::Button, public Component {
 public:
    enum class Action : uint8_t { RESTART, RESET_ALL, SCAN_DONGLE };

    void set_parent(LuxpowerSNAComponent *parent) { parent_ = parent; }
    void set_action(Action a)                      { action_ = a; }

 protected:
    void press_action() override;

 private:
    LuxpowerSNAComponent *parent_{nullptr};
    Action action_{Action::RESTART};
};

// ---------------------------------------------------------------------------
// Time entity
// ---------------------------------------------------------------------------
class LuxpowerSNATime : public Component {
 public:
    void set_parent(LuxpowerSNAComponent *parent) { parent_ = parent; }
    void set_register(uint16_t reg)               { register_addr_ = reg; }
    void set_name(const std::string &n)           { name_ = n; }

    void on_hold_update(const uint16_t *hold_regs);
    void set_time(const std::string &hhmm);
    std::string get_time() const { return current_hhmm_; }
    uint16_t get_register() const { return register_addr_; }

 private:
    LuxpowerSNAComponent *parent_{nullptr};
    uint16_t register_addr_{0};
    std::string name_;
    std::string current_hhmm_{"00:00"};

    static uint16_t encode_(uint8_t hour, uint8_t minute) {
        return (uint16_t)((minute << 8) | hour);
    }
    static void decode_(uint16_t val, uint8_t &hour, uint8_t &minute) {
        hour   = val & 0x00FF;
        minute = (val >> 8) & 0xFF;
    }
};

// ---------------------------------------------------------------------------
// Hub / main component
// ---------------------------------------------------------------------------
class LuxpowerSNAComponent : public Component {
 public:
    // ---- Component lifecycle ----
    void setup()       override;
    void loop()        override;
    void dump_config() override;
    float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

    // ---- Configuration setters ----
    void set_host(const std::string &h) {
        host_ = h;
        if (!h.empty()) save_host_prefs_();
    }
    void set_port(uint16_t p)                    { port_ = p; }
    void set_dongle_serial(const std::string &s) { dongle_serial_ = s; }
    void set_inverter_serial(const std::string &s){ inverter_serial_ = s; }
    void set_update_interval(uint32_t ms)         { update_interval_ms_ = ms; }
    void set_hold_update_interval(uint32_t ms)    { hold_interval_ms_ = ms; }

    // ---- Runtime reconfiguration ----
    void reconnect() {
        ESP_LOGI(TAG, "reconnect() called – closing socket and resetting state");
        close_socket_();
        last_connect_ms_ = 0;
        initial_hold_done_ = false;
    }

    bool is_config_ready() const {
        return !host_.empty()
            && dongle_serial_.size() == 10
            && inverter_serial_.size() == 10;
    }

    // ---- Write queue ----
    void queue_write(uint16_t reg, uint16_t value);

    uint16_t get_hold_register(uint16_t reg) const {
        return (reg < 240) ? hold_regs_[reg] : 0;
    }

    // ---- Platform registration ----
    void register_switch(LuxpowerSNASwitch *sw)  { switches_.push_back(sw); }
    void register_number(LuxpowerSNANumber *num) { numbers_.push_back(num); }
    void register_button(LuxpowerSNAButton *btn) { /* fire-and-forget */ }
    void register_time(LuxpowerSNATime *t)       { times_.push_back(t); }

    // ---- Actions called by buttons ----
    void action_restart();
    void action_reset_all();
    void action_scan_dongle();  // ← all scan logic here, NOT in Button

    // ---- Sensor setters (Section 1 – bank 0) ----
    void set_lux_status_text_sensor(text_sensor::TextSensor *s)         { lux_status_text_ = s; }
    void set_lux_battery_status_text_sensor(text_sensor::TextSensor *s) { lux_bat_status_text_ = s; }
    void set_scan_status_text_sensor(text_sensor::TextSensor *s)        { scan_status_text_ = s; }

    void set_lux_current_solar_voltage_1_sensor(sensor::Sensor *s) { pv_v1_ = s; }
    void set_lux_current_solar_voltage_2_sensor(sensor::Sensor *s) { pv_v2_ = s; }
    void set_lux_current_solar_voltage_3_sensor(sensor::Sensor *s) { pv_v3_ = s; }
    void set_lux_battery_voltage_sensor(sensor::Sensor *s)         { bat_v_ = s; }
    void set_lux_battery_percent_sensor(sensor::Sensor *s)         { bat_soc_ = s; }
    void set_soh_sensor(sensor::Sensor *s)                         { bat_soh_ = s; }
    void set_lux_internal_fault_sensor(sensor::Sensor *s)          { internal_fault_ = s; }
    void set_lux_current_solar_output_1_sensor(sensor::Sensor *s)  { pv_p1_ = s; }
    void set_lux_current_solar_output_2_sensor(sensor::Sensor *s)  { pv_p2_ = s; }
    void set_lux_current_solar_output_3_sensor(sensor::Sensor *s)  { pv_p3_ = s; }
    void set_lux_battery_charge_sensor(sensor::Sensor *s)          { bat_chg_ = s; }
    void set_lux_battery_discharge_sensor(sensor::Sensor *s)       { bat_dischg_ = s; }
    void set_lux_grid_voltage_r_sensor(sensor::Sensor *s)          { grid_v_r_ = s; }
    void set_lux_grid_voltage_s_sensor(sensor::Sensor *s)          { grid_v_s_ = s; }
    void set_lux_grid_voltage_t_sensor(sensor::Sensor *s)          { grid_v_t_ = s; }
    void set_lux_grid_frequency_live_sensor(sensor::Sensor *s)     { grid_freq_ = s; }
    void set_lux_grid_voltage_live_sensor(sensor::Sensor *s)       { grid_v_live_ = s; }
    void set_lux_power_from_inverter_live_sensor(sensor::Sensor *s){ p_inv_ = s; }
    void set_lux_power_to_inverter_live_sensor(sensor::Sensor *s)  { p_rec_ = s; }
    void set_lux_power_current_clamp_sensor(sensor::Sensor *s)     { rms_current_ = s; }
    void set_grid_power_factor_sensor(sensor::Sensor *s)           { pf_ = s; }
    void set_eps_voltage_r_sensor(sensor::Sensor *s)               { eps_v_r_ = s; }
    void set_eps_voltage_s_sensor(sensor::Sensor *s)               { eps_v_s_ = s; }
    void set_eps_voltage_t_sensor(sensor::Sensor *s)               { eps_v_t_ = s; }
    void set_eps_frequency_sensor(sensor::Sensor *s)               { eps_freq_ = s; }
    void set_lux_power_to_eps_sensor(sensor::Sensor *s)            { p_to_eps_ = s; }
    void set_lux_power_to_grid_live_sensor(sensor::Sensor *s)      { p_to_grid_ = s; }
    void set_lux_power_from_grid_live_sensor(sensor::Sensor *s)    { p_to_user_ = s; }
    void set_lux_daily_solar_array_1_sensor(sensor::Sensor *s)     { e_pv1_day_ = s; }
    void set_lux_daily_solar_array_2_sensor(sensor::Sensor *s)     { e_pv2_day_ = s; }
    void set_lux_daily_solar_array_3_sensor(sensor::Sensor *s)     { e_pv3_day_ = s; }
    void set_lux_power_from_inverter_daily_sensor(sensor::Sensor *s){ e_inv_day_ = s; }
    void set_lux_power_to_inverter_daily_sensor(sensor::Sensor *s) { e_rec_day_ = s; }
    void set_lux_daily_battery_charge_sensor(sensor::Sensor *s)    { e_chg_day_ = s; }
    void set_lux_daily_battery_discharge_sensor(sensor::Sensor *s) { e_dischg_day_ = s; }
    void set_lux_power_to_eps_daily_sensor(sensor::Sensor *s)      { e_eps_day_ = s; }
    void set_lux_power_to_grid_daily_sensor(sensor::Sensor *s)     { e_to_grid_day_ = s; }
    void set_lux_power_from_grid_daily_sensor(sensor::Sensor *s)   { e_to_user_day_ = s; }
    void set_bus1_voltage_sensor(sensor::Sensor *s)                { v_bus1_ = s; }
    void set_bus2_voltage_sensor(sensor::Sensor *s)                { v_bus2_ = s; }
    // Derived
    void set_lux_current_solar_output_sensor(sensor::Sensor *s)    { pv_total_ = s; }
    void set_lux_daily_solar_sensor(sensor::Sensor *s)             { e_pv_day_total_ = s; }
    void set_lux_power_to_home_sensor(sensor::Sensor *s)           { p_home_ = s; }
    void set_lux_battery_flow_sensor(sensor::Sensor *s)            { bat_flow_ = s; }
    void set_lux_grid_flow_sensor(sensor::Sensor *s)               { grid_flow_ = s; }
    void set_lux_home_consumption_live_sensor(sensor::Sensor *s)   { home_live_ = s; }
    void set_lux_home_consumption_sensor(sensor::Sensor *s)        { home_day_ = s; }

    // ---- Sensor setters (Section 2 – bank 1) ----
    void set_lux_total_solar_array_1_sensor(sensor::Sensor *s)     { e_pv1_all_ = s; }
    void set_lux_total_solar_array_2_sensor(sensor::Sensor *s)     { e_pv2_all_ = s; }
    void set_lux_total_solar_array_3_sensor(sensor::Sensor *s)     { e_pv3_all_ = s; }
    void set_lux_power_from_inverter_total_sensor(sensor::Sensor *s){ e_inv_all_ = s; }
    void set_lux_power_to_inverter_total_sensor(sensor::Sensor *s) { e_rec_all_ = s; }
    void set_lux_total_battery_charge_sensor(sensor::Sensor *s)    { e_chg_all_ = s; }
    void set_lux_total_battery_discharge_sensor(sensor::Sensor *s) { e_dischg_all_ = s; }
    void set_lux_power_to_eps_total_sensor(sensor::Sensor *s)      { e_eps_all_ = s; }
    void set_lux_power_to_grid_total_sensor(sensor::Sensor *s)     { e_to_grid_all_ = s; }
    void set_lux_power_from_grid_total_sensor(sensor::Sensor *s)   { e_to_user_all_ = s; }
    void set_lux_fault_code_sensor(sensor::Sensor *s)              { fault_code_ = s; }
    void set_lux_warning_code_sensor(sensor::Sensor *s)            { warning_code_ = s; }
    void set_lux_internal_temp_sensor(sensor::Sensor *s)           { t_inner_ = s; }
    void set_lux_radiator1_temp_sensor(sensor::Sensor *s)          { t_rad1_ = s; }
    void set_lux_radiator2_temp_sensor(sensor::Sensor *s)          { t_rad2_ = s; }
    void set_lux_battery_temperature_live_sensor(sensor::Sensor *s){ t_bat_ = s; }
    void set_lux_uptime_sensor(sensor::Sensor *s)                  { uptime_ = s; }
    void set_lux_total_solar_sensor(sensor::Sensor *s)             { e_pv_all_total_ = s; }
    void set_lux_home_consumption_total_sensor(sensor::Sensor *s)  { home_total_ = s; }

    // ---- Sensor setters (Section 3 – bank 2) ----
    void set_lux_bms_limit_charge_sensor(sensor::Sensor *s)        { bms_max_chg_ = s; }
    void set_lux_bms_limit_discharge_sensor(sensor::Sensor *s)     { bms_max_dischg_ = s; }
    void set_charge_voltage_ref_sensor(sensor::Sensor *s)          { chg_volt_ref_ = s; }
    void set_discharge_cutoff_voltage_sensor(sensor::Sensor *s)    { dischg_cut_v_ = s; }
    void set_battery_status_inv_sensor(sensor::Sensor *s)          { bat_status_inv_ = s; }
    void set_lux_battery_count_sensor(sensor::Sensor *s)           { bat_count_ = s; }
    void set_lux_battery_capacity_ah_sensor(sensor::Sensor *s)     { bat_cap_ah_ = s; }
    void set_lux_battery_current_sensor(sensor::Sensor *s)         { bat_curr_ = s; }
    void set_max_cell_volt_sensor(sensor::Sensor *s)               { max_cell_v_ = s; }
    void set_min_cell_volt_sensor(sensor::Sensor *s)               { min_cell_v_ = s; }
    void set_max_cell_temp_sensor(sensor::Sensor *s)               { max_cell_t_ = s; }
    void set_min_cell_temp_sensor(sensor::Sensor *s)               { min_cell_t_ = s; }
    void set_lux_battery_cycle_count_sensor(sensor::Sensor *s)     { bat_cycles_ = s; }
    void set_lux_home_consumption_2_live_sensor(sensor::Sensor *s) { p_load2_ = s; }

    // ---- Sensor setters (Section 4 – bank 3) ----
    void set_lux_current_generator_voltage_sensor(sensor::Sensor *s)       { gen_v_ = s; }
    void set_lux_current_generator_frequency_sensor(sensor::Sensor *s)     { gen_freq_ = s; }
    void set_lux_current_generator_power_sensor(sensor::Sensor *s)         { gen_p_ = s; }
    void set_lux_current_generator_power_daily_sensor(sensor::Sensor *s)   { gen_p_day_ = s; }
    void set_lux_current_generator_power_all_sensor(sensor::Sensor *s)     { gen_p_all_ = s; }
    void set_lux_current_eps_L1_voltage_sensor(sensor::Sensor *s)          { eps_l1_v_ = s; }
    void set_lux_current_eps_L2_voltage_sensor(sensor::Sensor *s)          { eps_l2_v_ = s; }
    void set_lux_current_eps_L1_watt_sensor(sensor::Sensor *s)             { eps_l1_w_ = s; }
    void set_lux_current_eps_L2_watt_sensor(sensor::Sensor *s)             { eps_l2_w_ = s; }

    // ---- Sensor setters (Section 5 – bank 4) ----
    void set_p_load_ongrid_sensor(sensor::Sensor *s)  { p_load_ongrid_ = s; }
    void set_e_load_day_sensor(sensor::Sensor *s)     { e_load_day_ = s; }
    void set_e_load_all_l_sensor(sensor::Sensor *s)   { e_load_all_ = s; }

 private:
    // ---- Socket ----
    // ---- NVS host persistence (survives MQTT overwrite) ----
    void save_host_prefs_();
    void load_host_prefs_();

    bool  start_connect_();
    bool  check_connect_();
    void  close_socket_();
    int   send_bytes_(const uint8_t *data, size_t len);
    void  try_recv_();
    bool  try_process_packet_();

    // ---- Packet builders ----
    void  send_read_input_(uint16_t start_reg, uint16_t count = 40);
    void  send_read_hold_(uint16_t start_reg, uint16_t count = 40);
    void  send_write_single_(uint16_t reg, uint16_t value);
    void  send_heartbeat_response_(const uint8_t *pkt, size_t len);

    // ---- Packet processors ----
    void  process_packet_(const uint8_t *buf, size_t len);
    void  process_read_input_(uint16_t start_reg, const uint8_t *data, size_t data_len);
    void  process_read_hold_(uint16_t start_reg, const uint8_t *data, uint8_t count);
    void  process_write_single_(uint16_t reg, uint16_t value);
    void  notify_hold_listeners_();

    // ---- Bank processors ----
    void  process_bank0_(const Bank0 &d);
    void  process_bank1_(const Bank1 &d);
    void  process_bank2_(const Bank2 &d);
    void  process_bank3_(const Bank3 &d);
    void  process_bank4_(const Bank4 &d);

    // ---- CRC ----
    static uint16_t crc16_(const uint8_t *data, size_t len);

    // ---- Publish helpers ----
    static void pub(sensor::Sensor         *s, float v)            { if (s) s->publish_state(v); }
    static void pub(text_sensor::TextSensor *s, const std::string &v) { if (s) s->publish_state(v); }

    // ---- Apply scan result (called from loop() on main thread) ----
    void apply_scanned_host_(const std::string &ip);

    // ---- Scan internals (FreeRTOS task, batch parallel connect)
    // ScanParams is defined here in the header — do NOT redefine in .cpp.
    struct ScanParams {
        uint8_t  a, b, c, self_octet;
        uint16_t port;
        LuxpowerSNAComponent *hub;
    };
    static void scan_task_fn_(void *param);
    // Note: port is passed explicitly so task doesn't race with port_ changes
    void do_scan_(uint8_t a, uint8_t b, uint8_t c, uint8_t self_octet, uint16_t port);

    // ---- Scan state (written by task, read by loop()) ----
    // ESP32-S2 is single-core so volatile is sufficient; no mutex needed.
    volatile bool scanning_{false};
    volatile bool scan_result_pending_{false};
    volatile bool scan_found_{false};
    char     found_ip_buf_[20]{};
    uint32_t scan_start_ms_{0};                    // watchdog: time scan started
    static const uint32_t SCAN_TIMEOUT_MS = 30000; // 30s; reset if task dies silently
    // Deferred apply: set in loop(), applied on NEXT loop() tick
    // so apply_scanned_host_ runs outside the scan_result_pending_ block,
    // avoiding on_value lambda → reconnect() re-entrant loop.
    bool        deferred_apply_{false};
    std::string deferred_ip_{};

    // ---- State machine ----
    enum class State : uint8_t {
        DISCONNECTED,
        CONNECTING,
        IDLE,
        POLLING_INPUT,
        POLLING_HOLD,
        WRITING,
    };
    State    state_     = State::DISCONNECTED;
    uint8_t  bank_idx_  = 0;
    bool     awaiting_  = false;
    uint32_t req_sent_ms_ = 0;

    static const uint32_t RESPONSE_TIMEOUT_MS = 4000;

    // ---- TCP ----
    std::string host_;
    uint16_t    port_{8000};
    int         sock_fd_{-1};

    // ---- Serials ----
    std::string dongle_serial_;
    std::string inverter_serial_;

    // ---- Timing ----
    uint32_t update_interval_ms_ = 20000;
    uint32_t hold_interval_ms_   = 60000;
    uint32_t last_input_poll_ms_ = 0;
    uint32_t last_hold_poll_ms_  = 0;
    uint32_t last_connect_ms_    = 0;
    bool     initial_hold_done_  = false;

    // ---- Receive buffer ----
    uint8_t  recv_buf_[512];
    size_t   recv_buf_len_ = 0;

    // ---- Hold register cache (reg 0-239) ----
    uint16_t hold_regs_[240] = {};

    // ---- Write queue ----
    std::queue<WriteCmd> write_queue_;

    // ---- Platform entities ----
    std::vector<LuxpowerSNASwitch*> switches_;
    std::vector<LuxpowerSNANumber*> numbers_;
    std::vector<LuxpowerSNATime*>   times_;

    // ---- Status text tables ----
    static const char *STATUS_TEXTS[193];
    static const char *BAT_STATUS_TEXTS[17];

    // ---- Sensor pointers (bank 0) ----
    text_sensor::TextSensor *lux_status_text_{nullptr};
    text_sensor::TextSensor *lux_bat_status_text_{nullptr};
    text_sensor::TextSensor *scan_status_text_{nullptr};
    sensor::Sensor *pv_v1_{nullptr}, *pv_v2_{nullptr}, *pv_v3_{nullptr};
    sensor::Sensor *bat_v_{nullptr}, *bat_soc_{nullptr}, *bat_soh_{nullptr};
    sensor::Sensor *internal_fault_{nullptr};
    sensor::Sensor *pv_p1_{nullptr}, *pv_p2_{nullptr}, *pv_p3_{nullptr};
    sensor::Sensor *bat_chg_{nullptr}, *bat_dischg_{nullptr};
    sensor::Sensor *grid_v_r_{nullptr}, *grid_v_s_{nullptr}, *grid_v_t_{nullptr};
    sensor::Sensor *grid_freq_{nullptr}, *grid_v_live_{nullptr};
    sensor::Sensor *p_inv_{nullptr}, *p_rec_{nullptr};
    sensor::Sensor *rms_current_{nullptr}, *pf_{nullptr};
    sensor::Sensor *eps_v_r_{nullptr}, *eps_v_s_{nullptr}, *eps_v_t_{nullptr};
    sensor::Sensor *eps_freq_{nullptr}, *p_to_eps_{nullptr};
    sensor::Sensor *p_to_grid_{nullptr}, *p_to_user_{nullptr};
    sensor::Sensor *e_pv1_day_{nullptr}, *e_pv2_day_{nullptr}, *e_pv3_day_{nullptr};
    sensor::Sensor *e_inv_day_{nullptr}, *e_rec_day_{nullptr};
    sensor::Sensor *e_chg_day_{nullptr}, *e_dischg_day_{nullptr}, *e_eps_day_{nullptr};
    sensor::Sensor *e_to_grid_day_{nullptr}, *e_to_user_day_{nullptr};
    sensor::Sensor *v_bus1_{nullptr}, *v_bus2_{nullptr};
    // Derived
    sensor::Sensor *pv_total_{nullptr}, *e_pv_day_total_{nullptr};
    sensor::Sensor *p_home_{nullptr}, *bat_flow_{nullptr}, *grid_flow_{nullptr};
    sensor::Sensor *home_live_{nullptr}, *home_day_{nullptr};
    // Bank 1
    sensor::Sensor *e_pv1_all_{nullptr}, *e_pv2_all_{nullptr}, *e_pv3_all_{nullptr};
    sensor::Sensor *e_inv_all_{nullptr}, *e_rec_all_{nullptr};
    sensor::Sensor *e_chg_all_{nullptr}, *e_dischg_all_{nullptr}, *e_eps_all_{nullptr};
    sensor::Sensor *e_to_grid_all_{nullptr}, *e_to_user_all_{nullptr};
    sensor::Sensor *fault_code_{nullptr}, *warning_code_{nullptr};
    sensor::Sensor *t_inner_{nullptr}, *t_rad1_{nullptr}, *t_rad2_{nullptr}, *t_bat_{nullptr};
    sensor::Sensor *uptime_{nullptr};
    sensor::Sensor *e_pv_all_total_{nullptr}, *home_total_{nullptr};
    // Bank 2
    sensor::Sensor *bms_max_chg_{nullptr}, *bms_max_dischg_{nullptr};
    sensor::Sensor *chg_volt_ref_{nullptr}, *dischg_cut_v_{nullptr};
    sensor::Sensor *bat_status_inv_{nullptr}, *bat_count_{nullptr}, *bat_cap_ah_{nullptr};
    sensor::Sensor *bat_curr_{nullptr};
    sensor::Sensor *max_cell_v_{nullptr}, *min_cell_v_{nullptr};
    sensor::Sensor *max_cell_t_{nullptr}, *min_cell_t_{nullptr};
    sensor::Sensor *bat_cycles_{nullptr}, *p_load2_{nullptr};
    // Bank 3
    sensor::Sensor *gen_v_{nullptr}, *gen_freq_{nullptr};
    sensor::Sensor *gen_p_{nullptr}, *gen_p_day_{nullptr}, *gen_p_all_{nullptr};
    sensor::Sensor *eps_l1_v_{nullptr}, *eps_l2_v_{nullptr};
    sensor::Sensor *eps_l1_w_{nullptr}, *eps_l2_w_{nullptr};
    // Bank 4
    sensor::Sensor *p_load_ongrid_{nullptr}, *e_load_day_{nullptr}, *e_load_all_{nullptr};
};

}  // namespace luxpower_sna
}  // namespace esphome