// =============================================================================
// lux_registers.h
// LuxPower SNA5/6K — Complete Modbus Register Definition Table
//
// Sources:
//   - Official V16 Protocol 2025-01-09 (OwlBawl/Luxpower-Modbus-RTU)
//   - APK reverse engineering (LocalMidSetFragment, LocalOffGridSetFragment, etc.)
//   - HA integration (number.py, LXPPacket.py)
//   - NamPham Arduino reference implementation
//
// Register spaces:
//   INPUT  (FC04) — real-time read-only data
//   HOLD   (FC03 read / FC06 write) — config, control
//
// Usage:
//   #include "lux_registers.h"
//   const LuxRegDef* def = lux_find_input(5);   // find INPUT reg 5
//   const LuxRegDef* def = lux_find_hold(105);  // find HOLD reg 105
//   const char* s = lux_state_name(reg0_val);   // decode working state
// =============================================================================

#pragma once
#include <stdint.h>

// =============================================================================
// ENUMERATIONS
// =============================================================================

// Modbus function code category
enum LuxRegType : uint8_t {
    REG_INPUT = 0,  // FC04: Read Input Registers (real-time, read-only)
    REG_HOLD  = 1,  // FC03: Read Holding Registers / FC06: Write Single Register
};

// Raw value interpretation
enum LuxValType : uint8_t {
    VAL_U16   = 0,  // Unsigned 16-bit. real = raw * scale
    VAL_S16   = 1,  // Signed 16-bit (two's complement). real = raw * scale
    VAL_U32   = 2,  // Unsigned 32-bit — LOW register at addr, HIGH at addr+1. real = (hi<<16|lo) * scale
    VAL_BITS  = 3,  // Bitmap — publish raw uint16; use LuxBitDef table to unpack individual bits
    VAL_TIME  = 4,  // Packed time — high byte = Hour (0-23), low byte = Minute (0-59)
    VAL_ASCII = 5,  // ASCII pair — high byte = char[n], low byte = char[n+1]
    VAL_SKIP  = 6,  // Reserved / unknown — read in bulk but do not publish
};

// Read/write access
enum LuxAccess : uint8_t {
    ACC_R  = 0,  // Read-only  — subscribe to lux/state/{name}, never write
    ACC_RW = 1,  // Read/Write — subscribe to lux/state/{name}, accept lux/cmd/set/{name}
    ACC_W  = 2,  // Write-only — only accept lux/cmd/set/{name}, no state publish
};

// Polling schedule group
enum LuxPollGroup : uint8_t {
    POLL_BOOT   = 0,  // Once on startup only (firmware version, serial number, device type)
    POLL_FAST   = 1,  // Every 5s  — real-time: PV, battery, grid, EPS power values
    POLL_SLOW   = 2,  // Every 60s — daily/total energy, temperatures, BMS status
    POLL_CONFIG = 3,  // Every 5min — holding register config snapshot
    POLL_MANUAL = 4,  // On-demand only — write-back targets, time sync, etc.
};

// =============================================================================
// REGISTER DEFINITION STRUCT
// =============================================================================

struct LuxRegDef {
    uint16_t      addr;          // Modbus register address
    LuxRegType    reg_type;      // INPUT (FC04) or HOLD (FC03/06)
    LuxValType    val_type;      // How to interpret raw 16-bit value
    LuxAccess     access;        // R / RW / W
    float         scale;         // Multiply raw value by scale to get real-world value
    const char*   unit;          // SI unit string (e.g. "V", "W", "%", "")
    const char*   mqtt_name;     // MQTT topic suffix: lux/state/{mqtt_name}
    const char*   friendly_name; // Human-readable label (for HA entity names)
    int16_t       min_raw;       // Minimum raw value allowed for writes (-1 = no limit)
    int16_t       max_raw;       // Maximum raw value allowed for writes (-1 = no limit)
    LuxPollGroup  poll_group;
};

// =============================================================================
// INPUT REGISTERS (FC04) — Read-Only Real-Time Data
// Organised into bulk read blocks — see LUX_READ_BLOCKS below
// =============================================================================

