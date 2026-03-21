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

static esp_mqtt_client_handle_t s_log_mqtt_client = NULL;

// Custom vprintf: print to UART + publish to MQTT
static int lux_log_vprintf(const char *fmt, va_list args) {
    // 1. Always write to UART (original behavior)
    int ret = vprintf(fmt, args);

    // 2. Publish to MQTT if connected
    // Need a fresh va_list — can't reuse args after vprintf consumed it.
    // Use a fixed buffer to format the message.
    if (s_log_mqtt_client) {
        char buf[256];
        // Re-format: we can't va_copy reliably here without C99 guarantee,
        // so we use a separate vsnprintf pass. The fmt+args are still valid
        // because vprintf on Xtensa doesn't consume the va_list (it's passed
        // by value in the ABI). This is safe on ESP-IDF/Xtensa.
        vsnprintf(buf, sizeof(buf), fmt, args);
        // Strip trailing newline for cleaner MQTT messages
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
            buf[--len] = '\0';
        if (len > 0)
            esp_mqtt_client_publish(s_log_mqtt_client,
                                    MQTT_LOG_TOPIC, buf, (int)len, 0, 0);
    }
    return ret;
}

// Call once after MQTT connected
static void lux_log_mqtt_attach(esp_mqtt_client_handle_t client) {
    s_log_mqtt_client = client;
    esp_log_set_vprintf(lux_log_vprintf);
}

// Call on MQTT disconnect to fall back to UART only
static void lux_log_mqtt_detach(void) {
    s_log_mqtt_client = NULL;
    esp_log_set_vprintf(vprintf);
}