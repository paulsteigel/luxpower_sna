"""
ESPHome time-slot platform for LuxPower SNA.

Maps a single hold register to an HH:MM time value.
Register encoding (from HA LXPPacket.py):
    raw_value = minute * 256 + hour
    (i.e. low byte = hour, high byte = minute)

Since ESPHome has no native time-input entity, this platform exposes each
slot as a text entity ("HH:MM") backed by a template text component.
The LuxpowerSNATime C++ object updates the text entity state when hold
registers refresh, and writes back to the inverter when the user changes
the value in HA.

Usage in YAML:

  text:
    - platform: template
      id: ac_charge_start1
      name: "AC Charge Start 1"
      icon: "mdi:timer-outline"
      mode: text
      optimistic: true
      restore_value: true
      initial_value: "00:00"
      on_value:
        then:
          - lambda: id(lux_ac_start1).set_time(x);

  # The LuxpowerSNATime helper object (no UI itself, just the register binding):
  luxpower_sna_time:
    - id: lux_ac_start1
      luxpower_sna_id: lux_hub
      register: 68

For convenience, a standalone platform is also provided that creates
the text entity AND the time helper in one block:

  time:
    - platform: luxpower_sna
      luxpower_sna_id: lux_hub
      name: "AC Charge Start 1"
      register: 68
      icon: "mdi:timer-outline"
      restore_value: true
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text
from esphome.const import CONF_ID, CONF_NAME, CONF_ICON

from . import (
    luxpower_sna_ns,
    LUXPOWER_SNA_COMPONENT_SCHEMA,
    CONF_LUXPOWER_SNA_ID,
)

CONF_REGISTER     = "register"
CONF_RESTORE_VALUE = "restore_value"

LuxpowerSNATime = luxpower_sna_ns.class_(
    "LuxpowerSNATime", cg.Component
)

CONFIG_SCHEMA = cv.All(
    LUXPOWER_SNA_COMPONENT_SCHEMA.extend({
        cv.GenerateID():               cv.declare_id(LuxpowerSNATime),
        cv.Required(CONF_REGISTER):    cv.int_range(min=0, max=239),
        cv.Required(CONF_NAME):        cv.string,
        cv.Optional(CONF_ICON, default="mdi:timer-outline"): cv.icon,
        cv.Optional(CONF_RESTORE_VALUE, default=True): cv.boolean,
    }).extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])

    # Create the LuxpowerSNATime helper (register ↔ hub binding)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_parent(hub))
    cg.add(var.set_register(config[CONF_REGISTER]))
    cg.add(var.set_name(config[CONF_NAME]))
    cg.add(hub.register_time(var))
