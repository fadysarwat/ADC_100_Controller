/*
 * mcal_gpio.h
 * ADC-100 Controller — Central Pin Map
 */

#pragma once

#include "driver/gpio.h"

/* RS485 */
#define PIN_RS485_DE    GPIO_NUM_4
#define PIN_RS485_DI    GPIO_NUM_5
#define PIN_RS485_RO    GPIO_NUM_6

/* Relay */
#define PIN_RELAY       GPIO_NUM_7

/* Inputs */
#define PIN_REED        GPIO_NUM_8
#define PIN_TAMPER      GPIO_NUM_9
#define PIN_FIRE        GPIO_NUM_10

/* Capacitive Touch */
#define PIN_TOUCH       GPIO_NUM_11

/* Status LED */
#define PIN_LED_R       GPIO_NUM_12
#define PIN_LED_G       GPIO_NUM_13

/* Buzzer */
#define PIN_BUZZER      GPIO_NUM_14

void mcal_gpio_init(void);