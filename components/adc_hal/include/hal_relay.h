/*
 * hal_relay.h
 * ADC-100 Controller
 */

#pragma once

#include <stdbool.h>

void hal_relay_init(void);
void hal_relay_open(void);
void hal_relay_close(void);
bool hal_relay_is_open(void);