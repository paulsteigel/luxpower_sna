"""
ESPHome button platform for LuxPower SNA.

Supported actions:
  - restart:    writes reg 11 = 128  (inverter reboots)
  - reset_all:  writes reg 11 = 2    (factory reset settings)

Example YAML:

  button:
    - platform: luxpower_sna
      luxpower_sna_id: lux_hub
      name: "Inverter Restart"
      action: restart
      icon: "mdi:restart"

    - platform: luxpower_sna
      luxpower_sna_id: lux_hub
      name: "Reset All Settings"
      action: reset_all
      icon: "mdi:restore"
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_ID, CONF_ICON

from . import (
    luxpower_sna_ns,
    LUXPOWER_SNA_COMPONENT_SCHEMA,
    CONF_LUXPOWER_SNA_ID,
)

CONF_ACTION = "action"

ACTIONS = ["restart", "reset_all"]

LuxpowerSNAButton = luxpower_sna_ns.class_(
    "LuxpowerSNAButton", button.Button, cg.Component
)
ButtonAction = luxpower_sna_ns.enum("LuxpowerSNAButton::Action", is_class=True)

ACTION_MAP = {
    "restart":   ButtonAction.RESTART,
    "reset_all": ButtonAction.RESET_ALL,
}

CONFIG_SCHEMA = cv.All(
    button.button_schema(LuxpowerSNAButton).extend(
        LUXPOWER_SNA_COMPONENT_SCHEMA
    ).extend({
        cv.Required(CONF_ACTION): cv.one_of(*ACTIONS, lower=True),
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    var = await button.new_button(config)

    cg.add(var.set_parent(hub))
    cg.add(var.set_action(ACTION_MAP[config[CONF_ACTION]]))
    cg.add(hub.register_button(var))
