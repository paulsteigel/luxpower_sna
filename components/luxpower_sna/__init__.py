# components/luxpower_sna/__init__.py
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import CORE, coroutine_with_priority
from esphome.const import CONF_ID

DEPENDENCIES = ["wifi"]
AUTO_LOAD = ["sensor", "text_sensor"]
MULTI_CONF = True

luxpower_sna_ns = cg.esphome_ns.namespace("luxpower_sna")
LuxpowerSNAComponent = luxpower_sna_ns.class_("LuxpowerSNAComponent", cg.PollingComponent)

CONF_LUXPOWER_SNA_ID = "luxpower_sna_id"

# Configuration parameter keys that reference template component IDs
CONF_HOST = "host"
CONF_PORT = "port" 
CONF_DONGLE_SERIAL = "dongle_serial"
CONF_INVERTER_SERIAL = "inverter_serial"

LUXPOWER_SNA_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
    }
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LuxpowerSNAComponent),
            cv.Required(CONF_HOST): cv.use_id(),           # Any component with state
            cv.Required(CONF_PORT): cv.use_id(),           # Any component with state
            cv.Required(CONF_DONGLE_SERIAL): cv.use_id(),  # Any component with state
            cv.Required(CONF_INVERTER_SERIAL): cv.use_id(), # Any component with state
        }
    )
    .extend(cv.polling_component_schema("20s"))
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Set references to template input components
    host_var = await cg.get_variable(config[CONF_HOST])
    port_var = await cg.get_variable(config[CONF_PORT])
    dongle_serial_var = await cg.get_variable(config[CONF_DONGLE_SERIAL])
    inverter_serial_var = await cg.get_variable(config[CONF_INVERTER_SERIAL])
    
    cg.add(var.set_host_input(host_var))
    cg.add(var.set_port_input(port_var))
    cg.add(var.set_dongle_serial_input(dongle_serial_var))
    cg.add(var.set_inverter_serial_input(inverter_serial_var))
