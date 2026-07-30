idf_component_register(
    SRCS "src/mcal_gpio.c"
         "src/mcal_rs485.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
)