# components/luxpower_sna/sensor.py
import esphome.codegen as cg
import esphome.config_validation as cv  # We need cv for PLATFORM_SCHEMA
from esphome.components import sensor
from esphome.const import (
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

from . import LUXPOWER_SNA_COMPONENT_SCHEMA, CONF_LUXPOWER_SNA_ID

# ... (YAML_TO_C_NAMES and SENSOR_TYPES dictionaries remain unchanged) ...
YAML_TO_C_NAMES = {
    "voltage": "battery_voltage", "soc": "soc", "battery_power": "battery_power",
    "charge_power": "charge_power", "discharge_power": "discharge_power", "pv_power": "pv_power",
    "inverter_power": "inverter_power", "grid_power": "grid_power", "load_power": "load_power",
    "eps_power": "eps_power", "pv1_voltage": "pv1_voltage", "pv1_power": "pv1_power",
    "pv2_voltage": "pv2_voltage", "pv2_power": "pv2_power", "grid_voltage": "grid_voltage",
    "grid_frequency": "grid_frequency", "power_factor": "power_factor", "eps_voltage": "eps_voltage",
    "eps_frequency": "eps_frequency", "pv_today": "pv_today", "inverter_today": "inverter_today",
    "charge_today": "charge_today", "discharge_today": "discharge_today",
    "grid_export_today": "grid_export_today", "grid_import_today": "grid_import_today",
    "load_today": "load_today", "eps_today": "eps_today", "inverter_temp": "inverter_temp",
    "radiator_temp": "radiator_temp", "battery_temp": "battery_temp", "status_code": "status_code",
}
SENSOR_TYPES = {
    "voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "soc": sensor.sensor_schema(unit_of_measurement=UNIT_PERCENT, icon="mdi:battery-high", state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "battery_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "charge_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:battery-plus"),
    "discharge_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:battery-minus"),
    "pv_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:solar-power"),
    "inverter_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:power-plug"),
    "grid_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:transmission-tower"),
    "load_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:home-lightning-bolt"),
    "eps_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:power-plug-off"),
    "pv1_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:solar-panel"),
    "pv1_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:solar-power"),
    "pv2_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:solar-panel"),
    "pv2_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:solar-power"),
    "grid_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "grid_frequency": sensor.sensor_schema(unit_of_measurement=UNIT_HERTZ, device_class=DEVICE_CLASS_FREQUENCY, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=2),
    "power_factor": sensor.sensor_schema(icon="mdi:angle-acute", state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=3),
    "eps_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "eps_frequency": sensor.sensor_schema(unit_of_measurement=UNIT_HERTZ, device_class=DEVICE_CLASS_FREQUENCY, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=2),
    "pv_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "inverter_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "charge_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "discharge_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "grid_export_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "grid_import_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "load_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "eps_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "inverter_temp": sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, device_class=DEVICE_CLASS_TEMPERATURE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "radiator_temp": sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, device_class=DEVICE_CLASS_TEMPERATURE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "battery_temp": sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, device_class=DEVICE_CLASS_TEMPERATURE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "status_code": sensor.sensor_schema(icon="mdi:information-outline", accuracy_decimals=0),
}


# The CONFIG_SCHEMA for the sensor platform
# --- THE FIX IS HERE ---
CONFIG_SCHEMA = cv.All(
    cv.PLATFORM_SCHEMA.extend(  # Changed from sensor.PLATFORM_SCHEMA
        {
            # Add all the possible sensor keys as Optional
            **{cv.Optional(key): schema for key, schema in SENSOR_TYPES.items()},
        }
    ).extend(LUXPOWER_SNA_COMPONENT_SCHEMA), # Extend with the linking schema
    # This validation ensures that the user provides at least one sensor key.
    cv.has_at_least_one_key(*SENSOR_TYPES.keys()),
)

async def to_code(config):
    # Get the hub object using the ID from the linking schema
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    
    # Loop through our defined sensor types and create the ones the user configured
    for yaml_key, c_name in YAML_TO_C_NAMES.items():
        if yaml_key in config:
            conf = config[yaml_key]
            sens = await sensor.new_sensor(conf)
            cg.add(getattr(hub, f"set_{c_name}_sensor")(sens))
