#pragma once
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "config.h"

// ── Register cache ────────────────────────────────────────────
typedef struct {
    uint16_t input[INPUT_REG_COUNT];   // FC04 real-time data
    uint16_t hold[HOLD_REG_COUNT];     // FC03 config data
    bool     input_valid;
    bool     hold_valid;
    uint32_t last_input_update_ms;
    uint32_t last_hold_update_ms;
    SemaphoreHandle_t mutex;
} reg_cache_t;

// ── Write command (from MQTT or LuxApp → inverter) ────────────
typedef enum {
    CMD_WRITE_SINGLE = 0,
    CMD_WRITE_MULTI  = 1,
    CMD_SET_LITHIUM  = 2,
    CMD_SET_LEADACID = 3,
} cmd_type_t;

typedef struct {
    cmd_type_t type;
    uint16_t   reg;
    uint16_t   value;
    // for write_multi (battery type):
    uint16_t   reg0;
    uint16_t   reg1;
    char       source[16];   // "mqtt", "cloud", "local_app"
} write_cmd_t;

// ── Event flags: notify MQTT task to publish ─────────────────
typedef struct {
    bool input_updated;
    bool hold_updated;
    SemaphoreHandle_t mutex;
} event_flags_t;

// ── Global singletons (defined in shared_state.c) ─────────────
extern reg_cache_t    g_regs;
extern QueueHandle_t  g_write_queue;   // write_cmd_t, depth=16
extern event_flags_t  g_events;

// ── Init ──────────────────────────────────────────────────────
static inline void shared_state_init(void) {
    g_regs.mutex = xSemaphoreCreateMutex();
    g_events.mutex = xSemaphoreCreateMutex();
    memset(&g_regs, 0, sizeof(g_regs));
    g_regs.input_valid = false;
    g_regs.hold_valid  = false;
    g_write_queue = xQueueCreate(16, sizeof(write_cmd_t));
}

// ── Thread-safe register read ─────────────────────────────────
static inline uint16_t reg_get_input(uint16_t addr) {
    if (addr >= INPUT_REG_COUNT) return 0;
    xSemaphoreTake(g_regs.mutex, portMAX_DELAY);
    uint16_t v = g_regs.input[addr];
    xSemaphoreGive(g_regs.mutex);
    return v;
}

static inline uint16_t reg_get_hold(uint16_t addr) {
    if (addr >= HOLD_REG_COUNT) return 0;
    xSemaphoreTake(g_regs.mutex, portMAX_DELAY);
    uint16_t v = g_regs.hold[addr];
    xSemaphoreGive(g_regs.mutex);
    return v;
}

// ── Bulk update (from Modbus response) ───────────────────────
static inline void reg_update_input(uint16_t start, const uint16_t *data, uint16_t count) {
    if (start + count > INPUT_REG_COUNT) count = INPUT_REG_COUNT - start;
    xSemaphoreTake(g_regs.mutex, portMAX_DELAY);
    memcpy(&g_regs.input[start], data, count * sizeof(uint16_t));
    g_regs.input_valid = true;
    g_regs.last_input_update_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    xSemaphoreGive(g_regs.mutex);

    xSemaphoreTake(g_events.mutex, portMAX_DELAY);
    g_events.input_updated = true;
    xSemaphoreGive(g_events.mutex);
}

static inline void reg_update_hold(uint16_t start, const uint16_t *data, uint16_t count) {
    if (start + count > HOLD_REG_COUNT) count = HOLD_REG_COUNT - start;
    xSemaphoreTake(g_regs.mutex, portMAX_DELAY);
    memcpy(&g_regs.hold[start], data, count * sizeof(uint16_t));
    g_regs.hold_valid = true;
    g_regs.last_hold_update_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    xSemaphoreGive(g_regs.mutex);

    xSemaphoreTake(g_events.mutex, portMAX_DELAY);
    g_events.hold_updated = true;
    xSemaphoreGive(g_events.mutex);
}

// ── Queue a write command ─────────────────────────────────────
static inline bool cmd_queue_write(uint16_t reg, uint16_t val, const char *src) {
    write_cmd_t cmd = {};
    cmd.type  = CMD_WRITE_SINGLE;
    cmd.reg   = reg;
    cmd.value = val;
    strncpy(cmd.source, src, sizeof(cmd.source) - 1);
    return xQueueSend(g_write_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

static inline bool cmd_queue_write_multi(uint16_t reg0, uint16_t reg1, const char *src) {
    write_cmd_t cmd = {};
    cmd.type  = CMD_WRITE_MULTI;
    cmd.reg0  = reg0;
    cmd.reg1  = reg1;
    strncpy(cmd.source, src, sizeof(cmd.source) - 1);
    return xQueueSend(g_write_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}
