/*
 * hal_door.h
 * ADC-100 Controller
 * Monitors Reed Switch, Tamper, and Fire Input
 */

#pragma once

#include <stdbool.h>

typedef enum {
    DOOR_EVENT_NONE,
    DOOR_EVENT_OPENED,
    DOOR_EVENT_CLOSED,
    DOOR_EVENT_HELD_OPEN,   /* Door open too long */
    DOOR_EVENT_TAMPER,
    DOOR_EVENT_FIRE,
} door_event_t;

void         hal_door_init(void);
door_event_t hal_door_poll(void);
bool         hal_door_is_open(void);
bool         hal_door_is_tamper(void);
bool         hal_door_is_fire(void);