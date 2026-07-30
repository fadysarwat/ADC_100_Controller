/*
 * hal_relay.c
 * ADC-100 Controller
 */

#include "hal_relay.h"
#include "mcal_gpio.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HAL_RELAY";

#define AUTO_LOCK_MS    5000   /* Auto-lock after 5 seconds */

static bool s_is_open = false;
static int64_t s_open_time = 0;

void hal_relay_init(void)
{
    gpio_set_level(PIN_RELAY, 0);
    s_is_open = false;
    ESP_LOGI(TAG, "Relay init done — door locked");
}

void hal_relay_open(void)
{
    gpio_set_level(PIN_RELAY, 1);
    s_is_open   = true;
    s_open_time = esp_timer_get_time();
    ESP_LOGI(TAG, "Relay OPEN");
}

void hal_relay_close(void)
{
    gpio_set_level(PIN_RELAY, 0);
    s_is_open = false;
    ESP_LOGI(TAG, "Relay CLOSED");
}

bool hal_relay_is_open(void)
{
    return s_is_open;
}