static const LuxRegDef LUX_INPUT_REGS[] = {
// addr  type       val_type  access  scale     unit    mqtt_name             friendly_name                    min   max   poll

// ── BLOCK IN_A: 0–39 — Core real-time (5s poll) ──────────────────────────────
{ 0,  REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "state",              "Working State",                   -1,   -1, POLL_FAST   },
{ 1,  REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "V",    "vpv1",               "PV1 Voltage",                     -1,   -1, POLL_FAST   },
{ 2,  REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "V",    "vpv2",               "PV2 Voltage",                     -1,   -1, POLL_FAST   },
{ 3,  REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "V",    "vpv3",               "PV3 Voltage",                     -1,   -1, POLL_FAST   },
{ 4,  REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "V",    "vbat",               "Battery Voltage",                 -1,   -1, POLL_FAST   },
{ 5,  REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "%",    "soc",                "Battery SOC",                     -1,   -1, POLL_FAST   },
// NOTE reg 5: high byte = SOH, low byte = SOC. Use bitmask 0x00FF for SOC.
{ 6,  REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "internal_fault",     "Internal Fault",                  -1,   -1, POLL_FAST   },
{ 7,  REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "ppv1",               "PV1 Power",                       -1,   -1, POLL_FAST   },
{ 8,  REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "ppv2",               "PV2 Power",                       -1,   -1, POLL_FAST   },
{ 9,  REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "ppv_total",          "PV Total Power (PV1+PV2+PV3)",    -1,   -1, POLL_FAST   },
{ 10, REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "p_charge",           "Battery Charge Power",            -1,   -1, POLL_FAST   },
{ 11, REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "p_discharge",        "Battery Discharge Power",         -1,   -1, POLL_FAST   },
{ 12, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "V",    "vac_r",              "Grid Voltage R-phase",            -1,   -1, POLL_FAST   },
{ 13, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "V",    "vac_s",              "Grid Voltage S-phase",            -1,   -1, POLL_FAST   },
{ 14, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "V",    "vac_t",              "Grid Voltage T-phase",            -1,   -1, POLL_FAST   },
{ 15, REG_INPUT, VAL_U16,   ACC_R,  0.01f,  "Hz",   "fac",                "Grid Frequency",                  -1,   -1, POLL_FAST   },
{ 16, REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "p_inv",              "Inverter Output Power (R)",       -1,   -1, POLL_FAST   },
{ 17, REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "p_rec",              "AC Charge Rectifier Power (R)",   -1,   -1, POLL_FAST   },
{ 18, REG_INPUT, VAL_U16,   ACC_R,  0.01f,  "A",    "iinv_rms",           "Inverter RMS Current (R)",        -1,   -1, POLL_FAST   },
{ 19, REG_INPUT, VAL_U16,   ACC_R,  0.001f, "",     "pf",                 "Power Factor",                    -1,   -1, POLL_FAST   },
// NOTE reg 19: (0,1000] → x/1000; (1000,2000) → (1000-x)/1000
{ 20, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "V",    "veps_r",             "EPS Voltage R-phase",             -1,   -1, POLL_FAST   },
{ 21, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "V",    "veps_s",             "EPS Voltage S-phase",             -1,   -1, POLL_FAST   },
{ 22, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "V",    "veps_t",             "EPS Voltage T-phase",             -1,   -1, POLL_FAST   },
{ 23, REG_INPUT, VAL_U16,   ACC_R,  0.01f,  "Hz",   "feps",               "EPS Frequency",                   -1,   -1, POLL_FAST   },
{ 24, REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "p_eps",              "EPS Active Power (R)",            -1,   -1, POLL_FAST   },
{ 25, REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "VA",   "s_eps",              "EPS Apparent Power (R)",          -1,   -1, POLL_FAST   },
{ 26, REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "p_to_grid",          "Power to Grid (R)",               -1,   -1, POLL_FAST   },
{ 27, REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "p_to_user",          "Power from Grid to User (R)",     -1,   -1, POLL_FAST   },
{ 28, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "kWh",  "epv1_day",           "PV1 Energy Today",                -1,   -1, POLL_SLOW   },
{ 29, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "kWh",  "epv2_day",           "PV2 Energy Today",                -1,   -1, POLL_SLOW   },
{ 30, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "kWh",  "epv_total_day",      "PV Total Energy Today",           -1,   -1, POLL_SLOW   },
{ 31, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "kWh",  "einv_day",           "Inverter Output Energy Today",    -1,   -1, POLL_SLOW   },
{ 32, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "kWh",  "erec_day",           "AC Charge Energy Today",          -1,   -1, POLL_SLOW   },
{ 33, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "kWh",  "echg_day",           "Battery Charge Energy Today",     -1,   -1, POLL_SLOW   },
{ 34, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "kWh",  "edischg_day",        "Battery Discharge Energy Today",  -1,   -1, POLL_SLOW   },
{ 35, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "kWh",  "eeps_day",           "EPS Energy Today",                -1,   -1, POLL_SLOW   },
{ 36, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "kWh",  "etogrid_day",        "Export to Grid Today",            -1,   -1, POLL_SLOW   },
{ 37, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "kWh",  "etouser_day",        "Import from Grid Today",          -1,   -1, POLL_SLOW   },
{ 38, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "V",    "vbus1",              "Bus Voltage 1",                   -1,   -1, POLL_FAST   },
{ 39, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "V",    "vbus2",              "Bus Voltage 2",                   -1,   -1, POLL_FAST   },

// ── BLOCK IN_B: 40–79 — Cumulative energy, faults, temperatures ──────────────
// NOTE: VAL_U32 entries consume TWO registers (addr = LOW, addr+1 = HIGH).
//       The bulk read block covers both; the firmware driver must combine them.
{ 40, REG_INPUT, VAL_U32,   ACC_R,  0.1f,   "kWh",  "epv1_all",           "PV1 Total Cumulative Energy",     -1,   -1, POLL_SLOW   },
{ 42, REG_INPUT, VAL_U32,   ACC_R,  0.1f,   "kWh",  "epv2_all",           "PV2 Total Cumulative Energy",     -1,   -1, POLL_SLOW   },
{ 44, REG_INPUT, VAL_U32,   ACC_R,  0.1f,   "kWh",  "epv_all",            "PV Total Cumulative Energy",      -1,   -1, POLL_SLOW   },
{ 46, REG_INPUT, VAL_U32,   ACC_R,  0.1f,   "kWh",  "einv_all",           "Inverter Total Output Energy",    -1,   -1, POLL_SLOW   },
{ 48, REG_INPUT, VAL_U32,   ACC_R,  0.1f,   "kWh",  "erec_all",           "AC Charge Total Energy",          -1,   -1, POLL_SLOW   },
{ 50, REG_INPUT, VAL_U32,   ACC_R,  0.1f,   "kWh",  "echg_all",           "Battery Total Charge Energy",     -1,   -1, POLL_SLOW   },
{ 52, REG_INPUT, VAL_U32,   ACC_R,  0.1f,   "kWh",  "edischg_all",        "Battery Total Discharge Energy",  -1,   -1, POLL_SLOW   },
{ 54, REG_INPUT, VAL_U32,   ACC_R,  0.1f,   "kWh",  "eeps_all",           "EPS Total Energy",                -1,   -1, POLL_SLOW   },
{ 56, REG_INPUT, VAL_U32,   ACC_R,  0.1f,   "kWh",  "etogrid_all",        "Total Export Energy",             -1,   -1, POLL_SLOW   },
{ 58, REG_INPUT, VAL_U32,   ACC_R,  0.1f,   "kWh",  "etouser_all",        "Total Import Energy",             -1,   -1, POLL_SLOW   },
{ 60, REG_INPUT, VAL_U32,   ACC_R,  1.0f,   "",     "fault_code",         "Fault Code",                      -1,   -1, POLL_FAST   },
{ 62, REG_INPUT, VAL_U32,   ACC_R,  1.0f,   "",     "warning_code",       "Warning Code",                    -1,   -1, POLL_FAST   },
{ 64, REG_INPUT, VAL_S16,   ACC_R,  1.0f,   "°C",   "t_inner",            "Internal Temperature",            -1,   -1, POLL_SLOW   },
{ 65, REG_INPUT, VAL_S16,   ACC_R,  1.0f,   "°C",   "t_rad1",             "Radiator Temperature 1",          -1,   -1, POLL_SLOW   },
{ 66, REG_INPUT, VAL_S16,   ACC_R,  1.0f,   "°C",   "t_rad2",             "Radiator Temperature 2",          -1,   -1, POLL_SLOW   },
{ 67, REG_INPUT, VAL_S16,   ACC_R,  1.0f,   "°C",   "t_bat",              "Battery Temperature",             -1,   -1, POLL_SLOW   },
{ 69, REG_INPUT, VAL_U32,   ACC_R,  1.0f,   "s",    "runtime",            "Total Runtime",                   -1,   -1, POLL_SLOW   },
{ 77, REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "ac_input_type",      "AC Input Type Flags",             -1,   -1, POLL_FAST   },
// NOTE reg 77 bits — see LUX_BITMAP_BITS below

// ── BLOCK IN_C: 80–113 — BMS / Battery data ──────────────────────────────────
// Reg 80 — BatType/BatComType packed: high byte=BatType, low byte=BatComType
// BatType: 0=LeadAcid 1=Lithium 2=NoSelection — mirrors HOLD reg 0 bits 15-12
// BatComType: 0=CAN 1=RS485 — use lux_bat_type() / lux_bat_com_type() helpers
{ 80, REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "bat_type_com",       "Battery Type / Com Type (packed)", -1,   -1, POLL_SLOW   },
{ 81, REG_INPUT, VAL_U16,   ACC_R,  0.01f,  "A",    "max_chg_curr_bms",   "Max Charge Current (BMS limit)",  -1,   -1, POLL_SLOW   },
{ 82, REG_INPUT, VAL_U16,   ACC_R,  0.01f,  "A",    "max_dischg_curr_bms","Max Discharge Current (BMS limit)",-1,  -1, POLL_SLOW   },
{ 83, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "V",    "chg_volt_ref_bms",   "Charge Voltage Ref (BMS)",        -1,   -1, POLL_SLOW   },
{ 84, REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "V",    "dischg_cut_bms",     "Discharge Cutoff Voltage (BMS)",  -1,   -1, POLL_SLOW   },
{ 85, REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "bat_status_0",       "BMS Status 0",                    -1,   -1, POLL_SLOW   },
{ 86, REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "bat_status_1",       "BMS Status 1",                    -1,   -1, POLL_SLOW   },
{ 87, REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "bat_status_2",       "BMS Status 2",                    -1,   -1, POLL_SLOW   },
{ 88, REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "bat_status_3",       "BMS Status 3",                    -1,   -1, POLL_SLOW   },
{ 89, REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "bat_status_4",       "BMS Status 4",                    -1,   -1, POLL_SLOW   },
{ 90, REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "bat_status_5",       "BMS Status 5",                    -1,   -1, POLL_SLOW   },
{ 91, REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "bat_status_6",       "BMS Status 6",                    -1,   -1, POLL_SLOW   },
{ 92, REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "bat_status_7",       "BMS Status 7",                    -1,   -1, POLL_SLOW   },
{ 93, REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "bat_status_8",       "BMS Status 8",                    -1,   -1, POLL_SLOW   },
{ 94, REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "bat_status_9",       "BMS Status 9",                    -1,   -1, POLL_SLOW   },
{ 95, REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "bat_status_inv",     "BMS Inverter Aggregate Status",   -1,   -1, POLL_SLOW   },
{ 96, REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "",     "bat_parallel_num",   "Batteries in Parallel",           -1,   -1, POLL_SLOW   },
{ 97, REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "Ah",   "bat_capacity_bms",   "Battery Capacity (BMS)",          -1,   -1, POLL_SLOW   },
{ 98, REG_INPUT, VAL_S16,   ACC_R,  0.01f,  "A",    "bat_current",        "Battery Current",                 -1,   -1, POLL_FAST   },
{ 99, REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "fault_bms",          "BMS Fault Code",                  -1,   -1, POLL_FAST   },
{ 100,REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "warning_bms",        "BMS Warning Code",                -1,   -1, POLL_FAST   },
{ 101,REG_INPUT, VAL_U16,   ACC_R,  0.001f, "V",    "max_cell_volt",      "Max Cell Voltage",                -1,   -1, POLL_SLOW   },
{ 102,REG_INPUT, VAL_U16,   ACC_R,  0.001f, "V",    "min_cell_volt",      "Min Cell Voltage",                -1,   -1, POLL_SLOW   },
{ 103,REG_INPUT, VAL_S16,   ACC_R,  0.1f,   "°C",   "max_cell_temp",      "Max Cell Temperature",            -1,   -1, POLL_SLOW   },
{ 104,REG_INPUT, VAL_S16,   ACC_R,  0.1f,   "°C",   "min_cell_temp",      "Min Cell Temperature",            -1,   -1, POLL_SLOW   },
{ 105,REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "bms_update_dry",     "BMS Update State / Dry Contact",  -1,   -1, POLL_SLOW   },
{ 106,REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "",     "cycle_count",        "Battery Cycle Count",             -1,   -1, POLL_SLOW   },
{ 107,REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "V",    "vbat_inv",           "Battery Voltage (Inverter Sample)",-1,  -1, POLL_FAST   },

// ── BLOCK IN_D: 113–132 — Serial number, parallel, generator ─────────────────
{ 113,REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "master_slave",       "Master/Slave Status",             -1,   -1, POLL_SLOW   },
{ 114,REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "ongrid_load_pwr",    "On-Grid Load Power",              -1,   -1, POLL_FAST   },
{ 115,REG_INPUT, VAL_ASCII, ACC_R,  1.0f,   "",     "sn_0_1",             "Serial Number chars [0-1]",       -1,   -1, POLL_BOOT   },
{ 116,REG_INPUT, VAL_ASCII, ACC_R,  1.0f,   "",     "sn_2_3",             "Serial Number chars [2-3]",       -1,   -1, POLL_BOOT   },
{ 117,REG_INPUT, VAL_ASCII, ACC_R,  1.0f,   "",     "sn_4_5",             "Serial Number chars [4-5]",       -1,   -1, POLL_BOOT   },
{ 118,REG_INPUT, VAL_ASCII, ACC_R,  1.0f,   "",     "sn_6_7",             "Serial Number chars [6-7]",       -1,   -1, POLL_BOOT   },
{ 119,REG_INPUT, VAL_ASCII, ACC_R,  1.0f,   "",     "sn_8_9",             "Serial Number chars [8-9]",       -1,   -1, POLL_BOOT   },
{ 120,REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "V",    "vbus_p",             "Half Bus Voltage",                -1,   -1, POLL_FAST   },
{ 121,REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "V",    "gen_volt",           "Generator Voltage",               -1,   -1, POLL_FAST   },
{ 122,REG_INPUT, VAL_U16,   ACC_R,  0.01f,  "Hz",   "gen_freq",           "Generator Frequency",             -1,   -1, POLL_FAST   },
{ 123,REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "gen_power",          "Generator Power",                 -1,   -1, POLL_FAST   },
{ 124,REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "kWh",  "egen_day",           "Generator Energy Today",          -1,   -1, POLL_SLOW   },
{ 125,REG_INPUT, VAL_U32,   ACC_R,  0.1f,   "kWh",  "egen_all",           "Generator Total Energy",          -1,   -1, POLL_SLOW   },
{ 153,REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "ac_couple_pwr",      "AC Coupled Inverter Power",       -1,   -1, POLL_FAST   },

// ── BLOCK IN_E: 170–178 — Load power, switch state, exception data ────────────
{ 170,REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "p_load",             "Load Power (On-Grid)",            -1,   -1, POLL_FAST   },
{ 171,REG_INPUT, VAL_U16,   ACC_R,  0.1f,   "kWh",  "eload_day",          "Load Energy Today",               -1,   -1, POLL_SLOW   },
{ 172,REG_INPUT, VAL_U32,   ACC_R,  0.1f,   "kWh",  "eload_all",          "Load Total Energy",               -1,   -1, POLL_SLOW   },
{ 174,REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "switch_state",       "Switch State",                    -1,   -1, POLL_FAST   },
{ 176,REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "exception_reason1",  "Exception Reason 1",              -1,   -1, POLL_SLOW   },
{ 177,REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "exception_reason2",  "Exception Reason 2",              -1,   -1, POLL_SLOW   },
{ 178,REG_INPUT, VAL_BITS,  ACC_R,  1.0f,   "",     "chg_dischg_disable", "Charge/Discharge Disable Flags",  -1,   -1, POLL_SLOW   },

// ── 3-phase extensions (3-phase inverters / 12K only) ────────────────────────
{ 180,REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "p_inv_s",            "Inverter Power S-phase",          -1,   -1, POLL_FAST   },
{ 181,REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "p_inv_t",            "Inverter Power T-phase",          -1,   -1, POLL_FAST   },
{ 184,REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "p_to_grid_s",        "Power to Grid S-phase",           -1,   -1, POLL_FAST   },
{ 185,REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "p_to_grid_t",        "Power to Grid T-phase",           -1,   -1, POLL_FAST   },
{ 186,REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "p_to_user_s",        "Power to User S-phase",           -1,   -1, POLL_FAST   },
{ 187,REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "p_to_user_t",        "Power to User T-phase",           -1,   -1, POLL_FAST   },
{ 210,REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "s",    "quick_chg_remain",   "Quick Charge Remaining Time",     -1,   -1, POLL_FAST   },
{ 232,REG_INPUT, VAL_U16,   ACC_R,  1.0f,   "W",    "smart_load_pwr",     "Smart Load Output Power",         -1,   -1, POLL_FAST   },
};

