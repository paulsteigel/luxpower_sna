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

from . import LuxpowerSNAComponent, CONF_LUXPOWER_SNA_ID

# Sensor types
CONF_BATTERY_CAPACITY_AH = "battery_capacity_ah"
CONF_POWER_FROM_GRID = "power_from_grid"
CONF_DAILY_SOLAR_GENERATION = "daily_solar_generation"

SENSORS = {
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

# Schema for the sensor platform, linking back to the hub
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
        **{cv.Optional(key): schema for key, schema in SENSORS.items()},
    }
)

async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    for key, conf in config.items():
        if key in SENSORS:
            # Create a new sensor
            sens = await sensor.new_sensor(conf)
            # Call the setter on the hub, e.g., hub->set_battery_voltage_sensor(sens)
            cg.add(getattr(hub, f"set_{key}_sensor")(sens))
