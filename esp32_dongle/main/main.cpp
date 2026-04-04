#include "config.h"   // must come first so RELAY_MODE is visible below

extern "C" {
    void lux_cloud_task(void *pvParam);
    void lux_mqtt_task(void *pvParam);
#ifdef RELAY_MODE
    void lux_relay_start(void);
#endif
    void lux_local_server_start(void);
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

// ─────────────────────────────────────────────────────────────
// NOTE: WiFi credentials are hardcoded for now.
// TODO Phase 2: Replace with captive portal (like ESPHome WiFiManager):
//   - On first boot (no saved creds in NVS), start AP-only mode
//   - Serve a config page at 10.10.10.1 to collect SSID/password
//   - Save creds to NVS, reboot into STA+AP mode
// ─────────────────────────────────────────────────────────────

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
    ESP_LOGI(TAG, "=== LuxPower ESP32 Dongle ===");
    ESP_LOGI(TAG, "Dongle SN  : %s", DONGLE_SN);
    ESP_LOGI(TAG, "Inverter SN: %s", INVERTER_SN);
    ESP_LOGI(TAG, "Cloud      : %s:%d", LUX_CLOUD_HOST, LUX_CLOUD_PORT);
#ifdef RELAY_MODE
    ESP_LOGI(TAG, "Mode       : RELAY (real dongle → ESP32 → cloud)");
#else
    ESP_LOGI(TAG, "Mode       : DONGLE (ESP32 polls cloud directly)");
#endif

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Shared state (register cache, write queue)
    shared_state_init();

    // WiFi STA + AP
    wifi_init();

    // mDNS → luxdongle.local
    mdns_init_service();

    // OTA web server on port 8080
    lux_ota_start();

#ifdef RELAY_MODE
    // Core 0: Relay server — real dongle drives the cloud connection
    // lux_cloud_task NOT started (dongle handles it)
    lux_relay_start();
    lux_local_server_start(); 
#else
    // Core 0: Cloud TCP — ESP32 acts as dongle, polls cloud directly
    xTaskCreatePinnedToCore(lux_cloud_task, "lux_cloud",
                            STACK_CLOUD, NULL, TASK_PRIO_CLOUD, NULL, 0);
#endif

    // Core 1: MQTT publish + subscribe (unchanged regardless of mode)
    xTaskCreatePinnedToCore(lux_mqtt_task, "lux_mqtt",
                            STACK_MQTT, NULL, TASK_PRIO_MQTT, NULL, 1);

    ESP_LOGI(TAG, "All tasks launched");
    ESP_LOGI(TAG, "OTA : http://luxdongle.local:%d  or  http://%s:%d",
             OTA_PORT, WIFI_AP_IP, OTA_PORT);
    ESP_LOGI(TAG, "Logs: mosquitto_sub -h myhome.sfdp.net -u mqtt_user "
                  "-P D1ndh1sk@ -t lux/log");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        ESP_LOGI(TAG, "Uptime: %lums | input=%d hold=%d | queue=%u",
                 (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                 g_regs.input_valid, g_regs.hold_valid,
                 (unsigned)uxQueueMessagesWaiting(g_write_queue));
    }
}