// =============================================================================
// HOLD REGISTERS (FC03 read / FC06 write)
// =============================================================================

static const LuxRegDef LUX_HOLD_REGS[] = {
// addr  type      val_type   access  scale     unit    mqtt_name              friendly_name                    min    max   poll

// ── Boot: Firmware / Identity (read once) ────────────────────────────────────
// HOLD Reg 0 — HOLD_MODEL (APK source: LocalMidSetFragment / LocalOffGridSetFragment)
// 16-bit packed field. Sub-fields decoded by lux_unpack_model() — see below.
//   Bits 15-12: batteryType   (0=LeadAcid, 1=Lithium, 2=NoSelection)
//   Bits 11-8:  lithiumType   (0=SPI, 1=CAN, 2=485, e.g. Pylon/Growatt/Weco...)
//   Bits  7-6:  measurement   (0=CounterType, 1=CTType)
//   Bits  5-4:  meterBrand    (0=CHNT, 1=Acrel, 2=Eastron, 3=WattNode)
//   Bit   3:    usVersion     (0=Standard, 1=US)
//   Bits  2-0:  meterType     (0=1ph/1way, 1=3ph/3way, 2=3ph/4way, etc.)
{ 0,  REG_HOLD, VAL_BITS,  ACC_RW,  1.0f,   "",     "hold_model",         "Hold Model Config (packed)",      -1,    -1, POLL_BOOT   },

{ 7,  REG_HOLD, VAL_ASCII, ACC_R,   1.0f,   "",     "fw_code_01",         "FW Code Chars 0/1",               -1,    -1, POLL_BOOT   },
{ 8,  REG_HOLD, VAL_ASCII, ACC_R,   1.0f,   "",     "fw_code_23",         "FW Code Chars 2/3",               -1,    -1, POLL_BOOT   },
{ 9,  REG_HOLD, VAL_U16,   ACC_R,   1.0f,   "",     "fw_ver_slave",       "Slave/Com CPU Version",           -1,    -1, POLL_BOOT   },
{ 10, REG_HOLD, VAL_U16,   ACC_R,   1.0f,   "",     "fw_ver_ctrl",        "Control CPU Version",             -1,    -1, POLL_BOOT   },
{ 15, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "",     "modbus_addr",        "Modbus Address",                   0,   150, POLL_BOOT   },
{ 16, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "",     "language",           "Language (0=EN 1=DE)",             0,     1, POLL_BOOT   },
{ 19, REG_HOLD, VAL_U16,   ACC_R,   1.0f,   "",     "device_type",        "Device Type Code",                -1,    -1, POLL_BOOT   },
// Device type codes: 0=Hybrid/SNA, 3=Off-grid SNA, 5=LSP, 6=12K, 7=AIO, 8=7-10K, 9=MidBox, 10=Gen3 6K, 11=SNA 12K

// ── Time sync (manual / boot) ─────────────────────────────────────────────────
{ 12, REG_HOLD, VAL_BITS,  ACC_RW,  1.0f,   "",     "time_year_month",    "Time: Year(high)/Month(low)",     -1,    -1, POLL_MANUAL },
{ 13, REG_HOLD, VAL_BITS,  ACC_RW,  1.0f,   "",     "time_day_hour",      "Time: Day(high)/Hour(low)",       -1,    -1, POLL_MANUAL },
{ 14, REG_HOLD, VAL_BITS,  ACC_RW,  1.0f,   "",     "time_min_sec",       "Time: Minute(high)/Second(low)",  -1,    -1, POLL_MANUAL },

// ── System Reset (write-only commands) ────────────────────────────────────────
{ 11, REG_HOLD, VAL_BITS,  ACC_W,   1.0f,   "",     "reset_cmd",          "Reset Command (bitmap)",          -1,    -1, POLL_MANUAL },
// Bit0=EnergyReset, Bit1=RestoreDefault, Bit7=RestartInverter

// ── PV Input Mode ─────────────────────────────────────────────────────────────
{ 20, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "",     "pv_input_mode",      "PV Input Mode",                    0,     7, POLL_CONFIG },

// ── Function Enable Bitmap 1 (reg 21) — key on/off switches ──────────────────
{ 21, REG_HOLD, VAL_BITS,  ACC_RW,  1.0f,   "",     "func_en",            "Function Enable Bitmap",          -1,    -1, POLL_CONFIG },
// See LUX_BITMAP_BITS for individual bit definitions

// ── Grid Protection Limits ────────────────────────────────────────────────────
{ 22, REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "start_pv_volt",      "PV Start Voltage",               900,  5000, POLL_CONFIG },
{ 23, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "s",    "connect_time",       "Grid Connect Wait Time",          30,   600, POLL_CONFIG },
{ 24, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "s",    "reconnect_time",     "Grid Reconnect Wait Time",         0,   900, POLL_CONFIG },
{ 25, REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "grid_volt_conn_low", "Grid Voltage Connect Low",        -1,    -1, POLL_CONFIG },
{ 26, REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "grid_volt_conn_high","Grid Voltage Connect High",       -1,    -1, POLL_CONFIG },
{ 27, REG_HOLD, VAL_U16,   ACC_RW,  0.01f,  "Hz",   "grid_freq_conn_low", "Grid Freq Connect Low",           -1,    -1, POLL_CONFIG },
{ 28, REG_HOLD, VAL_U16,   ACC_RW,  0.01f,  "Hz",   "grid_freq_conn_high","Grid Freq Connect High",          -1,    -1, POLL_CONFIG },
{ 29, REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "grid_lim1_low",      "Grid Volt Limit1 Low",            -1,    -1, POLL_CONFIG },
{ 30, REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "grid_lim1_high",     "Grid Volt Limit1 High",           -1,    -1, POLL_CONFIG },
{ 31, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "ms",   "grid_lim1_low_t",    "Grid Volt Limit1 Low Time",       -1,    -1, POLL_CONFIG },
{ 32, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "ms",   "grid_lim1_high_t",   "Grid Volt Limit1 High Time",      -1,    -1, POLL_CONFIG },
{ 33, REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "grid_lim2_low",      "Grid Volt Limit2 Low",            -1,    -1, POLL_CONFIG },
{ 34, REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "grid_lim2_high",     "Grid Volt Limit2 High",           -1,    -1, POLL_CONFIG },
{ 35, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "ms",   "grid_lim2_low_t",    "Grid Volt Limit2 Low Time",       -1,    -1, POLL_CONFIG },
{ 36, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "ms",   "grid_lim2_high_t",   "Grid Volt Limit2 High Time",      -1,    -1, POLL_CONFIG },
{ 37, REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "grid_lim3_low",      "Grid Volt Limit3 Low",            -1,    -1, POLL_CONFIG },
{ 38, REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "grid_lim3_high",     "Grid Volt Limit3 High",           -1,    -1, POLL_CONFIG },
{ 39, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "ms",   "grid_lim3_low_t",    "Grid Volt Limit3 Low Time",       -1,    -1, POLL_CONFIG },
{ 40, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "ms",   "grid_lim3_high_t",   "Grid Volt Limit3 High Time",      -1,    -1, POLL_CONFIG },
{ 41, REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "grid_mov_avg_high",  "Grid Volt Moving Avg High",       -1,    -1, POLL_CONFIG },
{ 42, REG_HOLD, VAL_U16,   ACC_RW,  0.01f,  "Hz",   "freq_lim1_low",      "Freq Limit1 Low",                 -1,    -1, POLL_CONFIG },
{ 43, REG_HOLD, VAL_U16,   ACC_RW,  0.01f,  "Hz",   "freq_lim1_high",     "Freq Limit1 High",                -1,    -1, POLL_CONFIG },
{ 44, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "ms",   "freq_lim1_low_t",    "Freq Limit1 Low Time",            -1,    -1, POLL_CONFIG },
{ 45, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "ms",   "freq_lim1_high_t",   "Freq Limit1 High Time",           -1,    -1, POLL_CONFIG },
{ 46, REG_HOLD, VAL_U16,   ACC_RW,  0.01f,  "Hz",   "freq_lim2_low",      "Freq Limit2 Low",                 -1,    -1, POLL_CONFIG },
{ 47, REG_HOLD, VAL_U16,   ACC_RW,  0.01f,  "Hz",   "freq_lim2_high",     "Freq Limit2 High",                -1,    -1, POLL_CONFIG },
{ 48, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "ms",   "freq_lim2_low_t",    "Freq Limit2 Low Time",            -1,    -1, POLL_CONFIG },
{ 49, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "ms",   "freq_lim2_high_t",   "Freq Limit2 High Time",           -1,    -1, POLL_CONFIG },
{ 50, REG_HOLD, VAL_U16,   ACC_RW,  0.01f,  "Hz",   "freq_lim3_low",      "Freq Limit3 Low",                 -1,    -1, POLL_CONFIG },
{ 51, REG_HOLD, VAL_U16,   ACC_RW,  0.01f,  "Hz",   "freq_lim3_high",     "Freq Limit3 High",                -1,    -1, POLL_CONFIG },
{ 52, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "ms",   "freq_lim3_low_t",    "Freq Limit3 Low Time",            -1,    -1, POLL_CONFIG },
{ 53, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "ms",   "freq_lim3_high_t",   "Freq Limit3 High Time",           -1,    -1, POLL_CONFIG },

// ── Reactive / Active Power Control ──────────────────────────────────────────
{ 54, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "max_q_pct_qv",       "Max Q% for Q(V) Curve",            0,   100, POLL_CONFIG },
{ 55, REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "qv_v2l",             "Q(V) V2L Undervoltage 2",         -1,    -1, POLL_CONFIG },
{ 56, REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "qv_v1l",             "Q(V) V1L Undervoltage 1",         -1,    -1, POLL_CONFIG },
{ 57, REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "qv_v1h",             "Q(V) V1H Overvoltage 1",          -1,    -1, POLL_CONFIG },
{ 58, REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "qv_v2h",             "Q(V) V2H Overvoltage 2",          -1,    -1, POLL_CONFIG },
{ 59, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "",     "reactive_pwr_cmd",   "Reactive Power CMD Type",          0,     7, POLL_CONFIG },
{ 60, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "active_pwr_pct",     "Active Power Percent CMD",         0,   100, POLL_CONFIG },
{ 61, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "reactive_pwr_pct",   "Reactive Power Percent CMD",       0,    60, POLL_CONFIG },
{ 62, REG_HOLD, VAL_U16,   ACC_RW,  0.001f, "",     "pf_cmd",             "Power Factor CMD",                -1,    -1, POLL_CONFIG },
{ 63, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "‰/min","power_soft_start",   "Power Soft Start Slope",           1,  4000, POLL_CONFIG },

// ── Core Charge / Discharge Power Control ────────────────────────────────────
{ 64, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "charge_pwr_pct",     "Charge Power Percent",             0,   100, POLL_CONFIG },
{ 65, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "dischg_pwr_pct",     "Discharge Power Percent",          0,   100, POLL_CONFIG },
{ 66, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "ac_chg_pwr_pct",     "AC Charge Power Percent",          0,   100, POLL_CONFIG },
{ 67, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "ac_chg_soc_lim",     "AC Charge SOC Limit",              0,   100, POLL_CONFIG },

// ── AC Charge Time Slots (3 periods) ─────────────────────────────────────────
{ 68, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "ac_chg_start_0",     "AC Charge Start Time 0",          -1,    -1, POLL_CONFIG },
{ 69, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "ac_chg_end_0",       "AC Charge End Time 0",            -1,    -1, POLL_CONFIG },
{ 70, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "ac_chg_start_1",     "AC Charge Start Time 1",          -1,    -1, POLL_CONFIG },
{ 71, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "ac_chg_end_1",       "AC Charge End Time 1",            -1,    -1, POLL_CONFIG },
{ 72, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "ac_chg_start_2",     "AC Charge Start Time 2",          -1,    -1, POLL_CONFIG },
{ 73, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "ac_chg_end_2",       "AC Charge End Time 2",            -1,    -1, POLL_CONFIG },

// ── Charge Priority (Charge First) Time Slots ────────────────────────────────
{ 74, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "chg_first_pwr",      "Charge Priority Power %",          0,   100, POLL_CONFIG },
{ 75, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "chg_first_soc",      "Charge Priority SOC Limit",        0,   100, POLL_CONFIG },
{ 76, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "chg_first_start_0",  "Charge Priority Start Time 0",    -1,    -1, POLL_CONFIG },
{ 77, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "chg_first_end_0",    "Charge Priority End Time 0",      -1,    -1, POLL_CONFIG },
{ 78, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "chg_first_start_1",  "Charge Priority Start Time 1",    -1,    -1, POLL_CONFIG },
{ 79, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "chg_first_end_1",    "Charge Priority End Time 1",      -1,    -1, POLL_CONFIG },
{ 80, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "chg_first_start_2",  "Charge Priority Start Time 2",    -1,    -1, POLL_CONFIG },
{ 81, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "chg_first_end_2",    "Charge Priority End Time 2",      -1,    -1, POLL_CONFIG },

// ── Forced Discharge Time Slots ───────────────────────────────────────────────
{ 82, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "forced_dischg_pwr",  "Forced Discharge Power %",         0,   100, POLL_CONFIG },
{ 83, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "forced_dischg_soc",  "Forced Discharge SOC Limit",       0,   100, POLL_CONFIG },
{ 84, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "forced_dischg_s0",   "Forced Discharge Start Time 0",   -1,    -1, POLL_CONFIG },
{ 85, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "forced_dischg_e0",   "Forced Discharge End Time 0",     -1,    -1, POLL_CONFIG },
{ 86, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "forced_dischg_s1",   "Forced Discharge Start Time 1",   -1,    -1, POLL_CONFIG },
{ 87, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "forced_dischg_e1",   "Forced Discharge End Time 1",     -1,    -1, POLL_CONFIG },
{ 88, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "forced_dischg_s2",   "Forced Discharge Start Time 2",   -1,    -1, POLL_CONFIG },
{ 89, REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "forced_dischg_e2",   "Forced Discharge End Time 2",     -1,    -1, POLL_CONFIG },

// ── EPS Settings ──────────────────────────────────────────────────────────────
{ 90, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "V",    "eps_volt_set",       "EPS Output Voltage",              -1,    -1, POLL_CONFIG },
{ 91, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "Hz",   "eps_freq_set",       "EPS Output Frequency",            50,    60, POLL_CONFIG },

// ── Q(V) Lock-in/out & Derate Timing ─────────────────────────────────────────
{ 92, REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "lock_in_v_pf",       "PF Curve Lock-In Voltage",        -1,    -1, POLL_CONFIG },
{ 93, REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "lock_out_v_pf",      "PF Curve Lock-Out Voltage",       -1,    -1, POLL_CONFIG },
{ 94, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "lock_in_p_qv",       "Q(V) Lock-In Power",               0,   100, POLL_CONFIG },
{ 95, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "lock_out_p_qv",      "Q(V) Lock-Out Power",              0,   100, POLL_CONFIG },
{ 96, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "ms",   "qv_delay",           "Q(V) Curve Delay",                 0,  2000, POLL_CONFIG },
{ 97, REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "ms",   "ovf_derate_delay",   "Over-Freq Derate Delay",           0,  1000, POLL_CONFIG },

// ── Lead-Acid Battery Parameters ─────────────────────────────────────────────
{ 99, REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "la_chg_volt",        "Lead-Acid Charge Voltage",       500,   590, POLL_CONFIG },
{ 100,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "la_dischg_cut",      "Lead-Acid Discharge Cutoff",     400,   520, POLL_CONFIG },
{ 101,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "A",    "charge_rate",        "Charge Rate (Lead-Acid)",          0,   140, POLL_CONFIG },
{ 102,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "A",    "dischg_rate",        "Discharge Rate (Lead-Acid)",       0,   140, POLL_CONFIG },
{ 103,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "feed_in_pwr_pct",    "Feed-In Grid Power %",             0,   100, POLL_CONFIG },
{ 105,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "eod_soc",            "End-of-Discharge SOC (On-Grid)",  10,    90, POLL_CONFIG },
{ 106,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "°C",   "la_temp_low_dischg", "LA Temp Low Limit Discharge",     -1,    -1, POLL_CONFIG },
{ 107,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "°C",   "la_temp_high_dischg","LA Temp High Limit Discharge",    -1,    -1, POLL_CONFIG },
{ 108,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "°C",   "la_temp_low_chg",    "LA Temp Low Limit Charge",        -1,    -1, POLL_CONFIG },
{ 109,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "°C",   "la_temp_high_chg",   "LA Temp High Limit Charge",       -1,    -1, POLL_CONFIG },

// ── Function Enable Bitmap 2 (reg 110) ───────────────────────────────────────
{ 110,REG_HOLD, VAL_BITS,  ACC_RW,  1.0f,   "",     "func_en1",           "Function Enable 1 Bitmap",        -1,    -1, POLL_CONFIG },

// ── Parallel / Phase Config ───────────────────────────────────────────────────
{ 112,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "",     "system_type",        "System Type (Parallel)",           0,     4, POLL_CONFIG },
{ 113,REG_HOLD, VAL_U16,   ACC_W,   1.0f,   "",     "composed_phase",     "Set Composed Phase",               0,     3, POLL_MANUAL },

// ── Frequency Derate / CT / Derating ─────────────────────────────────────────
{ 115,REG_HOLD, VAL_U16,   ACC_RW,  0.01f,  "Hz",   "ovf_derate_start",   "OVF Derate Start Point",        5000,  5200, POLL_CONFIG },
{ 116,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "W",    "p_to_user_start_d",  "P-to-User Start Discharge",       -1,    -1, POLL_CONFIG },
{ 117,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "W",    "p_to_user_start_c",  "P-to-User Start Charge",          -1,    -1, POLL_CONFIG },
{ 118,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "vbat_start_derating","Vbat Start Derating",             -1,    -1, POLL_CONFIG },
{ 119,REG_HOLD, VAL_S16,   ACC_RW,  1.0f,   "W",    "ct_power_offset",    "CT Power Offset (signed)",     -1000,  1000, POLL_CONFIG },

// ── System Enable Bitmap (reg 120) ───────────────────────────────────────────
{ 120,REG_HOLD, VAL_BITS,  ACC_RW,  1.0f,   "",     "sys_enable",         "System Enable Bitmap",            -1,    -1, POLL_CONFIG },
// Bits: 0=HalfHourACChg, 1-3=ACChargeType(0-5), 4-5=DischgCtrlType, 6=OnGridEODType, 7=GenChargeType, 8=SeparateZeroExportEn

{ 124,REG_HOLD, VAL_U16,   ACC_RW,  0.01f,  "Hz",   "ovf_derate_end",     "OVF Derate End Point",          5000,  5200, POLL_CONFIG },
{ 125,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "soc_low_eps",        "SOC Low Limit EPS Discharge",      0,   100, POLL_CONFIG },

// ── Optimal Charge/Discharge 30-min Schedule (regs 126-131) ──────────────────
{ 126,REG_HOLD, VAL_BITS,  ACC_RW,  1.0f,   "",     "opt_chg_00_04",      "Optimal Chg/Dischg 00:00-04:00",  -1,    -1, POLL_CONFIG },
{ 127,REG_HOLD, VAL_BITS,  ACC_RW,  1.0f,   "",     "opt_chg_04_08",      "Optimal Chg/Dischg 04:00-08:00",  -1,    -1, POLL_CONFIG },
{ 128,REG_HOLD, VAL_BITS,  ACC_RW,  1.0f,   "",     "opt_chg_08_12",      "Optimal Chg/Dischg 08:00-12:00",  -1,    -1, POLL_CONFIG },
{ 129,REG_HOLD, VAL_BITS,  ACC_RW,  1.0f,   "",     "opt_chg_12_16",      "Optimal Chg/Dischg 12:00-16:00",  -1,    -1, POLL_CONFIG },
{ 130,REG_HOLD, VAL_BITS,  ACC_RW,  1.0f,   "",     "opt_chg_16_20",      "Optimal Chg/Dischg 16:00-20:00",  -1,    -1, POLL_CONFIG },
{ 131,REG_HOLD, VAL_BITS,  ACC_RW,  1.0f,   "",     "opt_chg_20_24",      "Optimal Chg/Dischg 20:00-24:00",  -1,    -1, POLL_CONFIG },
// Each register: 8 x 2-bit slots (00=Default, 01=ACCharge, 10=PVCharge, 11=Discharge)

{ 132,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "bat_cell_volt_lim",  "Battery Cell Voltage Limits",     -1,    -1, POLL_CONFIG },
{ 133,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "",     "bat_cell_series",    "Battery Cell Series/Parallel",    -1,    -1, POLL_CONFIG },

// ── Under/Over Frequency Derate ───────────────────────────────────────────────
{ 134,REG_HOLD, VAL_U16,   ACC_RW,  0.01f,  "Hz",   "uvf_derate_start",   "UVF Derate Start Point",          -1,    -1, POLL_CONFIG },
{ 135,REG_HOLD, VAL_U16,   ACC_RW,  0.01f,  "Hz",   "uvf_derate_end",     "UVF Derate End Point",            -1,    -1, POLL_CONFIG },
{ 136,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "ovf_derate_ratio",   "OVF Derate Ratio",                 1,   100, POLL_CONFIG },
{ 137,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "W",    "spec_load_comp",     "Specific Load Compensate",        -1,    -1, POLL_CONFIG },

// ── Fine-resolution Power % (0.1% steps) ─────────────────────────────────────
{ 138,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "%",    "chg_pwr_fine",       "Charge Power % (0.1% res)",        0,  1000, POLL_CONFIG },
{ 139,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "%",    "dischg_pwr_fine",    "Discharge Power % (0.1% res)",     0,  1000, POLL_CONFIG },
{ 140,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "%",    "ac_chg_pwr_fine",    "AC Charge Power % (0.1% res)",     0,  1000, POLL_CONFIG },
{ 141,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "%",    "chg_first_pwr_fine", "Charge Priority % (0.1% res)",     0,  1000, POLL_CONFIG },
{ 142,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "%",    "forced_dischg_fine", "Forced Discharge % (0.1% res)",    0,  1000, POLL_CONFIG },
{ 143,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "%",    "active_pwr_fine",    "Active Power % (0.1% res)",        0,  1000, POLL_CONFIG },

// ── Battery Configuration ─────────────────────────────────────────────────────
{ 144,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "float_chg_volt",     "Float Charge Voltage",           500,   560, POLL_CONFIG },
{ 145,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "",     "output_prio",        "Output Priority (0=Bat 1=PV 2=AC)", 0,   3, POLL_CONFIG },
{ 146,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "",     "line_mode",          "Line Mode (0=APL 1=UPS 2=GEN)",    0,     2, POLL_CONFIG },
{ 147,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "Ah",   "battery_capacity",   "Battery Capacity (Unmatched)",     0, 10000, POLL_CONFIG },
{ 148,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "battery_nom_volt",   "Battery Nominal Voltage",        400,   590, POLL_CONFIG },

// ── Equalization ──────────────────────────────────────────────────────────────
{ 149,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "equalization_volt",  "Equalization Voltage",           500,   590, POLL_CONFIG },
{ 150,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "day",  "equalization_int",   "Equalization Interval",            0,   365, POLL_CONFIG },
{ 151,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "hr",   "equalization_time",  "Equalization Duration",            0,    24, POLL_CONFIG },

// ── AC First Time Slots ───────────────────────────────────────────────────────
{ 152,REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "ac_first_start_0",   "AC First Start Time 0",           -1,    -1, POLL_CONFIG },
{ 153,REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "ac_first_end_0",     "AC First End Time 0",             -1,    -1, POLL_CONFIG },
{ 154,REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "ac_first_start_1",   "AC First Start Time 1",           -1,    -1, POLL_CONFIG },
{ 155,REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "ac_first_end_1",     "AC First End Time 1",             -1,    -1, POLL_CONFIG },
{ 156,REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "ac_first_start_2",   "AC First Start Time 2",           -1,    -1, POLL_CONFIG },
{ 157,REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "ac_first_end_2",     "AC First End Time 2",             -1,    -1, POLL_CONFIG },

// ── AC Charge Thresholds ──────────────────────────────────────────────────────
{ 158,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "ac_chg_start_volt",  "AC Charge Start Voltage",        385,   520, POLL_CONFIG },
{ 159,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "ac_chg_end_volt",    "AC Charge End Voltage",          480,   590, POLL_CONFIG },
{ 160,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "ac_chg_start_soc",   "AC Charge Start SOC",              0,    90, POLL_CONFIG },
{ 161,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "ac_chg_end_soc",     "AC Charge End SOC",                0,   100, POLL_CONFIG },

// ── Battery Warning / Low Thresholds ─────────────────────────────────────────
{ 162,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "bat_low_volt",       "Battery Low Voltage Alarm",      400,   500, POLL_CONFIG },
{ 163,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "bat_low_back_volt",  "Battery Low Recovery Voltage",   420,   520, POLL_CONFIG },
{ 164,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "bat_low_soc",        "Battery Low SOC Alarm",            0,    90, POLL_CONFIG },
{ 165,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "bat_low_back_soc",   "Battery Low Recovery SOC",        20,   100, POLL_CONFIG },
{ 166,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "bat_low_util_volt",  "Bat Low → Grid Transfer Voltage",444,   514, POLL_CONFIG },
{ 167,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "bat_low_util_soc",   "Bat Low → Grid Transfer SOC",     0,   100, POLL_CONFIG },
{ 168,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "A",    "ac_chg_bat_curr",    "AC Charge Battery Current",        0,   140, POLL_CONFIG },
{ 169,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "ongrid_eod_volt",    "On-Grid EOD Voltage",            400,   560, POLL_CONFIG },

// ── SOC Calibration Curve ─────────────────────────────────────────────────────
{ 171,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "soc_curve_v1",       "SOC Curve Battery Voltage 1",    -1,    -1, POLL_CONFIG },
{ 172,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "soc_curve_v2",       "SOC Curve Battery Voltage 2",    -1,    -1, POLL_CONFIG },
{ 173,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "soc_curve_soc1",     "SOC Curve SOC Point 1",          -1,    -1, POLL_CONFIG },
{ 174,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "soc_curve_soc2",     "SOC Curve SOC Point 2",          -1,    -1, POLL_CONFIG },
{ 175,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "mΩ",   "soc_curve_res",      "SOC Curve Inner Resistance",     -1,    -1, POLL_CONFIG },

// ── Grid / Generator Power Limits ─────────────────────────────────────────────
{ 176,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "W",    "max_grid_input",     "Max Grid Input Power",           -1,    -1, POLL_CONFIG },
{ 177,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "W",    "gen_rate_power",     "Generator Rated Power",          -1,    -1, POLL_CONFIG },

// ── Function Enable Bitmap 3 (reg 179) ───────────────────────────────────────
{ 179,REG_HOLD, VAL_BITS,  ACC_RW,  1.0f,   "",     "func_en2",           "Function Enable 2 Bitmap",       -1,    -1, POLL_CONFIG },

// ── Volt-Watt Curve ───────────────────────────────────────────────────────────
{ 181,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "volt_watt_v1",       "Volt-Watt Curve V1",             -1,    -1, POLL_CONFIG },
{ 182,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "volt_watt_v2",       "Volt-Watt Curve V2",             -1,    -1, POLL_CONFIG },
{ 183,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "ms",   "volt_watt_delay",    "Volt-Watt Delay",                -1,    -1, POLL_CONFIG },
{ 184,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "volt_watt_p2",       "Volt-Watt P2",                    0,   200, POLL_CONFIG },

// ── Q(V) / Q(P) Curve Parameters ─────────────────────────────────────────────
{ 185,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "vref_qv",            "Q(V) Vref",                      -1,    -1, POLL_CONFIG },
{ 186,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "s",    "vref_filter_time",   "Q(V) Vref Filter Time",          -1,    -1, POLL_CONFIG },
{ 187,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "qv_q3",              "Q(V) Q3",                        -1,    -1, POLL_CONFIG },
{ 188,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "qv_q4",              "Q(V) Q4",                        -1,    -1, POLL_CONFIG },
{ 189,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "qp_p1",              "Q(P) P1",                        -1,    -1, POLL_CONFIG },
{ 190,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "qp_p2",              "Q(P) P2",                        -1,    -1, POLL_CONFIG },
{ 191,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "qp_p3",              "Q(P) P3",                        -1,    -1, POLL_CONFIG },
{ 192,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "qp_p4",              "Q(P) P4",                        -1,    -1, POLL_CONFIG },
{ 193,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "uvf_increase_ratio", "UVF Increase Ratio",               1,   100, POLL_CONFIG },

// ── Generator Charge Thresholds ───────────────────────────────────────────────
{ 194,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "gen_chg_start_v",    "Gen Charge Start Voltage",       384,   520, POLL_CONFIG },
{ 195,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "gen_chg_end_v",      "Gen Charge End Voltage",         480,   590, POLL_CONFIG },
{ 196,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "gen_chg_start_soc",  "Gen Charge Start SOC",             0,    90, POLL_CONFIG },
{ 197,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "gen_chg_end_soc",    "Gen Charge End SOC",              20,   100, POLL_CONFIG },
{ 198,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "A",    "max_gen_chg_curr",   "Max Generator Charge Current",     0,  4000, POLL_CONFIG },

// ── Thermal / Charge Termination ─────────────────────────────────────────────
{ 199,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "°C",   "over_temp_derate",   "Over-Temp Derate Start Point",   600,   900, POLL_CONFIG },
{ 201,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "chg_first_end_v",    "Charge Priority End Voltage",    480,   590, POLL_CONFIG },
{ 202,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "forced_dischg_end_v","Forced Discharge End Voltage",   400,   560, POLL_CONFIG },
{ 204,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "Ah",   "lead_capacity",      "Lead-Acid Battery Capacity",      50,  5000, POLL_CONFIG },

// ── Smart Load Control ────────────────────────────────────────────────────────
{ 213,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "smart_load_on_v",    "Smart Load On Voltage",          480,   590, POLL_CONFIG },
{ 214,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "smart_load_off_v",   "Smart Load Off Voltage",         400,   520, POLL_CONFIG },
{ 215,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "smart_load_on_soc",  "Smart Load On SOC",                0,   100, POLL_CONFIG },
{ 216,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "smart_load_off_soc", "Smart Load Off SOC",               0,   100, POLL_CONFIG },
{ 217,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "kW",   "start_pv_power",     "Start PV Power",                   0,   120, POLL_CONFIG },

// ── Peak Shaving ──────────────────────────────────────────────────────────────
{ 206,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "kW",   "peak_shav_pwr",      "Grid Peak Shaving Power",          0,   255, POLL_CONFIG },
{ 207,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "peak_shav_soc",      "Grid Peak Shaving SOC",            0,   100, POLL_CONFIG },
{ 208,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "peak_shav_volt",     "Grid Peak Shaving Voltage",      480,   590, POLL_CONFIG },
{ 209,REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "peak_shav_start_0",  "Peak Shaving Start Time 0",       -1,    -1, POLL_CONFIG },
{ 210,REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "peak_shav_end_0",    "Peak Shaving End Time 0",         -1,    -1, POLL_CONFIG },
{ 211,REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "peak_shav_start_1",  "Peak Shaving Start Time 1",       -1,    -1, POLL_CONFIG },
{ 212,REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "peak_shav_end_1",    "Peak Shaving End Time 1",         -1,    -1, POLL_CONFIG },

// ── AC Couple Thresholds ──────────────────────────────────────────────────────
{ 220,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "ac_couple_start_soc","AC Couple Start SOC",              0,   100, POLL_CONFIG },
{ 221,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "ac_couple_end_soc",  "AC Couple End SOC",                0,   255, POLL_CONFIG },
{ 222,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "ac_couple_start_v",  "AC Couple Start Voltage",        400,   595, POLL_CONFIG },
{ 223,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "ac_couple_end_v",    "AC Couple End Voltage",          420,   800, POLL_CONFIG },

// ── Function Enable Bitmap 4 (reg 233) ───────────────────────────────────────
{ 233,REG_HOLD, VAL_BITS,  ACC_RW,  1.0f,   "",     "func_en4",           "Function Enable 4 Bitmap",       -1,    -1, POLL_CONFIG },
// Bits: 0=QuickChgStart, 1=BattBackup, 2=Maintenance, 3=WorkingMode,
//       4-7=DryContactorMultiplex (1=RSD,2=DarkStart,3=SmartLoad,4=NonCrit), 10=OverFreq_fstop

{ 234,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "min",  "quick_chg_time",     "Quick Charge Duration",          -1,    -1, POLL_CONFIG },

// ── Battery Stop Charge ───────────────────────────────────────────────────────
{ 227,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "bat_stop_chg_soc",   "Battery Stop Charge SOC",        10,   101, POLL_CONFIG },
{ 228,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "bat_stop_chg_volt",  "Battery Stop Charge Voltage",   400,   595, POLL_CONFIG },

// ── Hysteresis (Delta SOC / Delta Volt) ──────────────────────────────────────
{ 253,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "%",    "delta_soc",          "Delta SOC (Hysteresis)",           5,    80, POLL_CONFIG },
{ 254,REG_HOLD, VAL_U16,   ACC_RW,  0.1f,   "V",    "delta_volt",         "Delta Voltage (Hysteresis)",      20,   100, POLL_CONFIG },

// ── Generator Timer Slots ─────────────────────────────────────────────────────
{ 256,REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "gen_start_0",        "Generator Start Time 0",         -1,    -1, POLL_CONFIG },
{ 257,REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "gen_end_0",          "Generator End Time 0",           -1,    -1, POLL_CONFIG },
{ 258,REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "gen_start_1",        "Generator Start Time 1",         -1,    -1, POLL_CONFIG },
{ 259,REG_HOLD, VAL_TIME,  ACC_RW,  1.0f,   "",     "gen_end_1",          "Generator End Time 1",           -1,    -1, POLL_CONFIG },

// ── Bus OVP / Discharge Recovery ─────────────────────────────────────────────
{ 260,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "V",    "bus_ovp_set",        "Bus OVP Set Point",              550,   595, POLL_CONFIG },
{ 261,REG_HOLD, VAL_U16,   ACC_RW,  1.0f,   "",     "dischg_recov_thresh","Discharge Recovery Threshold",   -1,    -1, POLL_CONFIG },
};

