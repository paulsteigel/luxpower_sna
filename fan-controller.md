#This is the code for fan-controller for luxpower fan replacement.
# This is an alternative option for replacing luxpower fan control. My SNA5000 WM have all three fans died and I decide to make this replacement.
The project require 2-3 fans (4 wires), and esp8266-12F and few other stuff. We use power from the inverter itself
The board is ready on easyeda at the following links: https://u.easyeda.com/account/user/projects/index/detail?project=60d399b81286460786372871727a968b&listType=all
<img width="785" height="521" alt="image" src="https://github.com/user-attachments/assets/0540eb91-ed14-4794-ba54-849835e31dd8" />
And here is the yaml code for esphome

substitutions:
  name: fan_controller
  temperature_threshold_low_default: "30.0" # Default value for low temperature threshold
  temperature_threshold_high_default: "50.0" # Default value for high temperature threshold
  minimum_fan_level_default: "20.0" # Default value for minimum fan level

esphome:
  name: ${name}
  friendly_name: 0001-fan-controller
  on_boot: 
    priority: -100 # Run after other components are initialized
    then:
      - fan.turn_on:
          id: ${name}_fan1
          speed: !lambda "return id(${name}_minimum_fan_level_on_start).state / 100.0;" # Set initial fan speed based on percentage
      - fan.turn_on:
          id: ${name}_fan2
          speed: !lambda "return id(${name}_minimum_fan_level_on_start).state / 100.0;" # Set initial fan speed based on percentage

esp8266:
  board: d1_mini 

# Reduced logging to improve stability
logger:
  level: DEBUG

wifi:
  use_address: [host].freemyip.com # for remote ota (i have a version with ota-http but not yet included here as it will require http server)
  ap:
    ssid: ""

mqtt:
  broker: host
  username: mqtt_user
  password: pass
  client_id: ${name}_mqtt

captive_portal:
web_server:
  port: 80

ota:
  - platform: esphome
    port: 8266
    password: "D1ndh1sk@"

packages:
  dev_info_pkg: !include ./commons/dev_info.yaml # for quick inclusion of some sensors (up time, Ip, wifi strength)

# Temperature sensors
one_wire:
  - platform: gpio
    id: ${name}_dallas
    pin: D4

sensor:
  - platform: dallas_temp
    address: 0xb200000cc1dd0e28
    id: ${name}_temp_sensor1
    name: "${name} Heat sink 1"
    update_interval: 10s
    accuracy_decimals: 2
    filters:
      - throttle: 5s # Reduce update frequency
      - sliding_window_moving_average:
          window_size: 5
          send_every: 1

    # Example configuration entry
  - platform: copy
    source_id: ${name}_temp_sensor1
    id: ${name}_copy_temp_sensor1
    name: "${name} Copy of source_sensor 1"
    internal: True
    on_value:
      then:
        - script.execute: ${name}_set_fan1_state

  - platform: dallas_temp
    address: 0x7b3c01d6079a0b28
    id: ${name}_temp_sensor2
    name: "${name} Heat sink 2"
    update_interval: 10s
    accuracy_decimals: 2
    filters:
      - throttle: 5s
      - sliding_window_moving_average:
          window_size: 5
          send_every: 1

    # Example configuration entry
  - platform: copy
    source_id: ${name}_temp_sensor2
    name: "${name}Copy of source_sensor"
    id: ${name}_copy_temp_sensor2
    internal: True
    on_value:
      then:
        - script.execute: ${name}_set_fan2_state

  # RPM sensors
  - platform: pulse_counter
    pin: D1
    id: ${name}_fan_rpm_1
    name: "${name} Fan RPM 1"
    update_interval: 5s
    unit_of_measurement: "V/phút"
    accuracy_decimals: 0
    filters:
      - multiply: 1.0

  - platform: pulse_counter
    pin: D6
    id: ${name}_fan_rpm_2
    name: "${name} Fan RPM 2"
    update_interval: 5s
    unit_of_measurement: "V/phút"
    accuracy_decimals: 0
    filters:
      - multiply: 1.0

output:
  - platform: esp8266_pwm
    id: ${name}_pwm1
    pin: D5
    frequency: 25000 Hz

  - platform: esp8266_pwm
    id: ${name}_pwm2
    pin: D2
    frequency: 25000 Hz

