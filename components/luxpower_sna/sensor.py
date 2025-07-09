    # esphome_config/custom_components/luxpower_sna/sensor.py

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
    UNIT_VOLT,
    UNIT_WATT,
)

# --- Import the linking schema and ID from __init__.py ---
from . import LUXPOWER_SNA_COMPONENT_SCHEMA, CONF_LUXPOWER_SNA_ID

# A dictionary that defines all possible sensors.
# The keys here MUST match the keys in the YAML and will be used to generate C++ function names.
SENSOR_TYPES = {
    "pv1_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:solar-panel"),
    "pv1_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:solar-power"),
    "battery_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "charge_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:battery-plus"),
    "discharge_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:battery-minus"),
    "inverter_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:power-plug"),
    "soc": sensor.sensor_schema(unit_of_measurement=UNIT_PERCENT, icon="mdi:battery-high", state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
}

# --- The CONFIG_SCHEMA for the sensor platform ---
# THIS IS THE CORRECTED LINE: Use sensor.PLATFORM_SCHEMA
CONFIG_SCHEMA = sensor.PLATFORM_SCHEMA.extend(
    {
        cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
        **{cv.Optional(key): schema for key, schema in SENSOR_TYPES.items()},
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    
    # Simplified loop: Iterate directly over the sensor types we defined.
    for yaml_key, schema in SENSOR_TYPES.items():
        if yaml_key in config:
            conf = config[yaml_key]
            # e.g., for "pv1_voltage", the C++ setter will be "set_pv1_voltage_sensor"
            setter = f"set_{yaml_key}_sensor" 
            sens = await sensor.new_sensor(conf)
            cg.add(getattr(hub, setter)(sens))