// =============================================================================
// BULK READ BLOCKS
// Used by the firmware's periodic poller to issue minimal FC03/FC04 requests.
// Each block reads a contiguous range. The driver then indexes into the
// LUX_INPUT_REGS / LUX_HOLD_REGS arrays to decode each register.
// =============================================================================

struct LuxReadBlock {
    LuxRegType    reg_type;
    uint16_t      start_addr;
    uint16_t      count;
    LuxPollGroup  poll_group;
    const char*   label;
};

static const LuxReadBlock LUX_READ_BLOCKS[] = {
    // ── INPUT blocks (FC04) ────────────────────────────────────────────────────
    { REG_INPUT,   0,  40, POLL_FAST,   "IN_A: Core real-time (state, PV, bat, grid, EPS, power)" },
    { REG_INPUT,  40,  40, POLL_SLOW,   "IN_B: Cumulative energy + fault/warn codes + temps" },
    { REG_INPUT,  80,  35, POLL_SLOW,   "IN_C: BMS status, battery cell data, cycle count" },
    { REG_INPUT, 113,  20, POLL_BOOT,   "IN_D: Serial number (5 regs), parallel, gen voltage/power" },
    { REG_INPUT, 170,  13, POLL_SLOW,   "IN_E: Load power, switch state, exception reasons" },
    // Optional extended blocks — enable based on device type:
    { REG_INPUT, 180,  12, POLL_FAST,   "IN_F: 3-phase S/T powers (12K / Trip only)" },
    { REG_INPUT, 210,   1, POLL_FAST,   "IN_G: Quick charge remaining time" },
    { REG_INPUT, 232,   1, POLL_FAST,   "IN_H: Smart load output power" },

    // ── HOLD blocks (FC03) ─────────────────────────────────────────────────────
    { REG_HOLD,    0,  21, POLL_BOOT,   "H_BOOT: HOLD_MODEL(0), FW version, time(12-14), modbus addr, language, device type(19)" },
    { REG_HOLD,   20,  55, POLL_CONFIG, "H_A: PV mode, func flags, grid limits regs 20-74" },
    { REG_HOLD,   74,  50, POLL_CONFIG, "H_B: Charge/discharge control + all 9 time slot pairs" },
    { REG_HOLD,   90,  55, POLL_CONFIG, "H_C: EPS, QV params, LA battery, EOD, func bitmaps" },
    { REG_HOLD,  144,  60, POLL_CONFIG, "H_D: Battery config, equalization, AC charge thresholds" },
    { REG_HOLD,  162,  40, POLL_CONFIG, "H_E: Warning thresholds, SOC curve, grid/gen power limits" },
    { REG_HOLD,  176,  50, POLL_CONFIG, "H_F: FuncEn2, volt-watt, QV/QP curves, gen charge" },
    { REG_HOLD,  199,  65, POLL_CONFIG, "H_G: Temp derate, smart load, peak shaving, AC couple" },
    { REG_HOLD,  225,  37, POLL_CONFIG, "H_H: Stop charge, hysteresis, gen timers, bus OVP" },
};

