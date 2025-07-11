To help anyone run a LuxPower logger from a remote site and publish data to a Home Assistant server via MQTT, I’ve started working on a solution based on insights from the excellent LuxPython_DEV project, which provides a Python-based integration for Home Assistant.
While that integration works well, it requires users to open a port and configure port forwarding to the WiFi dongle on the remote router. This setup might be fine in some countries, but in parts of Vietnam—where ISPs often assign non-public (local) WAN IPs—remote access becomes impossible.
As a first milestone, I now have a working ESP32-based LuxPower logger using ESPHome firmware. It supports read-only mode and can expose almost all available sensors from the LuxPower Integration.
To use this custom component, simply add the following to your ESPHome YAML configuration:
# External component definitions:
external_components: # skip this line if you already have this
```
#Add the two following lines
  - source: github://paulsteigel/luxpower_sna@main
    refresh: 0s   
#Definition for the component
luxpower_sna:
  id: luxpower_hub
  host: "xxx.xxx.xxx.xxx" # Internal IP address of your Wifi Dongle
  port: 8000
  dongle_serial: "xxxxxxxxxx" # Your Dongle Serial
  inverter_serial_number: "xxxxxxxxxx" # Your Inverter serial number. Currently this is not needed but just leave it
  update_interval: 20s
  
# Definition sensors, you should be selective on the following sensor, keep what you need for later Lovelace UI CARD
sensor:
  - platform: luxpower_sna
    id: my_luxpower_sna # Replace with the ID of your luxpower_sna component
    update_interval: 20s

    # --- PV Sensors ---
    pv1_voltage:
      name: "PV1 Voltage"
    pv2_voltage:
      name: "PV2 Voltage"
    pv3_voltage:
      name: "PV3 Voltage"
    pv1_power:
      name: "PV1 Power"
    pv2_power:
      name: "PV2 Power"
    pv3_power:
      name: "PV3 Power"

    # --- Battery Sensors ---
    battery_voltage:
      name: "Battery Voltage"
    battery_current:
      name: "Battery Current"
    soc:
      name: "Battery SOC"
    soh:
      name: "Battery SOH"
    charge_power:
      name: "Charge Power"
    discharge_power:
      name: "Discharge Power"
    charge_voltage_ref:
      name: "Charge Voltage Ref"
    discharge_cutoff_voltage:
      name: "Discharge Cutoff Voltage"
    max_charge_current:
      name: "Max Charge Current"
    max_discharge_current:
      name: "Max Discharge Current"
    battery_count:
      name: "Battery Count"
    battery_capacity:
      name: "Battery Capacity"
    battery_status_inv:
      name: "Battery Status"
    max_cell_voltage:
      name: "Max Cell Voltage"
    min_cell_voltage:
      name: "Min Cell Voltage"
    max_cell_temp:
      name: "Max Cell Temp"
    min_cell_temp:
      name: "Min Cell Temp"
    cycle_count:
      name: "Cycle Count"

    # --- Grid Sensors ---
    power_from_grid:
      name: "Power From Grid"
    power_to_grid:
      name: "Power To Grid"
    grid_voltage_r:
      name: "Grid Voltage R"
    grid_voltage_s:
      name: "Grid Voltage S"
    grid_voltage_t:
      name: "Grid Voltage T"
    grid_frequency:
      name: "Grid Frequency"
    power_factor:
      name: "Power Factor"

    # --- Inverter & System Sensors ---
    inverter_power:
      name: "Inverter Power"
    p_load2:
      name: "P Load2"
    bus1_voltage:
      name: "Bus1 Voltage"
    bus2_voltage:
      name: "Bus2 Voltage"
    temp_inner:
      name: "Temp Inner"
    temp_radiator:
      name: "Temp Radiator"
    temp_radiator2:
      name: "Temp Radiator 2"
    temp_battery:
      name: "Temp Battery"
    uptime:
      name: "Uptime"

    # --- EPS (Backup) Sensors ---
    eps_active_power:
      name: "EPS Active Power"
    eps_apparent_power:
      name: "EPS Apparent Power"
    eps_voltage_r:
      name: "EPS Voltage R"
    eps_voltage_s:
      name: "EPS Voltage S"
    eps_voltage_t:
      name: "EPS Voltage T"
    eps_frequency:
      name: "EPS Frequency"
    eps_L1_volt:
      name: "EPS L1 Voltage"
    eps_L2_volt:
      name: "EPS L2 Voltage"
    eps_L1_watt:
      name: "EPS L1 Power"
    eps_L2_watt:
      name: "EPS L2 Power"

    # --- Generator Sensors ---
    gen_input_volt:
      name: "Gen Input Voltage"
    gen_input_freq:
      name: "Gen Input Frequency"
    gen_power_watt:
      name: "Gen Power"
      
    # --- Daily Energy Sensors (kWh) ---
    pv1_energy_today:
      name: "PV1 Energy Today"
    pv2_energy_today:
      name: "PV2 Energy Today"
    pv3_energy_today:
      name: "PV3 Energy Today"
    inverter_energy_today:
      name: "Inverter Energy Today"
    ac_charging_today:
      name: "AC Charging Today"
    charging_today:
      name: "Charging Today"
    discharging_today:
      name: "Discharging Today"
    eps_today:
      name: "EPS Today"
    exported_today:
      name: "Exported Today"
    grid_today:
      name: "Grid Today"
    gen_power_day:
      name: "Gen Power Today"

    # --- Total Energy Sensors (kWh) ---
    total_pv1_energy:
      name: "Total PV1 Energy"
    total_pv2_energy:
      name: "Total PV2 Energy"
    total_pv3_energy:
      name: "Total PV3 Energy"
    total_inverter_output:
      name: "Total Inverter Output"
    total_recharge_energy:
      name: "Total Recharge Energy"
    total_charged:
      name: "Total Charged"
    total_discharged:
      name: "Total Discharged"
    total_eps_energy:
      name: "Total EPS Energy"
    total_exported:
      name: "Total Exported"
    total_imported:
      name: "Total Imported"
    gen_power_all:
      name: "Total Gen Power"

# 2. Define the text sensor for the inverter serial
text_sensor:
  - platform: luxpower_sna
    luxpower_sna_id: luxpower_hub
    inverter_serial:
      name: "Inverter Serial Number"
```

Here’s a snapshot of the sensor list on my combined ESP32-S2 Lolin logger for JKBMS and LuxPower:
<img width="340" height="784" alt="image" src="https://github.com/user-attachments/assets/ed58fca8-83d4-4c9d-bacf-e270755629cc" />
I'm excited to share that the logger is already functional in read-only mode, and it integrates almost all the sensors available in the official LuxPower Integration.
In the coming days, I’ll begin adding support for buttons, switches, and time-based control—making it possible to fully control your LuxPower inverter just as easily as with the original Integration by @guybw's team. I also plan to design a Lovelace card to improve the Home Assistant dashboard experience.
🔗 Component link: https://github.com/paulsteigel/luxpower_sna
**Creadits:**
+ https://github.com/guybw/LuxPython_DEV
