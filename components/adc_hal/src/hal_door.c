/*
 * hal_door.c
 * ADC-100 Controller
 */

#include "hal_door.h"
#include "mcal_gpio.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "HAL_DOOR";

#define HELD_OPEN_MS    30000   /* 30 seconds */

static bool    s_last_door_state  = false;
static int64_t s_door_open_time   = 0;
static bool    s_held_open_alerted = false;

void hal_door_init(void)
{
    s_last_door_state  = hal_door_is_open();
    s_door_open_time   = 0;
    s_held_open_alerted = false;
    ESP_LOGI(TAG, "Door monitor init done");
}

bool hal_door_is_open(void)
{
    /* Reed switch: LOW = door open (pulled to GND by magnet missing) */
    return gpio_get_level(PIN_REED) == 1;
}

bool hal_door_is_tamper(void)
{
    /* Active LOW — returns true only when pulled to GND */
    return gpio_get_level(PIN_TAMPER) == 0;
}

bool hal_door_is_fire(void)
{
    return gpio_get_level(PIN_FIRE) == 0;   /* Active LOW */
}

door_event_t hal_door_poll(void)
{
    /* Fire check — highest priority */
    if (hal_door_is_fire()) {
        ESP_LOGW(TAG, "FIRE INPUT detected!");
        return DOOR_EVENT_FIRE;
    }

    /* Tamper check */
    if (hal_door_is_tamper()) {
        ESP_LOGW(TAG, "TAMPER detected!");
        return DOOR_EVENT_TAMPER;
    }

    bool current = hal_door_is_open();

    /* Edge detection */
    if (current && !s_last_door_state) {
        s_last_door_state  = true;
        s_door_open_time   = esp_timer_get_time();
        s_held_open_alerted = false;
        ESP_LOGI(TAG, "Door OPENED");
        return DOOR_EVENT_OPENED;
    }

    if (!current && s_last_door_state) {
        s_last_door_state  = false;
        s_door_open_time   = 0;
        s_held_open_alerted = false;
        ESP_LOGI(TAG, "Door CLOSED");
        return DOOR_EVENT_CLOSED;
    }

    /* Held open check */
    if (current && s_door_open_time != 0 && !s_held_open_alerted) {
        int64_t elapsed = (esp_timer_get_time() - s_door_open_time) / 1000;
        if (elapsed >= HELD_OPEN_MS) {
            s_held_open_alerted = true;
            ESP_LOGW(TAG, "Door HELD OPEN for %lld ms", elapsed);
            return DOOR_EVENT_HELD_OPEN;
        }
    }

    return DOOR_EVENT_NONE;
}