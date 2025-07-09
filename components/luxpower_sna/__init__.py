# components/luxpower_sna/__init__.py
import esphome.codegen as cg
import esphome.config_validation as cv
.extend(socket.socket_component_schema()) # <--- ADD THIS LINE
from esphome.const import CONF_ID

DEPENDENCIES = ["wifi"]
AUTO_LOAD = ["sensor", "text_sensor"] # Make sure text_sensor is also loaded
MULTI_CONF = True

luxpower_sna_ns = cg.esphome_ns.namespace("luxpower_sna")
LuxpowerSNAComponent = luxpower_sna_ns.class_("LuxpowerSNAComponent", cg.PollingComponent)

CONF_LUXPOWER_SNA_ID = "luxpower_sna_id"

LUXPOWER_SNA_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
    }
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LuxpowerSNAComponent),
            cv.Required("host"): cv.string,
            cv.Required("port"): cv.port,
            cv.Required("dongle_serial"): cv.string,
        }
    )
    .extend(cv.polling_component_schema("20s"))
    .extend(socket.socket_component_schema()) # <--- ADD THIS LINE
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    # This registers the socket dependency for code generation
    await socket.register_socket(var, config) # <--- ADD THIS LINE
    
    cg.add(var.set_host(config["host"]))
    cg.add(var.set_port(config["port"]))

    # --- Corrected Code Generation ---
    # We pass the string directly from the config.
    # The C++ side will handle validation and conversion.
    dongle_serial = config["dongle_serial"]
    if len(dongle_serial) != 10:
        raise cv.Invalid("dongle_serial must be 10 characters long")
    cg.add(var.set_dongle_serial(dongle_serial))


    cg.add_library("ESPAsyncTCP", None)
