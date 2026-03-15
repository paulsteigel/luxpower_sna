import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_ID, CONF_MAX_VALUE, CONF_MIN_VALUE, CONF_STEP

from . import luxpower_sna_ns, CONF_LUXPOWER_SNA_ID
from . import LuxpowerSNAComponent  # noqa

CONF_REGISTER = "register"
CONF_BITMASK  = "bitmask"
CONF_BITSHIFT = "bitshift"
CONF_DIVISOR  = "divisor"
CONF_SIGNED   = "signed"

LuxpowerSNANumber = luxpower_sna_ns.class_("LuxpowerSNANumber", number.Number)

CONFIG_SCHEMA = number.number_schema(LuxpowerSNANumber).extend({
    cv.GenerateID(CONF_LUXPOWER_SNA_ID):           cv.use_id(LuxpowerSNAComponent),
    cv.Required(CONF_REGISTER):                    cv.int_range(min=0, max=239),
    cv.Required(CONF_MIN_VALUE):                   cv.float_,
    cv.Required(CONF_MAX_VALUE):                   cv.float_,
    cv.Optional(CONF_STEP,     default=1.0):       cv.positive_float,
    cv.Optional(CONF_BITMASK,  default=0xFFFF):    cv.hex_int,
    cv.Optional(CONF_BITSHIFT, default=0):         cv.int_range(min=0, max=15),
    cv.Optional(CONF_DIVISOR,  default=1):         cv.positive_int,
    cv.Optional(CONF_SIGNED,   default=False):     cv.boolean,
})


async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    var = await number.new_number(
        config,
        min_value=config[CONF_MIN_VALUE],
        max_value=config[CONF_MAX_VALUE],
        step=config[CONF_STEP],
    )
    cg.add(var.set_parent(hub))
    cg.add(var.set_register(config[CONF_REGISTER]))
    cg.add(var.set_bitmask(config[CONF_BITMASK]))
    cg.add(var.set_bitshift(config[CONF_BITSHIFT]))
    cg.add(var.set_divisor(config[CONF_DIVISOR]))
    cg.add(var.set_signed(config[CONF_SIGNED]))
    cg.add(hub.register_number(var))