// =============================================================================
// BITMAP BIT DEFINITIONS
// Individual bit meanings for VAL_BITS registers.
// Used to publish separate binary_sensor / switch entities per bit.
// =============================================================================

struct LuxBitDef {
    uint16_t    reg_addr;
    LuxRegType  reg_type;
    uint8_t     bit_pos;        // 0 = LSB
    const char* mqtt_name;      // lux/state/{mqtt_name} → 0 or 1
    const char* friendly_name;
};

static const LuxBitDef LUX_BITMAP_BITS[] = {
    // ── Input Reg 77 — AC Input Type ───────────────────────────────────────────
    { 77, REG_INPUT, 0, "inp_is_gen",           "AC Input: 0=Grid 1=Generator" },
    { 77, REG_INPUT, 1, "ac_couple_flow",        "AC Couple Inverter Flow" },
    { 77, REG_INPUT, 2, "ac_couple_active",      "AC Couple Enabled" },
    { 77, REG_INPUT, 3, "smart_load_flow",       "Smart Load Flow" },
    { 77, REG_INPUT, 4, "smart_load_active",     "Smart Load Enabled" },
    { 77, REG_INPUT, 5, "eps_load_show",         "EPS Load Power Visible" },
    { 77, REG_INPUT, 6, "grid_load_show",        "Grid Load Power Visible" },
    { 77, REG_INPUT, 7, "pload_show",            "P-Load Visible" },

    // ── Hold Reg 21 — FuncEn (key user-facing switches) ────────────────────────
    { 21, REG_HOLD,  0, "eps_en",               "EPS (Off-Grid) Enabled" },
    { 21, REG_HOLD,  1, "ovf_derate_en",        "OVF Load Derate Enabled" },
    { 21, REG_HOLD,  2, "drms_en",              "DRMS Enabled" },
    { 21, REG_HOLD,  3, "lvrt_en",              "LVRT Enabled" },
    { 21, REG_HOLD,  4, "anti_island_en",       "Anti-Island Enabled" },
    { 21, REG_HOLD,  5, "neutral_detect_en",    "Neutral Detect Enabled" },
    { 21, REG_HOLD,  6, "grid_on_power_ss_en",  "Grid-On Power Soft Start" },
    { 21, REG_HOLD,  7, "ac_charge_en",         "AC Charge Enabled" },
    { 21, REG_HOLD,  8, "seamless_eps_en",      "Seamless EPS Transition" },
    { 21, REG_HOLD,  9, "standby_en",           "Standby Mode" },
    { 21, REG_HOLD, 10, "forced_dischg_en",     "Forced Discharge Enabled" },
    { 21, REG_HOLD, 11, "forced_chg_en",        "Forced Charge Enabled" },
    { 21, REG_HOLD, 12, "iso_en",               "ISO Detection Enabled" },
    { 21, REG_HOLD, 13, "gfci_en",              "GFCI Enabled" },
    { 21, REG_HOLD, 14, "dci_en",               "DCI Enabled" },
    { 21, REG_HOLD, 15, "feed_in_grid_en",      "Feed-In Grid Enabled" },

    // ── Hold Reg 110 — FunctionEn1 ─────────────────────────────────────────────
    { 110, REG_HOLD,  0, "ac_storage_en",        "AC Storage Enabled" },
    { 110, REG_HOLD,  1, "fast_zero_export",      "Fast Zero Export" },
    { 110, REG_HOLD,  2, "microgrid_en",          "Micro-Grid Enabled" },
    { 110, REG_HOLD,  3, "bat_shared",            "Battery Shared" },
    { 110, REG_HOLD,  4, "chg_last_en",           "Charge Last Enabled" },
    { 110, REG_HOLD,  7, "buzzer_en",             "Buzzer Enabled" },
    { 110, REG_HOLD, 10, "take_load_together",    "Take Load Together" },
    { 110, REG_HOLD, 11, "ongrid_working_mode",   "On-Grid Working Mode" },
    { 110, REG_HOLD, 14, "green_mode",            "Green Mode" },
    { 110, REG_HOLD, 15, "eco_mode",              "Eco Mode" },

    // ── Hold Reg 120 — stSysEnable ─────────────────────────────────────────────
    { 120, REG_HOLD,  0, "half_hour_ac_chg",      "Half-Hour AC Charge" },
    { 120, REG_HOLD,  6, "ongrid_eod_type",       "On-Grid EOD Type (0=Volt 1=SOC)" },
    { 120, REG_HOLD,  7, "gen_charge_type",       "Generator Charge Type" },
    { 120, REG_HOLD,  8, "sep_zero_export_en",    "Separate Zero Export" },

    // ── Hold Reg 179 — uFunctionEn2 ────────────────────────────────────────────
    { 179, REG_HOLD,  0, "ac_ct_dir",             "AC CT Direction" },
    { 179, REG_HOLD,  1, "pv_ct_dir",             "PV CT Direction" },
    { 179, REG_HOLD,  3, "bat_wakeup_pv_sell",    "Bat Wakeup → PV Sell First" },
    { 179, REG_HOLD,  4, "volt_watt_en",          "Volt-Watt Enabled" },
    { 179, REG_HOLD,  6, "act_pwr_cmd_en",        "Active Power CMD Enabled" },
    { 179, REG_HOLD,  7, "grid_peak_shav_en",     "Grid Peak Shaving Enabled" },
    { 179, REG_HOLD,  8, "gen_peak_shav_en",      "Gen Peak Shaving Enabled" },
    { 179, REG_HOLD,  9, "bat_chg_ctrl",          "Battery Charge Control" },
    { 179, REG_HOLD, 10, "bat_dischg_ctrl",       "Battery Discharge Control" },
    { 179, REG_HOLD, 11, "ac_coupling_en",        "AC Coupling Enabled" },
    { 179, REG_HOLD, 12, "pv_arc_en",             "PV Arc (AFCI) Enabled" },
    { 179, REG_HOLD, 13, "smart_load_en",         "Smart Load Enabled (Hold)" },
    { 179, REG_HOLD, 15, "ongrid_always_on",      "On-Grid Always On" },
};

