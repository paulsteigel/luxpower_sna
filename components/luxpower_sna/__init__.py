import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_HOST, CONF_PORT, CONF_UPDATE_INTERVAL

# "network" does not exist in ESPHome – only "wifi" is needed
DEPENDENCIES = ["wifi"]
AUTO_LOAD = ["sensor", "text_sensor", "switch", "number"]
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
    cv.Required(CONF_HOST):             cv.string,
    cv.Required(CONF_PORT):             cv.port,
    cv.Required(CONF_DONGLE_SERIAL):    cv.string,
    cv.Required(CONF_INVERTER_SERIAL):  cv.string,
    cv.Optional(CONF_UPDATE_INTERVAL,      default="20s"): cv.update_interval,
    cv.Optional(CONF_HOLD_UPDATE_INTERVAL, default="60s"): cv.update_interval,
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_host(config[CONF_HOST]))
    cg.add(var.set_port(config[CONF_PORT]))

    dongle = config[CONF_DONGLE_SERIAL]
    if len(dongle) != 10:
        raise cv.Invalid("dongle_serial must be exactly 10 characters")
    cg.add(var.set_dongle_serial(dongle))

    inverter = config[CONF_INVERTER_SERIAL]
    if len(inverter) != 10:
        raise cv.Invalid("inverter_serial must be exactly 10 characters")
    cg.add(var.set_inverter_serial(inverter))

    cg.add(var.set_update_interval_ms(config[CONF_UPDATE_INTERVAL]))
    cg.add(var.set_hold_update_interval_ms(config[CONF_HOLD_UPDATE_INTERVAL]))
