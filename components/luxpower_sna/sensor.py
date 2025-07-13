import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, text_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_HERTZ,
    UNIT_KILOWATT_HOURS,
    UNIT_PERCENT,
    UNIT_VOLT,
    UNIT_WATT,
    CONF_UPDATE_INTERVAL
)

from . import LUXPOWER_SNA_COMPONENT_SCHEMA, CONF_LUXPOWER_SNA_ID

YAML_TO_C_NAMES = {
    # Existing sensors
    "pv1_voltage": "pv1_voltage", "pv2_voltage": "pv2_voltage", "pv3_voltage": "pv3_voltage",
    "battery_voltage": "battery_voltage", "soc": "soc", "soh": "soh",
    "pv1_power": "pv1_power", "pv2_power": "pv2_power", "pv3_power": "pv3_power",
    "charge_power": "charge_power", "discharge_power": "discharge_power",
    "inverter_power": "inverter_power", "power_to_grid": "power_to_grid", "power_from_grid": "power_from_grid",
    "grid_voltage_r": "grid_voltage_r", "grid_voltage_s": "grid_voltage_s", "grid_voltage_t": "grid_voltage_t",
    "grid_frequency": "grid_frequency", "power_factor": "power_factor",
    "eps_voltage_r": "eps_voltage_r", "eps_voltage_s": "eps_voltage_s", "eps_voltage_t": "eps_voltage_t",
    "eps_frequency": "eps_frequency",
    "eps_active_power": "eps_active_power", "eps_apparent_power": "eps_apparent_power",
    "bus1_voltage": "bus1_voltage", "bus2_voltage": "bus2_voltage",
    "pv1_energy_today": "pv1_energy_today", "pv2_energy_today": "pv2_energy_today", "pv3_energy_today": "pv3_energy_today",
    "inverter_energy_today": "inverter_energy_today", "ac_charging_today": "ac_charging_today",
    "charging_today": "charging_today", "discharging_today": "discharging_today",
    "eps_today": "eps_today", "exported_today": "exported_today", "grid_today": "grid_today",
    "total_pv1_energy": "total_pv1_energy", "total_pv2_energy": "total_pv2_energy", "total_pv3_energy": "total_pv3_energy",
    "total_inverter_output": "total_inverter_output", "total_recharge_energy": "total_recharge_energy",
    "total_charged": "total_charged", "total_discharged": "total_discharged",
    "total_eps_energy": "total_eps_energy", "total_exported": "total_exported", "total_imported": "total_imported",
    "temp_inner": "temp_inner", "temp_radiator": "temp_radiator", "temp_radiator2": "temp_radiator2",
    "temp_battery": "temp_battery", "uptime": "uptime",
    "max_charge_current": "max_charge_current", "max_discharge_current": "max_discharge_current",
    "charge_voltage_ref": "charge_voltage_ref", "discharge_cutoff_voltage": "discharge_cutoff_voltage",
    "battery_current": "battery_current", "battery_count": "battery_count",
    "battery_capacity": "battery_capacity", "battery_status_inv": "battery_status_inv",
    "max_cell_voltage": "max_cell_voltage", "min_cell_voltage": "min_cell_voltage",
    "max_cell_temp": "max_cell_temp", "min_cell_temp": "min_cell_temp", "cycle_count": "cycle_count",
    "gen_input_volt": "gen_input_volt", "gen_input_freq": "gen_input_freq",
    "gen_power_watt": "gen_power_watt", "gen_power_day": "gen_power_day", "gen_power_all": "gen_power_all",
    "eps_L1_volt": "eps_L1_volt", "eps_L2_volt": "eps_L2_volt",
    "eps_L1_watt": "eps_L1_watt", "eps_L2_watt": "eps_L2_watt",
    
    # New sensors
    "grid_voltage_avg": "grid_voltage_avg",
    "p_pv_total": "p_pv_total",
    "internal_fault": "internal_fault",
    "ct_clamp_live": "ct_clamp_live",
    "home_consumption2": "home_consumption2",
    "total_pv_energy": "total_pv_energy",
    "home_consumption_total": "home_consumption_total",
    "battery_flow": "battery_flow",
    
    # Text sensors
    "status_text": "status_text",
    "battery_status_text": "battery_status_text",
    "inverter_serial": "inverter_serial"
}

