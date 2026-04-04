#include "lux_rs485.h"
#include "config.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "rs485";

// ── RX buffer size ────────────────────────────────────────────
// Largest possible response: fn=0x03/0x04 with 125 regs
// = addr(1)+fn(1)+bc(1)+data(250)+crc(2) = 255 bytes
#define RX_BUF 300

// Inter-character timeout at 19200 baud:
//   1 char ≈ 0.52 ms → 3.5-char gap ≈ 1.8 ms → use 5 ms to be safe
#define INTERCHAR_MS 5

// Maximum total wait for a response
#define RESP_TIMEOUT_MS 350

static SemaphoreHandle_t s_mutex = NULL;

// ── CRC-16 Modbus (poly 0xA001, init 0xFFFF) ──────────────────
static uint16_t mb_crc(const uint8_t *d, size_t n) {
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= *d++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (crc & 1 ? 0xA001 : 0);
    }
    return crc;
}

// ── Determine expected response length from partially-received data ──
// Returns 0 if we can't tell yet (need more bytes).
// On exception (fn | 0x80): 5 bytes fixed.
// fn=0x03/0x04: need byte[2]=byte_count → total = 3 + bc + 2.
// fn=0x06/0x10: echo / standard ACK → 8 bytes fixed.
static size_t expected_len(const uint8_t *buf, size_t have) {
    if (have < 2) return 0;
    uint8_t fn = buf[1];
    if (fn & 0x80) return 5;                      // exception
    if (fn == 0x03 || fn == 0x04) {
        if (have < 3) return 0;
        return (size_t)3 + buf[2] + 2;
    }
    // fn=0x06, fn=0x10: 8
    return 8;
}

// ── Low-level send + receive ───────────────────────────────────
static rs485_err_t mb_transact(const uint8_t *req, size_t req_len,
                                uint8_t *resp, size_t *out_len) {
    // Flush stale RX bytes (e.g. from a previous timeout)
    uart_flush_input(MODBUS_UART_NUM);

    // Transmit
    int sent = uart_write_bytes(MODBUS_UART_NUM, req, req_len);
    if (sent != (int)req_len) {
        ESP_LOGE(TAG, "TX short: %d/%d", sent, (int)req_len);
        return RS485_ERR_FRAME;
    }
    // Wait until TX FIFO is drained (half-duplex: must finish TX before RX)
    if (uart_wait_tx_done(MODBUS_UART_NUM, pdMS_TO_TICKS(100)) != ESP_OK) {
        ESP_LOGE(TAG, "TX drain timeout");
        return RS485_ERR_TIMEOUT;
    }

    // Receive loop
    uint8_t buf[RX_BUF];
    size_t  total    = 0;
    TickType_t end   = xTaskGetTickCount() + pdMS_TO_TICKS(RESP_TIMEOUT_MS);

    while (xTaskGetTickCount() < end && total < sizeof(buf)) {
        TickType_t left = end - xTaskGetTickCount();
        TickType_t wait = left < pdMS_TO_TICKS(INTERCHAR_MS)
                          ? left : pdMS_TO_TICKS(INTERCHAR_MS);

        int n = uart_read_bytes(MODBUS_UART_NUM,
                                buf + total, sizeof(buf) - total, wait);
        if (n > 0) total += n;

        // Early exit once we have a complete frame
        size_t exp = expected_len(buf, total);
        if (exp && total >= exp) break;
    }

    if (total == 0) {
        ESP_LOGW(TAG, "No response");
        return RS485_ERR_TIMEOUT;
    }

    // CRC check
    size_t exp = expected_len(buf, total);
    if (!exp || total < exp) {
        ESP_LOGW(TAG, "Short frame: got %u expected %u", (unsigned)total, (unsigned)exp);
        return RS485_ERR_FRAME;
    }
    uint16_t crc_calc = mb_crc(buf, exp - 2);
    uint16_t crc_recv = buf[exp-2] | ((uint16_t)buf[exp-1] << 8);
    if (crc_calc != crc_recv) {
        ESP_LOGW(TAG, "CRC err: calc=0x%04X recv=0x%04X", crc_calc, crc_recv);
        return RS485_ERR_CRC;
    }

    // Modbus exception?
    if (buf[1] & 0x80) {
        ESP_LOGW(TAG, "Exception fn=0x%02X code=0x%02X", buf[1] & 0x7F, buf[2]);
        return RS485_ERR_EXCEPTION;
    }

    memcpy(resp, buf, exp);
    *out_len = exp;
    return RS485_OK;
}

