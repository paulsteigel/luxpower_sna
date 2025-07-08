# components/luxpower_sna/sensor.py
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
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

from . import luxpower_sna_ns, LuxpowerSNAComponent

# --- Local constants for our custom sensor types ---
CONF_LUXPOWER_SNA_ID = "luxpower_sna_id"
CONF_BATTERY_CAPACITY_AH = "battery_capacity_ah"
CONF_POWER_FROM_GRID = "power_from_grid"
CONF_DAILY_SOLAR_GENERATION = "daily_solar_generation"

# --- A dictionary that defines all possible sensors ---
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

# --- Schema for the 'sensor:' platform ---
PLATFORM_SCHEMA = cv.All(
    sensor.PLATFORM_SCHEMA.extend(
        {
            # This is the key that links to the hub
            cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
            # Make all our defined sensors optional
            **{cv.Optional(key): schema for key, schema in SENSOR_TYPES.items()},
        }
    ),
    # Ensure at least one sensor is defined
    cv.has_at_least_one_key(*SENSOR_TYPES),
)

async def to_code(config):
    # Get the hub object using the ID
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    
    # Loop through the configuration and create the sensors that were defined
    for key, conf in config.items():
        if key in SENSOR_TYPES:
            sens = await sensor.new_sensor(conf)
            # Register the sensor with the hub, e.g., add_sensor("battery_voltage", sens_obj)
            cg.add(hub.add_sensor(key, sens))
