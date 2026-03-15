import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID

from . import luxpower_sna_ns, CONF_LUXPOWER_SNA_ID

LuxpowerSNAComponent = luxpower_sna_ns.class_("LuxpowerSNAComponent", cg.Component)
LuxpowerSNASwitch    = luxpower_sna_ns.class_("LuxpowerSNASwitch", switch.Switch)

CONF_REGISTER = "register"
CONF_BITMASK  = "bitmask"

# cv.All: first schema validates switch entity fields (name, icon, id...),
# second schema validates our custom keys with ALLOW_EXTRA so switch keys
# are not rejected.
CONFIG_SCHEMA = cv.All(
    switch.switch_schema(LuxpowerSNASwitch),
    cv.Schema({
        cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
        cv.Required(CONF_REGISTER):          cv.int_range(min=0, max=239),
        cv.Required(CONF_BITMASK):           cv.hex_int,
    }, extra=cv.ALLOW_EXTRA),
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    var = await switch.new_switch(config)
    cg.add(var.set_parent(hub))
    cg.add(var.set_register(config[CONF_REGISTER]))
    cg.add(var.set_bitmask(config[CONF_BITMASK]))
    cg.add(hub.register_switch(var))
