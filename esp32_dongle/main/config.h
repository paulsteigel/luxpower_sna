#pragma once
#include <stdint.h>

// ── WiFi STA (home LAN) ───────────────────────────────────────
#define WIFI_STA_SSID        "HOME"
#define WIFI_STA_PASS        "ngoc12345"

// ── WiFi AP (for LuxApp local connection) ────────────────────
#define WIFI_AP_SSID         "LuxDongle-AP"
#define WIFI_AP_PASS         "luxpower1"
#define WIFI_AP_IP           "10.10.10.1"
#define WIFI_AP_GW           "10.10.10.1"
#define WIFI_AP_NETMASK      "255.255.255.0"

// ── LuxPower Cloud ────────────────────────────────────────────
#define LUX_CLOUD_HOST       "120.79.53.27"
#define LUX_CLOUD_PORT       4346
#define LUX_LOCAL_PORT       8000   // LuxApp connects here

// ── Dongle / Inverter identity ────────────────────────────────
// Replace with your actual serial numbers
#define DONGLE_SN            "BA32500699"   // 10 chars
#define INVERTER_SN          "3253631886"   // 10 chars

// ── fn=0x10 WRITE_MULTI unknown field ────────────────────────
// Observed constant across all captured sessions (reverse engineered)
#define WRITE_MULTI_UNK_LEN  10
static const uint8_t WRITE_MULTI_UNK[WRITE_MULTI_UNK_LEN] = {
    0x2B, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ── RS485 / Modbus ────────────────────────────────────────────
#define RS485_TX_PIN         1
#define RS485_RX_PIN         3
#define RS485_DE_RE_PIN      21
#define RS485_BAUD           19200
#define MODBUS_SLAVE_ADDR    1
#define MODBUS_UART_NUM      UART_NUM_1

// ── MQTT ─────────────────────────────────────────────────────
#define MQTT_BROKER_URI      "mqtt://myhome.sfdp.net:1883"
#define MQTT_USER            "mqtt_user"
#define MQTT_PASS            "D1ndh1sk@"
#define MQTT_CLIENT_ID       "esp-luxpower"
#define MQTT_PREFIX          "lux"         // topic prefix: lux/state/xxx
#define MQTT_CMD_PREFIX      "lux/cmd"     // lux/cmd/set/xxx

// ── Timing (ms) ──────────────────────────────────────────────
#define POLL_INPUT_MS        5000
#define POLL_HOLD_MS         60000
#define HEARTBEAT_INTERVAL_MS 15000
#define CLOUD_RECONNECT_MS   10000
#define MODBUS_TIMEOUT_MS    500
#define MODBUS_INTER_FRAME_MS 10

// ── FreeRTOS task priorities ─────────────────────────────────
#define TASK_PRIO_MODBUS     5
#define TASK_PRIO_CLOUD      4
#define TASK_PRIO_LOCAL_SRV  3
#define TASK_PRIO_MQTT       3

// ── FreeRTOS task stack sizes ────────────────────────────────
#define STACK_MODBUS         4096
#define STACK_CLOUD          8192
#define STACK_LOCAL_SRV      4096
#define STACK_MQTT           4096

// ── Register counts ───────────────────────────────────────────
#define INPUT_REG_COUNT      240
#define HOLD_REG_COUNT       240

// ── Cloud filter: registers allowed to be written by cloud ───
// Anything NOT in this list will be logged and blocked
// Based on observed legitimate writes during reverse engineering
static const uint16_t CLOUD_WRITE_WHITELIST[] = {
    0,    // hold_model (battery type) - fn=0x10
    99,   // la_chg_volt
    101,  // charge_rate
    102,  // dischg_rate
    105,  // eod_soc
    120,  // sys_enable (EOD mode)
    125,  // soc_low_eps
    160,  // ac_chg_start_soc
    161,  // ac_chg_end_soc
    162,  // bat_low_volt
    163,  // bat_low_back_volt
    164,  // bat_low_soc
    165,  // bat_low_back_soc
};
#define CLOUD_WHITELIST_LEN  (sizeof(CLOUD_WRITE_WHITELIST) / sizeof(uint16_t))
