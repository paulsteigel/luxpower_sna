#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/ip4_addr.h"
#include "esp_netif_types.h"

#include "config.h"
#include "shared_state.h"

static const char *TAG = "main";

// ── WiFi event bits ───────────────────────────────────────────
static EventGroupHandle_t s_wifi_events;
#define WIFI_STA_CONNECTED_BIT BIT0
#define WIFI_STA_FAILED_BIT    BIT1

// ── Task declarations ─────────────────────────────────────────
extern void lux_cloud_task(void *arg);
extern void lux_mqtt_task(void *arg);

// ── WiFi event handler ────────────────────────────────────────
static int s_retry = 0;
#define MAX_RETRY 10

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *data) {
    if (base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (s_retry < MAX_RETRY) {
                esp_wifi_connect();
                s_retry++;
                ESP_LOGW(TAG, "WiFi retry %d/%d", s_retry, MAX_RETRY);
            } else {
                xEventGroupSetBits(s_wifi_events, WIFI_STA_FAILED_BIT);
                ESP_LOGE(TAG, "WiFi STA failed after %d retries", MAX_RETRY);
            }
        } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
            ESP_LOGI(TAG, "LuxApp connected to AP, MAC: "MACSTR, MAC2STR(e->mac));
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            ESP_LOGI(TAG, "LuxApp disconnected from AP");
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WiFi STA IP: " IPSTR, IP2STR(&e->ip_info.ip));
        s_retry = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_STA_CONNECTED_BIT);
    }
}

// ── Init WiFi: STA + AP simultaneously ───────────────────────
static void wifi_init(void) {
    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create STA and AP netif
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_t *ap_netif  = esp_netif_create_default_wifi_ap();
    (void)sta_netif;

    // Set static IP for AP
    esp_netif_ip_info_t ap_ip = {};
    ip4addr_aton(WIFI_AP_IP,      (ip4_addr_t *)&ap_ip.ip);
    ip4addr_aton(WIFI_AP_GW,      (ip4_addr_t *)&ap_ip.gw);
    ip4addr_aton(WIFI_AP_NETMASK, (ip4_addr_t *)&ap_ip.netmask);
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ap_ip));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                wifi_event_handler, NULL));

    // STA config
    wifi_config_t sta_cfg = {};
    strncpy((char *)sta_cfg.sta.ssid,     WIFI_STA_SSID, 32);
    strncpy((char *)sta_cfg.sta.password, WIFI_STA_PASS, 64);
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    // AP config
    wifi_config_t ap_cfg = {};
    strncpy((char *)ap_cfg.ap.ssid,     WIFI_AP_SSID, 32);
    strncpy((char *)ap_cfg.ap.password, WIFI_AP_PASS, 64);
    ap_cfg.ap.ssid_len    = strlen(WIFI_AP_SSID);
    ap_cfg.ap.channel     = 6;
    ap_cfg.ap.authmode    = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.max_connection = 2;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,  &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi started — STA: %s | AP: %s (%s)",
             WIFI_STA_SSID, WIFI_AP_SSID, WIFI_AP_IP);

    // Wait for STA connection (non-fatal if fails — AP still works)
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                            WIFI_STA_CONNECTED_BIT | WIFI_STA_FAILED_BIT,
                                            pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(30000));
    if (bits & WIFI_STA_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi STA connected ✓");
    } else {
        ESP_LOGW(TAG, "WiFi STA not connected — AP-only mode");
    }
}

// ── App entry point ───────────────────────────────────────────
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== LuxPower Dongle Replacement v1.0 ===");
    ESP_LOGI(TAG, "Dongle SN:   %s", DONGLE_SN);
    ESP_LOGI(TAG, "Inverter SN: %s", INVERTER_SN);

    // NVS init (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Shared state (register cache, write queue, event flags)
    shared_state_init();

    // WiFi: STA (home LAN) + AP (for LuxApp)
    wifi_init();

    // ── Launch tasks ──────────────────────────────────────────
    // Core 0: Cloud TCP client (protocol-heavy)
    xTaskCreatePinnedToCore(
        lux_cloud_task,
        "lux_cloud",
        STACK_CLOUD,
        NULL,
        TASK_PRIO_CLOUD,
        NULL,
        0   // Core 0
    );

    // Core 1: MQTT (network I/O)
    xTaskCreatePinnedToCore(
        lux_mqtt_task,
        "lux_mqtt",
        STACK_MQTT,
        NULL,
        TASK_PRIO_MQTT,
        NULL,
        1   // Core 1
    );

    ESP_LOGI(TAG, "All tasks launched");

    // main loop — watchdog + status log
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        ESP_LOGI(TAG, "Status — input_valid=%d hold_valid=%d queue_depth=%u",
                 g_regs.input_valid, g_regs.hold_valid,
                 (unsigned)uxQueueMessagesWaiting(g_write_queue));
    }
}
