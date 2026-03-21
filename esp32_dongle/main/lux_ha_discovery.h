#pragma once
// ── Home Assistant MQTT Discovery ─────────────────────────────
// Publishes discovery payloads so HA auto-creates all entities.
// Call lux_ha_discovery_publish(client) once after MQTT connects.
//
// Creates:
//   sensor  × ~20  (vbat, soc, ppv, power, temp, ...)
//   number  × 3    (charge_rate, dischg_rate, eod_soc)
//   switch  × 2    (eod_soc_mode, gen_chg)  — via sys_enable bits
//   button  × 2    (restart, reset_all)
// ─────────────────────────────────────────────────────────────

#include <stdio.h>
#include <string.h>
#include "mqtt_client.h"
#include "esp_log.h"
#include "config.h"

static const char *HA_TAG = "ha_disc";

// HA discovery prefix
#define HA_PREFIX        "homeassistant"
#define HA_NODE_ID       "luxpower_sna"

// Device info block (reused in every payload)
#define HA_DEVICE \
    "\"dev\":{" \
        "\"ids\":[\"" DONGLE_SN "\"]," \
        "\"name\":\"LuxPower SNA\"," \
        "\"mf\":\"LuxPower\"," \
        "\"mdl\":\"SNA 5/6K\"," \
        "\"sw\":\"esp32-dongle-v1\"," \
        "\"cu\":\"http://luxdongle.local:8080\"" \
    "}"

static void ha_pub(esp_mqtt_client_handle_t c,
                   const char *type, const char *obj_id,
                   const char *payload) {
    char topic[128];
    snprintf(topic, sizeof(topic),
             HA_PREFIX "/%s/" HA_NODE_ID "/%s/config", type, obj_id);
    // retain=1 so HA keeps entity after ESP32 reboot
    esp_mqtt_client_publish(c, topic, payload, 0, 1, 1);
    ESP_LOGD(HA_TAG, "Discovery: %s", topic);
}

// ── Sensor ────────────────────────────────────────────────────
static void ha_sensor(esp_mqtt_client_handle_t c,
                       const char *obj_id, const char *name,
                       const char *state_topic,
                       const char *unit, const char *dev_class,
                       const char *icon) {
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{"
        "\"name\":\"%s\","
        "\"stat_t\":\"%s\","
        "\"unit_of_meas\":\"%s\","
        "%s%s%s"   // dev_class (optional)
        "%s%s%s"   // icon (optional)
        "\"uniq_id\":\"" HA_NODE_ID "_%s\","
        "\"avty_t\":\"" MQTT_PREFIX "/state/vpv1\","
        "\"avty_tpl\":\"{{value|float(0)|string}}\","
        HA_DEVICE
        "}",
        name, state_topic, unit,
        dev_class ? "\"dev_cla\":\"" : "",
        dev_class ? dev_class : "",
        dev_class ? "\"," : "",
        icon ? "\"ic\":\"" : "",
        icon ? icon : "",
        icon ? "\"," : "",
        obj_id
    );
    ha_pub(c, "sensor", obj_id, buf);
}

// ── Number (writable) ─────────────────────────────────────────
static void ha_number(esp_mqtt_client_handle_t c,
                       const char *obj_id, const char *name,
                       const char *state_topic, const char *cmd_topic,
                       float min, float max, float step,
                       const char *unit, const char *icon) {
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{"
        "\"name\":\"%s\","
        "\"stat_t\":\"%s\","
        "\"cmd_t\":\"%s\","
        "\"min\":%.0f,\"max\":%.0f,\"step\":%.1f,"
        "\"unit_of_meas\":\"%s\","
        "\"ic\":\"%s\","
        "\"uniq_id\":\"" HA_NODE_ID "_%s\","
        "\"ret\":true,"
        HA_DEVICE
        "}",
        name, state_topic, cmd_topic,
        min, max, step, unit, icon, obj_id
    );
    ha_pub(c, "number", obj_id, buf);
}

// ── Switch ────────────────────────────────────────────────────
// For sys_enable bitmap bits — send full integer value via MQTT
static void ha_select(esp_mqtt_client_handle_t c,
                       const char *obj_id, const char *name,
                       const char *state_topic, const char *cmd_topic,
                       const char *options,   // JSON array string
                       const char *icon) {
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{"
        "\"name\":\"%s\","
        "\"stat_t\":\"%s\","
        "\"cmd_t\":\"%s\","
        "\"options\":%s,"
        "\"ic\":\"%s\","
        "\"uniq_id\":\"" HA_NODE_ID "_%s\","
        HA_DEVICE
        "}",
        name, state_topic, cmd_topic,
        options, icon, obj_id
    );
    ha_pub(c, "select", obj_id, buf);
}

// ── Button ────────────────────────────────────────────────────
static void ha_button(esp_mqtt_client_handle_t c,
                       const char *obj_id, const char *name,
                       const char *cmd_topic, const char *payload_press,
                       const char *icon) {
    char buf[384];
    snprintf(buf, sizeof(buf),
        "{"
        "\"name\":\"%s\","
        "\"cmd_t\":\"%s\","
        "\"pl_prs\":\"%s\","
        "\"ic\":\"%s\","
        "\"uniq_id\":\"" HA_NODE_ID "_%s\","
        HA_DEVICE
        "}",
        name, cmd_topic, payload_press, icon, obj_id
    );
    ha_pub(c, "button", obj_id, buf);
}

