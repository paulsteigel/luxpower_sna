import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID

from . import luxpower_sna_ns, CONF_LUXPOWER_SNA_ID

# Re-declare via namespace lookup (idempotent in ESPHome codegen – safe to call
# again here; avoids circular-import issues with importing from __init__.py)
LuxpowerSNAComponent = luxpower_sna_ns.class_("LuxpowerSNAComponent", cg.Component)
LuxpowerSNASwitch    = luxpower_sna_ns.class_("LuxpowerSNASwitch", switch.Switch)

CONF_REGISTER = "register"
CONF_BITMASK  = "bitmask"

CONFIG_SCHEMA = switch.switch_schema(LuxpowerSNASwitch).extend({
    cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
    cv.Required(CONF_REGISTER):          cv.int_range(min=0, max=239),
    cv.Required(CONF_BITMASK):           cv.hex_int,
})


async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    var = await switch.new_switch(config)
    cg.add(var.set_parent(hub))
    cg.add(var.set_register(config[CONF_REGISTER]))
    cg.add(var.set_bitmask(config[CONF_BITMASK]))
    cg.add(hub.register_switch(var))
