/*
 * hal_ntp.h
 * ADC-100 Controller — NTP time sync
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Initialize and sync NTP — call after WiFi connected */
void hal_ntp_init(void);

/* Returns true if time is synced */
bool hal_ntp_is_synced(void);

/* Returns current Unix timestamp — 0 if not synced */
uint32_t hal_ntp_get_time(void);