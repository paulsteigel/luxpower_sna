# components/luxpower_sna/__init__.py
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    # CONF_HOST, <--- REMOVED THIS LINE
    CONF_PORT,
    CONF_UPDATE_INTERVAL,
)

# This component will auto-load the sensor and text_sensor platforms.
AUTO_LOAD = ["sensor", "text_sensor"]
MULTI_CONF = True

# --- DEFINE OUR OWN CONSTANTS ---
# This allows the user to write "host:" in their YAML, which is more intuitive.
CONF_HOST = "host" 
CONF_DONGLE_SERIAL = "dongle_serial"
CONF_INVERTER_SERIAL_NUMBER = "inverter_serial_number"

# Create a namespace for our C++ code
luxpower_sna_ns = cg.esphome_ns.namespace("esphome::luxpower_sna")
LuxpowerSNAComponent = luxpower_sna_ns.class_("LuxpowerSNAComponent", cg.PollingComponent)

# Define the ID that sub-components (like sensor) will use to find the hub
CONF_LUXPOWER_SNA_ID = "luxpower_sna_id"

# This is the reusable "linking" schema for sub-components
LUXPOWER_SNA_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
    }
)

# --- This is the schema for the main 'luxpower_sna:' hub component ---
CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LuxpowerSNAComponent),
            # Use our self-defined CONF_HOST constant here
            cv.Required(CONF_HOST): cv.string, 
            cv.Required(CONF_PORT): cv.port,
            cv.Required(CONF_DONGLE_SERIAL): cv.string,
            cv.Required(CONF_INVERTER_SERIAL_NUMBER): cv.string,
        }
    )
    .extend(cv.polling_component_schema("10s"))
)

async def to_code(config):
    # This generates the C++ code for the hub component
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Use our self-defined CONF_HOST to access the config value
    cg.add(var.set_host(config[CONF_HOST])) 
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_dongle_serial(config[CONF_DONGLE_SERIAL]))
    cg.add(var.set_inverter_serial(config[CONF_INVERTER_SERIAL_NUMBER]))
