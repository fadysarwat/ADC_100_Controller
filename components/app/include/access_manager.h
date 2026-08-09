/*
 * access_manager.h
 * ADC-100 Controller
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    ACCESS_RESULT_GRANTED,
    ACCESS_RESULT_DENIED,
    ACCESS_RESULT_DURESS,
    ACCESS_RESULT_LOCKOUT,
    ACCESS_RESULT_FIRE,
    ACCESS_RESULT_TAMPER,
    ACCESS_RESULT_SCHEDULE_DENIED,   /* جديد — رفض بسبب الـ schedule */
} access_result_t;

typedef enum {
    ACCESS_METHOD_RFID,
    ACCESS_METHOD_PIN,
    ACCESS_METHOD_QR,
    ACCESS_METHOD_TOUCH,
    ACCESS_METHOD_BLE,
    ACCESS_METHOD_MQTT,
} access_method_t;

typedef struct {
    access_method_t method;
    uint8_t         data[64];
    uint8_t         data_len;
} access_token_t;

void            access_manager_init(void);
access_result_t access_manager_check(const access_token_t *token);
void            access_manager_tick(void);
void            access_manager_set_event_callback(void (*cb)(const char *event_type,
                                                              const char *method,
                                                              const char *result));