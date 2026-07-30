/*
 * hal_buzzer.c
 * ADC-100 Controller
 */

#include "hal_buzzer.h"
#include "mcal_gpio.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "HAL_BUZZER";

static void beep(int on_ms, int off_ms, int times)
{
    for (int i = 0; i < times; i++) {
        gpio_set_level(PIN_BUZZER, 1);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        gpio_set_level(PIN_BUZZER, 0);
        if (i < times - 1)
            vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
}

void hal_buzzer_init(void)
{
    gpio_set_level(PIN_BUZZER, 0);
    ESP_LOGI(TAG, "Buzzer init done");
}

void hal_buzzer_granted(void)
{
    beep(100, 50, 2);   /* 2 short beeps */
}

void hal_buzzer_denied(void)
{
    beep(500, 0, 1);    /* 1 long beep */
}

void hal_buzzer_silent(void)
{
    /* Duress — no sound */
}

void hal_buzzer_alert(void)
{
    beep(200, 100, 5);  /* 5 rapid beeps */
}