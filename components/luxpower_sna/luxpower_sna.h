#pragma once

// ---------------------------------------------------------------------------
// LuxPower SNA ESPHome Component – IDF-compatible (lwip sockets, no WiFiClient)
// Supports: sensors (READ_INPUT), switches (READ_HOLD / WRITE_SINGLE), numbers
// ---------------------------------------------------------------------------

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"         // millis()
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
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
    uint16_t prefix;          // 0xA1 0x1A  (LE: 0x1AA1)
    uint16_t protocol;        // 2
    uint16_t frame_length;    // total_length - 6
    uint8_t  address;         // 0x01
    uint8_t  tcp_function;
    char     dongle[10];
    uint16_t data_length;
};  // 20 bytes

struct LuxTranslatedData {
    uint8_t  address;         // ACTION_WRITE = 0
    uint8_t  device_function;
    char     serial[10];
    uint16_t reg_start;
    uint8_t  value_length;    // present when protocol==2 && !WRITE_SINGLE
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
    int16_t  t_inner, t_rad_1, t_rad_2, t_bat;  // regs 64-67
    uint16_t _reserved68;                         // reg 68 (hold register bank, not used in INPUT)
    uint32_t uptime;                              // regs 69-70
    uint8_t  _tail[18];                           // regs 71-79 (padding to complete 40-register bank)
};  // 80 bytes = 40 registers

// Bank 2: registers 80-119
struct Bank2 {
    uint16_t _r80;
    int16_t  max_chg_curr, max_dischg_curr, charge_volt_ref, dischg_cut_volt;
    uint8_t  _placeholder[20];   // reg 85-94
    int16_t  bat_status_inv;     // reg 95
    int16_t  bat_count;          // reg 96
    int16_t  bat_capacity;       // reg 97
    int16_t  bat_current;        // reg 98
    int16_t  _r99, _r100;
    int16_t  max_cell_volt;      // reg 101
    int16_t  min_cell_volt;      // reg 102
    int16_t  max_cell_temp;      // reg 103
    int16_t  min_cell_temp;      // reg 104
    uint16_t _r105;
    int16_t  bat_cycle_count;    // reg 106
    uint8_t  _r107_113[14];      // reg 107-113
    int16_t  p_load2;            // reg 114
    uint8_t  _r115_119[10];      // reg 115-119
};  // 80 bytes

// Bank 3: registers 120-159
struct Bank3 {
    uint16_t _r120;
    int16_t  gen_input_volt;     // reg 121
    int16_t  gen_input_freq;     // reg 122
    int16_t  gen_power_watt;     // reg 123
    int16_t  gen_power_day;      // reg 124
    int16_t  gen_power_all;      // reg 125
    uint16_t _r126;
    int16_t  eps_L1_volt;        // reg 127
    int16_t  eps_L2_volt;        // reg 128
    int16_t  eps_L1_watt;        // reg 129
    int16_t  eps_L2_watt;        // reg 130
    uint8_t  _r131_159[58];      // reg 131-159
};  // 80 bytes

// Bank 4: registers 160-199
struct Bank4 {
    uint8_t  _r160_169[20];      // reg 160-169 (10 regs)
    int16_t  p_load_ongrid;      // reg 170
    int16_t  e_load_day;         // reg 171
    int16_t  e_load_all_l;       // reg 172
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
class LuxpowerSNASwitch : public switch_::Switch {
 public:
    void set_parent(LuxpowerSNAComponent *parent) { parent_ = parent; }
    void set_register(uint16_t reg)  { register_addr_ = reg; }
    void set_bitmask(uint16_t mask)  { bitmask_ = mask; }
    uint16_t get_register() const    { return register_addr_; }
    uint16_t get_bitmask()  const    { return bitmask_; }

    /// Called by hub when hold register data arrives
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
class LuxpowerSNANumber : public number::Number {
 public:
    void set_parent(LuxpowerSNAComponent *parent) { parent_ = parent; }
    void set_register(uint16_t reg)   { register_addr_ = reg; }
    void set_bitmask(uint16_t mask)   { bitmask_ = mask; }
    void set_bitshift(uint8_t shift)  { bitshift_ = shift; }
    void set_divisor(uint16_t div)    { divisor_ = div; }
    void set_signed(bool s)           { is_signed_ = s; }
    uint16_t get_register() const     { return register_addr_; }

