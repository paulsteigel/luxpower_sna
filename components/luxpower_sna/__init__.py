import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    CONF_HOST,
    CONF_PORT,
    CONF_UPDATE_INTERVAL,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_KILO_WATT_HOUR,
    UNIT_VOLT,
    UNIT_WATT,
    ICON_EMPTY,
)

# Use the same namespace as in your C++ files
luxpower_sna_ns = cg.esphome_ns.namespace("luxpower_sna")

# Main Component
LuxpowerSnaComponent = luxpower_sna_ns.class_("LuxpowerSnaComponent", cg.PollingComponent)
# Custom Sensor
LuxpowerSnaSensor = luxpower_sna_ns.class_("LuxpowerSnaSensor", sensor.Sensor, cg.Component)

# Custom constants for the config
CONF_DONGLE_SERIAL = "dongle_serial"
CONF_INVERTER_SERIAL_NUMBER = "inverter_serial_number"

# A list of all possible sensors we can create
# This is derived from LXPPacket.py and sensor.py
SENSORS = {
    "status": {"class": sensor.Sensor, "unit": ICON_EMPTY, "icon": "mdi:information-outline"},
    "v_pv_1": {"class": sensor.Sensor, "unit": UNIT_VOLT, "icon": "mdi:solar-power", "device": DEVICE_CLASS_VOLTAGE},
    "v_pv_2": {"class": sensor.Sensor, "unit": UNIT_VOLT, "icon": "mdi:solar-power", "device": DEVICE_CLASS_VOLTAGE},
    "v_bat": {"class": sensor.Sensor, "unit": UNIT_VOLT, "icon": "mdi:battery", "device": DEVICE_CLASS_VOLTAGE},
    "soc": {"class": sensor.Sensor, "unit": "%", "icon": "mdi:battery-high", "device": DEVICE_CLASS_POWER},
    "p_pv_1": {"class": sensor.Sensor, "unit": UNIT_WATT, "icon": "mdi:solar-power", "device": DEVICE_CLASS_POWER, "state": STATE_CLASS_MEASUREMENT},
    "p_pv_2": {"class": sensor.Sensor, "unit": UNIT_WATT, "icon": "mdi:solar-power", "device": DEVICE_CLASS_POWER, "state": STATE_CLASS_MEASUREMENT},
    "p_pv_total": {"class": sensor.Sensor, "unit": UNIT_WATT, "icon": "mdi:solar-power", "device": DEVICE_CLASS_POWER, "state": STATE_CLASS_MEASUREMENT},
    "p_charge": {"class": sensor.Sensor, "unit": UNIT_WATT, "icon": "mdi:battery-charging", "device": DEVICE_CLASS_POWER, "state": STATE_CLASS_MEASUREMENT},
    "p_discharge": {"class": sensor.Sensor, "unit": UNIT_WATT, "icon": "mdi:battery-minus", "device": DEVICE_CLASS_POWER, "state": STATE_CLASS_MEASUREMENT},
    "p_inv": {"class": sensor.Sensor, "unit": UNIT_WATT, "icon": "mdi:power-plug", "device": DEVICE_CLASS_POWER, "state": STATE_CLASS_MEASUREMENT},
    "p_to_grid": {"class": sensor.Sensor, "unit": UNIT_WATT, "icon": "mdi:transmission-tower-export", "device": DEVICE_CLASS_POWER, "state": STATE_CLASS_MEASUREMENT},
    "p_to_user": {"class": sensor.Sensor, "unit": UNIT_WATT, "icon": "mdi:transmission-tower-import", "device": DEVICE_CLASS_POWER, "state": STATE_CLASS_MEASUREMENT},
    "p_load": {"class": sensor.Sensor, "unit": UNIT_WATT, "icon": "mdi:home-lightning-bolt", "device": DEVICE_CLASS_POWER, "state": STATE_CLASS_MEASUREMENT},
    "e_pv_total_day": {"class": sensor.Sensor, "unit": UNIT_KILO_WATT_HOUR, "icon": "mdi:solar-power", "device": DEVICE_CLASS_ENERGY, "state": STATE_CLASS_TOTAL_INCREASING},
    "e_inv_day": {"class": sensor.Sensor, "unit": UNIT_KILO_WATT_HOUR, "icon": "mdi:power-plug", "device": DEVICE_CLASS_ENERGY, "state": STATE_CLASS_TOTAL_INCREASING},
    "e_chg_day": {"class": sensor.Sensor, "unit": UNIT_KILO_WATT_HOUR, "icon": "mdi:battery-charging", "device": DEVICE_CLASS_ENERGY, "state": STATE_CLASS_TOTAL_INCREASING},
    "e_dischg_day": {"class": sensor.Sensor, "unit": UNIT_KILO_WATT_HOUR, "icon": "mdi:battery-minus", "device": DEVICE_CLASS_ENERGY, "state": STATE_CLASS_TOTAL_INCREASING},
    "e_to_grid_day": {"class": sensor.Sensor, "unit": UNIT_KILO_WATT_HOUR, "icon": "mdi:transmission-tower-export", "device": DEVICE_CLASS_ENERGY, "state": STATE_CLASS_TOTAL_INCREASING},
    "e_to_user_day": {"class": sensor.Sensor, "unit": UNIT_KILO_WATT_HOUR, "icon": "mdi:transmission-tower-import", "device": DEVICE_CLASS_ENERGY, "state": STATE_CLASS_TOTAL_INCREASING},
    "t_inner": {"class": sensor.Sensor, "unit": UNIT_CELSIUS, "icon": "mdi:thermometer", "device": DEVICE_CLASS_TEMPERATURE},
    "t_rad_1": {"class": sensor.Sensor, "unit": UNIT_CELSIUS, "icon": "mdi:thermometer", "device": DEVICE_CLASS_TEMPERATURE},
    "t_bat": {"class": sensor.Sensor, "unit": UNIT_CELSIUS, "icon": "mdi:thermometer", "device": DEVICE_CLASS_TEMPERATURE},
    "bat_current": {"class": sensor.Sensor, "unit": UNIT_AMPERE, "icon": "mdi:current-dc", "device": DEVICE_CLASS_CURRENT},
}

