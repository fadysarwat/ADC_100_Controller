/*
 * hal_ble.h
 * ADC-100 Controller — BLE beacon and command receiver
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define BLE_CHALLENGE_LEN  32
#define BLE_RESPONSE_LEN   32
#define BLE_SECRET_LEN     32

//default secret key
#define BLE_DEFAULT_SECRET  "ADC100-DEFAULT-KEY" 

/* Callback when a valid open command is received via BLE */
typedef void (*ble_open_cb_t)(void);

void hal_ble_init(const char *device_id);
void hal_ble_start(void);
void hal_ble_stop(void);
void hal_ble_set_open_callback(ble_open_cb_t cb);
void hal_ble_set_secret(const uint8_t *secret, size_t len);
bool hal_ble_is_active(void);