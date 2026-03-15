"""
LuxPower SNA – Switch platform.

Follows the same pattern as syssi/esphome-jk-bms:
switches are optional named keys inside the hub schema, NOT a platform list.

YAML usage:
  luxpower_sna:
    id: lux_hub
    host: "192.168.1.100"
    ...

  switch:
    - platform: luxpower_sna
      luxpower_sna_id: lux_hub
      normal_or_standby:
        name: "Normal or Standby"
      ac_charge_enable:
        name: "AC Charge Enable"
      feed_in_grid:
        name: "Feed In Grid"
      charge_priority:
        name: "Charge Priority"
      power_backup_enable:
        name: "Power Backup"
      seamless_eps_switching:
        name: "Seamless EPS Switching"
      forced_discharge_enable:
        name: "Forced Discharge"
      charge_last:
        name: "Charge Last"
      enable_peak_shaving:
        name: "Grid Peak Shaving"
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID

from . import luxpower_sna_ns, CONF_LUXPOWER_SNA_ID

LuxpowerSNAComponent = luxpower_sna_ns.class_("LuxpowerSNAComponent", cg.Component)
LuxpowerSNASwitch    = luxpower_sna_ns.class_("LuxpowerSNASwitch", switch.Switch, cg.Component)

# (name, register, bitmask) – mirrors HA integration switch.py
SWITCHES = {
    "normal_or_standby":        ("Normal / Standby",        21, 0x0200),
    "ac_charge_enable":      ("AC Charge Enable",         21, 0x0080),
    "feed_in_grid":          ("Feed In Grid",             21, 0x8000),
    "charge_priority":       ("Charge Priority",          21, 0x0800),
    "power_backup_enable":   ("Power Backup Enable",      21, 0x0001),
    "seamless_eps_switching":("Seamless EPS Switching",   21, 0x0100),
    "forced_discharge_enable":("Force Discharge Enable",  21, 0x0400),
    "dci_enable":            ("DCI Enable",               21, 0x4000),
    "gfci_enable":           ("GFCI Enable",              21, 0x2000),
    "grid_on_power_ss":      ("Grid On Power SS",         21, 0x0040),
    "neutral_detect_enable": ("Neutral Detect Enable",    21, 0x0020),
    "anti_island_enable":    ("Anti Island Enable",       21, 0x0010),
    "drms_enable":           ("DRMS Enable",              21, 0x0004),
    "ovf_load_derate_enable":("OVF Load Derate Enable",   21, 0x0002),
    "charge_last":           ("Charge Last",             110, 0x0010),
    "take_load_together":    ("Take Load Together",      110, 0x0400),
    "enable_peak_shaving":     ("Grid Peak Shaving",       179, 0x0080),
}

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
    **{
        cv.Optional(key): switch.switch_schema(
            LuxpowerSNASwitch,
        ).extend(cv.COMPONENT_SCHEMA)
        for key in SWITCHES
    },
})


async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])

    for key, (default_name, register, bitmask) in SWITCHES.items():
        if key not in config:
            continue
        conf = config[key]
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)
        await switch.register_switch(var, conf)
        cg.add(var.set_parent(hub))
        cg.add(var.set_register(register))
        cg.add(var.set_bitmask(bitmask))
        cg.add(hub.register_switch(var))
