/*
 * hal_mqtt.h
 * ADC-100 Controller — MQTT client
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* MQTT event callback type */
typedef void (*mqtt_message_cb_t)(const char *topic, const char *data, int data_len);

void hal_mqtt_init(const char *broker_url, const char *username,
                   const char *password, const char *device_id);
bool hal_mqtt_is_connected(void);
void hal_mqtt_publish(const char *topic, const char *data);
void hal_mqtt_subscribe(const char *topic);
void hal_mqtt_set_message_callback(mqtt_message_cb_t cb);