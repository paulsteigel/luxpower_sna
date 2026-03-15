"""
ESPHome number platform for LuxPower SNA.

Each entity maps to a holding register (or a bitmask within one).

Example YAML:

  # Percentage (raw = displayed %)
  number:
    - platform: luxpower_sna
      luxpower_sna_id: lux_hub
      name: "System Charge Power Rate"
      register: 64
      min_value: 0
      max_value: 100
      step: 1
      unit_of_measurement: "%"

  # Voltage (raw / 10 = volts)
  number:
    - platform: luxpower_sna
      luxpower_sna_id: lux_hub
      name: "Charge Voltage"
      register: 99
      min_value: 0
      max_value: 600
      step: 0.1
      divisor: 10
      unit_of_measurement: "V"

  # Signed (CT clamp offset, register 119)
  number:
    - platform: luxpower_sna
      luxpower_sna_id: lux_hub
      name: "CT Clamp Offset"
      register: 119
      min_value: -199
      max_value: 199
      step: 1
      signed: true
      unit_of_measurement: "W"

  # Partial bitmask (AC Charge Mode bits in register 120)
  number:
    - platform: luxpower_sna
      luxpower_sna_id: lux_hub
      name: "AC Charge Mode"
      register: 120
      bitmask: 0x0006   # bits 1-2
      bitshift: 1
      min_value: 0
      max_value: 3
      step: 1
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    CONF_ID,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_STEP,
    CONF_UNIT_OF_MEASUREMENT,
    CONF_ICON,
    CONF_MODE,
)

from . import (
    luxpower_sna_ns,
    LUXPOWER_SNA_COMPONENT_SCHEMA,
    CONF_LUXPOWER_SNA_ID,
)

CONF_REGISTER  = "register"
CONF_BITMASK   = "bitmask"
CONF_BITSHIFT  = "bitshift"
CONF_DIVISOR   = "divisor"
CONF_SIGNED    = "signed"

LuxpowerSNANumber = luxpower_sna_ns.class_(
    "LuxpowerSNANumber", number.Number
)

CONFIG_SCHEMA = number.number_schema(LuxpowerSNANumber).extend(
        LUXPOWER_SNA_COMPONENT_SCHEMA
    ).extend({
        cv.Required(CONF_REGISTER):  cv.int_range(min=0, max=239),
        cv.Required(CONF_MIN_VALUE): cv.float_,
        cv.Required(CONF_MAX_VALUE): cv.float_,
        cv.Optional(CONF_STEP,     default=1.0):     cv.positive_float,
        cv.Optional(CONF_BITMASK,  default=0xFFFF):  cv.hex_int,
        cv.Optional(CONF_BITSHIFT, default=0):        cv.int_range(min=0, max=15),
        cv.Optional(CONF_DIVISOR,  default=1):        cv.positive_int,
        cv.Optional(CONF_SIGNED,   default=False):    cv.boolean,
})


async def to_code(config):
    hub = await cg.get_variable(config[CONF_LUXPOWER_SNA_ID])
    var = await number.new_number(
        config,
        min_value=config[CONF_MIN_VALUE],
        max_value=config[CONF_MAX_VALUE],
        step=config[CONF_STEP],
    )

    cg.add(var.set_parent(hub))
    cg.add(var.set_register(config[CONF_REGISTER]))
    cg.add(var.set_bitmask(config[CONF_BITMASK]))
    cg.add(var.set_bitshift(config[CONF_BITSHIFT]))
    cg.add(var.set_divisor(config[CONF_DIVISOR]))
    cg.add(var.set_signed(config[CONF_SIGNED]))

    cg.add(hub.register_number(var))
