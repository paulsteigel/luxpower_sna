# esphome_config/custom_components/luxpower_sna/sensor.py

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_CELSIUS,
    UNIT_HERTZ,
    UNIT_KILOWATT_HOURS,
    UNIT_PERCENT,
    UNIT_VOLT,
    UNIT_WATT,
)

from . import luxpower_sna_ns, LuxpowerSNAComponent, LUXPOWER_SNA_COMPONENT_SCHEMA, CONF_LUXPOWER_SNA_ID

# --- A dictionary that defines all possible sensors ---
# The keys here are the keys you will use in your YAML
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
CONFIG_SCHEMA = cv.All(
    sensor.SENSOR_PLATFORM_SCHEMA.extend(
        {
            cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
            **{cv.Optional(key): schema for key, schema in SENSOR_TYPES.items()},
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.has_at_least_one_key(*SENSOR_TYPES.keys()),
)

async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    
    for yaml_key, schema in SENSOR_TYPES.items():
        if yaml_key in config:
            conf = config[yaml_key]
            # e.g. for "pv1_voltage", the C++ setter will be "set_pv1_voltage_sensor"
            setter = f"set_{yaml_key}_sensor" 
            sens = await sensor.new_sensor(conf)
            cg.add(getattr(hub, setter)(sens))
