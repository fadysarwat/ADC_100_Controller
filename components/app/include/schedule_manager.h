/*
 * schedule_manager.h
 * ADC-100 Controller — NTP-based access scheduling
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     enabled;       /* الـ scheduling مفعّل ولا لا */
    uint8_t  start_hour;    /* ساعة البداية 0-23 */
    uint8_t  start_minute;  /* دقيقة البداية 0-59 */
    uint8_t  end_hour;      /* ساعة النهاية 0-23 */
    uint8_t  end_minute;    /* دقيقة النهاية 0-59 */
} schedule_t;

/* Initialize — loads schedule from NVS */
void schedule_manager_init(void);

/* Set new schedule and save to NVS */
void schedule_manager_set(const schedule_t *schedule);

/* Get current schedule */
void schedule_manager_get(schedule_t *schedule);

/* Returns true if access is allowed right now */
bool schedule_manager_is_allowed(void);