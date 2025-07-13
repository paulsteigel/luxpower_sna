import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, text_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_BATTERY,
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
    # System sensors
    "lux_firmware_version": "lux_firmware_version",
    "lux_inverter_model": "lux_inverter_model",
    "lux_status_text": "lux_status_text",
    "lux_battery_status_text": "lux_battery_status_text",
    "lux_data_last_received_time": "lux_data_last_received_time",
    
    # Section1 sensors
    "lux_current_solar_voltage_1": "lux_current_solar_voltage_1",
    "lux_current_solar_voltage_2": "lux_current_solar_voltage_2",
    "lux_current_solar_voltage_3": "lux_current_solar_voltage_3",
    "lux_battery_voltage": "lux_battery_voltage",
    "lux_battery_percent": "lux_battery_percent",
    "soh": "soh",
    "lux_internal_fault": "lux_internal_fault",
    "lux_current_solar_output_1": "lux_current_solar_output_1",
    "lux_current_solar_output_2": "lux_current_solar_output_2",
    "lux_current_solar_output_3": "lux_current_solar_output_3",
    "lux_battery_charge": "lux_battery_charge",
    "lux_battery_discharge": "lux_battery_discharge",
    "lux_grid_voltage_r": "lux_grid_voltage_r",
    "lux_grid_voltage_s": "lux_grid_voltage_s",
    "lux_grid_voltage_t": "lux_grid_voltage_t",
    "lux_grid_frequency_live": "lux_grid_frequency_live",
    "lux_grid_voltage_live": "lux_grid_voltage_live",
    "lux_power_from_inverter_live": "lux_power_from_inverter_live",
    "lux_power_to_inverter_live": "lux_power_to_inverter_live",
    "lux_power_current_clamp": "lux_power_current_clamp",
    "grid_power_factor": "grid_power_factor",
    "eps_voltage_r": "eps_voltage_r",
    "eps_voltage_s": "eps_voltage_s",
    "eps_voltage_t": "eps_voltage_t",
    "eps_frequency": "eps_frequency",
    "lux_power_to_eps": "lux_power_to_eps",
    "apparent_eps_power": "apparent_eps_power",
    "lux_power_to_grid_live": "lux_power_to_grid_live",
    "lux_power_from_grid_live": "lux_power_from_grid_live",
    "lux_daily_solar_array_1": "lux_daily_solar_array_1",
    "lux_daily_solar_array_2": "lux_daily_solar_array_2",
    "lux_daily_solar_array_3": "lux_daily_solar_array_3",
    "lux_power_from_inverter_daily": "lux_power_from_inverter_daily",
    "lux_power_to_inverter_daily": "lux_power_to_inverter_daily",
    "lux_daily_battery_charge": "lux_daily_battery_charge",
    "lux_daily_battery_discharge": "lux_daily_battery_discharge",
    "lux_power_to_eps_daily": "lux_power_to_eps_daily",
    "lux_power_to_grid_daily": "lux_power_to_grid_daily",
    "lux_power_from_grid_daily": "lux_power_from_grid_daily",
    "bus1_voltage": "bus1_voltage",
    "bus2_voltage": "bus2_voltage",
    "lux_current_solar_output": "lux_current_solar_output",
    "lux_daily_solar": "lux_daily_solar",
    "lux_power_to_home": "lux_power_to_home",
    "lux_battery_flow": "lux_battery_flow",
    "lux_grid_flow": "lux_grid_flow",
    "lux_home_consumption_live": "lux_home_consumption_live",
    "lux_home_consumption": "lux_home_consumption",
    
    # Section2 sensors
    "lux_total_solar_array_1": "lux_total_solar_array_1",
    "lux_total_solar_array_2": "lux_total_solar_array_2",
    "lux_total_solar_array_3": "lux_total_solar_array_3",
    "lux_power_from_inverter_total": "lux_power_from_inverter_total",
    "lux_power_to_inverter_total": "lux_power_to_inverter_total",
    "lux_total_battery_charge": "lux_total_battery_charge",
    "lux_total_battery_discharge": "lux_total_battery_discharge",
    "lux_power_to_eps_total": "lux_power_to_eps_total",
    "lux_power_to_grid_total": "lux_power_to_grid_total",
    "lux_power_from_grid_total": "lux_power_from_grid_total",
    "lux_fault_code": "lux_fault_code",
    "lux_warning_code": "lux_warning_code",
    "lux_internal_temp": "lux_internal_temp",
    "lux_radiator1_temp": "lux_radiator1_temp",
    "lux_radiator2_temp": "lux_radiator2_temp",
    "lux_battery_temperature_live": "lux_battery_temperature_live",
    "lux_uptime": "lux_uptime",
    "lux_total_solar": "lux_total_solar",
    "lux_home_consumption_total": "lux_home_consumption_total",
    
    # Section3 sensors
    "lux_bms_limit_charge": "lux_bms_limit_charge",
    "lux_bms_limit_discharge": "lux_bms_limit_discharge",
    "charge_voltage_ref": "charge_voltage_ref",
    "discharge_cutoff_voltage": "discharge_cutoff_voltage",
    "battery_status_inv": "battery_status_inv",
    "lux_battery_count": "lux_battery_count",
    "lux_battery_capacity_ah": "lux_battery_capacity_ah",
    "lux_battery_current": "lux_battery_current",
    "max_cell_volt": "max_cell_volt",
    "min_cell_volt": "min_cell_volt",
    "max_cell_temp": "max_cell_temp",
    "min_cell_temp": "min_cell_temp",
    "lux_battery_cycle_count": "lux_battery_cycle_count",
    "lux_home_consumption_2_live": "lux_home_consumption_2_live",
    "lux_home_consumption_2_live_alias": "lux_home_consumption_2_live_alias",
    
    # Section4 sensors
    "lux_current_generator_voltage": "lux_current_generator_voltage",
    "lux_current_generator_frequency": "lux_current_generator_frequency",
    "lux_current_generator_power": "lux_current_generator_power",
    "lux_current_generator_power_daily": "lux_current_generator_power_daily",
    "lux_current_generator_power_all": "lux_current_generator_power_all",
    "lux_current_eps_L1_voltage": "lux_current_eps_L1_voltage",
    "lux_current_eps_L2_voltage": "lux_current_eps_L2_voltage",
    "lux_current_eps_L1_watt": "lux_current_eps_L1_watt",
    "lux_current_eps_L2_watt": "lux_current_eps_L2_watt",
    
    # Section5 sensors
    "p_load_ongrid": "p_load_ongrid",
    "e_load_day": "e_load_day",
    "e_load_all_l": "e_load_all_l",
}

