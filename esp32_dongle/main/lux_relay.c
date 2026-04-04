// lux_relay.c
// Transparent TCP relay: Real Dongle → ESP32:4346 → Cloud
//
// Data flows: Dongle sends RESP frames (fn=03/04) → ESP32 parses → shared_state
// → lux_mqtt_task picks up & publishes to HA automatically

#include "lux_relay.h"
#include "lux_proto.h"
#include "shared_state.h"
#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <errno.h>

static const char *TAG = "lux_relay";

#define RELAY_LISTEN_PORT   LUX_CLOUD_PORT
#define CLOUD_HOST          LUX_CLOUD_HOST
#define CLOUD_PORT          LUX_CLOUD_PORT
#define BUF_SIZE            1024
#define RELAY_CONN_STACK    5120
#define RELAY_SRV_STACK     3072
#define RELAY_TASK_PRIO     5

// ── Hex dump (debug) ──────────────────────────────────────────
static void hex_dump(const char *label, const uint8_t *buf, int len) {
    char line[200];
    int n   = len > 32 ? 32 : len;
    int pos = 0;
    for (int i = 0; i < n; i++)
        pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", buf[i]);
    if (len > 32)
        snprintf(line + pos, sizeof(line) - pos, "...[+%d]", len - 32);
    ESP_LOGI(TAG, "[%s] %d B: %s", label, len, line);
}

// ── Parse dongle RESP frame → shared_state ────────────────────
// Dongle sends fn=03 (HOLD RESP) and fn=04 (INPUT RESP) with register data.
// Cloud only sends REQ frames — no data there.
static void relay_process_dongle_frame(const uint8_t *buf, size_t total) {
    lux_parsed_t p = lux_parse(buf, total);

    if (p.type == LUX_PKT_HEARTBEAT) return;
    if (!p.crc_ok) {
        ESP_LOGW(TAG, "Bad CRC — dropping frame");
        return;
    }
    const uint8_t *df = p.df;
    size_t df_len     = p.df_len;
    if (!df || df_len < 16) return;

    // Only fn=03 (HOLD) and fn=04 (INPUT) carry register data
    if (p.dev_fn != LUX_FN_READ_INPUT && p.dev_fn != LUX_FN_READ_HOLD)
        return;

    // df layout:
    //  [0]      action
    //  [1]      fn (03=HOLD / 04=INPUT)
    //  [2..11]  inverter SN (10 bytes)
    //  [12..13] start_reg LE
    //  [14]     byte_count
    //  [15..]   data (LE uint16 pairs)
    //  [-2..-1] CRC
    uint16_t start      = df[12] | ((uint16_t)df[13] << 8);
    uint8_t  byte_count = df[14];
    if (df_len < (size_t)(15 + byte_count + 2)) return;

    uint16_t count = byte_count / 2;
    if (count == 0 || count > 128) return;

    uint16_t regs[128];
    const uint8_t *raw = df + 15;
    for (uint16_t i = 0; i < count; i++)
        regs[i] = (uint16_t)raw[i*2] | ((uint16_t)raw[i*2+1] << 8);  // LE

    if (p.dev_fn == LUX_FN_READ_INPUT) {
        ESP_LOGI(TAG, "← INPUT start=%u count=%u", start, count);
        reg_update_input(start, regs, count);
    } else if (p.dev_fn == LUX_FN_READ_HOLD) {
        ESP_LOGI(TAG, "← HOLD  start=%u count=%u", start, count);
        reg_update_hold(start, regs, count);
    }
}

// ── Frame callbacks ───────────────────────────────────────────
// Dongle → Server: RESP frames with actual register data → parse!
static void on_dongle_frame(const uint8_t *buf, size_t len) {
    hex_dump("D→S", buf, (int)len);
    relay_process_dongle_frame(buf, len);   // ← data is HERE
}

// Server → Dongle: REQ frames only — no register data, skip
static void on_cloud_frame(const uint8_t *buf, size_t len) {
    hex_dump("S→D", buf, (int)len);
}

// ── Reassemble TCP stream into complete frames ────────────────
typedef struct {
    uint8_t buf[BUF_SIZE];
    size_t  len;
} frame_buf_t;

static void frame_buf_push(frame_buf_t *fb, const uint8_t *data, int n,
                            void (*on_frame)(const uint8_t *, size_t)) {
    if (fb->len + n > BUF_SIZE) { fb->len = 0; }
    memcpy(fb->buf + fb->len, data, n);
    fb->len += n;

    while (fb->len >= 6) {
        uint8_t *p = fb->buf;

        // Resync to magic bytes
        if (p[0] != LUX_MAGIC_0 || p[1] != LUX_MAGIC_1) {
            size_t i;
            for (i = 1; i + 1 < fb->len; i++)
                if (p[i] == LUX_MAGIC_0 && p[i+1] == LUX_MAGIC_1) break;
            memmove(fb->buf, fb->buf + i, fb->len - i);
            fb->len -= i;
            continue;
        }

        uint16_t fl    = p[4] | ((uint16_t)p[5] << 8);
        size_t   total = (size_t)fl + 6;
        if (total > BUF_SIZE) { fb->len = 0; break; }
        if (fb->len < total)  break;

        on_frame(p, total);
        memmove(fb->buf, fb->buf + total, fb->len - total);
        fb->len -= total;
    }
}

