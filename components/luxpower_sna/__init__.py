import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
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
    UNIT_AMPERE_HOURS,
)

# Local constants
CONF_HOST = "host"
CONF_PORT = "port"
CONF_UPDATE_INTERVAL = "update_interval"
CONF_DONGLE_SERIAL = "dongle_serial"
CONF_INVERTER_SERIAL_NUMBER = "inverter_serial_number"

DEPENDENCIES = ["wifi"]

luxpower_sna_ns = cg.esphome_ns.namespace('luxpower_sna')
LuxpowerInverterComponent = luxpower_sna_ns.class_('LuxpowerInverterComponent', cg.PollingComponent)

# Custom sensor names
CONF_LUXPOWER_SNA_ID = "luxpower_sna_id"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_BATTERY_CURRENT = "battery_current"
CONF_BATTERY_CAPACITY_AH = "battery_capacity_ah"
CONF_POWER_FROM_GRID = "power_from_grid"
CONF_DAILY_SOLAR_GENERATION = "daily_solar_generation"


# Main component schema
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LuxpowerInverterComponent),
        cv.Required(CONF_HOST): cv.string,
        cv.Required(CONF_PORT): cv.port,
        cv.Required(CONF_DONGLE_SERIAL): cv.string,
        cv.Required(CONF_INVERTER_SERIAL_NUMBER): cv.string,
    }
).extend(cv.polling_component_schema(CONF_UPDATE_INTERVAL))

# Sensor platform schema
SENSORS_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerInverterComponent),
    
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
        unit_of_measurement=UNIT_AMPERE_HOURS,
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
        state_class=STATE_CLASS_MEASUREMENT, # Use STATE_CLASS_TOTAL_INCREASING in newer ESPHome
        accuracy_decimals=1,
    ),
})

# Combine schemas for validation
CONFIG_SCHEMA = cv.All(
    CONFIG_SCHEMA.extend({cv.Optional("sensor"): SENSORS_SCHEMA}),
)


async def to_code(config):
    # Process the main component config
    if 'luxpower_sna' in config:
        parent_config = config['luxpower_sna']
        var = cg.new_Pvariable(parent_config[CONF_ID])
        await cg.register_component(var, parent_config)

        cg.add(var.set_host(parent_config[CONF_HOST]))
        cg.add(var.set_port(parent_config[CONF_PORT]))
        
        dongle_serial = parent_config[CONF_DONGLE_SERIAL].encode('ascii')
        if len(dongle_serial) != 10:
            raise cv.Invalid("dongle_serial must be 10 characters long")
        cg.add(var.set_dongle_serial(list(dongle_serial)))
        
        inverter_serial = parent_config[CONF_INVERTER_SERIAL_NUMBER].encode('ascii')
        if len(inverter_serial) != 10:
            raise cv.Invalid("inverter_serial_number must be 10 characters long")
        cg.add(var.set_inverter_serial_number(list(inverter_serial)))

        # Process the sensor configs
        if "sensor" in config:
            sensor_config = config["sensor"]
            paren = await cg.get_variable(sensor_config[CONF_LUXPOWER_SNA_ID])
            
            # Map Python config to C++ setters
            if CONF_BATTERY_VOLTAGE in sensor_config:
                sens = await sensor.new_sensor(sensor_config[CONF_BATTERY_VOLTAGE])
                cg.add(paren.set_battery_voltage_sensor(sens))
            if CONF_BATTERY_CURRENT in sensor_config:
                sens = await sensor.new_sensor(sensor_config[CONF_BATTERY_CURRENT])
                cg.add(paren.set_battery_current_sensor(sens))
            if CONF_BATTERY_CAPACITY_AH in sensor_config:
                sens = await sensor.new_sensor(sensor_config[CONF_BATTERY_CAPACITY_AH])
                cg.add(paren.set_battery_capacity_ah_sensor(sens))
            if CONF_POWER_FROM_GRID in sensor_config:
                sens = await sensor.new_sensor(sensor_config[CONF_POWER_FROM_GRID])
                cg.add(paren.set_power_from_grid_sensor(sens))
            if CONF_DAILY_SOLAR_GENERATION in sensor_config:
                sens = await sensor.new_sensor(sensor_config[CONF_DAILY_SOLAR_GENERATION])
                cg.add(paren.set_daily_solar_generation_sensor(sens))

    # Add required libraries
    cg.add_library("ESPAsyncTCP", None)