// ── Build fn=0x03/0x04 request ────────────────────────────────
static size_t build_read_req(uint8_t *buf, uint8_t fn,
                              uint16_t start, uint16_t count) {
    buf[0] = MODBUS_SLAVE_ADDR;
    buf[1] = fn;
    buf[2] = (start >> 8) & 0xFF;  buf[3] = start & 0xFF;
    buf[4] = (count >> 8) & 0xFF;  buf[5] = count & 0xFF;
    uint16_t crc = mb_crc(buf, 6);
    buf[6] = crc & 0xFF;  buf[7] = (crc >> 8) & 0xFF;
    return 8;
}

// Parse fn=0x03/0x04 response → out[] in host uint16_t (BE→LE)
static rs485_err_t parse_read_resp(const uint8_t *resp, size_t len,
                                    uint16_t *out, uint16_t count) {
    uint8_t bc = resp[2];
    if (bc != count * 2) {
        ESP_LOGW(TAG, "bc mismatch: got %u want %u", bc, count * 2);
        return RS485_ERR_FRAME;
    }
    const uint8_t *d = resp + 3;
    for (uint16_t i = 0; i < count; i++)
        out[i] = ((uint16_t)d[i*2] << 8) | d[i*2+1];  // BE → host LE
    return RS485_OK;
}

