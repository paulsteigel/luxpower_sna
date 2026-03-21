#pragma once
// lux_relay.h
// Transparent TCP relay: Real Dongle → ESP32:4346 → Cloud 47.81.11.236:4346
// Parsed C→D frames are fed into shared_state automatically.
// lux_mqtt_task sees the updates and publishes to Home Assistant.

/**
 * Start the relay server (TCP listen on LUX_CLOUD_PORT = 4346).
 * Must be called AFTER WiFi STA is connected.
 * Only used when RELAY_MODE is defined in config.h.
 */
void lux_relay_start(void);