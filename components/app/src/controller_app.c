/*
 * controller_app.c
 * ADC-100 Controller — Main application logic
 */

#include "controller_app.h"
#include "access_manager.h"
#include "hal_rs485.h"
#include "hal_door.h"
#include "hal_touch.h"
#include "hal_led.h"
#include "hal_buzzer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "CTRL_APP";

/* Process incoming RS485 frame from Reader */
static void process_rs485_frame(const rs485_frame_t *frame)
{
    access_token_t token = {0};
    token.data_len = frame->data_len;
    memcpy(token.data, frame->data, frame->data_len);

    /* Map RS485 token type to access method */
    switch (frame->type) {
        case RS485_TYPE_RFID: token.method = ACCESS_METHOD_RFID; break;
        case RS485_TYPE_PIN:  token.method = ACCESS_METHOD_PIN;  break;
        case RS485_TYPE_QR:   token.method = ACCESS_METHOD_QR;   break;
        default:
            ESP_LOGW(TAG, "Unknown token type: 0x%02X", frame->type);
            hal_rs485_send_response(RS485_TYPE_DENIED);
            return;
    }

    /* Check credential and send response back to Reader */
    access_result_t result = access_manager_check(&token);

    switch (result) {
        case ACCESS_RESULT_GRANTED:
            hal_rs485_send_response(RS485_TYPE_GRANTED);
            break;
        case ACCESS_RESULT_DURESS:
            hal_rs485_send_response(RS485_TYPE_DURESS);
            break;
        default:
            hal_rs485_send_response(RS485_TYPE_DENIED);
            break;
    }
}

/* RS485 listener task — waits for frames from Reader */
static void rs485_task(void *arg)
{
    rs485_frame_t frame;
    ESP_LOGI(TAG, "RS485 listener started");

    while (1) {
        if (hal_rs485_recv_frame(&frame, 100)) {
            process_rs485_frame(&frame);
        }
    }
}

/* Door monitor task — checks reed, tamper, fire, touch */
static void door_task(void *arg)
{
    ESP_LOGI(TAG, "Door monitor started");

    while (1) {
        /* Check capacitive touch — open from inside */
        if (hal_touch_is_pressed()) {
            ESP_LOGI(TAG, "Touch pressed — opening door");
            access_token_t token = { .method = ACCESS_METHOD_TOUCH };
            access_manager_check(&token);
            vTaskDelay(pdMS_TO_TICKS(500)); /* debounce */
        }

        door_event_t event = hal_door_poll();

        switch (event) {
            case DOOR_EVENT_OPENED:
                ESP_LOGI(TAG, "Door opened");
                break;

            case DOOR_EVENT_CLOSED:
                ESP_LOGI(TAG, "Door closed");
                break;

            case DOOR_EVENT_HELD_OPEN:
                /* TODO: send MQTT alert */
                ESP_LOGW(TAG, "Door held open alert");
                hal_buzzer_alert();
                break;

            case DOOR_EVENT_TAMPER:
                /* Alert only — do NOT open door */
                ESP_LOGW(TAG, "TAMPER detected — alert only");
                hal_led_red();
                hal_buzzer_alert();
                break;

            case DOOR_EVENT_FIRE:
                /* Force open on fire alarm */
                ESP_LOGW(TAG, "FIRE — forcing door open");
                access_manager_check(&(access_token_t){
                    .method = ACCESS_METHOD_TOUCH
                });
                break;

            default:
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* Main tick task — auto-lock and LED reset */
static void tick_task(void *arg)
{
    while (1) {
        access_manager_tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void controller_app_start(void)
{
    access_manager_init();

    /* Start all tasks */
    xTaskCreate(rs485_task, "rs485_task", 4096, NULL, 5, NULL);
    xTaskCreate(door_task,  "door_task",  4096, NULL, 4, NULL);
    xTaskCreate(tick_task,  "tick_task",  2048, NULL, 3, NULL);

    ESP_LOGI(TAG, "Controller app started");
}