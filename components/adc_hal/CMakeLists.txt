idf_component_register(
    SRCS "src/hal_rs485.c"
         "src/hal_relay.c"
         "src/hal_led.c"
         "src/hal_buzzer.c"
         "src/hal_door.c"
         "src/hal_touch.c"
    INCLUDE_DIRS "include"
    REQUIRES mcal driver esp_timer
)