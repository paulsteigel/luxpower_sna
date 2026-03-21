#pragma once
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "shared_state.h"
#include "config.h"

static const char *OTA_TAG = "ota";

// ── Status page ────────────────────────────────────────────────
static esp_err_t ota_status_handler(httpd_req_t *req) {
    const esp_app_desc_t *desc = esp_app_get_description();
    char buf[2048];
    snprintf(buf, sizeof(buf),
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'><title>LuxDongle</title>"
        "<meta http-equiv='refresh' content='10'>"
        "<style>body{font-family:monospace;padding:20px;background:#111;color:#0f0}"
        "h2{color:#0f0}table{border-collapse:collapse;width:100%%}"
        "td{padding:4px 12px;border:1px solid #333}"
        "input[type=file],input[type=submit]{margin:8px 4px;padding:6px 12px}"
        "input[type=submit]{background:#080;color:#fff;border:none;cursor:pointer}"
        "</style></head><body>"
        "<h2>&#x26A1; LuxDongle</h2>"
        "<table>"
        "<tr><td>Firmware</td><td>%s %s</td></tr>"
        "<tr><td>Dongle SN</td><td>%s</td></tr>"
        "<tr><td>Inverter SN</td><td>%s</td></tr>"
        "<tr><td>Input valid</td><td>%s</td></tr>"
        "<tr><td>Hold valid</td><td>%s</td></tr>"
        "<tr><td>V_bat</td><td>%.1f V</td></tr>"
        "<tr><td>SOC</td><td>%u %%</td></tr>"
        "<tr><td>PV total</td><td>%u W</td></tr>"
        "<tr><td>Charge</td><td>%u W</td></tr>"
        "<tr><td>Discharge</td><td>%u W</td></tr>"
        "</table>"
        "<h3>OTA Firmware Update</h3>"
        "<form method='POST' action='/ota' enctype='multipart/form-data'>"
        "<input type='file' name='f' accept='.bin'>&nbsp;"
        "<input type='submit' value='Flash &amp; Reboot'>"
        "</form>"
        "<p style='color:#555;font-size:11px'>Auto-refresh every 10s</p>"
        "</body></html>",
        desc->project_name, desc->version,
        DONGLE_SN, INVERTER_SN,
        g_regs.input_valid ? "YES" : "no",
        g_regs.hold_valid  ? "YES" : "no",
        g_regs.input[4] * 0.1f,          // vbat
        g_regs.input[5] & 0xFF,          // soc
        g_regs.input[7] + g_regs.input[8], // ppv1+ppv2
        g_regs.input[10],                // p_charge
        g_regs.input[11]                 // p_discharge
    );
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// ── OTA upload handler ─────────────────────────────────────────
static esp_err_t ota_upload_handler(httpd_req_t *req) {
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "No OTA partition found");
        return ESP_FAIL;
    }

    esp_ota_handle_t handle;
    if (esp_ota_begin(part, OTA_SIZE_UNKNOWN, &handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "OTA begin failed");
        return ESP_FAIL;
    }

    char   buf[1024];
    size_t written  = 0;
    int    received;
    int    remaining = req->content_len;

    while (remaining > 0) {
        int to_recv = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
        received = httpd_req_recv(req, buf, to_recv);
        if (received <= 0) {
            esp_ota_abort(handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Receive error");
            return ESP_FAIL;
        }
        if (esp_ota_write(handle, buf, received) != ESP_OK) {
            esp_ota_abort(handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "OTA write error");
            return ESP_FAIL;
        }
        remaining -= received;
        written   += received;
    }

    if (esp_ota_end(handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "OTA validation failed");
        return ESP_FAIL;
    }
    esp_ota_set_boot_partition(part);

    ESP_LOGI(OTA_TAG, "OTA OK — wrote %u bytes, rebooting...", (unsigned)written);
    httpd_resp_sendstr(req,
        "<!DOCTYPE html><html><body style='background:#111;color:#0f0;font-family:monospace'>"
        "<h2>&#x2705; Flash OK — rebooting...</h2>"
        "<p>Page will reload in 10s.</p>"
        "<script>setTimeout(()=>location.href='/',10000)</script>"
        "</body></html>");

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

// ── Start OTA HTTP server ──────────────────────────────────────
static void lux_ota_start(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = OTA_PORT;
    cfg.max_uri_handlers = 4;
    cfg.recv_wait_timeout  = 30;
    cfg.send_wait_timeout  = 30;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(OTA_TAG, "Failed to start HTTP server on port %d", OTA_PORT);
        return;
    }
    
    httpd_uri_t status = {
        .uri = "/", .method = HTTP_GET, .handler = ota_status_handler, .user_ctx = NULL
    };
    httpd_uri_t upload = {
        .uri = "/ota", .method = HTTP_POST, .handler = ota_upload_handler, .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &status);
    httpd_register_uri_handler(server, &upload);

    ESP_LOGI(OTA_TAG, "OTA server ready → http://luxdongle.local:%d", OTA_PORT);
}