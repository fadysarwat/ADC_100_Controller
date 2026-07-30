/*
 * hal_buzzer.h
 * ADC-100 Controller
 */

#pragma once

void hal_buzzer_init(void);
void hal_buzzer_granted(void);
void hal_buzzer_denied(void);
void hal_buzzer_silent(void);
void hal_buzzer_alert(void);