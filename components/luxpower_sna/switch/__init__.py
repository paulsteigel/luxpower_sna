import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID, CONF_NAME
from .. import luxpower_sna_ns, LuxpowerSNAComponent, CONF_LUXPOWER_SNA_ID

DEPENDENCIES = ["luxpower_sna"]

LuxPowerSwitch = luxpower_sna_ns.class_("LuxPowerSwitch", switch.Switch, cg.Component)

# ALL switch types from the Python switch.py - complete mapping
SWITCH_TYPES = {
    # Register 21 switches (Most Significant Byte)
    "feed_in_grid": (21, "FEED_IN_GRID"),
    "dci_enable": (21, "DCI_ENABLE"),
    "gfci_enable": (21, "GFCI_ENABLE"),
    "r21_unknown_bit_12": (21, "R21_UNKNOWN_BIT_12"),
    "charge_priority": (21, "CHARGE_PRIORITY"),
    "forced_discharge_enable": (21, "FORCED_DISCHARGE_ENABLE"),
    "normal_or_standby": (21, "NORMAL_OR_STANDBY"),
    "seamless_eps_switching": (21, "SEAMLESS_EPS_SWITCHING"),
    
    # Register 21 switches (Least Significant Byte)
    "ac_charge_enable": (21, "AC_CHARGE_ENABLE"),
    "grid_on_power_ss": (21, "GRID_ON_POWER_SS"),
    "neutral_detect_enable": (21, "NEUTRAL_DETECT_ENABLE"),
    "anti_island_enable": (21, "ANTI_ISLAND_ENABLE"),
    "r21_unknown_bit_3": (21, "R21_UNKNOWN_BIT_3"),
    "drms_enable": (21, "DRMS_ENABLE"),
    "ovf_load_derate_enable": (21, "OVF_LOAD_DERATE_ENABLE"),
    "power_backup_enable": (21, "POWER_BACKUP_ENABLE"),
    
    # Register 110 switches
    "take_load_together": (110, "TAKE_LOAD_TOGETHER"),
    "charge_last": (110, "CHARGE_LAST"),
    "micro_grid_enable": (110, "MICRO_GRID_ENABLE"),
    "fast_zero_export_enable": (110, "FAST_ZERO_EXPORT_ENABLE"),
    "run_without_grid": (110, "RUN_WITHOUT_GRID"),
    "pv_grid_off_enable": (110, "PV_GRID_OFF_ENABLE"),
    
    # Register 120 switches (AC Charge Mode and other settings)
    "gen_chrg_acc_to_soc": (120, "GEN_CHRG_ACC_TO_SOC"),
    "r120_unknown_bit_06": (120, "R120_UNKNOWN_BIT_06"),
    "r120_unknown_bit_05": (120, "R120_UNKNOWN_BIT_05"),
    "discharg_acc_to_soc": (120, "DISCHARG_ACC_TO_SOC"),
    "r120_unknown_bit_03": (120, "R120_UNKNOWN_BIT_03"),
    "ac_charge_mode_b_02": (120, "AC_CHARGE_MODE_B_02"),
    "ac_charge_mode_b_01": (120, "AC_CHARGE_MODE_B_01"),
    "r120_unknown_bit_00": (120, "R120_UNKNOWN_BIT_00"),
    
    # Register 179 switches (Peak Shaving and other advanced features)
    "r179_unknown_bit_15": (179, "R179_UNKNOWN_BIT_15"),
    "r179_unknown_bit_14": (179, "R179_UNKNOWN_BIT_14"),
    "r179_unknown_bit_13": (179, "R179_UNKNOWN_BIT_13"),
    "r179_unknown_bit_12": (179, "R179_UNKNOWN_BIT_12"),
    "r179_unknown_bit_11": (179, "R179_UNKNOWN_BIT_11"),
    "r179_unknown_bit_10": (179, "R179_UNKNOWN_BIT_10"),
    "r179_unknown_bit_09": (179, "R179_UNKNOWN_BIT_09"),
    "r179_unknown_bit_08": (179, "R179_UNKNOWN_BIT_08"),
    "enable_peak_shaving": (179, "ENABLE_PEAK_SHAVING"),
    "r179_unknown_bit_06": (179, "R179_UNKNOWN_BIT_06"),
    "r179_unknown_bit_05": (179, "R179_UNKNOWN_BIT_05"),
    "r179_unknown_bit_04": (179, "R179_UNKNOWN_BIT_04"),
    "r179_unknown_bit_03": (179, "R179_UNKNOWN_BIT_03"),
    "r179_unknown_bit_02": (179, "R179_UNKNOWN_BIT_02"),
    "r179_unknown_bit_01": (179, "R179_UNKNOWN_BIT_01"),
    "r179_unknown_bit_00": (179, "R179_UNKNOWN_BIT_00"),
}

CONF_SWITCH_TYPE = "switch_type"

CONFIG_SCHEMA = switch.switch_schema(LuxPowerSwitch).extend({
    cv.GenerateID(): cv.declare_id(LuxPowerSwitch),
    cv.GenerateID(CONF_LUXPOWER_SNA_ID): cv.use_id(LuxpowerSNAComponent),
    cv.Required(CONF_SWITCH_TYPE): cv.one_of(*SWITCH_TYPES.keys(), lower=True),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await switch.register_switch(var, config)

    parent = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    cg.add(var.set_parent(parent))

    switch_type = config[CONF_SWITCH_TYPE]
    register, bitmask_name = SWITCH_TYPES[switch_type]
    
    cg.add(var.set_register_address(register))
    cg.add(var.set_bitmask(getattr(luxpower_sna_ns, bitmask_name)))
    cg.add(var.set_switch_type(switch_type))
