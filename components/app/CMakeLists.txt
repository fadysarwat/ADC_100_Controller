idf_component_register(
    SRCS "src/controller_app.c"
         "src/credential_store.c"
         "src/access_manager.c"
         "src/mqtt_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES adc_hal mcal nvs_flash esp_timer bt
)