fan:
  - platform: speed
    id: ${name}_fan1
    output: ${name}_pwm1
    name: "PWM Fan 1"
    restore_mode: RESTORE_DEFAULT_ON

  - platform: speed
    id: ${name}_fan2
    output: ${name}_pwm2
    name: "PWM Fan 2"
    restore_mode: RESTORE_DEFAULT_ON

switch:
  - platform: template
    name: "Auto Fan Control"
    id: ${name}_auto_mode_switch
    restore_mode: RESTORE_DEFAULT_ON
    optimistic: true # Assumes the state changes immediately

number:
  - platform: template
    name: "Auto Control Min Temp"
    id: ${name}_auto_control_min_temp
    min_value: 0.0
    max_value: 100.0
    step: 0.1
    unit_of_measurement: "°C"
    mode: BOX
    initial_value: ${temperature_threshold_low_default}
    optimistic: true
    restore_value: true

  - platform: template
    name: "Auto Control Max Temp"
    id: ${name}_auto_control_max_temp
    min_value: 0.0
    max_value: 100.0
    step: 0.1
    unit_of_measurement: "°C"
    mode: BOX
    initial_value: ${temperature_threshold_high_default}
    optimistic: true
    restore_value: true

  - platform: template
    name: "Minimum Fan Level on Start"
    id: ${name}_minimum_fan_level_on_start
    min_value: 0.0
    max_value: 100.0
    step: 1.0
    unit_of_measurement: "%"
    mode: BOX
    initial_value: ${minimum_fan_level_default}
    optimistic: true
    restore_value: true

script:
  - id: ${name}_set_fan1_state
    then:
      - if:
          condition:
            # Check if auto mode is ON
            - switch.is_on: ${name}_auto_mode_switch
          then:
            - if:
                condition:
                  lambda: |-
                    return id(${name}_copy_temp_sensor1).state < id(${name}_auto_control_min_temp).state;
                then:
                  - fan.turn_off: ${name}_fan1
                else:
                  - fan.turn_on:
                      id: ${name}_fan1
                      speed: !lambda |-
                        if (id(${name}_copy_temp_sensor1).state >= id(${name}_auto_control_max_temp).state) {
                          // Over upper threshold, fan speed at maximum
                          ESP_LOGD("Fan speed calc", "Temperature is above or equal to upper threshold so setting to max");
                          return 100;
                        }
                        else {
                          float calc_speed = ((100.0 - id(${name}_minimum_fan_level_on_start).state) / 
                            (id(${name}_auto_control_max_temp).state - id(${name}_auto_control_min_temp).state)) * 
                            (id(${name}_copy_temp_sensor1).state - id(${name}_auto_control_min_temp).state) + 
                            id(${name}_minimum_fan_level_on_start).state;
                          ESP_LOGD("Fan speed calc", "calculated speed = %f", calc_speed);
                          return calc_speed;
                        }

  - id: ${name}_set_fan2_state
    then:
      - if:
          condition:
            # Check if auto mode is ON
            - switch.is_on: ${name}_auto_mode_switch
          then:
            - if:
                condition:
                  lambda: |-
                    return id(${name}_copy_temp_sensor2).state < id(${name}_auto_control_min_temp).state;
                then:
                  - fan.turn_off: ${name}_fan2 # Corrected from fan1 to fan2
                else:
                  - fan.turn_on:
                      id: ${name}_fan2 # Corrected from fan1 to fan2
                      speed: !lambda |-
                        if (id(${name}_copy_temp_sensor2).state >= id(${name}_auto_control_max_temp).state) {
                          // Over upper threshold, fan speed at maximum
                          ESP_LOGD("Fan speed calc", "Temperature is above or equal to upper threshold so setting to max");
                          return 100;
                        }
                        else {
                          float calc_speed = ((100.0 - id(${name}_minimum_fan_level_on_start).state) / 
                            (id(${name}_auto_control_max_temp).state - id(${name}_auto_control_min_temp).state)) * 
                            (id(${name}_copy_temp_sensor2).state - id(${name}_auto_control_min_temp).state) + 
                            id(${name}_minimum_fan_level_on_start).state;
                          ESP_LOGD("Fan speed calc", "calculated speed = %f", calc_speed);
                          return calc_speed;
                        }
