#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include "config.h"
#include "shared_state.h"

static const char *TAG = "lux_mqtt";
static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;

// ── MQTT sensor definitions ───────────────────────────────────
typedef struct {
    const char *name;
    uint16_t    reg;
    bool        is_input;    // true=input reg, false=hold reg
    float       scale;
    bool        is_signed;
    const char *unit;
    const char *device_class;
} mqtt_sensor_t;

static const mqtt_sensor_t SENSORS[] = {
    // INPUT registers (real-time data)
    {"vpv1",          1,  true,  0.1f,  false, "V",   "voltage"},
    {"vpv2",          2,  true,  0.1f,  false, "V",   "voltage"},
    {"vbat",          4,  true,  0.1f,  false, "V",   "voltage"},
    {"soc",           5,  true,  1.0f,  false, "%",   "battery"},
    {"ppv1",          7,  true,  1.0f,  false, "W",   "power"},
    {"ppv2",          8,  true,  1.0f,  false, "W",   "power"},
    {"p_charge",     10,  true,  1.0f,  false, "W",   "power"},
    {"p_discharge",  11,  true,  1.0f,  false, "W",   "power"},
    {"vac_r",        12,  true,  0.1f,  false, "V",   "voltage"},
    {"fac",          15,  true,  0.01f, false, "Hz",  "frequency"},
    {"p_inv",        16,  true,  1.0f,  false, "W",   "power"},
    {"p_to_grid",    26,  true,  1.0f,  false, "W",   "power"},
    {"p_to_user",    27,  true,  1.0f,  false, "W",   "power"},
    {"t_inner",      64,  true,  1.0f,  true,  "°C",  "temperature"},
    {"t_rad1",       65,  true,  1.0f,  true,  "°C",  "temperature"},
    {"t_bat",        67,  true,  1.0f,  true,  "°C",  "temperature"},
    {"bat_curr",     98,  true,  0.01f, true,  "A",   "current"},
    {"p_eps",        24,  true,  1.0f,  false, "W",   "power"},
    {"p_load",      170,  true,  1.0f,  false, "W",   "power"},
    // HOLD registers (config)
    {"charge_rate",  101, false, 1.0f,  false, "A",   NULL},
    {"dischg_rate",  102, false, 1.0f,  false, "A",   NULL},
    {"eod_soc",      105, false, 1.0f,  false, "%",   NULL},
    {"sys_enable",   120, false, 1.0f,  false, "",    NULL},
};
#define SENSOR_COUNT (sizeof(SENSORS) / sizeof(mqtt_sensor_t))

// ── Publish one value ─────────────────────────────────────────
static void mqtt_publish_float(const char *name, float value) {
    if (!s_connected || !s_client) return;
    char topic[64], payload[32];
    snprintf(topic,   sizeof(topic),   MQTT_PREFIX "/state/%s", name);
    snprintf(payload, sizeof(payload), "%.2f", value);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
}

static void mqtt_publish_int(const char *name, int value) {
    if (!s_connected || !s_client) return;
    char topic[64], payload[16];
    snprintf(topic,   sizeof(topic),   MQTT_PREFIX "/state/%s", name);
    snprintf(payload, sizeof(payload), "%d", value);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
}

// ── Publish all sensor data ───────────────────────────────────
static void mqtt_publish_all(void) {
    for (int i = 0; i < (int)SENSOR_COUNT; i++) {
        const mqtt_sensor_t *s = &SENSORS[i];
        uint16_t raw = s->is_input ? reg_get_input(s->reg) : reg_get_hold(s->reg);

        if (s->is_signed) {
            int16_t sv = (int16_t)raw;
            mqtt_publish_float(s->name, (float)sv * s->scale);
        } else {
            // Special: SOC is low byte only
            if (s->reg == 5 && s->is_input) raw = raw & 0xFF;
            mqtt_publish_float(s->name, (float)raw * s->scale);
        }
    }

    // Derived: PV total
    float ppv_total = (float)(reg_get_input(7) + reg_get_input(8));
    mqtt_publish_float("ppv_total", ppv_total);

    // Derived: battery power (positive=charge, negative=discharge)
    float bat_pow = (float)reg_get_input(10) - (float)reg_get_input(11);
    mqtt_publish_float("bat_power", bat_pow);

    ESP_LOGD(TAG, "Published %d sensors", (int)SENSOR_COUNT + 2);
}

