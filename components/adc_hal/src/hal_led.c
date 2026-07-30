/*
 * hal_led.c
 * ADC-100 Controller
 */

#include "hal_led.h"
#include "mcal_gpio.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "HAL_LED";

void hal_led_init(void)
{
    gpio_set_level(PIN_LED_R, 0);
    gpio_set_level(PIN_LED_G, 0);
    ESP_LOGI(TAG, "LED init done");
}

void hal_led_idle(void)
{
    gpio_set_level(PIN_LED_R, 0);
    gpio_set_level(PIN_LED_G, 0);
}

void hal_led_green(void)
{
    gpio_set_level(PIN_LED_R, 0);
    gpio_set_level(PIN_LED_G, 1);
}

void hal_led_red(void)
{
    gpio_set_level(PIN_LED_R, 1);
    gpio_set_level(PIN_LED_G, 0);
}

void hal_led_off(void)
{
    gpio_set_level(PIN_LED_R, 0);
    gpio_set_level(PIN_LED_G, 0);
}

