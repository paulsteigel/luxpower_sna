# components/luxpower_sna/sensor.py
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    CONF_BATTERY_VOLTAGE,
    CONF_BATTERY_CURRENT,
    DEVICE_CLASS_VOLTAGE,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_ENERGY,
    STATE_CLASS_MEASUREMENT,
    UNIT_VOLT,
    UNIT_AMPERE,
    UNIT_WATT,
    UNIT_KILOWATT_HOURS,
)

from . import luxpower_sna_ns

# --- Local constants for config keys ---
# Define these locally for compatibility with older ESPHome versions
CONF_HOST = "host"
CONF_PORT = "port"
CONF_UPDATE_INTERVAL = "update_interval"
CONF_DONGLE_SERIAL = "dongle_serial"
CONF_INVERTER_SERIAL_NUMBER = "inverter_serial_number"
CONF_LUXPOWER_SNA_ID = "luxpower_sna_id"

CONF_BATTERY_CAPACITY_AH = "battery_capacity_ah"
CONF_POWER_FROM_GRID = "power_from_grid"
CONF_DAILY_SOLAR_GENERATION = "daily_solar_generation"

# --- C++ Classes ---
LuxpowerSNAHub = luxpower_sna_ns.class_("LuxpowerSNAHub", cg.PollingComponent)

# --- Sensor Definitions ---
SENSOR_TYPES = {
    CONF_BATTERY_VOLTAGE: sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=1,
    ),
    CONF_BATTERY_CURRENT: sensor.sensor_schema(
        unit_of_measurement=UNIT_AMPERE,
        device_class=DEVICE_CLASS_CURRENT,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=1,
    ),
    CONF_BATTERY_CAPACITY_AH: sensor.sensor_schema(
        unit_of_measurement="Ah",
        icon="mdi:battery-medium",
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    ),
    CONF_POWER_FROM_GRID: sensor.sensor_schema(
        unit_of_measurement=UNIT_WATT,
        device_class=DEVICE_CLASS_POWER,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    ),
    CONF_DAILY_SOLAR_GENERATION: sensor.sensor_schema(
        unit_of_measurement=UNIT_KILOWATT_HOURS,
        device_class=DEVICE_CLASS_ENERGY,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=1,
    ),
}

# --- Platform Schema ---
# This defines what a `platform: luxpower_sna` block can contain.
# It includes a link to the hub and all possible sensor types.
PLATFORM_SCHEMA = cv.All(
    sensor.PLATFORM_SCHEMA.extend(
        {
            cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAHub),
            **{cv.Optional(key): schema for key, schema in SENSOR_TYPES.items()},
        }
    ).extend(cv.polling_component_schema("60s")),
    cv.has_at_least_one_key(*SENSOR_TYPES),
)

async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    for key, conf in config.items():
        if key in SENSOR_TYPES:
            # Create a new sensor object from the config
            sens = await sensor.new_sensor(conf)
            # Register the sensor with the hub, telling it what data this sensor wants
            cg.add(hub.register_sensor(key, sens))

# --- Hub Schema ---
# This is a separate schema that is processed only once to create the Hub.
# The @cv.only_with_platform decorator is key to making this work.
CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LuxpowerSNAHub),
            cv.Required(CONF_HOST): cv.string,
            cv.Required(CONF_PORT): cv.port,
            cv.Required(CONF_DONGLE_SERIAL): cv.string,
            cv.Required(CONF_INVERTER_SERIAL_NUMBER): cv.string,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_with_platform("sensor"),
)

async def setup_hub(config):
    # This is a bit of a hack in esphome to allow the hub to be configured
    # by a sensor platform. We find the sensor configs and merge them.
    hub_config = {}
    all_sensor_configs = config.get("sensor", [])
    for conf in all_sensor_configs:
        if conf["platform"] == "luxpower_sna":
            hub_config.update(conf)

    var = cg.new_Pvariable(hub_config[CONF_ID])
    await cg.register_component(var, hub_config)
    
    cg.add(var.set_host(hub_config[CONF_HOST]))
    cg.add(var.set_port(hub_config[CONF_PORT]))
    
    dongle_serial = hub_config[CONF_DONGLE_SERIAL].encode('ascii')
    if len(dongle_serial) != 10:
        raise cv.Invalid("dongle_serial must be 10 characters long")
    cg.add(var.set_dongle_serial(list(dongle_serial)))
    
    inverter_serial = hub_config[CONF_INVERTER_SERIAL_NUMBER].encode('ascii')
    if len(inverter_serial) != 10:
        raise cv.Invalid("inverter_serial_number must be 10 characters long")
    cg.add(var.set_inverter_serial_number(list(inverter_serial)))
    
    cg.add_library("ESPAsyncTCP", None)

# Register the hub setup function to be run last
cg.add_build_flag("-DUSE_LUXPOWER_SNA_HUB")
cg.add_global(setup_hub)