// =============================================================================
// WORKING STATE LOOKUP TABLE (Input Reg 0)
// =============================================================================

struct LuxStateDef {
    uint16_t    code;
    const char* name;
};

static const LuxStateDef LUX_STATES[] = {
    { 0x00, "Standby" },
    { 0x01, "Fault" },
    { 0x02, "Programming" },
    { 0x04, "PV → Grid" },
    { 0x08, "PV Charging" },
    { 0x0C, "PV Charge + Grid" },
    { 0x10, "Battery → Grid" },
    { 0x14, "PV + Battery → Grid" },
    { 0x20, "AC Charging" },
    { 0x28, "PV + AC Charging" },
    { 0x40, "Battery Off-Grid" },
    { 0x60, "Off-Grid + Battery Charging" },
    { 0x80, "PV Off-Grid" },
    { 0x88, "PV Charging + Off-Grid" },
    { 0xC0, "PV + Battery Off-Grid" },
};

// =============================================================================
// SIZE CONSTANTS
// =============================================================================

static const uint16_t LUX_INPUT_REG_COUNT  = sizeof(LUX_INPUT_REGS)  / sizeof(LuxRegDef);
static const uint16_t LUX_HOLD_REG_COUNT   = sizeof(LUX_HOLD_REGS)   / sizeof(LuxRegDef);
static const uint16_t LUX_READ_BLOCK_COUNT = sizeof(LUX_READ_BLOCKS)  / sizeof(LuxReadBlock);
static const uint16_t LUX_BITMAP_BIT_COUNT = sizeof(LUX_BITMAP_BITS)  / sizeof(LuxBitDef);
static const uint16_t LUX_STATE_COUNT      = sizeof(LUX_STATES)       / sizeof(LuxStateDef);

