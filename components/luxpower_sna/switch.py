"""
ESPHome switch platform for LuxPower SNA.

Each switch maps to a bitmask within a holding register.
Pattern: READ hold_reg → apply bitmask → WRITE_SINGLE

Example YAML:
  switch:
    - platform: luxpower_sna
      luxpower_sna_id: lux_hub
      name: "AC Charge Enable"
      register: 21
      bitmask: 0x0080   # LXPPacket.AC_CHARGE_ENABLE
      icon: "mdi:battery-charging"

Pre-defined bitmasks (reg 21) from HA integration LXPPacket.py:
  NORMAL_OR_STANDBY      = 0x0200
  POWER_BACKUP_ENABLE    = 0x0001
  FEED_IN_GRID           = 0x8000
  SEAMLESS_EPS_SWITCHING = 0x0100
  AC_CHARGE_ENABLE       = 0x0080
  CHARGE_PRIORITY        = 0x0800
  FORCED_DISCHARGE_ENABLE= 0x0400
  DCI_ENABLE             = 0x4000
  GFCI_ENABLE            = 0x2000
  GRID_ON_POWER_SS       = 0x0040
  NEUTRAL_DETECT_ENABLE  = 0x0020
  ANTI_ISLAND_ENABLE     = 0x0010
  DRMS_ENABLE            = 0x0004
  OVF_LOAD_DERATE_ENABLE = 0x0002

Register 110:
  TAKE_LOAD_TOGETHER     = 0x0400
  CHARGE_LAST            = 0x0010

Register 179:
  ENABLE_PEAK_SHAVING    = 0x0080
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID, CONF_NAME, CONF_ICON, CONF_RESTORE_MODE

from . import (
    luxpower_sna_ns,
    LUXPOWER_SNA_COMPONENT_SCHEMA,
    CONF_LUXPOWER_SNA_ID,
)

CONF_REGISTER = "register"
CONF_BITMASK  = "bitmask"

LuxpowerSNASwitch = luxpower_sna_ns.class_(
    "LuxpowerSNASwitch", switch.Switch, cg.Component
)

CONFIG_SCHEMA = switch.SWITCH_SCHEMA.extend(
    LUXPOWER_SNA_COMPONENT_SCHEMA
).extend({
    cv.GenerateID(): cv.declare_id(LuxpowerSNASwitch),
    cv.Required(CONF_REGISTER): cv.int_range(min=0, max=239),
    cv.Required(CONF_BITMASK):  cv.hex_int,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await switch.register_switch(var, config)

    cg.add(var.set_parent(hub))
    cg.add(var.set_register(config[CONF_REGISTER]))
    cg.add(var.set_bitmask(config[CONF_BITMASK]))

    cg.add(hub.register_switch(var))
