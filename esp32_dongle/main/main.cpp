// main.cpp — full version
// Fixes from testing:
//   - esp_wifi_set_ps(WIFI_PS_NONE) — prevents beacon timeout/WiFi drop
//   - Cloud IP 47.81.11.236 confirmed working
//   - RELAY_MODE only — lux_cloud_task not started
//   - shared_state_init() called before tasks

#include "config.h"

extern "C" {
    void lux_relay_start(void);
    void lux_local_server_start(void);
    void lux_mqtt_task(void *arg);
}

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
#include "mdns.h"

#include "shared_state.h"
#include "lux_ota.h"

static const char *TAG = "main";

static EventGroupHandle_t s_wifi_evt;
#define WIFI_STA_OK_BIT   BIT0
#define WIFI_STA_FAIL_BIT BIT1
static int s_retry = 0;
#define MAX_RETRY 10

static void wifi_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data) {
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            if (s_retry < MAX_RETRY) {
                esp_wifi_connect();
                ESP_LOGW(TAG, "WiFi retry %d/%d", ++s_retry, MAX_RETRY);
            } else {
                xEventGroupSetBits(s_wifi_evt, WIFI_STA_FAIL_BIT);
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "STA IP: " IPSTR, IP2STR(&e->ip_info.ip));
        s_retry = 0;
        xEventGroupSetBits(s_wifi_evt, WIFI_STA_OK_BIT);
    }
}

static void wifi_init(void) {
    s_wifi_evt = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    esp_netif_t *ap  = esp_netif_create_default_wifi_ap();
    (void)sta;

    // Static IP for AP
    esp_netif_ip_info_t ap_ip = {};
    ip4addr_aton(WIFI_AP_IP,      (ip4_addr_t *)&ap_ip.ip);
    ip4addr_aton(WIFI_AP_GW,      (ip4_addr_t *)&ap_ip.gw);
    ip4addr_aton(WIFI_AP_NETMASK, (ip4_addr_t *)&ap_ip.netmask);
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap, &ap_ip));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                wifi_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                wifi_handler, NULL));

    wifi_config_t sta_cfg = {};
    strncpy((char *)sta_cfg.sta.ssid,     WIFI_STA_SSID, 32);
    strncpy((char *)sta_cfg.sta.password, WIFI_STA_PASS, 64);
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    wifi_config_t ap_cfg = {};
    strncpy((char *)ap_cfg.ap.ssid,     WIFI_AP_SSID, 32);
    strncpy((char *)ap_cfg.ap.password, WIFI_AP_PASS, 64);
    ap_cfg.ap.ssid_len       = strlen(WIFI_AP_SSID);
    ap_cfg.ap.channel        = 6;
    ap_cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.max_connection = 2;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,  &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    // ── Critical fix: disable power saving to prevent beacon timeout ──
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "WiFi STA: %s | AP: %s (%s)",
             WIFI_STA_SSID, WIFI_AP_SSID, WIFI_AP_IP);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_evt,
        WIFI_STA_OK_BIT | WIFI_STA_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));

    if (bits & WIFI_STA_OK_BIT)
        ESP_LOGI(TAG, "WiFi STA connected ✓");
    else
        ESP_LOGW(TAG, "WiFi STA failed — AP-only mode (OTA still works)");
}

static void mdns_init_service(void) {
    ESP_ERROR_CHECK(mdns_init());
    mdns_hostname_set(MDNS_HOSTNAME);
    mdns_instance_name_set("LuxPower Dongle");
    mdns_service_add(NULL, "_http", "_tcp", OTA_PORT, NULL, 0);
    ESP_LOGI(TAG, "mDNS: http://%s.local:%d", MDNS_HOSTNAME, OTA_PORT);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== LuxPower ESP32 Relay + MQTT ===");
    ESP_LOGI(TAG, "Cloud  : %s:%d", LUX_CLOUD_HOST, LUX_CLOUD_PORT);
    ESP_LOGI(TAG, "Dongle : %s:%d", DONGLE_LOCAL_IP, DONGLE_LOCAL_PORT);

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Shared state — must be before any task that uses it
    shared_state_init();

    // WiFi STA + AP, power saving disabled
    wifi_init();

    // mDNS → luxdongle.local
    mdns_init_service();

    // OTA web server on port 8080
    lux_ota_start();

    // Port 4346: real dongle → cloud (transparent relay, parses frames → shared_state)
    lux_relay_start();

    // Port 8000: ESPHome → real dongle (transparent relay)
    lux_local_server_start();

    // Core 1: MQTT publish + HA discovery + subscribe commands
    xTaskCreatePinnedToCore(lux_mqtt_task, "lux_mqtt",
                            STACK_MQTT, NULL, TASK_PRIO_MQTT, NULL, 1);

    ESP_LOGI(TAG, "All tasks launched");
    ESP_LOGI(TAG, "OTA  : http://luxdongle.local:%d  or  http://%s:%d",
             OTA_PORT, WIFI_AP_IP, OTA_PORT);
    ESP_LOGI(TAG, "MQTT : mosquitto_sub -h myhome.sfdp.net -u mqtt_user "
                  "-P D1ndh1sk@ -t lux/#");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        ESP_LOGI(TAG, "Uptime: %lums | input=%d hold=%d | queue=%u",
                 (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                 g_regs.input_valid, g_regs.hold_valid,
                 (unsigned)uxQueueMessagesWaiting(g_write_queue));
    }
}