// ── Subscribe to command topics ───────────────────────────────
static void mqtt_subscribe_all(void) {
    char topic[64];
    // Numeric set commands
    const char *cmd_topics[] = {
        MQTT_CMD_PREFIX "/set/charge_rate",
        MQTT_CMD_PREFIX "/set/dischg_rate",
        MQTT_CMD_PREFIX "/set/eod_soc",
        MQTT_CMD_PREFIX "/set/sys_enable",
        MQTT_CMD_PREFIX "/set/battery_type",   // "lithium" or "leadacid"
        NULL
    };
    for (int i = 0; cmd_topics[i]; i++) {
        esp_mqtt_client_subscribe(s_client, cmd_topics[i], 1);
        ESP_LOGI(TAG, "Subscribed: %s", cmd_topics[i]);
    }
    (void)topic;
}

// ── Handle incoming MQTT command ──────────────────────────────
static void mqtt_handle_cmd(const char *topic, const char *data, int data_len) {
    char payload[32] = {};
    if (data_len > (int)sizeof(payload) - 1) data_len = sizeof(payload) - 1;
    memcpy(payload, data, data_len);

    ESP_LOGI(TAG, "CMD: %s = %s", topic, payload);

    if (strstr(topic, "charge_rate")) {
        int val = atoi(payload);
        if (val >= 0 && val <= 140)
            cmd_queue_write(101, (uint16_t)val, "mqtt");

    } else if (strstr(topic, "dischg_rate")) {
        int val = atoi(payload);
        if (val >= 0 && val <= 140)
            cmd_queue_write(102, (uint16_t)val, "mqtt");

    } else if (strstr(topic, "eod_soc")) {
        int val = atoi(payload);
        if (val >= 10 && val <= 90)
            cmd_queue_write(105, (uint16_t)val, "mqtt");

    } else if (strstr(topic, "sys_enable")) {
        int val = atoi(payload);
        cmd_queue_write(120, (uint16_t)val, "mqtt");

    } else if (strstr(topic, "battery_type")) {
        write_cmd_t cmd = {};
        if (strcmp(payload, "lithium") == 0) {
            cmd.type = CMD_SET_LITHIUM;
            ESP_LOGI(TAG, "CMD: set battery → Lithium brand6");
        } else if (strcmp(payload, "leadacid") == 0) {
            cmd.type = CMD_SET_LEADACID;
            ESP_LOGI(TAG, "CMD: set battery → LeadAcid brand6");
        } else {
            ESP_LOGW(TAG, "Unknown battery_type: %s", payload);
            return;
        }
        strncpy(cmd.source, "mqtt", sizeof(cmd.source)-1);
        xQueueSend(g_write_queue, &cmd, pdMS_TO_TICKS(100));
    }
}

// ── MQTT event handler ────────────────────────────────────────
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to broker");
            s_connected = true;
            mqtt_subscribe_all();
            mqtt_publish_all();
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from broker");
            s_connected = false;
            break;

        case MQTT_EVENT_DATA:
            if (event->topic && event->data) {
                char topic[128] = {};
                int tlen = event->topic_len < 127 ? event->topic_len : 127;
                memcpy(topic, event->topic, tlen);
                mqtt_handle_cmd(topic, event->data, event->data_len);
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            break;

        default: break;
    }
}

// ── Init and start MQTT client ────────────────────────────────
void lux_mqtt_init(void) {
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri        = MQTT_BROKER_URI,
        .credentials.username      = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS,
        .credentials.client_id     = MQTT_CLIENT_ID,
        .session.keepalive          = 60,
        .network.reconnect_timeout_ms = 5000,
    };
    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "MQTT client started → %s", MQTT_BROKER_URI);
}

// ── Periodic publish task ─────────────────────────────────────
void lux_mqtt_task(void *arg) {
    ESP_LOGI(TAG, "MQTT task started on core %d", xPortGetCoreID());
    lux_mqtt_init();

    uint32_t last_publish = 0;

    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Check event flags for immediate publish
        bool do_publish = false;
        xSemaphoreTake(g_events.mutex, portMAX_DELAY);
        if (g_events.input_updated) {
            g_events.input_updated = false;
            do_publish = true;
        }
        xSemaphoreGive(g_events.mutex);

        // Also publish on interval
        if (now - last_publish >= POLL_INPUT_MS) {
            do_publish = true;
            last_publish = now;
        }

        if (do_publish && s_connected) {
            mqtt_publish_all();
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
