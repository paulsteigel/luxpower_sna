#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "config.h"

// ── Magic / function bytes ────────────────────────────────────
#define LUX_MAGIC_0          0xA1
#define LUX_MAGIC_1          0x1A
#define LUX_TCP_FN           0xC2
#define LUX_HEARTBEAT        0xC1
#define LUX_ACTION_W         0x01

// ── Modbus function codes ─────────────────────────────────────
#define LUX_FN_READ_INPUT    0x04
#define LUX_FN_READ_HOLD     0x03
#define LUX_FN_WRITE_SINGLE  0x06
#define LUX_FN_WRITE_MULTI   0x10

// ── Frame directions (LE in wire) ─────────────────────────────
#define DIR_DONGLE_TO_SERVER  0x0002   // 02 00
#define DIR_DONGLE_RESP       0x0005   // 05 00

// ── CRC-16/Modbus ─────────────────────────────────────────────
static inline uint16_t lux_crc16(const uint8_t *d, size_t len) {
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= *d++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (crc & 1 ? 0xA001 : 0);
    }
    return crc;
}

// ── Build outer header (20 bytes) ────────────────────────────
static inline void lux_build_hdr(uint8_t *buf, uint16_t dir,
                                  uint16_t data_len, uint8_t seq) {
    uint16_t fl = data_len + 14;
    buf[0] = LUX_MAGIC_0;   buf[1] = LUX_MAGIC_1;
    buf[2] = dir & 0xFF;    buf[3] = (dir >> 8) & 0xFF;
    buf[4] = fl & 0xFF;     buf[5] = (fl >> 8) & 0xFF;
    buf[6] = seq;
    buf[7] = LUX_TCP_FN;
    memcpy(buf + 8, DONGLE_SN, 10);
    buf[18] = data_len & 0xFF;
    buf[19] = (data_len >> 8) & 0xFF;
}

// ── Heartbeat (19 bytes) ──────────────────────────────────────
static inline int lux_build_heartbeat(uint8_t *buf) {
    buf[0] = LUX_MAGIC_0;  buf[1] = LUX_MAGIC_1;
    buf[2] = 0x05;         buf[3] = 0x00;
    buf[4] = 0x0D;         buf[5] = 0x00;
    buf[6] = 0x01;
    buf[7] = LUX_HEARTBEAT;
    memcpy(buf + 8, DONGLE_SN, 10);
    buf[18] = 0x00;
    return 19;
}

// ── READ_INPUT request (38 bytes) ────────────────────────────
static inline int lux_build_read_input(uint8_t *buf,
                                        uint16_t start, uint16_t count,
                                        uint8_t seq) {
    lux_build_hdr(buf, DIR_DONGLE_TO_SERVER, 18, seq);
    uint8_t *df = buf + 20;
    df[0] = LUX_ACTION_W;
    df[1] = LUX_FN_READ_INPUT;
    memcpy(df + 2, INVERTER_SN, 10);
    df[12] = start & 0xFF;  df[13] = (start >> 8) & 0xFF;
    df[14] = count & 0xFF;  df[15] = (count >> 8) & 0xFF;
    uint16_t crc = lux_crc16(df, 16);
    df[16] = crc & 0xFF;    df[17] = (crc >> 8) & 0xFF;
    return 38;
}

// ── READ_HOLD request (38 bytes) ─────────────────────────────
static inline int lux_build_read_hold(uint8_t *buf,
                                       uint16_t start, uint16_t count,
                                       uint8_t seq) {
    lux_build_hdr(buf, DIR_DONGLE_TO_SERVER, 18, seq);
    uint8_t *df = buf + 20;
    df[0] = LUX_ACTION_W;
    df[1] = LUX_FN_READ_HOLD;
    memcpy(df + 2, INVERTER_SN, 10);
    df[12] = start & 0xFF;  df[13] = (start >> 8) & 0xFF;
    df[14] = count & 0xFF;  df[15] = (count >> 8) & 0xFF;
    uint16_t crc = lux_crc16(df, 16);
    df[16] = crc & 0xFF;    df[17] = (crc >> 8) & 0xFF;
    return 38;
}

// ── WRITE_SINGLE (38 bytes) ───────────────────────────────────
static inline int lux_build_write_single(uint8_t *buf,
                                          uint16_t reg, uint16_t val,
                                          uint8_t seq) {
    lux_build_hdr(buf, DIR_DONGLE_TO_SERVER, 18, seq);
    uint8_t *df = buf + 20;
    df[0] = LUX_ACTION_W;
    df[1] = LUX_FN_WRITE_SINGLE;
    memcpy(df + 2, INVERTER_SN, 10);
    df[12] = reg & 0xFF;  df[13] = (reg >> 8) & 0xFF;
    df[14] = val & 0xFF;  df[15] = (val >> 8) & 0xFF;
    uint16_t crc = lux_crc16(df, 16);
    df[16] = crc & 0xFF;  df[17] = (crc >> 8) & 0xFF;
    return 38;
}

