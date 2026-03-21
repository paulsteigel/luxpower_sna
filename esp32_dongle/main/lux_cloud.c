#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "config.h"
#include "lux_proto.h"
#include "shared_state.h"

static const char *TAG = "lux_cloud";

// ── Poll schedule: INPUT banks (5 banks × 40 regs) ───────────
static const uint16_t INPUT_BANKS[]  = {0, 40, 80, 120, 160};
static const uint16_t INPUT_COUNTS[] = {40, 40, 40, 20, 20};
#define INPUT_BANK_COUNT 5

// ── Poll schedule: HOLD banks (6 banks × 40 regs) ────────────
static const uint16_t HOLD_BANKS[]   = {0, 40, 80, 120, 160, 200};
static const uint16_t HOLD_COUNTS[]  = {40, 40, 40, 40, 40, 40};
#define HOLD_BANK_COUNT 6

// ── Receive buffer ────────────────────────────────────────────
#define RECV_BUF_SIZE 1024

typedef struct {
    int      sock;
    uint8_t  recv_buf[RECV_BUF_SIZE];
    size_t   recv_len;
    uint8_t  seq;
} cloud_ctx_t;

// ── Connect to cloud ─────────────────────────────────────────
static int cloud_connect(void) {
    struct addrinfo hints = {
        .ai_family   = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", LUX_CLOUD_PORT);

    if (getaddrinfo(LUX_CLOUD_HOST, port_str, &hints, &res) != 0 || !res) {
        ESP_LOGE(TAG, "DNS lookup failed for %s", LUX_CLOUD_HOST);
        return -1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        freeaddrinfo(res);
        return -1;
    }

    // 10s connect timeout
    struct timeval tv = {.tv_sec = 10, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int ret = connect(sock, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    if (ret != 0) {
        ESP_LOGE(TAG, "connect() failed: %d", errno);
        close(sock);
        return -1;
    }

    // Normal timeout after connect
    tv.tv_sec = 30;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "Connected to %s:%d", LUX_CLOUD_HOST, LUX_CLOUD_PORT);
    return sock;
}

// ── Send bytes ────────────────────────────────────────────────
static bool cloud_send(cloud_ctx_t *ctx, const uint8_t *buf, size_t len) {
    int sent = send(ctx->sock, buf, len, 0);
    if (sent < 0) {
        ESP_LOGW(TAG, "send() failed: %d", errno);
        return false;
    }
    return true;
}

// ── Send heartbeat ────────────────────────────────────────────
static bool cloud_send_heartbeat(cloud_ctx_t *ctx) {
    uint8_t buf[32];
    int len = lux_build_heartbeat(buf);
    ESP_LOGD(TAG, "→ HEARTBEAT");
    return cloud_send(ctx, buf, len);
}

// ── Send READ_INPUT for one bank ─────────────────────────────
static bool cloud_send_read_input(cloud_ctx_t *ctx, uint16_t start, uint16_t count) {
    uint8_t buf[64];
    int len = lux_build_read_input(buf, start, count, ctx->seq++);
    ESP_LOGD(TAG, "→ READ_INPUT start=%u count=%u", start, count);
    return cloud_send(ctx, buf, len);
}

// ── Send READ_HOLD for one bank ───────────────────────────────
static bool cloud_send_read_hold(cloud_ctx_t *ctx, uint16_t start, uint16_t count) {
    uint8_t buf[64];
    int len = lux_build_read_hold(buf, start, count, ctx->seq++);
    ESP_LOGD(TAG, "→ READ_HOLD start=%u count=%u", start, count);
    return cloud_send(ctx, buf, len);
}

// ── Parse register data from cloud response ──────────────────
// Cloud response df format for READ responses:
// [action][fn][inv_sn(10)][start(2)][byte_count(1)][data(N)] + CRC(2)
static void process_read_response(const uint8_t *df, size_t df_len, uint8_t fn) {
    if (df_len < 16) return;
    uint16_t start      = df[12] | ((uint16_t)df[13] << 8);
    uint8_t  byte_count = df[14];
    if (df_len < (size_t)(15 + byte_count + 2)) return;

    const uint8_t *raw = df + 15;
    uint16_t count = byte_count / 2;
    uint16_t regs[128];
    if (count > 128) count = 128;

    for (uint16_t i = 0; i < count; i++) {
        // Data comes big-endian from inverter via cloud
        regs[i] = ((uint16_t)raw[i*2] << 8) | raw[i*2+1];
    }

    if (fn == LUX_FN_READ_INPUT) {
        ESP_LOGD(TAG, "← READ_INPUT resp start=%u count=%u", start, count);
        reg_update_input(start, regs, count);
    } else if (fn == LUX_FN_READ_HOLD) {
        ESP_LOGD(TAG, "← READ_HOLD resp start=%u count=%u", start, count);
        reg_update_hold(start, regs, count);
    }
}

// ── Handle write command from cloud (with filter) ────────────
static void process_cloud_write(cloud_ctx_t *ctx, const lux_parsed_t *p) {
    if (p->type == LUX_PKT_WRITE_SINGLE_REQ) {
        if (!lux_cloud_write_allowed(p->reg)) {
            ESP_LOGW(TAG, "BLOCKED cloud write reg=%u val=%u (not in whitelist)",
                     p->reg, p->value);
            return;
        }
        ESP_LOGI(TAG, "← CLOUD WRITE reg=%u val=%u [ALLOWED]", p->reg, p->value);
        // Forward to write queue → Modbus task executes it
        cmd_queue_write(p->reg, p->value, "cloud");
        // Echo back to cloud (dongle ACK)
        uint8_t buf[64];
        int len = lux_build_write_single(buf, p->reg, p->value, ctx->seq++);
        cloud_send(ctx, buf, len);

    } else if (p->type == LUX_PKT_WRITE_MULTI_REQ) {
        // Battery type write — always reg 0..1
        ESP_LOGI(TAG, "← CLOUD WRITE_MULTI reg0=0x%04X reg1=0x%04X [ALLOWED]",
                 p->reg0, p->reg1);
        cmd_queue_write_multi(p->reg0, p->reg1, "cloud");
        // Echo ACK (dongle response to fn=0x10)
        uint8_t buf[64];
        lux_build_hdr(buf, DIR_DONGLE_RESP, 16, ctx->seq++);
        uint8_t *df = buf + 20;
        df[0] = 0x01; df[1] = LUX_FN_WRITE_MULTI;
        memcpy(df + 2, INVERTER_SN, 10);
        df[12] = 0x00; df[13] = 0x00;
        df[14] = 0x02; df[15] = 0x00;
        uint16_t crc = lux_crc16(df, 16);
        df[16] = crc & 0xFF; df[17] = (crc >> 8) & 0xFF;
        cloud_send(ctx, buf, 38);
    }
}

// ── Process one complete frame from cloud ────────────────────
static void cloud_process_frame(cloud_ctx_t *ctx, const uint8_t *buf, size_t len) {
    lux_parsed_t p = lux_parse(buf, len);

    if (p.type == LUX_PKT_HEARTBEAT) {
        ESP_LOGD(TAG, "← HEARTBEAT from cloud, echoing");
        cloud_send(ctx, buf, len);   // echo back as-is
        return;
    }

    if (!p.crc_ok && p.df_len >= 18) {
        ESP_LOGW(TAG, "CRC mismatch, dropping frame");
        return;
    }

    if (p.type == LUX_PKT_WRITE_SINGLE_REQ || p.type == LUX_PKT_WRITE_MULTI_REQ) {
        process_cloud_write(ctx, &p);
        return;
    }

    if (p.type == LUX_PKT_DATA_RESP || p.type == LUX_PKT_READ_REQ) {
        process_read_response(p.df, p.df_len, p.dev_fn);
        return;
    }
}

// ── Receive and reassemble frames ────────────────────────────
static bool cloud_recv_and_process(cloud_ctx_t *ctx) {
    uint8_t tmp[512];
    int n = recv(ctx->sock, tmp, sizeof(tmp), 0);
    if (n <= 0) {
        if (n == 0)
            ESP_LOGW(TAG, "Connection closed by cloud");
        else if (errno != EAGAIN && errno != EWOULDBLOCK)
            ESP_LOGW(TAG, "recv() error: %d", errno);
        return (errno == EAGAIN || errno == EWOULDBLOCK);
    }

    // Append to reassembly buffer
    if (ctx->recv_len + n > RECV_BUF_SIZE) {
        ESP_LOGW(TAG, "recv buffer overflow, resetting");
        ctx->recv_len = 0;
    }
    memcpy(ctx->recv_buf + ctx->recv_len, tmp, n);
    ctx->recv_len += n;

    // Extract complete frames
    while (ctx->recv_len >= 6) {
        uint8_t *p = ctx->recv_buf;

        // Sync on magic bytes
        if (p[0] != LUX_MAGIC_0 || p[1] != LUX_MAGIC_1) {
            size_t i;
            for (i = 1; i + 1 < ctx->recv_len; i++) {
                if (p[i] == LUX_MAGIC_0 && p[i+1] == LUX_MAGIC_1) break;
            }
            memmove(ctx->recv_buf, ctx->recv_buf + i, ctx->recv_len - i);
            ctx->recv_len -= i;
            continue;
        }

        uint16_t frame_len = p[4] | ((uint16_t)p[5] << 8);
        size_t total = frame_len + 6;

        if (total > RECV_BUF_SIZE) {
            ESP_LOGW(TAG, "Frame too large (%u), resetting", (unsigned)total);
            ctx->recv_len = 0;
            break;
        }
        if (ctx->recv_len < total) break;

        cloud_process_frame(ctx, p, total);
        memmove(ctx->recv_buf, ctx->recv_buf + total, ctx->recv_len - total);
        ctx->recv_len -= total;
    }
    return true;
}

// ── Send pending write commands from queue ───────────────────
static void cloud_flush_write_queue(cloud_ctx_t *ctx) {
    write_cmd_t cmd;
    while (xQueueReceive(g_write_queue, &cmd, 0) == pdTRUE) {
        uint8_t buf[64];
        int len = 0;
        if (cmd.type == CMD_WRITE_SINGLE) {
            ESP_LOGI(TAG, "→ WRITE reg=%u val=%u [src=%s]",
                     cmd.reg, cmd.value, cmd.source);
            len = lux_build_write_single(buf, cmd.reg, cmd.value, ctx->seq++);
        } else if (cmd.type == CMD_WRITE_MULTI) {
            ESP_LOGI(TAG, "→ WRITE_MULTI reg0=0x%04X [src=%s]",
                     cmd.reg0, cmd.source);
            len = lux_build_write_multi(buf, cmd.reg0, cmd.reg1, ctx->seq++);
        } else if (cmd.type == CMD_SET_LITHIUM) {
            ESP_LOGI(TAG, "→ SET_LITHIUM brand6 [src=%s]", cmd.source);
            len = lux_build_write_multi(buf, 0x801A, 0x0100, ctx->seq++);
        } else if (cmd.type == CMD_SET_LEADACID) {
            ESP_LOGI(TAG, "→ SET_LEADACID brand6 [src=%s]", cmd.source);
            len = lux_build_write_multi(buf, 0x8019, 0x0100, ctx->seq++);
        }
        if (len > 0) cloud_send(ctx, buf, len);
        vTaskDelay(pdMS_TO_TICKS(50));   // inter-command gap
    }
}

// ── Main cloud task ───────────────────────────────────────────
void lux_cloud_task(void *arg) {
    ESP_LOGI(TAG, "Cloud task started on core %d", xPortGetCoreID());

    cloud_ctx_t ctx = {};
    ctx.sock = -1;

    uint32_t last_heartbeat = 0;
    uint32_t last_input_poll = 0;
    uint32_t last_hold_poll  = 0;
    uint8_t  input_bank = 0;
    uint8_t  hold_bank  = 0;
    bool     initial_hold_done = false;

    while (1) {
        // ── Connect ──────────────────────────────────────────
        if (ctx.sock < 0) {
            ESP_LOGI(TAG, "Connecting to cloud...");
            ctx.sock = cloud_connect();
            if (ctx.sock < 0) {
                vTaskDelay(pdMS_TO_TICKS(CLOUD_RECONNECT_MS));
                continue;
            }
            ctx.recv_len = 0;
            ctx.seq = 1;
            last_heartbeat   = xTaskGetTickCount() * portTICK_PERIOD_MS;
            last_input_poll  = 0;
            last_hold_poll   = 0;
            input_bank       = 0;
            hold_bank        = 0;
            initial_hold_done = false;

            // Register with heartbeat immediately
            if (!cloud_send_heartbeat(&ctx)) {
                close(ctx.sock); ctx.sock = -1;
                continue;
            }
        }

        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // ── Receive pending data ──────────────────────────────
        if (!cloud_recv_and_process(&ctx)) {
            ESP_LOGW(TAG, "Connection lost, reconnecting...");
            close(ctx.sock); ctx.sock = -1;
            continue;
        }

        // ── Flush pending write commands ──────────────────────
        cloud_flush_write_queue(&ctx);

        // ── Heartbeat ─────────────────────────────────────────
        if (now - last_heartbeat >= HEARTBEAT_INTERVAL_MS) {
            last_heartbeat = now;
            if (!cloud_send_heartbeat(&ctx)) {
                close(ctx.sock); ctx.sock = -1;
                continue;
            }
        }

        // ── Initial HOLD poll (once on connect) ───────────────
        if (!initial_hold_done) {
            if (cloud_send_read_hold(&ctx,
                                     HOLD_BANKS[hold_bank],
                                     HOLD_COUNTS[hold_bank])) {
                hold_bank++;
                if (hold_bank >= HOLD_BANK_COUNT) {
                    hold_bank = 0;
                    initial_hold_done = true;
                    last_hold_poll = now;
                    ESP_LOGI(TAG, "Initial HOLD poll complete");
                }
            } else {
                close(ctx.sock); ctx.sock = -1;
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // ── Periodic INPUT poll ───────────────────────────────
        if (now - last_input_poll >= POLL_INPUT_MS) {
            for (int i = 0; i < INPUT_BANK_COUNT; i++) {
                if (!cloud_send_read_input(&ctx, INPUT_BANKS[i], INPUT_COUNTS[i])) {
                    close(ctx.sock); ctx.sock = -1;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(150));
                cloud_recv_and_process(&ctx);
            }
            last_input_poll = now;
        }

        // ── Periodic HOLD poll ────────────────────────────────
        if (now - last_hold_poll >= POLL_HOLD_MS) {
            for (int i = 0; i < HOLD_BANK_COUNT; i++) {
                if (!cloud_send_read_hold(&ctx, HOLD_BANKS[i], HOLD_COUNTS[i])) {
                    close(ctx.sock); ctx.sock = -1;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(150));
                cloud_recv_and_process(&ctx);
            }
            last_hold_poll = now;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
