#pragma once
// lux_local_server.h
// TCP :8000 — LuxApp local connection
// ESP32 pushes shared_state data to the app using the same A1 1A protocol.
// Call lux_local_server_start() after WiFi is connected.

void lux_local_server_start(void);