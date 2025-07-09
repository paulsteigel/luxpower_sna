# In luxpower_sna/__init__.py

# ... other imports ...
from esphome.const import (
    CONF_ID,
    CONF_HOST,
    CONF_PORT,
    # ... other consts ...
)

# ...

# REMOVE THIS from the list of keys
# CONF_INVERTER_SERIAL_NUMBER = "inverter_serial_number" 

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LuxpowerSNAComponent),
        cv.Required(CONF_HOST): cv.string,
        cv.Required(CONF_PORT): cv.port,
        cv.Required(CONF_DONGLE_SERIAL): cv.string,
        # AND REMOVE THIS LINE
        # cv.Required(CONF_INVERTER_SERIAL_NUMBER): cv.string, 
    }
).extend(cv.polling_component_schema("10s"))

# ... rest of the file ...