// ── Per-connection relay task ──────────────────────────────────
typedef struct { int dongle_sock; } relay_conn_arg_t;

static void relay_conn_task(void *arg) {
    relay_conn_arg_t *ctx = (relay_conn_arg_t *)arg;
    int ds = ctx->dongle_sock;
    free(ctx);

    // Connect upstream to real cloud
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(CLOUD_PORT),
    };
    inet_pton(AF_INET, CLOUD_HOST, &addr.sin_addr);
    int cs = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (cs < 0 || connect(cs, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "Cannot reach cloud (%d) — dropping dongle", errno);
        if (cs >= 0) close(cs);
        close(ds);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Relay: dongle fd=%d ↔ cloud fd=%d", ds, cs);

    frame_buf_t fb_d2s = {}, fb_s2d = {};
    uint8_t     tmp[512];
    int         maxfd = (ds > cs ? ds : cs) + 1;

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(ds, &rfds);
        FD_SET(cs, &rfds);
        struct timeval tv = { .tv_sec = 90 };

        int r = select(maxfd, &rfds, NULL, NULL, &tv);
        if (r < 0)  { ESP_LOGE(TAG, "select err %d", errno); break; }
        if (r == 0) { ESP_LOGW(TAG, "Relay idle timeout"); break; }

        // Dongle → Cloud (RESP frames — data lives here!)
        if (FD_ISSET(ds, &rfds)) {
            int n = recv(ds, tmp, sizeof(tmp), 0);
            if (n <= 0) { ESP_LOGI(TAG, "Dongle disconnected"); break; }
            frame_buf_push(&fb_d2s, tmp, n, on_dongle_frame);  // ← parse!
            if (send(cs, tmp, n, 0) < 0) { ESP_LOGE(TAG, "→cloud err"); break; }
        }

        // Cloud → Dongle (REQ frames only — no data)
        if (FD_ISSET(cs, &rfds)) {
            int n = recv(cs, tmp, sizeof(tmp), 0);
            if (n <= 0) { ESP_LOGI(TAG, "Cloud disconnected"); break; }
            frame_buf_push(&fb_s2d, tmp, n, on_cloud_frame);
            if (send(ds, tmp, n, 0) < 0) { ESP_LOGE(TAG, "→dongle err"); break; }
        }
    }

    close(ds);
    close(cs);
    ESP_LOGI(TAG, "Relay connection closed");
    vTaskDelete(NULL);
}

// ── Accept loop ────────────────────────────────────────────────
static void relay_server_task(void *arg) {
    int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls < 0) { ESP_LOGE(TAG, "socket: %d", errno); vTaskDelete(NULL); return; }

    int opt = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in ba = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(RELAY_LISTEN_PORT),
    };
    if (bind(ls, (struct sockaddr *)&ba, sizeof(ba)) ||
        listen(ls, 2)) {
        ESP_LOGE(TAG, "bind/listen: %d", errno);
        close(ls); vTaskDelete(NULL); return;
    }
    ESP_LOGI(TAG, "Relay listening :%d → %s:%d",
             RELAY_LISTEN_PORT, CLOUD_HOST, CLOUD_PORT);

    // Set accept timeout so we can log "still waiting" periodically
    struct timeval tv = { .tv_sec = 15 };
    setsockopt(ls, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint32_t wait_count = 0;
    while (1) {
        struct sockaddr_in ca; socklen_t cl = sizeof(ca);
        int cs = accept(ls, (struct sockaddr *)&ca, &cl);

        if (cs < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout — log heartbeat every 15s
                ESP_LOGI(TAG, "Waiting for dongle... (%lus)  point dongle → 192.168.100.21:4346",
                         (unsigned long)(++wait_count * 15));
            } else {
                ESP_LOGE(TAG, "accept err %d", errno);
            }
            continue;
        }

        char ip[16];
        inet_ntop(AF_INET, &ca.sin_addr, ip, sizeof(ip));
        ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        ESP_LOGI(TAG, "Dongle connected from %s:%d  fd=%d",
                 ip, ntohs(ca.sin_port), cs);

        relay_conn_arg_t *a = malloc(sizeof(relay_conn_arg_t));
        if (!a) { close(cs); continue; }
        a->dongle_sock = cs;
        xTaskCreate(relay_conn_task, "relay_conn",
                    RELAY_CONN_STACK, a, RELAY_TASK_PRIO, NULL);
    }
}

// ── Public API ─────────────────────────────────────────────────
void lux_relay_start(void) {
    xTaskCreatePinnedToCore(relay_server_task, "relay_srv",
                            RELAY_SRV_STACK, NULL,
                            RELAY_TASK_PRIO, NULL, 0);
    ESP_LOGI(TAG, "Relay task started — point real dongle here");
}