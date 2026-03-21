idf_component_register(
    SRCS
        "main.cpp"
        "shared_state.c"
        "lux_cloud.c"
        "lux_mqtt.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        lwip
        mqtt
        freertos
        esp_common
)
