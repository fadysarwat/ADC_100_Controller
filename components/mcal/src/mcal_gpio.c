
/*
 * mcal_gpio.c
 * ADC-100 Controller
 */

#include "mcal_gpio.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "MCAL_GPIO";

void mcal_gpio_init(void)
{
    /* Outputs */
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << PIN_RELAY)   |
                        (1ULL << PIN_LED_R)    |
                        (1ULL << PIN_LED_G)    |
                        (1ULL << PIN_BUZZER),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);

    /* Default state */
    gpio_set_level(PIN_RELAY,  0);
    gpio_set_level(PIN_LED_R,  0);
    gpio_set_level(PIN_LED_G,  0);
    gpio_set_level(PIN_BUZZER, 0);

    /* Inputs with pullup */
    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << PIN_REED)   |
                        (1ULL << PIN_TAMPER)  |
                        (1ULL << PIN_FIRE),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_cfg);

    ESP_LOGI(TAG, "GPIO init done");
}

