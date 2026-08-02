idf_component_register(
    SRCS "src/hal_rs485.c"
         "src/hal_relay.c"
         "src/hal_led.c"
         "src/hal_buzzer.c"
         "src/hal_door.c"
         "src/hal_touch.c"
         "src/hal_wifi.c"
         "src/hal_mqtt.c"
         "src/hal_ble.c"
    INCLUDE_DIRS "include"
    REQUIRES mcal driver esp_timer esp_wifi esp_event esp_netif mqtt nvs_flash esp_http_server mbedtls bt
)