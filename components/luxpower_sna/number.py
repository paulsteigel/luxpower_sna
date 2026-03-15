"""
LuxPower SNA – Number platform (JK BMS pattern).

YAML usage:
  number:
    - platform: luxpower_sna
      luxpower_sna_id: lux_hub
      charge_power_percent:
        name: "System Charge Power Rate"
      discharge_power_percent:
        name: "System Discharge Power Rate"
      ac_charge_power_percent:
        name: "AC Charge Power Rate"
      ac_charge_soc_limit:
        name: "AC Charge Battery Level"
      discharge_cutoff_soc:
        name: "On-grid Discharge Cut-off SOC"
      forced_discharge_power_percent:
        name: "Forced Discharge Power Rate"
      priority_charge_rate:
        name: "Priority Charge Rate"
      priority_charge_soc:
        name: "Priority Charge Level"
      charge_voltage:
        name: "Charge Voltage"
      discharge_cutoff_voltage:
        name: "Discharge Cut-off Voltage"
      ct_clamp_offset:
        name: "CT Clamp Offset"
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_ID

from . import luxpower_sna_ns, CONF_LUXPOWER_SNA_ID

LuxpowerSNAComponent = luxpower_sna_ns.class_("LuxpowerSNAComponent", cg.Component)
LuxpowerSNANumber    = luxpower_sna_ns.class_("LuxpowerSNANumber", number.Number, cg.Component)

# (register, min, max, step, divisor, bitmask, bitshift, signed, unit)
NUMBERS = {
    # Percentage registers (LPNE – raw = %)
    "charge_power_percent":          (64,  0,   100, 1,   1,   0xFFFF, 0, False, "%"),
    "discharge_power_percent":       (65,  0,   100, 1,   1,   0xFFFF, 0, False, "%"),
    "ac_charge_power_percent":       (66,  0,   100, 1,   1,   0xFFFF, 0, False, "%"),
    "ac_charge_soc_limit":           (67,  0,   100, 1,   1,   0xFFFF, 0, False, "%"),
    "priority_charge_rate":          (74,  0,   100, 1,   1,   0xFFFF, 0, False, "%"),
    "priority_charge_soc":           (75,  0,   100, 1,   1,   0xFFFF, 0, False, "%"),
    "forced_discharge_power_percent":(82,  0,   100, 1,   1,   0xFFFF, 0, False, "%"),
    "forced_discharge_soc_limit":    (83,  0,   100, 1,   1,   0xFFFF, 0, False, "%"),
    "feed_in_grid_power_percent":    (103, 0,   100, 1,   1,   0xFFFF, 0, False, "%"),
    "discharge_cutoff_soc":          (105, 0,   100, 1,   1,   0xFFFF, 0, False, "%"),
    "offgrid_discharge_cutoff_soc":  (125, 0,   100, 1,   1,   0xFFFF, 0, False, "%"),
    "ac_charge_start_soc":           (160, 0,   100, 1,   1,   0xFFFF, 0, False, "%"),
    "ac_charge_end_soc":             (161, 0,   100, 1,   1,   0xFFFF, 0, False, "%"),
    "battery_warning_soc":           (164, 0,   100, 1,   1,   0xFFFF, 0, False, "%"),
    "battery_warning_recovery_soc":  (165, 0,   100, 1,   1,   0xFFFF, 0, False, "%"),
    # Voltage registers (LDTE – raw = 0.1V)
    "charge_voltage":                (99,  40,  60,  0.1, 10,  0xFFFF, 0, False, "V"),
    "discharge_cutoff_voltage":      (100, 40,  60,  0.1, 10,  0xFFFF, 0, False, "V"),
    "ac_charge_start_voltage":       (158, 40,  60,  0.1, 10,  0xFFFF, 0, False, "V"),
    "ac_charge_end_voltage":         (159, 40,  60,  0.1, 10,  0xFFFF, 0, False, "V"),
    "battery_warning_voltage":       (162, 40,  60,  0.1, 10,  0xFFFF, 0, False, "V"),
    "battery_warning_recovery_volt": (163, 40,  60,  0.1, 10,  0xFFFF, 0, False, "V"),
    "ongrid_eod_voltage":            (169, 40,  60,  0.1, 10,  0xFFFF, 0, False, "V"),
    # Signed (CT clamp, stored as 0.1W units)
    "ct_clamp_offset":               (119, -19.9, 19.9, 0.1, 10, 0xFFFF, 0, True,  "W"),
    # Current registers (LNNE)
    "charge_current_limit":          (101, 0,  255,  1,   1,   0xFFFF, 0, False, "A"),
    "discharge_current_limit":       (102, 0,  255,  1,   1,   0xFFFF, 0, False, "A"),
    "ac_charge_battery_current":     (168, 0,  255,  1,   1,   0xFFFF, 0, False, "A"),
    # Grid Peak-Shaving power (kW, LDTE)
    "grid_peak_shaving_power":       (206, 0,  90,   0.1, 10,  0xFFFF, 0, False, "kW"),
}

def _number_schema(reg, min_v, max_v, step, divisor, bitmask, bitshift, signed, unit):
    return number.number_schema(LuxpowerSNANumber).extend({
        cv.Optional("divisor",  default=divisor):  cv.positive_int,
        cv.Optional("bitmask",  default=bitmask):  cv.hex_int,
        cv.Optional("bitshift", default=bitshift): cv.int_range(min=0, max=15),
        cv.Optional("signed",   default=signed):   cv.boolean,
    }).extend(cv.COMPONENT_SCHEMA)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
    **{
        cv.Optional(key): _number_schema(*params)
        for key, params in NUMBERS.items()
    },
})


async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])

    for key, (register, min_v, max_v, step, divisor, bitmask, bitshift, signed, unit) in NUMBERS.items():
        if key not in config:
            continue
        conf = config[key]
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)
        await number.register_number(
            var, conf,
            min_value=min_v,
            max_value=max_v,
            step=step,
        )
        cg.add(var.set_parent(hub))
        cg.add(var.set_register(register))
        cg.add(var.set_bitmask(conf.get("bitmask", bitmask)))
        cg.add(var.set_bitshift(conf.get("bitshift", bitshift)))
        cg.add(var.set_divisor(conf.get("divisor", divisor)))
        cg.add(var.set_signed(conf.get("signed", signed)))
        cg.add(hub.register_number(var))
