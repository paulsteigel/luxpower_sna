# components/luxpower_sna/__init__.py
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID # Keep CONF_ID, as it seems to import correctly

# Define these constants locally if they cannot be imported from esphome.const
# This is a workaround for the ImportError you are experiencing.
CONF_HOST = "host"
CONF_PORT = "port"
CONF_UPDATE_INTERVAL = "update_interval"
CONF_UNIT_OF_MEASUREMENT = "unit_of_measurement"
CONF_ICON = "icon"
CONF_DEVICE_CLASS = "device_class"
CONF_STATE_CLASS = "state_class"

# Import sensor component (will be used by sensor.py, but needed for schema definition here)
from esphome.components import sensor

# CODEOWNERS = ["@your_github_username"] # Optional: Add your GitHub username here
DEPENDENCIES = ["wifi"] # Assuming reliance on WiFi for TCP connection
MULTI_CONF = True # Allows multiple instances of this component if needed

# Define the C++ component class
luxpower_sna_ns = cg.esphome_ns.namespace('luxpower_sna')
LuxpowerInverterComponent = luxpower_sna_ns.class_('LuxpowerInverterComponent', cg.PollingComponent)

# Define the C++ sensor class (declared here, implemented in luxpower_sna_sensor.h/cpp)
LuxpowerSnaSensor = luxpower_sna_ns.class_('LuxpowerSnaSensor', sensor.Sensor)

# Custom configuration keys for the main component
CONF_DONGLE_SERIAL = "dongle_serial"
CONF_INVERTER_SERIAL_NUMBER = "inverter_serial_number"
CONF_SENSORS = "sensors" # Key for defining multiple sensors
CONF_REGISTER_ADDRESS = "register_address"
CONF_DIVISOR = "divisor"
CONF_IS_SIGNED = "is_signed"

# Schema for an individual Luxpower SNA sensor (moved here for main component to reference)
SENSOR_SCHEMA = sensor.SENSOR_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(LuxpowerSnaSensor), # Each sensor needs an ID
    cv.Required("name"): cv.string, # Sensor name
    cv.Required(CONF_REGISTER_ADDRESS): cv.hex_uint16_t, # Corrected: Register address as hex (e.g., 0x007D)
    cv.Optional(CONF_DIVISOR, default=1.0): cv.float_, # Divisor for scaling (e.g., 10.0 for 0.1 precision)
    cv.Optional(CONF_IS_SIGNED, default=False): cv.boolean, # Whether the register value is signed
}).extend(cv.polling_component_schema("60s")) # Sensors can have their own update interval if desired

# Define the schema for the main luxpower_sna component
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LuxpowerInverterComponent),
        cv.Required(CONF_HOST): cv.string,
        cv.Required(CONF_PORT): cv.port,
        cv.Required(CONF_DONGLE_SERIAL): cv.string,
        cv.Required(CONF_INVERTER_SERIAL_NUMBER): cv.string,
        cv.Optional(CONF_UPDATE_INTERVAL, default="20s"): cv.update_interval,
        # Allow defining multiple sensors under this component
        cv.Optional(CONF_SENSORS): cv.All(
            cv.ensure_list(SENSOR_SCHEMA),
        ),
    }
).extend(cv.COMPONENT_SCHEMA) # Extend with base component schema

# Define the to_code function to generate C++ code from the YAML configuration
async def to_code(config):
    # Create an instance of the LuxpowerInverterComponent class
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set the host, port, and serial numbers
    cg.add(var.set_host(config[CONF_HOST]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_dongle_serial(config[CONF_DONGLE_SERIAL]))
    cg.add(var.set_inverter_serial_number(config[CONF_INVERTER_SERIAL_NUMBER]))

    # Generate code for each sensor defined in the YAML
    if CONF_SENSORS in config:
        for sens_config in config[CONF_SENSORS]:
            # Create a new sensor object
            s = cg.new_Pvariable(sens_config[CONF_ID])
            # Register the sensor with ESPHome's sensor component
            await sensor.register_sensor(s, sens_config)
            # Set the parent component for the sensor
            cg.add(s.set_parent(var))
            # Set sensor-specific properties
            cg.add(s.set_register_address(sens_config[CONF_REGISTER_ADDRESS]))
            cg.add(s.set_divisor(sens_config[CONF_DIVISOR]))
            cg.add(s.set_is_signed(sens_config[CONF_IS_SIGNED]))
            # Add the sensor to the list of sensors managed by the main component
            cg.add(var.add_sensor(s))
