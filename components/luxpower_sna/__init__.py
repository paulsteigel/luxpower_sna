# custom_components/luxpower_inverter/__init__.py
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PORT, CONF_HOST

# Define the C++ namespace and the main component class.
# This 'luxpower_inverter' namespace will be used in your C++ files.
luxpower_ns = cg.esphome_ns.namespace('luxpower_inverter')
LuxPowerInverterComponent = luxpower_ns.class_('LuxPowerInverterComponent', cg.Component)

# Define the configuration schema for your custom component.
# This dictates what parameters users can specify in their ESPHome YAML file.
CONFIG_SCHEMA = cv.Schema({
    # Generate a unique ID for this component instance in the C++ code.
    cv.GenerateID(): cv.declare_id(LuxPowerInverterComponent),

    # Required: The IP address or hostname of the Luxpower inverter dongle.
    cv.Required(CONF_HOST): cv.string,

    # Optional: The TCP port for communication. Defaults to 8000.
    cv.Optional(CONF_PORT, default=8000): cv.port,

    # Required: The serial number of the Wi-Fi dongle.
    cv.Required("dongle_serial"): cv.string,

    # Required: The serial number of the inverter itself.
    cv.Required("serial_number"): cv.string,

    # Optional: The interval for reading/writing data to the inverter.
    # Defaults to 20 seconds. Ensures a minimum interval of 20 seconds.
    cv.Optional("update_interval", default="20s"): cv.All(
        cv.time_period, cv.Range(min=cv.TimePeriod(seconds=20))
    ),

}).extend(cv.COMPONENT_SCHEMA) # Extend with standard ESPHome component options

# This function generates the C++ code based on the YAML configuration.
def to_code(config):
    # Create a new C++ variable for our main component.
    var = cg.new_Pvariable(config[CONF_ID])

    # Register the component with ESPHome.
    yield cg.register_component(var, config)

    # Pass the configured host and port to the C++ component.
    cg.add(var.set_inverter_host(config[CONF_HOST]))
    cg.add(var.set_inverter_port(config[CONF_PORT]))

    # Pass the dongle and inverter serial numbers to the C++ component.
    cg.add(var.set_dongle_serial(config["dongle_serial"]))
    cg.add(var.set_inverter_serial_number(config["serial_number"]))

    # Pass the update interval to the C++ component.
    cg.add(var.set_update_interval(config["update_interval"]))