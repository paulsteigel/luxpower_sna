import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID # This one is core and always available

#
# YOU WERE RIGHT. We must define these constants locally because they are not
# guaranteed to be in esphome.const unless another component defines them.
#
CONF_HOST = "host"
CONF_PORT = "port"
CONF_UPDATE_INTERVAL = "update_interval"

DEPENDENCIES = ["wifi"]

luxpower_sna_ns = cg.esphome_ns.namespace('luxpower_sna')
LuxpowerInverterComponent = luxpower_sna_ns.class_('LuxpowerInverterComponent', cg.PollingComponent)

# Custom constants for our component
CONF_DONGLE_SERIAL = "dongle_serial"
CONF_INVERTER_SERIAL_NUMBER = "inverter_serial_number"

# Define the schema for the main luxpower_sna component
# This now uses our locally defined constants and will pass validation.
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LuxpowerInverterComponent),
        cv.Required(CONF_HOST): cv.string,
        cv.Required(CONF_PORT): cv.port,
        cv.Required(CONF_DONGLE_SERIAL): cv.string,
        cv.Required(CONF_INVERTER_SERIAL_NUMBER): cv.string,
    }
).extend(cv.polling_component_schema(CONF_UPDATE_INTERVAL))


# This function generates the C++ code from the YAML configuration
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set the host, port, and serial numbers using our local constants
    cg.add(var.set_host(config[CONF_HOST]))
    cg.add(var.set_port(config[CONF_PORT]))
    
    # Encode serials to byte arrays for C++
    dongle_serial = config[CONF_DONGLE_SERIAL].encode('ascii')
    if len(dongle_serial) != 10:
        raise cv.Invalid("dongle_serial must be 10 characters long")
    cg.add(var.set_dongle_serial(list(dongle_serial)))
    
    inverter_serial = config[CONF_INVERTER_SERIAL_NUMBER].encode('ascii')
    if len(inverter_serial) != 10:
        raise cv.Invalid("inverter_serial_number must be 10 characters long")
    cg.add(var.set_inverter_serial_number(list(inverter_serial)))

    # Add required libraries
    cg.add_library("ESPAsyncTCP", None)
