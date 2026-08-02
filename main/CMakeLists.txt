idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES app adc_hal mcal bt
)