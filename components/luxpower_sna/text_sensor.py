# components/luxpower_sna/text_sensor.py
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor

from . import LUXPOWER_SNA_COMPONENT_SCHEMA, CONF_LUXPOWER_SNA_ID

TEXT_SENSOR_TYPES = {
    "status_text": text_sensor.text_sensor_schema(
        icon="mdi:information-outline",
    ),
}

# --- THE CORRECTED SCHEMA DEFINITION ---
CONFIG_SCHEMA = cv.All(
    # The base schema for a text_sensor platform is text_sensor.TEXT_SENSOR_PLATFORM_SCHEMA
    text_sensor.TEXT_SENSOR_PLATFORM_SCHEMA.extend(
        {
            **{cv.Optional(key): schema for key, schema in TEXT_SENSOR_TYPES.items()},
        }
    ).extend(LUXPOWER_SNA_COMPONENT_SCHEMA), # Extend with the linking schema
    cv.has_at_least_one_key(*TEXT_SENSOR_TYPES.keys()),
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    for key, conf in config.items():
        if key in TEXT_SENSOR_TYPES:
            sens = await text_sensor.new_text_sensor(conf)
            cg.add(getattr(hub, f"set_{key}_sensor")(sens))
