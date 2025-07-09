# esphome_config/custom_components/luxpower_sna/__init__.py

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Define the namespace for our C++ code
luxpower_sna_ns = cg.esphome_ns.namespace("esphome::luxpower_sna")
LuxpowerSNAComponent = luxpower_sna_ns.class_("LuxpowerSNAComponent", cg.PollingComponent)

# Define a key for linking the sensor platform to this main component
CONF_LUXPOWER_SNA_ID = "luxpower_sna_id"

# Define the YAML schema for the main `luxpower_sna:` block
# Using string literals "host" and "port" is the correct way
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LuxpowerSNAComponent),
        cv.Required("host"): cv.string,
        cv.Required("port"): cv.port,
        cv.Required("dongle_serial"): cv.string,
    }
).extend(cv.polling_component_schema("20s")) # Also corrected the update interval here to match your YAML

# Define a separate schema that other platforms (like sensor) can use to link back
LUXPOWER_SNA_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
    }
)

# The function to generate the C++ code for the main component
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Use string literals to access the config values
    cg.add(var.set_host(config["host"]))
    cg.add(var.set_port(config["port"]))
    cg.add(var.set_dongle_serial(config["dongle_serial"]))

