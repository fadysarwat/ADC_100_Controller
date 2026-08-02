/*
 * hal_wifi.h
 * ADC-100 Controller — WiFi connection manager
 */

#pragma once

#include <stdbool.h>

typedef enum {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED,
    WIFI_STATE_PROVISIONING,
} wifi_state_t;

/* Initialize WiFi — starts provisioning if no credentials saved */
void         hal_wifi_init(void);
wifi_state_t hal_wifi_get_state(void);
bool         hal_wifi_is_connected(void);
void         hal_wifi_wait_connected(void);

/* Force re-provisioning (reset saved credentials) */
void         hal_wifi_reset_credentials(void);