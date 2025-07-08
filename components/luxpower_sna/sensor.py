# components/luxpower_sna/sensor.py
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_VOLTAGE,
    CONF_CURRENT,
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

# --- Import the linking schema and ID from __init__.py, just like jk_bms does ---
from . import LUXPOWER_SNA_COMPONENT_SCHEMA, CONF_LUXPOWER_SNA_ID

# --- Local constants for our custom sensor types ---
CONF_BATTERY_CAPACITY_AH = "battery_capacity_ah"
CONF_POWER_FROM_GRID = "power_from_grid"
CONF_DAILY_SOLAR_GENERATION = "daily_solar_generation"

# --- A dictionary that defines all possible sensors ---
SENSOR_TYPES = {
    CONF_VOLTAGE: sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=1,
    ),
    CONF_CURRENT: sensor.sensor_schema(
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

# --- The CONFIG_SCHEMA for the sensor platform ---
# It starts with the imported linking schema and extends it with all sensor options.
CONFIG_SCHEMA = LUXPOWER_SNA_COMPONENT_SCHEMA.extend(
    {
        **{cv.Optional(key): schema for key, schema in SENSOR_TYPES.items()},
    }
)

async def to_code(config):
    # Get the hub object using the ID from the linking schema
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    
    # Loop through our defined sensor types and create the ones the user configured
    for key in SENSOR_TYPES:
        if key in config:
            conf = config[key]
            sens = await sensor.new_sensor(conf)
            cg.add(hub.add_sensor(key, sens))
