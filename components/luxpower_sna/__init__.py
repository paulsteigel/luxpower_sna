# esphome_config/custom_components/luxpower_sna/__init__.py

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, text_sensor, socket
from esphome.const import (
    CONF_ID,
    # CONF_HOST and CONF_PORT are not in esphome.const, so we remove them
    CONF_UPDATE_INTERVAL,
)

# This component requires WiFi to make network connections.
DEPENDENCIES = ["wifi"]
# This component provides sensors, so we auto-load the sensor platforms.
AUTO_LOAD = ["sensor", "text_sensor"]
# This allows for configuring multiple inverters in one ESPHome file.
MULTI_CONF = True

# Define the C++ namespace for our component.
luxpower_sna_ns = cg.esphome_ns.namespace("luxpower_sna")
# Define the C++ class for our component, inheriting from PollingComponent.
LuxpowerSNAComponent = luxpower_sna_ns.class_("LuxpowerSNAComponent", cg.PollingComponent)

# Define custom configuration keys to avoid using "magic strings".
CONF_DONGLE_SERIAL = "dongle_serial"
CONF_NUM_BANKS = "num_banks_to_request"
CONF_LUXPOWER_SNA_ID = "luxpower_sna_id"


# This is the main schema that defines the YAML configuration for the component hub.
CONFIG_SCHEMA = (
    cv.Schema(
        {
            # Every component needs an ID.
            cv.GenerateID(): cv.declare_id(LuxpowerSNAComponent),
            # --- CORRECTED HERE ---
            # Use literal strings for host and port as they don't have constants.
            cv.Required("host"): cv.string,
            cv.Required("port"): cv.port,
            # Define our custom required settings.
            # We use a validator to ensure the serial number is the correct length.
            cv.Required(CONF_DONGLE_SERIAL): cv.string_with_length(10, 10),
            # Define our custom optional settings.
            cv.Optional(CONF_NUM_BANKS, default=1): cv.positive_int,
        }
    )
    # Inherit the settings from PollingComponent (like update_interval).
    .extend(cv.polling_component_schema("20s"))
    # Inherit the settings from the socket component to enable networking.
    .extend(socket.socket_component_schema())
)

# This is the schema used by child components (like sensors) to find their parent hub.
LUXPOWER_SNA_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
    }
)

# This function is called by ESPHome to generate the C++ code from the YAML config.
async def to_code(config):
    # Get a C++ variable representing our component.
    var = cg.new_Pvariable(config[CONF_ID])
    # Register this variable as a PollingComponent.
    await cg.register_component(var, config)

    # Register this component as a user of the socket API.
    await socket.register_socket(var, config)

    # --- CORRECTED HERE ---
    # Use literal strings to access the config values.
    cg.add(var.set_host(config["host"]))
    cg.add(var.set_port(config["port"]))

    cg.add(var.set_dongle_serial(config[CONF_DONGLE_SERIAL]))
    cg.add(var.set_num_banks_to_request(config[CONF_NUM_BANKS]))
