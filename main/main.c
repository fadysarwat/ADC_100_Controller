#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mcal_gpio.h"
#include "hal_led.h"
#include "hal_buzzer.h"
#include "hal_relay.h"
#include "hal_door.h"
#include "hal_touch.h"
#include "hal_rs485.h"
#include "hal_wifi.h"
#include "hal_ble.h"
#include "hal_ntp.h"
#include "mqtt_manager.h"
#include "controller_app.h"
#include "access_manager.h"

static const char *TAG = "CTRL";

#define DEVICE_ID "ctrl_001"

/* BLE open callback — called when valid BLE command received */
static void on_ble_open(void)
{
    ESP_LOGI(TAG, "BLE open command received");
    access_token_t token = { .method = ACCESS_METHOD_BLE };
    access_manager_check(&token);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ADC-100 Controller ===");

    /* NVS must be initialized before WiFi and credentials */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    mcal_gpio_init();
    hal_led_init();
    hal_buzzer_init();
    hal_relay_init();
    hal_door_init();
    hal_touch_init();
    hal_rs485_init();

    /* Initialize BLE */
    hal_ble_set_open_callback(on_ble_open);
    hal_ble_init(DEVICE_ID);

    /* Connect to WiFi — starts provisioning if no credentials saved */
    hal_wifi_init();
    hal_wifi_wait_connected();

    /* Sync NTP time — must be after WiFi */
    if (hal_wifi_is_connected()) {
        hal_ntp_init();
    }

    /* Start MQTT if WiFi connected */
    if (hal_wifi_is_connected()) {
        mqtt_manager_init(DEVICE_ID);
    } else {
        ESP_LOGW(TAG, "WiFi failed — running offline");
    }

    /* Set event callback — publishes access events to MQTT */
    access_manager_set_event_callback(mqtt_manager_publish_event);

    /* Start controller tasks */
    controller_app_start();

    hal_led_green();
    ESP_LOGI(TAG, "Controller ready...");
}