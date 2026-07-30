/*
 * hal_touch.h
 * ADC-100 Controller — Capacitive touch input
 */

#pragma once

#include <stdbool.h>

void hal_touch_init(void);
bool hal_touch_is_pressed(void);