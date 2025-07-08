# custom_components/luxpower_sna/__init__.py
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID, CONF_NAME, CONF_UNIT_OF_MEASUREMENT, CONF_DEVICE_CLASS,
    CONF_STATE_CLASS, CONF_ACCURACY_DECIMALS, CONF_ICON
)
from esphome.components import sensor

luxpower_sna_ns = cg.esphome_ns.namespace("luxpower_sna")
# CORRECTED LINE: Reference the actual C++ class name "LuxPowerInverter"
LuxPowerInverter = luxpower_sna_ns.class_("LuxPowerInverter", cg.Component)
LuxpowerSnaSensor = luxpower_sna_ns.class_("LuxpowerSnaSensor", sensor.Sensor)

LuxpowerRegType = luxpower_sna_ns.enum("LuxpowerRegType")
LUX_REG_TYPES = {
    "INT": LuxpowerRegType.LUX_REG_TYPE_INT,
    "FLOAT_DIV10": LuxpowerRegType.LUX_REG_TYPE_FLOAT_DIV10,
    "SIGNED_INT": LuxpowerRegType.LUX_REG_TYPE_SIGNED_INT,
    "FIRMWARE": LuxpowerRegType.LUX_REG_TYPE_FIRMWARE,
    "MODEL": LuxpowerRegType.LUX_REG_TYPE_MODEL,
    "BITMASK": LuxpowerRegType.LUX_REG_TYPE_BITMASK,
    "TIME_MINUTES": LuxpowerRegType.LUX_REG_TYPE_TIME_MINUTES,
}

CONF_HOST = "host"
CONF_PORT = "port"
CONF_UPDATE_INTERVAL = "update_interval"
CONF_SENSORS = "sensors"
CONF_UNIQUE_ID = "unique_id"

CONF_DONGLE_SERIAL = "dongle_serial"
CONF_INVERTER_SERIAL_NUMBER = "inverter_serial_number"
CONF_REGISTER_ADDRESS = "register"
CONF_REG_TYPE = "reg_type"
CONF_BANK = "bank"

LUXPOWER_SENSOR_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=cv.Optional(CONF_UNIT_OF_MEASUREMENT),
    device_class=cv.Optional(CONF_DEVICE_CLASS),
    state_class=cv.Optional(CONF_STATE_CLASS),
    accuracy_decimals=cv.Optional(CONF_ACCURACY_DECIMALS),
    icon=cv.Optional(CONF_ICON),
).extend(
    {
        cv.GenerateID(): cv.declare_id(LuxpowerSnaSensor),
        cv.Required(CONF_REGISTER_ADDRESS): cv.hex_uint16_t,
        cv.Required(CONF_REG_TYPE): cv.enum(LUX_REG_TYPES, upper=True),
        cv.Optional(CONF_BANK, default=0): cv.uint8_t,
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        # CORRECTED LINE: Reference the updated Python variable for LuxPowerInverter
        cv.GenerateID(): cv.declare_id(LuxPowerInverter),
        cv.Required(CONF_HOST): cv.string,
        cv.Required(CONF_PORT): cv.port,
        cv.Required(CONF_DONGLE_SERIAL): cv.string,
        cv.Required(CONF_INVERTER_SERIAL_NUMBER): cv.string,
        cv.Optional(CONF_UPDATE_INTERVAL, default="60s"): cv.update_interval,
        cv.Optional(CONF_SENSORS): cv.All(
            cv.ensure_list(LUXPOWER_SENSOR_SCHEMA),
            cv.Length(min=1)
        ),
    }
).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    # CORRECTED LINE: Instantiate the LuxPowerInverter class
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # These setter methods will need to be added to LuxPowerInverter class in .h and .cpp
    cg.add(var.set_inverter_host(config[CONF_HOST]))
    cg.add(var.set_inverter_port(config[CONF_PORT]))
    cg.add(var.set_dongle_serial(config[CONF_DONGLE_SERIAL]))
    cg.add(var.set_inverter_serial_number(config[CONF_INVERTER_SERIAL_NUMBER]))
    cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL]))

    if CONF_SENSORS in config:
        for sens_config in config[CONF_SENSORS]:
            sens = cg.new_Pvariable(sens_config[CONF_ID])
            await sensor.register_sensor(sens, sens_config)
            # This method (add_luxpower_sensor) will need to be added to LuxPowerInverter class
            cg.add(var.add_luxpower_sensor(sens,
                                            sens_config[CONF_NAME],
                                            sens_config[CONF_REGISTER_ADDRESS],
                                            sens_config[CONF_REG_TYPE],
                                            sens_config[CONF_BANK]))
