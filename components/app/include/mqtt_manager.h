/*
 * mqtt_manager.h
 * ADC-100 Controller — MQTT business logic
 */

#pragma once

#include <stdbool.h>

/* Initialize MQTT manager — call after WiFi connected */
void mqtt_manager_init(const char *device_id);

/* Publish access event to cloud */
void mqtt_manager_publish_event(const char *event_type,
                                const char *method,
                                const char *result);

/* Publish device status to cloud */
void mqtt_manager_publish_status(void);

/* Publish periodic heartbeat — full device health snapshot */
void mqtt_manager_publish_heartbeat(void);