// =============================================================================
// HELPER FUNCTIONS (inline, header-only)
// =============================================================================

// Find INPUT register definition by Modbus address
inline const LuxRegDef* lux_find_input(uint16_t addr) {
    for (uint16_t i = 0; i < LUX_INPUT_REG_COUNT; i++)
        if (LUX_INPUT_REGS[i].addr == addr) return &LUX_INPUT_REGS[i];
    return nullptr;
}

// Find HOLD register definition by Modbus address
inline const LuxRegDef* lux_find_hold(uint16_t addr) {
    for (uint16_t i = 0; i < LUX_HOLD_REG_COUNT; i++)
        if (LUX_HOLD_REGS[i].addr == addr) return &LUX_HOLD_REGS[i];
    return nullptr;
}

// Decode Working State code to string
inline const char* lux_state_name(uint16_t code) {
    for (uint16_t i = 0; i < LUX_STATE_COUNT; i++)
        if (LUX_STATES[i].code == code) return LUX_STATES[i].name;
    return "Unknown";
}

// Decode packed time register → hour and minute
inline void lux_unpack_time(uint16_t raw, uint8_t& hour, uint8_t& minute) {
    hour   = (raw >> 8) & 0xFF;
    minute = raw & 0xFF;
}

// Encode hour/minute back to packed time register value
inline uint16_t lux_pack_time(uint8_t hour, uint8_t minute) {
    return ((uint16_t)hour << 8) | minute;
}