# Define the schema for a single sensor
SENSOR_SCHEMA = sensor.sensor_schema().extend(
    {
        cv.GenerateID(): cv.declare_id(LuxpowerSnaSensor),
    }
)

# Define the main component schema
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LuxpowerSnaComponent),
        cv.Required(CONF_HOST): cv.string,
        cv.Required(CONF_PORT): cv.port,
        cv.Required(CONF_DONGLE_SERIAL): cv.string,
        cv.Required(CONF_INVERTER_SERIAL_NUMBER): cv.string,
        cv.Optional(CONF_UPDATE_INTERVAL, default="20s"): cv.update_interval,
        # Define all the sensors the user can add
        cv.Optional(type) : SENSOR_SCHEMA for type in SENSORS
    }
).extend(cv.polling_component_schema("60s")) # Add a default polling interval if user doesn't specify

# This function is called by ESPHome to generate the C++ code
async def to_code(config):
    # Register the main component
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set configuration values
    cg.add(var.set_host(config[CONF_HOST]))
    cg.add(var.set_port(config[CONF_PORT]))
    # Convert serials to byte arrays for C++
    dongle_serial = config[CONF_DONGLE_SERIAL].encode('ascii')
    cg.add(var.set_dongle_serial(dongle_serial))
    inverter_serial = config[CONF_INVERTER_SERIAL_NUMBER].encode('ascii')
    cg.add(var.set_inverter_serial(inverter_serial))

    # Register all sensors that the user has defined in their YAML
    for type, conf in config.items():
        if type not in SENSORS:
            continue
        
        sens = await sensor.new_sensor(conf)
        # Call the register_sensor method in our C++ component
        cg.add(var.register_sensor(type, sens))

    # Add required libraries
    cg.add_library("ESPAsyncTCP", None)