    /// Called by hub when hold register data arrives
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
    void set_host(const std::string &h)          { host_ = h; }
    void set_port(uint16_t p)                    { port_ = p; }
    void set_dongle_serial(const std::string &s) { dongle_serial_ = s; }
    void set_inverter_serial(const std::string &s){ inverter_serial_ = s; }
    void set_update_interval(uint32_t ms)         { update_interval_ms_ = ms; }
    void set_hold_update_interval(uint32_t ms)    { hold_interval_ms_ = ms; }

    // ---- Runtime reconfiguration ----------------------------------------
    // Call these from text/number entity on_value lambdas, then call reconnect().
    // They are safe to call at any time; if currently connected, the existing
    // socket is closed and a new connection is attempted with the new params.
    void reconnect() {
        ESP_LOGI(TAG, "reconnect() called – closing socket and resetting state");
        close_socket_();           // sets state_ = DISCONNECTED
        last_connect_ms_ = 0;      // connect immediately on next loop()
        initial_hold_done_ = false; // re-fetch hold registers after reconnect
    }

    // Returns true when config is complete enough to attempt a connection
    bool is_config_ready() const {
        return !host_.empty()
            && dongle_serial_.size() == 10
            && inverter_serial_.size() == 10;
    }

    // ---- Called by switches / numbers to request a register write ----
    void queue_write(uint16_t reg, uint16_t value);

    /// Returns cached hold register value (safe to call from Switch::write_state)
    uint16_t get_hold_register(uint16_t reg) const {
        return (reg < 240) ? hold_regs_[reg] : 0;
    }

    // ---- Platform registration ----
    void register_switch(LuxpowerSNASwitch *sw)  { switches_.push_back(sw); }
    void register_number(LuxpowerSNANumber *num) { numbers_.push_back(num); }

    // ---- Sensor setters (Section 1 – bank 0) ----
    void set_lux_status_text_sensor(text_sensor::TextSensor *s)         { lux_status_text_ = s; }
    void set_lux_battery_status_text_sensor(text_sensor::TextSensor *s) { lux_bat_status_text_ = s; }

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
    // ---- Socket helpers ----
    bool  start_connect_();
    bool  check_connect_();        // returns true when connected
    void  close_socket_();
    int   send_bytes_(const uint8_t *data, size_t len);
    void  try_recv_();
    bool  try_process_packet_();   // returns true if a full packet was handled

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

    // ---- Bank processors (mirror of Python get_device_values_bankN) ----
    void  process_bank0_(const Bank0 &d);
    void  process_bank1_(const Bank1 &d);
    void  process_bank2_(const Bank2 &d);
    void  process_bank3_(const Bank3 &d);
    void  process_bank4_(const Bank4 &d);

    // ---- CRC ----
    static uint16_t crc16_(const uint8_t *data, size_t len);

    // ---- Publish helpers ----
    static void pub(sensor::Sensor      *s, float v) { if (s) s->publish_state(v); }
    static void pub(text_sensor::TextSensor *s, const std::string &v) { if (s) s->publish_state(v); }

    // ---- State machine ----
    enum class State : uint8_t {
        DISCONNECTED,
        CONNECTING,
        IDLE,
        POLLING_INPUT,   // cycling input banks
        POLLING_HOLD,    // cycling hold banks
        WRITING,         // processing write queue
    };
    State    state_     = State::DISCONNECTED;
    uint8_t  bank_idx_  = 0;     // index into poll sequence (0-4 for input, 0-5 for hold)
    bool     awaiting_  = false; // waiting for response
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

    // ---- Status texts ----
    static const char *STATUS_TEXTS[193];
    static const char *BAT_STATUS_TEXTS[17];

    // ---- Sensor pointers (bank 0) ----
    text_sensor::TextSensor *lux_status_text_{nullptr};
    text_sensor::TextSensor *lux_bat_status_text_{nullptr};
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