// Combine two consecutive 16-bit registers into a 32-bit value (low first)
inline uint32_t lux_combine_u32(uint16_t low, uint16_t high) {
    return ((uint32_t)high << 16) | low;
}

// Extract SOC from reg 5 (low byte only; high byte = SOH)
inline uint8_t lux_soc_from_reg5(uint16_t raw) {
    return raw & 0xFF;
}

// Extract SOH from reg 5 (high byte)
inline uint8_t lux_soh_from_reg5(uint16_t raw) {
    return (raw >> 8) & 0xFF;
}

// =============================================================================
// HOLD_MODEL (HOLD Reg 0) — Packed Sub-Field Definitions
// Source: APK reverse engineering (LocalMidSetFragment, LocalOffGridSetFragment)
// =============================================================================

// Battery type (bits 15-12 of HOLD reg 0)
enum LuxBatteryType : uint8_t {
    BAT_LEAD_ACID    = 0,
    BAT_LITHIUM      = 1,
    BAT_NO_SELECTION = 2,
};

// Lithium battery brand/protocol (bits 11-8 of HOLD reg 0)
// Only valid when batteryType == BAT_LITHIUM
enum LuxLithiumType : uint8_t {
    LI_SPI           = 0,   // SPI communication
    LI_CAN_PYLON     = 1,   // CAN — Pylontech
    LI_485_PYLON     = 2,   // RS485 — Pylontech
    LI_CAN_WECO      = 3,   // CAN — WECO
    LI_485_WECO      = 4,   // RS485 — WECO
    LI_CAN_SOLTARO   = 5,   // CAN — Soltaro
    LI_CAN_ALPHA     = 6,   // CAN — Alpha
    LI_CAN_VARTA     = 7,   // CAN — Varta
    LI_CAN_GROWATT   = 8,   // CAN — Growatt
    LI_485_GROWATT   = 9,   // RS485 — Growatt
    LI_CAN_GENERIC   = 10,  // CAN — Generic
    LI_485_GENERIC   = 11,  // RS485 — Generic
};

// CT measurement type (bits 7-6 of HOLD reg 0)
enum LuxMeasurement : uint8_t {
    MEAS_COUNTER = 0,
    MEAS_CT      = 1,
};

// Meter brand (bits 5-4 of HOLD reg 0)
enum LuxMeterBrand : uint8_t {
    METER_CHNT     = 0,
    METER_ACREL    = 1,
    METER_EASTRON  = 2,
    METER_WATTNODE = 3,
};

// Meter type (bits 2-0 of HOLD reg 0)
enum LuxMeterType : uint8_t {
    METER_1PH_1WAY = 0,
    METER_3PH_3WAY = 1,
    METER_3PH_4WAY = 2,
};

// Decoded HOLD_MODEL sub-fields
struct LuxModelConfig {
    LuxBatteryType  battery_type;
    LuxLithiumType  lithium_type;
    LuxMeasurement  measurement;
    LuxMeterBrand   meter_brand;
    bool            us_version;
    LuxMeterType    meter_type;
};

// Unpack HOLD reg 0 raw value into LuxModelConfig
inline LuxModelConfig lux_unpack_model(uint16_t raw) {
    LuxModelConfig m;
    m.battery_type = (LuxBatteryType)((raw >> 12) & 0x0F);
    m.lithium_type = (LuxLithiumType)((raw >>  8) & 0x0F);
    m.measurement  = (LuxMeasurement)((raw >>  6) & 0x03);
    m.meter_brand  = (LuxMeterBrand) ((raw >>  4) & 0x03);
    m.us_version   = (raw >> 3) & 0x01;
    m.meter_type   = (LuxMeterType)  ( raw        & 0x07);
    return m;
}

// Pack LuxModelConfig back into a raw uint16_t for writing to HOLD reg 0
inline uint16_t lux_pack_model(const LuxModelConfig& m) {
    return ((uint16_t)(m.battery_type & 0x0F) << 12)
         | ((uint16_t)(m.lithium_type & 0x0F) <<  8)
         | ((uint16_t)(m.measurement  & 0x03) <<  6)
         | ((uint16_t)(m.meter_brand  & 0x03) <<  4)
         | ((uint16_t)(m.us_version   & 0x01) <<  3)
         | ((uint16_t)(m.meter_type   & 0x07));
}

// Battery type string helpers
inline const char* lux_battery_type_name(LuxBatteryType t) {
    switch (t) {
        case BAT_LEAD_ACID:    return "Lead-Acid";
        case BAT_LITHIUM:      return "Lithium";
        case BAT_NO_SELECTION: return "Not Selected";
        default:               return "Unknown";
    }
}

inline const char* lux_lithium_type_name(LuxLithiumType t) {
    switch (t) {
        case LI_SPI:         return "SPI";
        case LI_CAN_PYLON:   return "CAN-Pylontech";
        case LI_485_PYLON:   return "RS485-Pylontech";
        case LI_CAN_WECO:    return "CAN-WECO";
        case LI_485_WECO:    return "RS485-WECO";
        case LI_CAN_SOLTARO: return "CAN-Soltaro";
        case LI_CAN_ALPHA:   return "CAN-Alpha";
        case LI_CAN_VARTA:   return "CAN-Varta";
        case LI_CAN_GROWATT: return "CAN-Growatt";
        case LI_485_GROWATT: return "RS485-Growatt";
        case LI_CAN_GENERIC: return "CAN-Generic";
        case LI_485_GENERIC: return "RS485-Generic";
        default:             return "Unknown";
    }
}

// Unpack INPUT reg 80 — BatType (high byte) and BatComType (low byte)
inline uint8_t lux_bat_type(uint16_t raw)     { return (raw >> 8) & 0xFF; }
inline uint8_t lux_bat_com_type(uint16_t raw) { return raw & 0xFF; }
// BatComType: 0=CAN, 1=RS485

// Check if a HOLD register is writable
inline bool lux_is_writable(const LuxRegDef* def) {
    return def && (def->access == ACC_RW || def->access == ACC_W);
}

// Validate a raw write value against register min/max
inline bool lux_validate_write(const LuxRegDef* def, int16_t raw_val) {
    if (!def || !lux_is_writable(def)) return false;
    if (def->min_raw != -1 && raw_val < def->min_raw) return false;
    if (def->max_raw != -1 && raw_val > def->max_raw) return false;
    return true;
}