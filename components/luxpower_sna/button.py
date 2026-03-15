"""
LuxPower SNA – Button platform (JK BMS pattern).

YAML usage:
  button:
    - platform: luxpower_sna
      luxpower_sna_id: lux_hub
      restart:
        name: "Inverter Restart"
      reset_all:
        name: "Reset All Settings"
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_ID

from . import luxpower_sna_ns, CONF_LUXPOWER_SNA_ID

LuxpowerSNAComponent = luxpower_sna_ns.class_("LuxpowerSNAComponent", cg.Component)
LuxpowerSNAButton    = luxpower_sna_ns.class_("LuxpowerSNAButton", button.Button, cg.Component)

ButtonAction = luxpower_sna_ns.enum("LuxpowerSNAButton::Action", is_class=True)

# (action_enum, default_icon)
BUTTONS = {
    "restart":   (ButtonAction.RESTART,   "mdi:restart"),
    "reset_all": (ButtonAction.RESET_ALL, "mdi:restore"),
}

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
    **{
        cv.Optional(key): button.button_schema(
            LuxpowerSNAButton,
            icon=icon,
        ).extend(cv.COMPONENT_SCHEMA)
        for key, (_, icon) in BUTTONS.items()
    },
})


async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])

    for key, (action, _) in BUTTONS.items():
        if key not in config:
            continue
        conf = config[key]
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)
        await button.register_button(var, conf)
        cg.add(var.set_parent(hub))
        cg.add(var.set_action(action))
        cg.add(hub.register_button(var))
