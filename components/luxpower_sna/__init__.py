import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_UPDATE_INTERVAL
# CONF_HOST / CONF_PORT do not exist in esphome.const – define locally
CONF_HOST             = "host"
CONF_PORT             = "port"

DEPENDENCIES = ["wifi"]
AUTO_LOAD = ["sensor", "text_sensor", "switch", "number", "button"]
MULTI_CONF = True

luxpower_sna_ns = cg.esphome_ns.namespace("luxpower_sna")
LuxpowerSNAComponent = luxpower_sna_ns.class_("LuxpowerSNAComponent", cg.Component)

CONF_LUXPOWER_SNA_ID      = "luxpower_sna_id"
CONF_DONGLE_SERIAL        = "dongle_serial"
CONF_INVERTER_SERIAL      = "inverter_serial"
CONF_HOLD_UPDATE_INTERVAL = "hold_update_interval"

LUXPOWER_SNA_COMPONENT_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
})

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID():                    cv.declare_id(LuxpowerSNAComponent),
    cv.Optional(CONF_HOST,            default=""): cv.string,
    cv.Optional(CONF_PORT,            default=8000): cv.port,
    cv.Optional(CONF_DONGLE_SERIAL,   default=""): cv.string,
    cv.Optional(CONF_INVERTER_SERIAL, default=""): cv.string,
    cv.Optional(CONF_UPDATE_INTERVAL,      default="20s"): cv.update_interval,
    cv.Optional(CONF_HOLD_UPDATE_INTERVAL, default="60s"): cv.update_interval,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_host(config[CONF_HOST]))
    cg.add(var.set_port(config[CONF_PORT]))

    dongle = config[CONF_DONGLE_SERIAL]
    if dongle and len(dongle) != 10:
        raise cv.Invalid("dongle_serial must be exactly 10 characters")
    cg.add(var.set_dongle_serial(dongle))

    inverter = config[CONF_INVERTER_SERIAL]
    if inverter and len(inverter) != 10:
        raise cv.Invalid("inverter_serial must be exactly 10 characters")
    cg.add(var.set_inverter_serial(inverter))

    cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL]))
    cg.add(var.set_hold_update_interval(config[CONF_HOLD_UPDATE_INTERVAL]))