// ── Publish all discovery messages ───────────────────────────
static void lux_ha_discovery_publish(esp_mqtt_client_handle_t c) {
    ESP_LOGI(HA_TAG, "Publishing HA discovery...");

    // ── SENSORS ──────────────────────────────────────────────
    ha_sensor(c, "vpv1",       "PV1 Voltage",
              MQTT_PREFIX "/state/vpv1",       "V",   "voltage",    "mdi:solar-power");
    ha_sensor(c, "vpv2",       "PV2 Voltage",
              MQTT_PREFIX "/state/vpv2",       "V",   "voltage",    "mdi:solar-power");
    ha_sensor(c, "ppv1",       "PV1 Power",
              MQTT_PREFIX "/state/ppv1",       "W",   "power",      "mdi:solar-power");
    ha_sensor(c, "ppv2",       "PV2 Power",
              MQTT_PREFIX "/state/ppv2",       "W",   "power",      "mdi:solar-power");
    ha_sensor(c, "ppv_total",  "PV Total Power",
              MQTT_PREFIX "/state/ppv_total",  "W",   "power",      "mdi:solar-power-variant");
    ha_sensor(c, "vbat",       "Battery Voltage",
              MQTT_PREFIX "/state/vbat",       "V",   "voltage",    "mdi:battery");
    ha_sensor(c, "soc",        "Battery SOC",
              MQTT_PREFIX "/state/soc",        "%",   "battery",    NULL);
    ha_sensor(c, "bat_curr",   "Battery Current",
              MQTT_PREFIX "/state/bat_curr",   "A",   "current",    "mdi:current-dc");
    ha_sensor(c, "bat_power",  "Battery Power",
              MQTT_PREFIX "/state/bat_power",  "W",   "power",      "mdi:battery-charging");
    ha_sensor(c, "p_charge",   "Charge Power",
              MQTT_PREFIX "/state/p_charge",   "W",   "power",      "mdi:battery-arrow-up");
    ha_sensor(c, "p_discharge","Discharge Power",
              MQTT_PREFIX "/state/p_discharge","W",   "power",      "mdi:battery-arrow-down");
    ha_sensor(c, "vac_r",      "Grid Voltage",
              MQTT_PREFIX "/state/vac_r",      "V",   "voltage",    "mdi:transmission-tower");
    ha_sensor(c, "fac",        "Grid Frequency",
              MQTT_PREFIX "/state/fac",        "Hz",  "frequency",  "mdi:sine-wave");
    ha_sensor(c, "p_inv",      "Inverter Power",
              MQTT_PREFIX "/state/p_inv",      "W",   "power",      "mdi:lightning-bolt");
    ha_sensor(c, "p_to_grid",  "Power to Grid",
              MQTT_PREFIX "/state/p_to_grid",  "W",   "power",      "mdi:transmission-tower-export");
    ha_sensor(c, "p_to_user",  "Power to Load",
              MQTT_PREFIX "/state/p_to_user",  "W",   "power",      "mdi:home-lightning-bolt");
    ha_sensor(c, "p_load",     "Load Power",
              MQTT_PREFIX "/state/p_load",     "W",   "power",      "mdi:lightbulb-group");
    ha_sensor(c, "t_inner",    "Inverter Temp",
              MQTT_PREFIX "/state/t_inner",    "°C",  "temperature","mdi:thermometer");
    ha_sensor(c, "t_rad1",     "Radiator Temp",
              MQTT_PREFIX "/state/t_rad1",     "°C",  "temperature","mdi:thermometer");
    ha_sensor(c, "t_bat",      "Battery Temp",
              MQTT_PREFIX "/state/t_bat",      "°C",  "temperature","mdi:thermometer");

    // ── NUMBERS (writable) ────────────────────────────────────
    ha_number(c, "charge_rate", "Charge Current Limit",
              MQTT_PREFIX "/state/charge_rate",
              MQTT_CMD_PREFIX "/set/charge_rate",
              0, 110, 1, "A", "mdi:battery-arrow-up-outline");

    ha_number(c, "dischg_rate", "Discharge Current Limit",
              MQTT_PREFIX "/state/dischg_rate",
              MQTT_CMD_PREFIX "/set/dischg_rate",
              0, 110, 1, "A", "mdi:battery-arrow-down-outline");

    ha_number(c, "eod_soc", "End-of-Discharge SOC",
              MQTT_PREFIX "/state/eod_soc",
              MQTT_CMD_PREFIX "/set/eod_soc",
              10, 90, 1, "%", "mdi:battery-low");

    // ── SELECT (battery type) ─────────────────────────────────
    ha_select(c, "battery_type", "Battery Type",
              MQTT_PREFIX "/state/battery_type",
              MQTT_CMD_PREFIX "/set/battery_type",
              "[\"lithium\",\"leadacid\"]",
              "mdi:battery-heart");

    // ── BUTTONS ───────────────────────────────────────────────
    ha_button(c, "btn_restart",   "Restart Inverter",
              MQTT_CMD_PREFIX "/set/charge_rate",   // placeholder
              "__restart__",                         // handled in mqtt_handle_cmd
              "mdi:restart");

    ha_button(c, "btn_reset_all", "Reset All Settings",
              MQTT_CMD_PREFIX "/set/charge_rate",   // placeholder
              "__reset_all__",
              "mdi:restore");

    ESP_LOGI(HA_TAG, "Discovery published — check HA Devices for 'LuxPower SNA'");
}