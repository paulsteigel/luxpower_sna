import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import network # Changed from wifi/ethernet to just network
from esphome.const import (
    CONF_ID,
    CONF_HOST,
    CONF_PORT,
    CONF_TIMEOUT,
)

# DEPENDENCIES is now just 'network', which is cleaner
DEPENDENCIES = ["network"]

luxclient_ns = cg.esphome_ns.namespace("luxclient")
LuxClient = luxclient_ns.class_("LuxClient", cg.Component)

# --- REVISED SECTION ---
CONF_DONGLE_SERIAL = "dongle_serial"
# Use 'inverter_serial_number' as you suggested, but we'll still call it
# CONF_INVERTER_SERIAL internally for consistency. The user sees your version.
CONF_INVERTER_SERIAL = "inverter_serial_number"

def validate_serial(value):
    """Validate that the serial number is a 10-character string."""
    value = cv.string(value)
    if len(value) != 10:
        raise cv.Invalid("Serial numbers must be exactly 10 characters long.")
    return value
# --- END REVISED SECTION ---

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LuxClient),
        cv.Required(CONF_HOST): cv.string,
        cv.Optional(CONF_PORT, default=8000): cv.port, # Default updated to 8000
        cv.Required(CONF_DONGLE_SERIAL): validate_serial,
        cv.Required(CONF_INVERTER_SERIAL): validate_serial, # Key is now inverter_serial_number
        cv.Optional(
            CONF_TIMEOUT, default="1000ms"
        ): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_host(config[CONF_HOST]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_dongle_serial(config[CONF_DONGLE_SERIAL]))
    # We use the config key you specified in the YAML
    cg.add(var.set_inverter_serial(config[CONF_INVERTER_SERIAL]))
    cg.add(var.set_read_timeout(config[CONF_TIMEOUT]))

    # This part is no longer needed, as depending on "network" handles it.
    # if wifi.is_connected():
    #     cg.add_library("WiFi", None)
    # elif ethernet.is_connected():
    #     cg.add_library("ETH", None)