// ── WRITE_MULTI for battery type (43 bytes) ───────────────────
// reg0: 0x8019=LeadAcid-lux, 0x801A=Lithium-lux, etc.
// reg1: always 0x0100
// data in packet is Big-Endian (confirmed from captures)
static inline int lux_build_write_multi(uint8_t *buf,
                                         uint16_t reg0, uint16_t reg1,
                                         uint8_t seq) {
    lux_build_hdr(buf, DIR_DONGLE_TO_SERVER, 23, seq);
    uint8_t *df = buf + 20;
    df[0] = LUX_ACTION_W;
    df[1] = LUX_FN_WRITE_MULTI;
    memcpy(df + 2, WRITE_MULTI_UNK, 10);
    df[12] = 0x00;  df[13] = 0x00;  // start_reg=0 LE
    df[14] = 0x02;  df[15] = 0x00;  // count=2 LE
    df[16] = 0x04;                   // byte_count=4
    df[17] = (reg0 >> 8) & 0xFF;    // reg0 BE
    df[18] =  reg0 & 0xFF;
    df[19] = (reg1 >> 8) & 0xFF;    // reg1 BE
    df[20] =  reg1 & 0xFF;
    uint16_t crc = lux_crc16(df, 21);
    df[21] = crc & 0xFF;  df[22] = (crc >> 8) & 0xFF;
    return 43;
}

// ── Parsed packet ─────────────────────────────────────────────
typedef enum {
    LUX_PKT_UNKNOWN          = 0,
    LUX_PKT_HEARTBEAT        = 1,
    LUX_PKT_READ_REQ         = 2,
    LUX_PKT_WRITE_SINGLE_REQ = 3,
    LUX_PKT_WRITE_MULTI_REQ  = 4,
    LUX_PKT_DATA_RESP        = 5,
} lux_pkt_type_t;

typedef struct {
    lux_pkt_type_t  type;
    uint8_t         dev_fn;
    uint16_t        reg;
    uint16_t        value;
    uint16_t        reg0, reg1;
    uint16_t        count;
    bool            crc_ok;
    const uint8_t  *df;
    size_t          df_len;
} lux_parsed_t;

static inline lux_parsed_t lux_parse(const uint8_t *buf, size_t len) {
    lux_parsed_t p = {};
    if (len < 22) return p;
    if (buf[0] != LUX_MAGIC_0 || buf[1] != LUX_MAGIC_1) return p;
    if (buf[7] == LUX_HEARTBEAT) { p.type = LUX_PKT_HEARTBEAT; return p; }
    if (buf[7] != LUX_TCP_FN) return p;

    p.df     = buf + 20;
    p.df_len = len - 20;
    if (p.df_len < 16) return p;

    p.dev_fn = p.df[1];
    p.reg    = p.df[12] | ((uint16_t)p.df[13] << 8);
    p.value  = p.df[14] | ((uint16_t)p.df[15] << 8);

    if (p.df_len >= 18) {
        uint16_t cc = lux_crc16(p.df, p.df_len - 2);
        uint16_t cr = p.df[p.df_len-2] | ((uint16_t)p.df[p.df_len-1] << 8);
        p.crc_ok = (cc == cr);
    }

    switch (p.dev_fn) {
        case LUX_FN_WRITE_SINGLE:
            p.type = LUX_PKT_WRITE_SINGLE_REQ;
            break;
        case LUX_FN_WRITE_MULTI:
            if (p.df_len >= 23) {
                p.type  = LUX_PKT_WRITE_MULTI_REQ;
                p.reg0  = ((uint16_t)p.df[17] << 8) | p.df[18];
                p.reg1  = ((uint16_t)p.df[19] << 8) | p.df[20];
                p.count = p.value;
            }
            break;
        default:
            p.type  = LUX_PKT_READ_REQ;
            p.count = p.value;
            break;
    }
    return p;
}

// ── Cloud write filter ────────────────────────────────────────
static inline bool lux_cloud_write_allowed(uint16_t reg) {
    for (int i = 0; i < (int)CLOUD_WHITELIST_LEN; i++)
        if (CLOUD_WRITE_WHITELIST[i] == reg) return true;
    return false;
}