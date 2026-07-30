#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mcal_gpio.h"
#include "hal_led.h"
#include "hal_buzzer.h"
#include "hal_relay.h"
#include "hal_door.h"
#include "hal_touch.h"
#include "hal_rs485.h"
#include "controller_app.h"

static const char *TAG = "CTRL";

void app_main(void)
{
    ESP_LOGI(TAG, "=== ADC-100 Controller ===");

    mcal_gpio_init();
    hal_led_init();
    hal_buzzer_init();
    hal_relay_init();
    hal_door_init();
    hal_touch_init();
    hal_rs485_init();

    controller_app_start();

    hal_led_green();
    ESP_LOGI(TAG, "Controller ready...");
}