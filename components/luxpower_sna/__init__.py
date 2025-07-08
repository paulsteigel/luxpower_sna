import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_HOST,
    CONF_PORT,
    CONF_UPDATE_INTERVAL,
)

# Use the same namespace as in your C++ file
luxpower_sna_ns = cg.esphome_ns.namespace("luxpower_sna")

# Main Component Class
LuxpowerSnaComponent = luxpower_sna_ns.class_("LuxpowerSnaComponent", cg.PollingComponent)

# Custom constants for the config
CONF_DONGLE_SERIAL = "dongle_serial"
CONF_INVERTER_SERIAL_NUMBER = "inverter_serial_number"

# Define the main component schema
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LuxpowerSnaComponent),
        cv.Required(CONF_HOST): cv.string,
        cv.Required(CONF_PORT): cv.port,
        cv.Required(CONF_DONGLE_SERIAL): cv.string,
        cv.Required(CONF_INVERTER_SERIAL_NUMBER): cv.string,
    }
).extend(cv.polling_component_schema("20s")) # Use the polling component schema

# This function is called by ESPHome to generate the C++ code
async def to_code(config):
    # Register the main component
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set configuration values from YAML
    cg.add(var.set_host(config[CONF_HOST]))
    cg.add(var.set_port(config[CONF_PORT]))
    
    # Convert serials to byte arrays for C++
    # The Python code uses these as 10-byte ASCII strings
    dongle_serial = config[CONF_DONGLE_SERIAL].encode('ascii')
    if len(dongle_serial) != 10:
        raise cv.Invalid("dongle_serial must be 10 characters long")
    cg.add(var.set_dongle_serial(list(dongle_serial)))
    
    inverter_serial = config[CONF_INVERTER_SERIAL_NUMBER].encode('ascii')
    if len(inverter_serial) != 10:
        raise cv.Invalid("inverter_serial_number must be 10 characters long")
    cg.add(var.set_inverter_serial(list(inverter_serial)))

    # Add required libraries
    cg.add_library("ESPAsyncTCP", None)