SENSOR_TYPES = {
    # Existing sensor schemas
    "pv1_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:solar-panel"),
    "pv2_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:solar-panel"),
    "pv3_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:solar-panel"),
    "battery_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "soc": sensor.sensor_schema(unit_of_measurement=UNIT_PERCENT, device_class="battery", state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "soh": sensor.sensor_schema(unit_of_measurement=UNIT_PERCENT, icon="mdi:battery-heart-variant", state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "pv1_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:solar-power"),
    "pv2_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:solar-power"),
    "pv3_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:solar-power"),
    "charge_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:battery-plus"),
    "discharge_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:battery-minus"),
    "inverter_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:power-plug"),
    "power_to_grid": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:transmission-tower-export"),
    "power_from_grid": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:transmission-tower-import"),
    "grid_voltage_r": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "grid_voltage_s": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "grid_voltage_t": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "grid_frequency": sensor.sensor_schema(unit_of_measurement=UNIT_HERTZ, device_class=DEVICE_CLASS_FREQUENCY, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=2),
    "power_factor": sensor.sensor_schema(icon="mdi:angle-acute", state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=3),
    "eps_voltage_r": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "eps_voltage_s": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "eps_voltage_t": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "eps_frequency": sensor.sensor_schema(unit_of_measurement=UNIT_HERTZ, device_class=DEVICE_CLASS_FREQUENCY, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=2),
    "eps_active_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:power-plug-off"),
    "eps_apparent_power": sensor.sensor_schema(unit_of_measurement="VA", state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:power-plug-off"),
    "bus1_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:sine-wave"),
    "bus2_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:sine-wave"),
    "pv1_energy_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "pv2_energy_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "pv3_energy_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "inverter_energy_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "ac_charging_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "charging_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "discharging_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "eps_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "exported_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "grid_today": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "total_pv1_energy": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "total_pv2_energy": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "total_pv3_energy": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "total_inverter_output": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "total_recharge_energy": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "total_charged": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "total_discharged": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "total_eps_energy": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "total_exported": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "total_imported": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "temp_inner": sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, device_class=DEVICE_CLASS_TEMPERATURE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "temp_radiator": sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, device_class=DEVICE_CLASS_TEMPERATURE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "temp_radiator2": sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, device_class=DEVICE_CLASS_TEMPERATURE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "temp_battery": sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, device_class=DEVICE_CLASS_TEMPERATURE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "uptime": sensor.sensor_schema(unit_of_measurement="s", icon="mdi:timer-sand", state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=0),
    "max_charge_current": sensor.sensor_schema(unit_of_measurement=UNIT_AMPERE, device_class=DEVICE_CLASS_CURRENT, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=2),
    "max_discharge_current": sensor.sensor_schema(unit_of_measurement=UNIT_AMPERE, device_class=DEVICE_CLASS_CURRENT, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=2),
    "charge_voltage_ref": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "discharge_cutoff_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "battery_current": sensor.sensor_schema(unit_of_measurement=UNIT_AMPERE, device_class=DEVICE_CLASS_CURRENT, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=2),
    "battery_count": sensor.sensor_schema(icon="mdi:counter", accuracy_decimals=0),
    "battery_capacity": sensor.sensor_schema(unit_of_measurement="Ah", icon="mdi:battery-plus-variant", accuracy_decimals=0),
    "battery_status_inv": sensor.sensor_schema(icon="mdi:information-outline", accuracy_decimals=0),
    "max_cell_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=3),
    "min_cell_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=3),
    "max_cell_temp": sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, device_class=DEVICE_CLASS_TEMPERATURE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "min_cell_temp": sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, device_class=DEVICE_CLASS_TEMPERATURE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "cycle_count": sensor.sensor_schema(icon="mdi:battery-sync", state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=0),
    "gen_input_volt": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "gen_input_freq": sensor.sensor_schema(unit_of_measurement=UNIT_HERTZ, device_class=DEVICE_CLASS_FREQUENCY, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=2),
    "gen_power_watt": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "gen_power_day": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "gen_power_all": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "eps_L1_volt": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "eps_L2_volt": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "eps_L1_watt": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "eps_L2_watt": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    
    # New sensor schemas
    "grid_voltage_avg": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:transmission-tower"),
    "p_pv_total": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:solar-power"),
    "internal_fault": sensor.sensor_schema(icon="mdi:alert", state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "ct_clamp_live": sensor.sensor_schema(unit_of_measurement=UNIT_AMPERE, device_class=DEVICE_CLASS_CURRENT, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=2, icon="mdi:current-ac"),
    "home_consumption2": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:home-lightning-bolt"),
    "p_pv_total": sensor.sensor_schema(unit_of_measurement=UNIT_WATT,device_class=DEVICE_CLASS_POWER,state_class=STATE_CLASS_MEASUREMENT,accuracy_decimals=0,icon="mdi:solar-power"),
    "total_pv_energy": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY,state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1, icon="mdi:solar-panel-large"),
    "home_consumption_total": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1, icon="mdi:home-lightning-bolt-outline"),
    "battery_flow": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    
    # Text sensor schemas
    "status_text": text_sensor.text_sensor_schema(icon="mdi:information-outline"),
    "battery_status_text": text_sensor.text_sensor_schema(icon="mdi:battery-alert"),
    "inverter_serial": text_sensor.text_sensor_schema(icon="mdi:barcode-scan"),
}

CONFIG_SCHEMA = cv.All(
    LUXPOWER_SNA_COMPONENT_SCHEMA.extend(
        {
            **{cv.Optional(key): schema for key, schema in SENSOR_TYPES.items()},
            cv.Optional(CONF_UPDATE_INTERVAL, default="20s"): cv.update_interval,
        }
    ),    
    cv.has_at_least_one_key(*SENSOR_TYPES.keys()),
)
    
async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    
    for yaml_key, c_name in YAML_TO_C_NAMES.items():
        if yaml_key in config:
            conf = config[yaml_key]
            
            # Handle text sensors
            if yaml_key in ["status_text", "battery_status_text", "inverter_serial"]:
                sens = await text_sensor.new_text_sensor(conf)
            # Handle regular sensors
            else:
                sens = await sensor.new_sensor(conf)
                
            cg.add(getattr(hub, f"set_{c_name}_sensor")(sens))
