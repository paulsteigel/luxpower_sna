import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ADDRESS,
    CONF_ID,
    CONF_PORT,
    CONF_UPDATE_INTERVAL,
)

# Define custom configuration keys
CONF_DONGLE_SERIAL = "dongle_serial"
CONF_INVERTER_SERIAL = "inverter_serial"

# Create a namespace for our C++ code
luxpower_sna_ns = cg.esphome_ns.namespace("esphome::luxpower_sna")
LuxpowerSNAComponent = luxpower_sna_ns.class_("LuxpowerSNAComponent", cg.PollingComponent)

# Define the configuration schema for the main component
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LuxpowerSNAComponent),
        cv.Required(CONF_ADDRESS): cv.string,
        cv.Required(CONF_PORT): cv.port,
        cv.Required(CONF_DONGLE_SERIAL): cv.string,
        cv.Required(CONF_INVERTER_SERIAL): cv.string,
        cv.Optional(CONF_UPDATE_INTERVAL, default="10s"): cv.update_interval,
    }
).extend(cv.polling_component_schema("10s"))

# The function that generates the C++ code
async def to_code(config):
    hub = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(hub, config)
    
    cg.add(hub.set_address(config[CONF_ADDRESS]))
    cg.add(hub.set_port(config[CONF_PORT]))
    cg.add(hub.set_dongle_serial(config[CONF_DONGLE_SERIAL]))
    cg.add(hub.set_inverter_serial(config[CONF_INVERTER_SERIAL]))
