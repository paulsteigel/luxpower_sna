import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_TYPE

from . import LuxpowerSNAComponent

CONF_LUXPOWER_SNA_ID = "luxpower_sna_id"

TEXT_SENSORS = {
    "status_text": text_sensor.text_sensor_schema(
        icon="mdi:information-outline",
    ),
}

CONFIG_SCHEMA = cv.All(
    text_sensor.text_sensor_schema()
    .extend(
        {
            cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
            cv.Required(CONF_TYPE): cv.enum(TEXT_SENSORS, lower=True),
        }
    )
)

async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    sens = await text_sensor.new_text_sensor(config)
    cg.add(getattr(hub, f"set_{config[CONF_TYPE]}_sensor")(sens))
