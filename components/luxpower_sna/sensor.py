# esphome_config/custom_components/luxpower_sna/sensor.py

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_VOLTAGE,
    DEVICE_CLASS_POWER,
    STATE_CLASS_MEASUREMENT,
    UNIT_VOLT,
    UNIT_WATT,
)

# Pull in the namespace from our main component file (__init__.py)
from . import luxpower_sna_ns, LuxpowerSNAComponent

# Define a CONF key for linking to the main hub component
CONF_LUXPOWER_SNA_ID = "luxpower_sna_id"

# Define CONF keys for each sensor type
CONF_V_PV1 = "v_pv1"
CONF_P_PV1 = "p_pv1"
CONF_V_BAT = "v_bat"
CONF_P_CHARGE = "p_charge"
CONF_P_DISCHARGE = "p_discharge"
CONF_P_INV = "p_inv"

# A list of all our sensor types for easier iteration
SENSOR_TYPES = [
    CONF_V_PV1,
    CONF_P_PV1,
    CONF_V_BAT,
    CONF_P_CHARGE,
    CONF_P_DISCHARGE,
    CONF_P_INV,
]

# This is the schema that tells ESPHome what options are valid in the YAML
CONFIG_SCHEMA = cv.All(
    sensor.SENSOR_SCHEMA.extend(
        {
            # Link to the main hub component created in the YAML
            cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
            
            # Define each sensor as an optional key.
            # This allows users to only define the sensors they want.
            cv.Optional(CONF_V_PV1): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_VOLTAGE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_P_PV1): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_V_BAT): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_VOLTAGE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_P_CHARGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_P_DISCHARGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_P_INV): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA)
)

# This function is called by ESPHome to generate the C++ code
async def to_code(config):
    # Get the parent hub component
    parent = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    
    # Loop through all our defined sensor types
    for sens_type in SENSOR_TYPES:
        # If the user has defined this sensor in their YAML...
        if sens_config := config.get(sens_type):
            # ...create a new sensor object in C++...
            sens = await sensor.new_sensor(sens_config)
            # ...and call the corresponding C++ setter function on the parent hub.
            # e.g., for CONF_V_PV1, this calls parent->set_v_pv1_sensor(sens);
            setter_func = f"set_{sens_type}_sensor"
            cg.add(getattr(parent, setter_func)(sens))
