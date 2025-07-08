import esphome.codegen as cg
import esphome.config_validation as cv

#
# This is the crucial part. We are importing these constants from the
# ESPHome framework itself. They are already defined for us.
# This is the standard practice for all ESPHome components.
#
from esphome.const import (
    CONF_ID,
    CONF_HOST,
    CONF_PORT,
    CONF_UPDATE_INTERVAL,
)

# Use the same namespace as in your C++ file
luxpower_sna_ns = cg.esphome_ns.namespace("luxpower_sna")

# Main Component Class, inheriting from PollingComponent
LuxpowerSnaComponent = luxpower_sna_ns.class_("LuxpowerSnaComponent", cg.PollingComponent)

# We define our own constants only for things that are custom to our component
CONF_DONGLE_SERIAL = "dongle_serial"
CONF_INVERTER_SERIAL_NUMBER = "inverter_serial_number"

# Define the main component schema
CONFIG_SCHEMA = cv.Schema(
    {
        # We use the imported CONF_ID here. ESPHome knows this means the 'id:' key.
        cv.GenerateID(): cv.declare_id(LuxpowerSnaComponent),
        # We use the imported CONF_HOST and CONF_PORT for the 'host:' and 'port:' keys.
        cv.Required(CONF_HOST): cv.string,
        cv.Required(CONF_PORT): cv.port,
        # We use our custom constants for our specific keys.
        cv.Required(CONF_DONGLE_SERIAL): cv.string,
        cv.Required(CONF_INVERTER_SERIAL_NUMBER): cv.string,
    }
    # We extend the PollingComponent schema, which uses the imported CONF_UPDATE_INTERVAL
    # to handle the 'update_interval:' key for us automatically.
).extend(cv.polling_component_schema("20s"))

# This function is called by ESPHome to generate the C++ code
async def to_code(config):
    # Register the main component
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set configuration values from YAML
    cg.add(var.set_host(config[CONF_HOST]))
    cg.add(var.set_port(config[CONF_PORT]))
    
    # Convert serials to byte arrays for C++
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
