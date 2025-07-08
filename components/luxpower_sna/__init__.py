# components/luxpower_sna/__init__.py
import esphome.codegen as cg
import esphome.config_validation as cv
# Corrected import: We only import constants we know exist.
from esphome.const import CONF_ID, CONF_UPDATE_INTERVAL

# Automatically load the sensor platform when luxpower_sna is used
AUTO_LOAD = ["sensor"]
MULTI_CONF = True

# --- THE FIX: Define constants locally to support older ESPHome versions ---
CONF_HOST = "host"
CONF_PORT = "port"
CONF_DONGLE_SERIAL = "dongle_serial"
CONF_INVERTER_SERIAL_NUMBER = "inverter_serial_number"
CONF_LUXPOWER_SNA_ID = "luxpower_sna_id"

luxpower_sna_ns = cg.esphome_ns.namespace("luxpower_sna")
LuxpowerSNAComponent = luxpower_sna_ns.class_("LuxpowerSNAComponent", cg.PollingComponent)

# Schema for the main component, using our local constants
CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LuxpowerSNAComponent),
            cv.Required(CONF_HOST): cv.string,
            cv.Required(CONF_PORT): cv.port,
            cv.Required(CONF_DONGLE_SERIAL): cv.string,
            cv.Required(CONF_INVERTER_SERIAL_NUMBER): cv.string,
        }
    )
    .extend(cv.polling_component_schema("20s"))
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_host(config[CONF_HOST]))
    cg.add(var.set_port(config[CONF_PORT]))

    dongle_serial = config[CONF_DONGLE_SERIAL].encode('ascii')
    if len(dongle_serial) != 10:
        raise cv.Invalid("dongle_serial must be 10 characters long")
    cg.add(var.set_dongle_serial(list(dongle_serial)))

    inverter_serial = config[CONF_INVERTER_SERIAL_NUMBER].encode('ascii')
    if len(inverter_serial) != 10:
        raise cv.Invalid("inverter_serial_number must be 10 characters long")
    cg.add(var.set_inverter_serial_number(list(inverter_serial)))

    cg.add_library("ESPAsyncTCP", None)