// ── Public: init ──────────────────────────────────────────────
void lux_rs485_init(void) {
    uart_config_t cfg = {
        .baud_rate           = RS485_BAUD,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk          = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_param_config(MODBUS_UART_NUM, &cfg));

    // RS485_DE_RE_PIN wired to RTS — driver toggles it automatically
    ESP_ERROR_CHECK(uart_set_pin(MODBUS_UART_NUM,
                                 RS485_TX_PIN, RS485_RX_PIN,
                                 RS485_DE_RE_PIN,    // → RTS
                                 UART_PIN_NO_CHANGE));

    ESP_ERROR_CHECK(uart_driver_install(MODBUS_UART_NUM,
                                        RX_BUF * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_set_mode(MODBUS_UART_NUM,
                                  UART_MODE_RS485_HALF_DUPLEX));

    s_mutex = xSemaphoreCreateMutex();
    configASSERT(s_mutex);

    ESP_LOGI(TAG, "RS485 ready: UART%d TX=%d RX=%d DE=%d @%dbaud slave=%d",
             MODBUS_UART_NUM, RS485_TX_PIN, RS485_RX_PIN,
             RS485_DE_RE_PIN, RS485_BAUD, MODBUS_SLAVE_ADDR);
}

// ── Public: read input (fn=0x04) ──────────────────────────────
rs485_err_t lux_rs485_read_input(uint16_t start, uint16_t count, uint16_t *out) {
    if (!s_mutex || !out || count == 0 || count > 125) return RS485_ERR_FRAME;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    uint8_t req[8];
    size_t  req_len = build_read_req(req, 0x04, start, count);
    uint8_t resp[RX_BUF];
    size_t  resp_len = 0;

    rs485_err_t err = mb_transact(req, req_len, resp, &resp_len);
    if (err == RS485_OK)
        err = parse_read_resp(resp, resp_len, out, count);
    if (err != RS485_OK)
        ESP_LOGW(TAG, "read_input [%u+%u] %s", start, count, rs485_err_str(err));

    xSemaphoreGive(s_mutex);
    return err;
}

// ── Public: read hold (fn=0x03) ───────────────────────────────
rs485_err_t lux_rs485_read_hold(uint16_t start, uint16_t count, uint16_t *out) {
    if (!s_mutex || !out || count == 0 || count > 125) return RS485_ERR_FRAME;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    uint8_t req[8];
    size_t  req_len = build_read_req(req, 0x03, start, count);
    uint8_t resp[RX_BUF];
    size_t  resp_len = 0;

    rs485_err_t err = mb_transact(req, req_len, resp, &resp_len);
    if (err == RS485_OK)
        err = parse_read_resp(resp, resp_len, out, count);
    if (err != RS485_OK)
        ESP_LOGW(TAG, "read_hold [%u+%u] %s", start, count, rs485_err_str(err));

    xSemaphoreGive(s_mutex);
    return err;
}

// ── Public: write single (fn=0x06) ───────────────────────────
rs485_err_t lux_rs485_write_single(uint16_t reg, uint16_t value) {
    if (!s_mutex) return RS485_ERR_FRAME;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    uint8_t req[8];
    req[0] = MODBUS_SLAVE_ADDR;  req[1] = 0x06;
    req[2] = (reg   >> 8) & 0xFF;  req[3] = reg   & 0xFF;
    req[4] = (value >> 8) & 0xFF;  req[5] = value & 0xFF;
    uint16_t crc = mb_crc(req, 6);
    req[6] = crc & 0xFF;  req[7] = (crc >> 8) & 0xFF;

    uint8_t resp[RX_BUF];
    size_t  resp_len = 0;
    rs485_err_t err = mb_transact(req, 8, resp, &resp_len);

    if (err == RS485_OK) {
        // Inverter echoes the request verbatim
        if (resp_len >= 6 && memcmp(req, resp, 6) != 0)
            ESP_LOGW(TAG, "write_single: echo mismatch reg=%u val=%u", reg, value);
        else
            ESP_LOGI(TAG, "write_single reg=%u val=%u OK", reg, value);
    } else {
        ESP_LOGW(TAG, "write_single reg=%u val=%u %s", reg, value, rs485_err_str(err));
    }

    xSemaphoreGive(s_mutex);
    return err;
}

// ── Public: write multi (fn=0x10) ────────────────────────────
rs485_err_t lux_rs485_write_multi(uint16_t start,
                                   const uint16_t *values, uint16_t count) {
    if (!s_mutex || !values || count == 0 || count > 123) return RS485_ERR_FRAME;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // addr(1)+fn(1)+start(2)+count(2)+bc(1)+data(count×2)+crc(2)
    uint8_t req[256];
    req[0] = MODBUS_SLAVE_ADDR;  req[1] = 0x10;
    req[2] = (start >> 8) & 0xFF;  req[3] = start & 0xFF;
    req[4] = (count >> 8) & 0xFF;  req[5] = count & 0xFF;
    req[6] = (uint8_t)(count * 2);
    for (uint16_t i = 0; i < count; i++) {
        req[7 + i*2]     = (values[i] >> 8) & 0xFF;  // BE on wire
        req[7 + i*2 + 1] =  values[i] & 0xFF;
    }
    size_t req_len = 7 + count * 2;
    uint16_t crc = mb_crc(req, req_len);
    req[req_len++] = crc & 0xFF;
    req[req_len++] = (crc >> 8) & 0xFF;

    uint8_t resp[RX_BUF];
    size_t  resp_len = 0;
    rs485_err_t err = mb_transact(req, req_len, resp, &resp_len);

    if (err == RS485_OK)
        ESP_LOGI(TAG, "write_multi start=%u count=%u OK", start, count);
    else
        ESP_LOGW(TAG, "write_multi start=%u count=%u %s", start, count, rs485_err_str(err));

    xSemaphoreGive(s_mutex);
    return err;
}