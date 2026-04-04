// lux_local_server.c
// TCP :8000 — pure transparent relay
// ESPHome → ESP32:8000 → real dongle DONGLE_LOCAL_IP:DONGLE_LOCAL_PORT
// Verbose serial logging so connection flow is visible.

#include "lux_local_server.h"
#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <errno.h>
#include <stdlib.h>

static const char *TAG = "local";

#define LOCAL_PORT   8000
#define BUF_SIZE     1024
#define CONN_STACK   8192
#define SRV_STACK    4096
#define TASK_PRIO    4

// ── Per-connection task ───────────────────────────────────────
typedef struct { int client; char client_ip[20]; uint16_t client_port; } conn_arg_t;

static void conn_task(void *arg) {
    conn_arg_t *ctx = (conn_arg_t *)arg;
    int  cs          = ctx->client;
    char cip[20];
    uint16_t cport   = ctx->client_port;
    strncpy(cip, ctx->client_ip, sizeof(cip));
    free(ctx);

    ESP_LOGI(TAG, "[%s:%u] ── session start", cip, cport);

    // ── Connect to real dongle local server ───────────────────
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res  = NULL;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", DONGLE_LOCAL_PORT);

    if (getaddrinfo(DONGLE_LOCAL_IP, port_str, &hints, &res) != 0 || !res) {
        ESP_LOGE(TAG, "[%s:%u] DNS fail for %s", cip, cport, DONGLE_LOCAL_IP);
        close(cs);
        vTaskDelete(NULL);
        return;
    }
    int ds = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct timeval tv = { .tv_sec = 10 };
    setsockopt(ds, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(ds, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (ds < 0 || connect(ds, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "[%s:%u] Cannot reach dongle %s:%d  errno=%d",
                 cip, cport, DONGLE_LOCAL_IP, DONGLE_LOCAL_PORT, errno);
        if (ds >= 0) close(ds);
        freeaddrinfo(res);
        close(cs);
        vTaskDelete(NULL);
        return;
    }
    freeaddrinfo(res);

    // Clear timeout for relay operation
    tv.tv_sec = 0;
    setsockopt(ds, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(ds, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "[%s:%u] dongle %s:%d connected  fd:client=%d fd:dongle=%d",
             cip, cport, DONGLE_LOCAL_IP, DONGLE_LOCAL_PORT, cs, ds);

    // ── Relay loop ────────────────────────────────────────────
    uint8_t *buf  = malloc(BUF_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "[%s:%u] malloc fail", cip, cport);
        close(cs); close(ds);
        vTaskDelete(NULL);
        return;
    }

    uint32_t bytes_c2d = 0, bytes_d2c = 0;
    int maxfd = (cs > ds ? cs : ds) + 1;

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(cs, &rfds);
        FD_SET(ds, &rfds);
        struct timeval sel_tv = { .tv_sec = 120 };

        int r = select(maxfd, &rfds, NULL, NULL, &sel_tv);
        if (r < 0) {
            ESP_LOGW(TAG, "[%s:%u] select err errno=%d", cip, cport, errno);
            break;
        }
        if (r == 0) {
            ESP_LOGW(TAG, "[%s:%u] idle 120s — closing", cip, cport);
            break;
        }

        // ESPHome → dongle
        if (FD_ISSET(cs, &rfds)) {
            int n = recv(cs, buf, BUF_SIZE, 0);
            if (n <= 0) {
                ESP_LOGI(TAG, "[%s:%u] ESPHome closed (recv=%d errno=%d)",
                         cip, cport, n, errno);
                break;
            }
            bytes_c2d += n;
            ESP_LOGD(TAG, "[%s:%u] ESP→dongle %dB  (total=%lu)",
                     cip, cport, n, (unsigned long)bytes_c2d);
            if (send(ds, buf, n, 0) < 0) {
                ESP_LOGW(TAG, "[%s:%u] →dongle send fail errno=%d", cip, cport, errno);
                break;
            }
        }

        // Dongle → ESPHome
        if (FD_ISSET(ds, &rfds)) {
            int n = recv(ds, buf, BUF_SIZE, 0);
            if (n <= 0) {
                ESP_LOGW(TAG, "[%s:%u] dongle closed (recv=%d errno=%d) — "
                         "dongle may limit connections",
                         cip, cport, n, errno);
                break;
            }
            bytes_d2c += n;
            ESP_LOGD(TAG, "[%s:%u] dongle→ESP %dB  (total=%lu)",
                     cip, cport, n, (unsigned long)bytes_d2c);
            if (send(cs, buf, n, 0) < 0) {
                ESP_LOGW(TAG, "[%s:%u] →ESPHome send fail errno=%d", cip, cport, errno);
                break;
            }
        }
    }

    ESP_LOGI(TAG, "[%s:%u] ── session end  ESP→dongle:%luB  dongle→ESP:%luB",
             cip, cport,
             (unsigned long)bytes_c2d,
             (unsigned long)bytes_d2c);

    free(buf);
    close(cs);
    close(ds);
    vTaskDelete(NULL);
}

// ── Accept loop ───────────────────────────────────────────────
static void server_task(void *arg) {
    int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls < 0) { ESP_LOGE(TAG, "socket err %d", errno); vTaskDelete(NULL); return; }

    int opt = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in ba = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(LOCAL_PORT),
    };
    if (bind(ls, (struct sockaddr *)&ba, sizeof(ba)) || listen(ls, 4)) {
        ESP_LOGE(TAG, "bind/listen err %d", errno);
        close(ls); vTaskDelete(NULL); return;
    }
    ESP_LOGI(TAG, "Listening :%d  →  %s:%d",
             LOCAL_PORT, DONGLE_LOCAL_IP, DONGLE_LOCAL_PORT);

    while (1) {
        struct sockaddr_in ca; socklen_t cl = sizeof(ca);
        int cs = accept(ls, (struct sockaddr *)&ca, &cl);
        if (cs < 0) { ESP_LOGE(TAG, "accept err %d", errno); continue; }

        conn_arg_t *a = malloc(sizeof(conn_arg_t));
        if (!a) { close(cs); continue; }
        a->client      = cs;
        a->client_port = ntohs(ca.sin_port);
        inet_ntop(AF_INET, &ca.sin_addr, a->client_ip, sizeof(a->client_ip));

        ESP_LOGI(TAG, "ESPHome connecting: %s:%u", a->client_ip, a->client_port);
        xTaskCreate(conn_task, "local_conn", CONN_STACK, a, TASK_PRIO, NULL);
    }
}

void lux_local_server_start(void) {
    xTaskCreatePinnedToCore(server_task, "local_srv",
                            SRV_STACK, NULL, TASK_PRIO, NULL, 0);
    ESP_LOGI(TAG, "Local relay started :%d", LOCAL_PORT);
}