/*
 * mcal_rs485.h
 * ADC-100 Controller
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

void      mcal_rs485_init(void);
esp_err_t mcal_rs485_send(const uint8_t *data, size_t len);
int       mcal_rs485_recv(uint8_t *buf, size_t max_len, uint32_t timeout_ms);