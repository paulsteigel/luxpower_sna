#pragma once
// ── Remote logging via MQTT ────────────────────────────────────
// Usage:
//   1. Call lux_log_mqtt_attach(client) after MQTT_EVENT_CONNECTED
//   2. Subscribe to lux/log from any MQTT client to see ESP32 logs
//
// mosquitto_sub -h myhome.sfdp.net -u mqtt_user -P D1ndh1sk@ -t "lux/log"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "mqtt_client.h"
#include "esp_log.h"
#include "config.h"

static esp_mqtt_client_handle_t s_log_client  = NULL;

// Custom vprintf: print to UART + publish to MQTT
static int s_in_log = 0;

static int lux_log_vprintf(const char *fmt, va_list args) {
    if (s_in_log || !s_log_client) {
        return vprintf(fmt, args);
    }
    s_in_log = 1;
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    esp_mqtt_client_publish(s_log_client, "lux/log", buf, 0, 0, 0);
    s_in_log = 0;
    return strlen(buf);
}

// Call once after MQTT connected
static void lux_log_mqtt_attach(esp_mqtt_client_handle_t client) {
    s_log_client  = client;
    esp_log_set_vprintf(lux_log_vprintf);
}

// Call on MQTT disconnect to fall back to UART only
static void lux_log_mqtt_detach(void) {
    s_log_client  = NULL;
    esp_log_set_vprintf(vprintf);
}