SENSOR_TYPES = {
    # System sensors
    "lux_firmware_version": text_sensor.text_sensor_schema(icon="mdi:chip"),
    "lux_inverter_model": text_sensor.text_sensor_schema(icon="mdi:solar-panel"),
    "lux_status_text": text_sensor.text_sensor_schema(icon="mdi:information-outline"),
    "lux_battery_status_text": text_sensor.text_sensor_schema(icon="mdi:battery-alert"),
    "lux_data_last_received_time": sensor.sensor_schema(icon="mdi:clock", state_class=STATE_CLASS_MEASUREMENT),
    
    # Section1 sensors
    "lux_current_solar_voltage_1": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:solar-panel"),
    "lux_current_solar_voltage_2": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:solar-panel"),
    "lux_current_solar_voltage_3": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:solar-panel"),
    "lux_battery_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:battery"),
    "lux_battery_percent": sensor.sensor_schema(unit_of_measurement=UNIT_PERCENT, device_class=DEVICE_CLASS_BATTERY, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "soh": sensor.sensor_schema(unit_of_measurement=UNIT_PERCENT, icon="mdi:battery-heart", state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "lux_internal_fault": sensor.sensor_schema(icon="mdi:alert-circle", state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "lux_current_solar_output_1": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:solar-power"),
    "lux_current_solar_output_2": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:solar-power"),
    "lux_current_solar_output_3": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:solar-power"),
    "lux_battery_charge": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:battery-plus"),
    "lux_battery_discharge": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:battery-minus"),
    "lux_grid_voltage_r": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "lux_grid_voltage_s": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "lux_grid_voltage_t": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "lux_grid_frequency_live": sensor.sensor_schema(unit_of_measurement=UNIT_HERTZ, device_class=DEVICE_CLASS_FREQUENCY, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=2),
    "lux_grid_voltage_live": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:transmission-tower"),
    "lux_power_from_inverter_live": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:transmission-tower-export"),
    "lux_power_to_inverter_live": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:transmission-tower-import"),
    "lux_power_current_clamp": sensor.sensor_schema(unit_of_measurement=UNIT_AMPERE, device_class=DEVICE_CLASS_CURRENT, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=2, icon="mdi:current-ac"),
    "grid_power_factor": sensor.sensor_schema(icon="mdi:angle-acute", state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=3),
    "eps_voltage_r": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "eps_voltage_s": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "eps_voltage_t": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "eps_frequency": sensor.sensor_schema(unit_of_measurement=UNIT_HERTZ, device_class=DEVICE_CLASS_FREQUENCY, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=2),
    "lux_power_to_eps": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:power-plug-off"),
    "apparent_eps_power": sensor.sensor_schema(unit_of_measurement="VA", state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:flash"),
    "lux_power_to_grid_live": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:transmission-tower-export"),
    "lux_power_from_grid_live": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:transmission-tower-import"),
    "lux_daily_solar_array_1": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "lux_daily_solar_array_2": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "lux_daily_solar_array_3": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "lux_power_from_inverter_daily": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "lux_power_to_inverter_daily": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "lux_daily_battery_charge": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "lux_daily_battery_discharge": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "lux_power_to_eps_daily": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "lux_power_to_grid_daily": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "lux_power_from_grid_daily": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "bus1_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:flash-triangle"),
    "bus2_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:flash-triangle"),
    "lux_current_solar_output": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:solar-power"),
    "lux_daily_solar": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2, icon="mdi:solar-panel"),
    "lux_power_to_home": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:home-lightning-bolt"),
    "lux_battery_flow": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:battery-arrow-up-down"),
    "lux_grid_flow": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:transmission-tower"),
    "lux_home_consumption_live": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:home-lightning-bolt"),
    "lux_home_consumption": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2, icon="mdi:home-lightning-bolt-outline"),
    
    # Section2 sensors
    "lux_total_solar_array_1": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "lux_total_solar_array_2": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "lux_total_solar_array_3": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "lux_power_from_inverter_total": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "lux_power_to_inverter_total": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "lux_total_battery_charge": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "lux_total_battery_discharge": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "lux_power_to_eps_total": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "lux_power_to_grid_total": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "lux_power_from_grid_total": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "lux_fault_code": sensor.sensor_schema(icon="mdi:alert-circle", state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "lux_warning_code": sensor.sensor_schema(icon="mdi:alert", state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "lux_internal_temp": sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, device_class=DEVICE_CLASS_TEMPERATURE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "lux_radiator1_temp": sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, device_class=DEVICE_CLASS_TEMPERATURE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "lux_radiator2_temp": sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, device_class=DEVICE_CLASS_TEMPERATURE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "lux_battery_temperature_live": sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, device_class=DEVICE_CLASS_TEMPERATURE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "lux_uptime": sensor.sensor_schema(icon="mdi:timer", state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=0),
    "lux_total_solar": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1, icon="mdi:solar-panel-large"),
    "lux_home_consumption_total": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1, icon="mdi:home-lightning-bolt"),
    
    # Section3 sensors
    "lux_bms_limit_charge": sensor.sensor_schema(unit_of_measurement=UNIT_AMPERE, device_class=DEVICE_CLASS_CURRENT, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=2, icon="mdi:battery-arrow-up"),
    "lux_bms_limit_discharge": sensor.sensor_schema(unit_of_measurement=UNIT_AMPERE, device_class=DEVICE_CLASS_CURRENT, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=2, icon="mdi:battery-arrow-down"),
    "charge_voltage_ref": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "discharge_cutoff_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1),
    "battery_status_inv": sensor.sensor_schema(icon="mdi:information", state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "lux_battery_count": sensor.sensor_schema(icon="mdi:numeric", state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "lux_battery_capacity_ah": sensor.sensor_schema(unit_of_measurement="Ah", icon="mdi:battery-high", state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0),
    "lux_battery_current": sensor.sensor_schema(unit_of_measurement=UNIT_AMPERE, device_class=DEVICE_CLASS_CURRENT, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=2),
    "max_cell_volt": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=3, icon="mdi:battery-plus"),
    "min_cell_volt": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=3, icon="mdi:battery-minus"),
    "max_cell_temp": sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, device_class=DEVICE_CLASS_TEMPERATURE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:thermometer-high"),
    "min_cell_temp": sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, device_class=DEVICE_CLASS_TEMPERATURE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:thermometer-low"),
    "lux_battery_cycle_count": sensor.sensor_schema(icon="mdi:battery-sync", state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=0),
    "lux_home_consumption_2_live": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:home-lightning-bolt"),
    #"lux_home_consumption_2_live_alias": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:home-lightning-bolt"),
    
    # Section4 sensors
    "lux_current_generator_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:engine"),
    "lux_current_generator_frequency": sensor.sensor_schema(unit_of_measurement=UNIT_HERTZ, device_class=DEVICE_CLASS_FREQUENCY, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=2, icon="mdi:sine-wave"),
    "lux_current_generator_power": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:engine"),
    "lux_current_generator_power_daily": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "lux_current_generator_power_all": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
    "lux_current_eps_L1_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:power-plug-off"),
    "lux_current_eps_L2_voltage": sensor.sensor_schema(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=1, icon="mdi:power-plug-off"),
    "lux_current_eps_L1_watt": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:power-plug-off"),
    "lux_current_eps_L2_watt": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:power-plug-off"),
    
    # Section5 sensors
    "p_load_ongrid": sensor.sensor_schema(unit_of_measurement=UNIT_WATT, device_class=DEVICE_CLASS_POWER, state_class=STATE_CLASS_MEASUREMENT, accuracy_decimals=0, icon="mdi:home-lightning-bolt"),
    "e_load_day": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=2),
    "e_load_all_l": sensor.sensor_schema(unit_of_measurement=UNIT_KILOWATT_HOURS, device_class=DEVICE_CLASS_ENERGY, state_class=STATE_CLASS_TOTAL_INCREASING, accuracy_decimals=1),
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
            if yaml_key in ["lux_firmware_version", "lux_inverter_model", 
                            "lux_status_text", "lux_battery_status_text"]:
                sens = await text_sensor.new_text_sensor(conf)
            # Handle regular sensors
            else:
                sens = await sensor.new_sensor(conf)
                
            cg.add(getattr(hub, f"set_{c_name}_sensor")(sens))
