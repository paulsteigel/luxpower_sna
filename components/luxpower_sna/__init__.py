# components/luxpower_sna/__init__.py
import esphome.codegen as cg
import esphome.config_validation as cv

DEPENDENCIES = ["wifi"]
luxpower_sna_ns = cg.esphome_ns.namespace("luxpower_sna")
