import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID, CONF_NAME
from .. import luxpower_sna_ns, LuxpowerSNAComponent, CONF_LUXPOWER_SNA_ID

DEPENDENCIES = ["luxpower_sna"]

LuxPowerSwitch = luxpower_sna_ns.class_("LuxPowerSwitch", switch.Switch, cg.Component)

# Switch definitions with register and bitmask
SWITCH_DEFINITIONS = {
    # Register 21 switches
    "feed_in_grid": (21, 1 << 15),
    "dci_enable": (21, 1 << 14),
    "gfci_enable": (21, 1 << 13),
    "charge_priority": (21, 1 << 11),
    "forced_discharge_enable": (21, 1 << 10),
    "normal_or_standby": (21, 1 << 9),
    "seamless_eps_switching": (21, 1 << 8),
    "ac_charge_enable": (21, 1 << 7),
    "grid_on_power_ss": (21, 1 << 6),
    "neutral_detect_enable": (21, 1 << 5),
    "anti_island_enable": (21, 1 << 4),
    "drms_enable": (21, 1 << 2),
    "ovf_load_derate_enable": (21, 1 << 1),
    "power_backup_enable": (21, 1 << 0),
    
    # Register 110 switches
    "take_load_together": (110, 1 << 10),
    "charge_last": (110, 1 << 4),
    "micro_grid_enable": (110, 1 << 2),
    "fast_zero_export_enable": (110, 1 << 1),
    "pv_grid_off_enable": (110, 1 << 0),
    
    # Register 120 switches
    "gen_chrg_acc_to_soc": (120, 1 << 7),
    "discharg_acc_to_soc": (120, 1 << 4),
    "ac_charge_mode_b_01": (120, 1 << 1),
    "ac_charge_mode_b_02": (120, 1 << 2),
    
    # Register 179 switches
    "enable_peak_shaving": (179, 1 << 7),
}

# Create individual switch schemas
def create_switch_schema(switch_key):
    return switch.switch_schema(LuxPowerSwitch).extend({
        cv.GenerateID(): cv.declare_id(LuxPowerSwitch),
    }).extend(cv.COMPONENT_SCHEMA)

# Build the main config schema with all possible switches
switch_schemas = {}
for switch_key in SWITCH_DEFINITIONS.keys():
    switch_schemas[cv.Optional(switch_key)] = create_switch_schema(switch_key)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
    **switch_schemas
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    parent = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    
    # Process each configured switch
    for switch_key, switch_config in config.items():
        if switch_key == CONF_LUXPOWER_SNA_ID:
            continue
            
        if switch_key in SWITCH_DEFINITIONS:
            # Create the switch component
            var = cg.new_Pvariable(switch_config[CONF_ID])
            await cg.register_component(var, switch_config)
            await switch.register_switch(var, switch_config)
            
            # Configure the switch
            cg.add(var.set_parent(parent))
            
            register, bitmask = SWITCH_DEFINITIONS[switch_key]
            cg.add(var.set_register_address(register))
            cg.add(var.set_bitmask(bitmask))
            cg.add(var.set_switch_type(switch_key))
