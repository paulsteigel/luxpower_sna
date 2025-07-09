# components/luxpower_sna/text_sensor.py
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor

# --- Import the linking schema and ID from __init__.py ---
from . import LUXPOWER_SNA_COMPONENT_SCHEMA, CONF_LUXPOWER_SNA_ID

TEXT_SENSOR_TYPES = {
    "status_text": text_sensor.text_sensor_schema(
        icon="mdi:information-outline",
    ),
}

# --- The CONFIG_SCHEMA for the text_sensor platform, following your working sample ---
CONFIG_SCHEMA = cv.All(
    LUXPOWER_SNA_COMPONENT_SCHEMA.extend(
        {
            **{cv.Optional(key): schema for key, schema in TEXT_SENSOR_TYPES.items()},
        }
    ),
    cv.has_at_least_one_key(*TEXT_SENSOR_TYPES.keys()),
)

async def to_code(config):
    # Get the hub object using the ID from the linking schema
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])

    # Loop through our defined sensor types and create the ones the user configured
    for key in TEXT_SENSOR_TYPES:
        if key in config:
            conf = config[key]
            sens = await text_sensor.new_text_sensor(conf)
            # Call the specific setter function in the C++ component
            cg.add(getattr(hub, f"set_{key}_sensor")(sens))
