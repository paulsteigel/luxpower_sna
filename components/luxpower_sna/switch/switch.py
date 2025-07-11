# components/luxpower_sna/switch.py
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID, ICON_POWER

from . import (
    LUXPOWER_SNA_COMPONENT_SCHEMA,
    CONF_LUXPOWER_SNA_ID,
    LuxpowerSNASwitch,
    luxpower_sna_ns,
)

# Bitmasks from LXPPacket.py
# Register 21
NORMAL_OR_STANDBY = 1 << 9
POWER_BACKUP_ENABLE = 1 << 0
FEED_IN_GRID = 1 << 15
AC_CHARGE_ENABLE = 1 << 7
CHARGE_PRIORITY = 1 << 11
FORCED_DISCHARGE_ENABLE = 1 << 10
# Register 110
CHARGE_LAST = 1 << 4
# Register 179
ENABLE_PEAK_SHAVING = 1 << 7


SWITCH_TYPES = {
    "ac_charge_enable": (21, AC_CHARGE_ENABLE, "mdi:battery-charging-high"),
    "feed_in_grid": (21, FEED_IN_GRID, "mdi:transmission-tower-export"),
    "charge_priority": (21, CHARGE_PRIORITY, "mdi:battery-clock"),
    "forced_discharge_enable": (21, FORCED_DISCHARGE_ENABLE, "mdi:battery-minus"),
    "power_backup_enable": (21, POWER_BACKUP_ENABLE, "mdi:power-plug-off"),
    "normal_or_standby": (21, NORMAL_OR_STANDBY, ICON_POWER),
    "charge_last": (110, CHARGE_LAST, "mdi:battery-arrow-down"),
    "grid_peak_shaving": (179, ENABLE_PEAK_SHAVING, "mdi:chart-bell-curve"),
}

CONFIG_SCHEMA = LUXPOWER_SNA_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(key): switch.switch_schema(
            LuxpowerSNASwitch,
            icon=icon,
        )
        for key, (_, _, icon) in SWITCH_TYPES.items()
    }
)

async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    for key, (register, bitmask, _) in SWITCH_TYPES.items():
        if key in config:
            conf = config[key]
            var = cg.new_Pvariable(conf[CONF_ID])
            await cg.register_component(var, conf)
            await switch.register_switch(var, conf)

            # Link child to parent
            cg.add(var.set_parent(hub))
            cg.add(var.set_register_address(register))
            cg.add(var.set_bitmask(bitmask))
            
            # Link parent to child (for state updates)
            cg.add(getattr(hub, f"set_{key}_switch")(var))
