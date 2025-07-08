import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    CONF_HOST,
    CONF_PORT,
    CONF_UPDATE_INTERVAL,
    CONF_VOLTAGE,
    CONF_CURRENT,
    CONF_POWER,
    CONF_ENERGY,
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

DEPENDENCIES = ["wifi"]

luxpower_sna_ns = cg.esphome_ns.namespace('luxpower_sna')
LuxpowerInverterComponent = luxpower_sna_ns.class_('LuxpowerInverterComponent', cg.PollingComponent)

# --- Local constants for our component ---
CONF_DONGLE_SERIAL = "dongle_serial"
CONF_INVERTER_SERIAL_NUMBER = "inverter_serial_number"

# Sensor names
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_BATTERY_CURRENT = "battery_current"
CONF_BATTERY_CAPACITY_AH = "battery_capacity_ah"
CONF_POWER_FROM_GRID = "power_from_grid"
CONF_DAILY_SOLAR_GENERATION = "daily_solar_generation"


# --- The main configuration schema ---
# All configuration, including sensors, is now defined here.
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LuxpowerInverterComponent),
        cv.Required(CONF_HOST): cv.string,
        cv.Required(CONF_PORT): cv.port,
        cv.Required(CONF_DONGLE_SERIAL): cv.string,
        cv.Required(CONF_INVERTER_SERIAL_NUMBER): cv.string,

        # Sensor schemas are now part of the main component config
        cv.Optional(CONF_BATTERY_VOLTAGE): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=1,
        ),
        cv.Optional(CONF_BATTERY_CURRENT): sensor.sensor_schema(
            unit_of_measurement=UNIT_AMPERE,
            device_class=DEVICE_CLASS_CURRENT,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=1,
        ),
        cv.Optional(CONF_BATTERY_CAPACITY_AH): sensor.sensor_schema(
            unit_of_measurement="Ah", # Using string for compatibility
            icon="mdi:battery-medium",
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=0,
        ),
        cv.Optional(CONF_POWER_FROM_GRID): sensor.sensor_schema(
            unit_of_measurement=UNIT_WATT,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=0,
        ),
        cv.Optional(CONF_DAILY_SOLAR_GENERATION): sensor.sensor_schema(
            unit_of_measurement=UNIT_KILOWATT_HOURS,
            device_class=DEVICE_CLASS_ENERGY,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=1,
        ),
    }
).extend(cv.polling_component_schema(CONF_UPDATE_INTERVAL))


async def to_code(config):
    # This function now receives a single 'config' dictionary
    # containing both the main settings and any sensor settings.
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set main properties
    cg.add(var.set_host(config[CONF_HOST]))
    cg.add(var.set_port(config[CONF_PORT]))
    
    dongle_serial = config[CONF_DONGLE_SERIAL].encode('ascii')
    if len(dongle_serial) != 10:
        raise cv.Invalid("dongle_serial must be 10 characters long")
    cg.add(var.set_dongle_serial(list(dongle_serial)))
    
    inverter_serial = config[CONF_INVERTER_SERIAL_NUMBER].encode('ascii')
    if len(inverter_serial) != 10:
        raise cv.Invalid("inverter_serial_number must be 10 characters long")
    cg.add(var.set_inverter_serial_number(list(inverter_serial)))

    # Register sensors if they are defined in the config
    if CONF_BATTERY_VOLTAGE in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY_VOLTAGE])
        cg.add(var.set_battery_voltage_sensor(sens))
    if CONF_BATTERY_CURRENT in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY_CURRENT])
        cg.add(var.set_battery_current_sensor(sens))
    if CONF_BATTERY_CAPACITY_AH in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY_CAPACITY_AH])
        cg.add(var.set_battery_capacity_ah_sensor(sens))
    if CONF_POWER_FROM_GRID in config:
        sens = await sensor.new_sensor(config[CONF_POWER_FROM_GRID])
        cg.add(var.set_power_from_grid_sensor(sens))
    if CONF_DAILY_SOLAR_GENERATION in config:
        sens = await sensor.new_sensor(config[CONF_DAILY_SOLAR_GENERATION])
        cg.add(var.set_daily_solar_generation_sensor(sens))
        
    # Add required libraries
    cg.add_library("ESPAsyncTCP", None)
