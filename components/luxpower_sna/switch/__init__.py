# components/luxpower_sna/__init__.py
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import CORE, coroutine_with_priority
from esphome.const import CONF_ID

DEPENDENCIES = ["wifi"]
AUTO_LOAD = ["sensor", "text_sensor", "switch"] # <<< MODIFIED: Add switch
MULTI_CONF = True

luxpower_sna_ns = cg.esphome_ns.namespace("luxpower_sna")
LuxpowerSNAComponent = luxpower_sna_ns.class_("LuxpowerSNAComponent", cg.PollingComponent)
# <<< NEW: Forward declare the switch class
LuxpowerSNASwitch = luxpower_sna_ns.class_("LuxpowerSNASwitch", cg.Component)


CONF_LUXPOWER_SNA_ID = "luxpower_sna_id"

LUXPOWER_SNA_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
    }
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LuxpowerSNAComponent),
            cv.Required("host"): cv.string,
            cv.Required("port"): cv.port,
            cv.Required("dongle_serial"): cv.string,
            cv.Required("inverter_serial_number"): cv.string,
        }
    )
    .extend(cv.polling_component_schema("20s"))
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_host(config["host"]))
    cg.add(var.set_port(config["port"]))

    dongle_serial = config["dongle_serial"]
    if len(dongle_serial) != 10:
        raise cv.Invalid("dongle_serial must be 10 characters long")
    cg.add(var.set_dongle_serial(dongle_serial))

    inverter_serial = config["inverter_serial_number"]
    if len(inverter_serial) != 10:
        raise cv.Invalid("inverter_serial_number must be 10 characters long")
    cg.add(var.set_inverter_serial_number(inverter_serial))
