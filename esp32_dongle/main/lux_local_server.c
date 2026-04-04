// lux_local_server.c
// TCP :8000 — LuxApp local connection
// ESP32 acts as dongle local server: pushes INPUT/HOLD, accepts WRITE cmds.
// Same A1 1A protocol as cloud, same shared_state as relay.

#include "lux_local_server.h"
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

static const char *TAG = "lux_local";

#define LOCAL_PORT          8000
#define LOCAL_CONN_STACK    5120
#define LOCAL_SRV_STACK     3072
#define LOCAL_TASK_PRIO     4

// Push INPUT registers as a RESP frame to the app
// Mirrors what the real dongle does: push all input banks
static bool push_input(int sock, uint8_t seq) {
    // Banks: 0..39, 40..79, 80..119, 120..159, 160..199
    static const uint16_t banks[]  = {0,  40,  80, 120, 160};
    static const uint16_t counts[] = {40, 40,  40,  40,  40};

    for (int b = 0; b < 5; b++) {
        uint16_t start = banks[b];
        uint16_t count = counts[b];
        if (start >= INPUT_REG_COUNT) break;

        // Build RESP frame (same layout dongle uses)
        // Header (20) + df: [action][fn][inv_sn(10)][start(2)][bc(1)][data(count*2)][crc(2)]
        uint8_t  byte_count = count * 2;
        uint16_t df_len     = 1 + 1 + 10 + 2 + 1 + byte_count + 2;  // 17 + bc
        uint16_t total      = 20 + df_len;
        uint8_t  buf[512]   = {};

        lux_build_hdr(buf, DIR_DONGLE_RESP, df_len, seq++);
        uint8_t *df = buf + 20;
        df[0] = 0x01;
        df[1] = LUX_FN_READ_INPUT;
        memcpy(df + 2, INVERTER_SN, 10);
        df[12] = start & 0xFF;
        df[13] = (start >> 8) & 0xFF;
        df[14] = byte_count;

        // Fill register data (LE)
        uint8_t *data = df + 15;
        for (uint16_t i = 0; i < count; i++) {
            uint16_t v = reg_get_input(start + i);
            data[i*2]   = v & 0xFF;
            data[i*2+1] = (v >> 8) & 0xFF;
        }

        uint16_t crc = lux_crc16(df, df_len - 2);
        df[df_len - 2] = crc & 0xFF;
        df[df_len - 1] = (crc >> 8) & 0xFF;

        if (send(sock, buf, total, 0) < 0) {
            ESP_LOGW(TAG, "send INPUT bank %u failed: %d", start, errno);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return true;
}

// Push HOLD registers as RESP frames
static bool push_hold(int sock, uint8_t seq) {
    static const uint16_t banks[]  = {0,  40,  80, 120, 160, 200};
    static const uint16_t counts[] = {40, 40,  40,  40,  40,  40};

    for (int b = 0; b < 6; b++) {
        uint16_t start = banks[b];
        uint16_t count = counts[b];
        if (start >= HOLD_REG_COUNT) break;

        uint8_t  byte_count = count * 2;
        uint16_t df_len     = 1 + 1 + 10 + 2 + 1 + byte_count + 2;
        uint16_t total      = 20 + df_len;
        uint8_t  buf[512]   = {};

        lux_build_hdr(buf, DIR_DONGLE_RESP, df_len, seq++);
        uint8_t *df = buf + 20;
        df[0] = 0x01;
        df[1] = LUX_FN_READ_HOLD;
        memcpy(df + 2, INVERTER_SN, 10);
        df[12] = start & 0xFF;
        df[13] = (start >> 8) & 0xFF;
        df[14] = byte_count;

        uint8_t *data = df + 15;
        for (uint16_t i = 0; i < count; i++) {
            uint16_t v = reg_get_hold(start + i);
            data[i*2]   = v & 0xFF;
            data[i*2+1] = (v >> 8) & 0xFF;
        }

        uint16_t crc = lux_crc16(df, df_len - 2);
        df[df_len - 2] = crc & 0xFF;
        df[df_len - 1] = (crc >> 8) & 0xFF;

        if (send(sock, buf, total, 0) < 0) {
            ESP_LOGW(TAG, "send HOLD bank %u failed: %d", start, errno);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return true;
}

// Handle one complete frame from app (WRITE commands)
static void handle_app_frame(const uint8_t *buf, size_t len, int sock) {
    lux_parsed_t p = lux_parse(buf, len);

    // ── Heartbeat: echo back ──────────────────────────────────
    if (p.type == LUX_PKT_HEARTBEAT) {
        send(sock, buf, len, 0);
        return;
    }

    // ── READ request: respond from shared_state ───────────────
    // ESPHome sends fn=0x04 (INPUT) or fn=0x03 (HOLD) requests
    // and waits for a response — must reply or it times out.
    if (p.type == LUX_PKT_READ_REQ) {
        uint16_t start = p.reg;
        uint16_t count = p.value;   // count is in p.value for READ_REQ

        // Clamp to safe range
        bool is_input = (p.dev_fn == LUX_FN_READ_INPUT);
        uint16_t max  = is_input ? INPUT_REG_COUNT : HOLD_REG_COUNT;
        if (start >= max) count = 0;
        if (start + count > max) count = max - start;
        if (count > 125) count = 125;

        uint8_t  byte_count = count * 2;
        uint16_t df_len     = 1 + 1 + 10 + 2 + 1 + byte_count + 2;
        uint16_t total      = 20 + df_len;
        uint8_t  resp[512]  = {};

        // Use same seq as request so ESPHome can match it
        uint8_t seq = (len >= 7) ? buf[6] : 1;
        lux_build_hdr(resp, DIR_DONGLE_RESP, df_len, seq);

        uint8_t *df = resp + 20;
        df[0] = 0x01;
        df[1] = p.dev_fn;           // echo same fn (0x03 or 0x04)
        memcpy(df + 2, INVERTER_SN, 10);
        df[12] = start & 0xFF;
        df[13] = (start >> 8) & 0xFF;
        df[14] = byte_count;

        // Fill register data from shared_state (LE, same as wire format)
        uint8_t *data = df + 15;
        for (uint16_t i = 0; i < count; i++) {
            uint16_t v = is_input ? reg_get_input(start + i)
                                  : reg_get_hold(start + i);
            data[i*2]   = v & 0xFF;
            data[i*2+1] = (v >> 8) & 0xFF;
        }

        uint16_t crc = lux_crc16(df, df_len - 2);
        df[df_len - 2] = crc & 0xFF;
        df[df_len - 1] = (crc >> 8) & 0xFF;

        send(sock, resp, total, 0);
        return;
    }

    // ── WRITE single ──────────────────────────────────────────
    if (p.type == LUX_PKT_WRITE_SINGLE_REQ) {
        if (!lux_cloud_write_allowed(p.reg)) {
            ESP_LOGW(TAG, "Local WRITE blocked reg=%u", p.reg);
            return;
        }
        ESP_LOGI(TAG, "Local WRITE reg=%u val=%u", p.reg, p.value);
        cmd_queue_write(p.reg, p.value, "local");
    }

    // ── WRITE multi ───────────────────────────────────────────
    else if (p.type == LUX_PKT_WRITE_MULTI_REQ) {
        ESP_LOGI(TAG, "Local WRITE_MULTI reg0=0x%04X reg1=0x%04X",
                 p.reg0, p.reg1);
        cmd_queue_write_multi(p.reg0, p.reg1, "local");
    }
}
// ── Per-connection task ───────────────────────────────────────
typedef struct { int sock; } local_conn_arg_t;

static void local_conn_task(void *arg) {
    local_conn_arg_t *ctx = (local_conn_arg_t *)arg;
    int sock = ctx->sock;
    free(ctx);

    ESP_LOGI(TAG, "App connected fd=%d", sock);

    // Push initial snapshot immediately
    uint8_t seq = 1;
    if (g_regs.input_valid) push_input(sock, seq);
    seq += 10;
    if (g_regs.hold_valid)  push_hold(sock, seq);
    seq += 20;

    // Reassembly buffer
    uint8_t  rbuf[1024];
    size_t   rlen = 0;

    uint32_t last_input_push = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t last_hold_push  = xTaskGetTickCount() * portTICK_PERIOD_MS;

    struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };  // 200ms
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (1) {
        // ── Receive from app ──────────────────────────────────
        uint8_t tmp[256];
        int n = recv(sock, tmp, sizeof(tmp), 0);
        if (n > 0) {
            if (rlen + n <= sizeof(rbuf)) {
                memcpy(rbuf + rlen, tmp, n);
                rlen += n;
            } else {
                rlen = 0;  // overflow reset
            }

            // Parse complete frames
            while (rlen >= 6) {
                if (rbuf[0] != LUX_MAGIC_0 || rbuf[1] != LUX_MAGIC_1) {
                    size_t i;
                    for (i = 1; i + 1 < rlen; i++)
                        if (rbuf[i] == LUX_MAGIC_0 && rbuf[i+1] == LUX_MAGIC_1) break;
                    memmove(rbuf, rbuf + i, rlen - i);
                    rlen -= i;
                    continue;
                }
                uint16_t fl    = rbuf[4] | ((uint16_t)rbuf[5] << 8);
                size_t   total = (size_t)fl + 6;
                if (total > sizeof(rbuf)) { rlen = 0; break; }
                if (rlen < total) break;

                handle_app_frame(rbuf, total, sock);
                memmove(rbuf, rbuf + total, rlen - total);
                rlen -= total;
            }
        } else if (n == 0) {
            ESP_LOGI(TAG, "App disconnected");
            break;
        }
        // n < 0 → EAGAIN (timeout) → continue to push

        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // ── Push INPUT every 18s ──────────────────────────────
        if (now - last_input_push >= 18000 && g_regs.input_valid) {
            if (!push_input(sock, seq)) break;
            seq += 10;
            last_input_push = now;
        }

        // ── Push HOLD every 60s ───────────────────────────────
        if (now - last_hold_push >= 60000 && g_regs.hold_valid) {
            if (!push_hold(sock, seq)) break;
            seq += 20;
            last_hold_push = now;
        }
    }

    close(sock);
    ESP_LOGI(TAG, "Local connection closed fd=%d", sock);
    vTaskDelete(NULL);
}

// ── Accept loop ───────────────────────────────────────────────
static void local_server_task(void *arg) {
    int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls < 0) { ESP_LOGE(TAG, "socket: %d", errno); vTaskDelete(NULL); return; }

    int opt = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in ba = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(LOCAL_PORT),
    };
    if (bind(ls, (struct sockaddr *)&ba, sizeof(ba)) ||
        listen(ls, 4)) {
        ESP_LOGE(TAG, "bind/listen: %d", errno);
        close(ls); vTaskDelete(NULL); return;
    }
    ESP_LOGI(TAG, "Local server listening on :%d", LOCAL_PORT);

    while (1) {
        struct sockaddr_in ca; socklen_t cl = sizeof(ca);
        int cs = accept(ls, (struct sockaddr *)&ca, &cl);
        if (cs < 0) { ESP_LOGE(TAG, "accept: %d", errno); continue; }

        char ip[16];
        inet_ntop(AF_INET, &ca.sin_addr, ip, sizeof(ip));
        ESP_LOGI(TAG, "App connected from %s:%d", ip, ntohs(ca.sin_port));

        local_conn_arg_t *a = malloc(sizeof(local_conn_arg_t));
        if (!a) { close(cs); continue; }
        a->sock = cs;
        xTaskCreate(local_conn_task, "local_conn",
                    LOCAL_CONN_STACK, a, LOCAL_TASK_PRIO, NULL);
    }
}

// ── Public API ────────────────────────────────────────────────
void lux_local_server_start(void) {
    xTaskCreatePinnedToCore(local_server_task, "local_srv",
                            LOCAL_SRV_STACK, NULL,
                            LOCAL_TASK_PRIO, NULL, 0);
    ESP_LOGI(TAG, "Local server started on :%d", LOCAL_PORT);
}