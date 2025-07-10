# components/luxpower_sna/text_sensor.py
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor

from . import LUXPOWER_SNA_COMPONENT_SCHEMA, CONF_LUXPOWER_SNA_ID

TEXT_SENSOR_TYPES = {
    "inverter_serial": text_sensor.text_sensor_schema(
        icon="mdi:barcode-scan",
    ),
}

CONFIG_SCHEMA = cv.All(
    LUXPOWER_SNA_COMPONENT_SCHEMA.extend(
        {
            **{cv.Optional(key): schema for key, schema in TEXT_SENSOR_TYPES.items()},
        }
    ),
    cv.has_at_least_one_key(*TEXT_SENSOR_TYPES.keys()),
)

async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])

    for key in TEXT_SENSOR_TYPES:
        if key in config:
            conf = config[key]
            sens = await text_sensor.new_text_sensor(conf)
            cg.add(getattr(hub, f"set_{key}_sensor")(sens))
