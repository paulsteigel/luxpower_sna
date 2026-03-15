import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_ID

from . import luxpower_sna_ns, CONF_LUXPOWER_SNA_ID
from . import LuxpowerSNAComponent  # noqa

CONF_ACTION = "action"
ACTIONS     = ["restart", "reset_all"]

LuxpowerSNAButton = luxpower_sna_ns.class_("LuxpowerSNAButton", button.Button)
ButtonAction = luxpower_sna_ns.enum("LuxpowerSNAButton::Action", is_class=True)

ACTION_MAP = {
    "restart":   ButtonAction.RESTART,
    "reset_all": ButtonAction.RESET_ALL,
}

CONFIG_SCHEMA = button.button_schema(LuxpowerSNAButton).extend({
    cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
    cv.Required(CONF_ACTION):            cv.one_of(*ACTIONS, lower=True),
})


async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    var = await button.new_button(config)
    cg.add(var.set_parent(hub))
    cg.add(var.set_action(ACTION_MAP[config[CONF_ACTION]]))
    cg.add(hub.register_button(var))
