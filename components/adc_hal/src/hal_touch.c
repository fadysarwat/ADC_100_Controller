/*
 * hal_touch.c
 * ADC-100 Controller — Capacitive touch input
 * Uses ESP32-S3 built-in touch sensor API
 */

#include "hal_touch.h"
#include "esp_log.h"
#include "driver/touch_sensor.h"

static const char *TAG = "HAL_TOUCH";

#define TOUCH_CHANNEL    TOUCH_PAD_NUM11
#define TOUCH_THRESHOLD  50000

void hal_touch_init(void)
{
    /* Initialize touch pad peripheral */
    touch_pad_init();

    /* Set reference voltage */
    touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5,
                          TOUCH_HVOLT_ATTEN_1V);

    /* Configure touch channel */
    touch_pad_config(TOUCH_CHANNEL);

    /* Set timer trigger mode */
    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);

    /* Enable filter */
    touch_pad_filter_enable();

    /* Start touch sensor */
    touch_pad_fsm_start();

    ESP_LOGI(TAG, "Touch pad init done — TOUCH_PAD_NUM11");
}

bool hal_touch_is_pressed(void)
{
    uint32_t val = 0;
    touch_pad_read_raw_data(TOUCH_CHANNEL, &val);
    //ESP_LOGI("TOUCH", "Raw value: %lu", val);  /* مؤقت للكاليبريشن */
    return val > TOUCH_THRESHOLD;
}