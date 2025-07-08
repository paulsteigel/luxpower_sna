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

from . import luxpower_sna_ns, LuxpowerSNAComponent

CONF_LUXPOWER_SNA_ID = "luxpower_sna_id"
CONF_BATTERY_CAPACITY_AH = "battery_capacity_ah"
CONF_POWER_FROM_GRID = "power_from_grid"
CONF_DAILY_SOLAR_GENERATION = "daily_solar_generation"

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

# =========================================================================
# ======================== THE CORRECTED PART =============================
# =========================================================================
#
# We now call sensor.sensor_schema() as a function to get the base,
# then extend it. This is the modern method used by jk_bms.
#
PLATFORM_SCHEMA = cv.All(
    sensor.sensor_schema().extend(
        {
            cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
            **{cv.Optional(key): schema for key, schema in SENSOR_TYPES.items()},
        }
    ),
    cv.has_at_least_one_key(*SENSOR_TYPES),
)
# =========================================================================
# =========================================================================


async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    for key, conf in config.items():
        if key in SENSOR_TYPES:
            sens = await sensor.new_sensor(conf)
            cg.add(hub.add_sensor(key, sens))

