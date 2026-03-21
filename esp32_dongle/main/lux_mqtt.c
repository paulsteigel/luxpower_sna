#include "lux_ha_discovery.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include "config.h"
#include "shared_state.h"
#include "lux_log_mqtt.h"

static const char *TAG = "lux_mqtt";
static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;

// ── Sensor map ────────────────────────────────────────────────
typedef struct {
    const char *name;
    uint16_t    reg;
    bool        is_input;
    float       scale;
    bool        is_signed;
} mqtt_sensor_t;

static const mqtt_sensor_t SENSORS[] = {
    {"vpv1",      1,  true,  0.1f,  false},
    {"vpv2",      2,  true,  0.1f,  false},
    {"vbat",      4,  true,  0.1f,  false},
    {"soc",       5,  true,  1.0f,  false},   // low byte only
    {"ppv1",      7,  true,  1.0f,  false},
    {"ppv2",      8,  true,  1.0f,  false},
    {"p_charge",  10, true,  1.0f,  false},
    {"p_discharge",11,true,  1.0f,  false},
    {"vac_r",     12, true,  0.1f,  false},
    {"fac",       15, true,  0.01f, false},
    {"p_inv",     16, true,  1.0f,  false},
    {"p_to_grid", 26, true,  1.0f,  false},
    {"p_to_user", 27, true,  1.0f,  false},
    {"t_inner",   64, true,  1.0f,  true },
    {"t_rad1",    65, true,  1.0f,  true },
    {"t_bat",     67, true,  1.0f,  true },
    {"bat_curr",  98, true,  0.01f, true },
    {"p_load",   170, true,  1.0f,  false},
    // HOLD (config)
    {"charge_rate", 101, false, 1.0f, false},
    {"dischg_rate", 102, false, 1.0f, false},
    {"eod_soc",     105, false, 1.0f, false},
};
#define SENSOR_COUNT (sizeof(SENSORS) / sizeof(mqtt_sensor_t))

// ── Publish helpers ───────────────────────────────────────────
static void pub_float(const char *name, float val) {
    if (!s_connected) return;
    char topic[64], payload[24];
    snprintf(topic,   sizeof(topic),   MQTT_PREFIX "/state/%s", name);
    snprintf(payload, sizeof(payload), "%.2f", val);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
}

static void mqtt_publish_all(void) {
    for (int i = 0; i < (int)SENSOR_COUNT; i++) {
        const mqtt_sensor_t *s = &SENSORS[i];
        uint16_t raw = s->is_input ? reg_get_input(s->reg)
                                   : reg_get_hold(s->reg);
        float val;
        if (s->is_signed) {
            val = (float)(int16_t)raw * s->scale;
        } else {
            // SOC stored in low byte
            if (s->reg == 5 && s->is_input) raw = raw & 0xFF;
            val = (float)raw * s->scale;
        }
        pub_float(s->name, val);
    }
    // Derived sensors
    pub_float("ppv_total",
              (float)(reg_get_input(7) + reg_get_input(8)));
    pub_float("bat_power",
              (float)reg_get_input(10) - (float)reg_get_input(11));
}

// ── Subscribe to commands ─────────────────────────────────────
static void mqtt_subscribe_all(void) {
    const char *topics[] = {
        MQTT_CMD_PREFIX "/set/charge_rate",
        MQTT_CMD_PREFIX "/set/dischg_rate",
        MQTT_CMD_PREFIX "/set/eod_soc",
        MQTT_CMD_PREFIX "/set/sys_enable",
        MQTT_CMD_PREFIX "/set/battery_type",
        MQTT_CMD_PREFIX "/control",
        NULL
    };
    for (int i = 0; topics[i]; i++)
        esp_mqtt_client_subscribe(s_client, topics[i], 1);
}

// ── Handle incoming command ───────────────────────────────────
static void mqtt_handle_cmd(const char *topic, const char *data, int dlen) {
    char payload[32] = {};
    if (dlen > (int)sizeof(payload)-1) dlen = sizeof(payload)-1;
    memcpy(payload, data, dlen);
    ESP_LOGI(TAG, "CMD %s = %s", topic, payload);

    if (strstr(topic, "control")) {
        if (strcmp(payload, "__restart__") == 0)
            cmd_queue_write(11, 128, "mqtt");
        else if (strcmp(payload, "__reset_all__") == 0)
            cmd_queue_write(11, 2, "mqtt");

    } else if (strstr(topic, "charge_rate")) {
        int v = atoi(payload);
        if (v >= 0 && v <= 140) cmd_queue_write(101, (uint16_t)v, "mqtt");

    } else if (strstr(topic, "dischg_rate")) {
        int v = atoi(payload);
        if (v >= 0 && v <= 140) cmd_queue_write(102, (uint16_t)v, "mqtt");

    } else if (strstr(topic, "eod_soc")) {
        int v = atoi(payload);
        if (v >= 10 && v <= 90) cmd_queue_write(105, (uint16_t)v, "mqtt");

    } else if (strstr(topic, "sys_enable")) {
        cmd_queue_write(120, (uint16_t)atoi(payload), "mqtt");

    } else if (strstr(topic, "battery_type")) {
        write_cmd_t cmd = {};
        strncpy(cmd.source, "mqtt", sizeof(cmd.source)-1);
        if (strcmp(payload, "lithium") == 0)
            cmd.type = CMD_SET_LITHIUM;
        else if (strcmp(payload, "leadacid") == 0)
            cmd.type = CMD_SET_LEADACID;
        else { ESP_LOGW(TAG, "Unknown battery_type: %s", payload); return; }
        xQueueSend(g_write_queue, &cmd, pdMS_TO_TICKS(100));
    }
}
// ── MQTT event handler ────────────────────────────────────────
static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data) {
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)data;
    switch ((esp_mqtt_event_id_t)id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to broker");
            s_connected = true;
            lux_log_mqtt_attach(ev->client);   // redirect logs to MQTT
            mqtt_subscribe_all();
            lux_ha_discovery_publish(ev->client);
            mqtt_publish_all();
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected");
            s_connected = false;
            lux_log_mqtt_detach();
            break;

        case MQTT_EVENT_DATA:
            if (ev->topic && ev->data) {
                char topic[128] = {};
                int tl = ev->topic_len < 127 ? ev->topic_len : 127;
                memcpy(topic, ev->topic, tl);
                mqtt_handle_cmd(topic, ev->data, ev->data_len);
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            break;

        default: break;
    }
}

void lux_mqtt_init(void) {
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS,
        .credentials.client_id = MQTT_CLIENT_ID,
        .session.keepalive = 60,
        .network.reconnect_timeout_ms = 5000,
    };
    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "MQTT client → %s", MQTT_BROKER_URI);
}

void lux_mqtt_task(void *arg) {
    ESP_LOGI(TAG, "MQTT task on core %d", xPortGetCoreID());
    lux_mqtt_init();

    uint32_t last_pub = 0;
    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        bool do_pub = false;
        xSemaphoreTake(g_events.mutex, portMAX_DELAY);
        if (g_events.input_updated) { g_events.input_updated = false; do_pub = true; }
        xSemaphoreGive(g_events.mutex);

        if (now - last_pub >= POLL_INPUT_MS) { do_pub = true; last_pub = now; }
        if (do_pub && s_connected) mqtt_publish_all();

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}