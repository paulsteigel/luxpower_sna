#pragma once
#include <stdint.h>

// ── Mode selection ────────────────────────────────────────────
// Define RELAY_MODE to act as transparent MITM relay (real dongle → ESP32 → cloud)
// Undefine to act as a dongle itself (ESP32 polls cloud directly)
#define RELAY_MODE

// ── WiFi STA (home LAN) ───────────────────────────────────────
// TODO: Phase 2 — replace with captive portal / WiFiManager style
#define WIFI_STA_SSID        "HOME"
#define WIFI_STA_PASS        "ngoc12345"

// ── WiFi AP (for OTA + status page + future captive portal) ──
#define WIFI_AP_SSID         "LuxDongle-AP"
#define WIFI_AP_PASS         "luxpower1"
#define WIFI_AP_IP           "10.10.10.1"
#define WIFI_AP_GW           "10.10.10.1"
#define WIFI_AP_NETMASK      "255.255.255.0"

// ── mDNS hostname → http://luxdongle.local:8080 ──────────────
#define MDNS_HOSTNAME        "luxdongle"

// ── LuxPower Cloud ────────────────────────────────────────────
#define LUX_CLOUD_HOST       "120.79.53.27" //"47.81.11.236"   // updated from 120.79.53.27
#define LUX_CLOUD_PORT       4346

// ── Dongle / Inverter identity ────────────────────────────────
#define DONGLE_SN            "BA32500699"   // "BA12150911"
#define INVERTER_SN          "3253631886"   // "2193038031"

// ── fn=0x10 WRITE_MULTI unknown field (constant, observed) ───
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
#define MQTT_PREFIX          "lux"
#define MQTT_CMD_PREFIX      "lux/cmd"
#define MQTT_LOG_TOPIC       "lux/log"

// ── OTA web server ────────────────────────────────────────────
#define OTA_PORT             8080

// ── Timing (ms) ──────────────────────────────────────────────
#define POLL_INPUT_MS             5000
#define POLL_HOLD_MS              60000
#define HEARTBEAT_INTERVAL_MS     15000
#define CLOUD_RECONNECT_MS        10000
#define BATTERY_SETTLE_MS         25000   // wait after battery type change

// ── FreeRTOS task config ──────────────────────────────────────
#define TASK_PRIO_CLOUD      4
#define TASK_PRIO_MQTT       3
#define STACK_CLOUD          8192
#define STACK_MQTT           4096

// ── Register counts ───────────────────────────────────────────
#define INPUT_REG_COUNT      240
#define HOLD_REG_COUNT       240

// ── Cloud write whitelist (confirmed from captures) ───────────
static const uint16_t CLOUD_WRITE_WHITELIST[] = {
    0,    // hold_model (battery type) - fn=0x10
    99,   // la_chg_volt
    101,  // charge_rate
    102,  // dischg_rate
    105,  // eod_soc
    120,  // sys_enable
    125,  // soc_low_eps
    160,  // ac_chg_start_soc
    161,  // ac_chg_end_soc
    162,  // bat_low_volt
    163,  // bat_low_back_volt
    164,  // bat_low_soc
    165,  // bat_low_back_soc
};
#define CLOUD_WHITELIST_LEN  (sizeof(CLOUD_WRITE_WHITELIST) / sizeof(uint16_t))