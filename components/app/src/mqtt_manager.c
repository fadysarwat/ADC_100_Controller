/*
 * mqtt_manager.c
 * ADC-100 Controller — MQTT business logic
 */

#include "mqtt_manager.h"
#include "hal_mqtt.h"
#include "hal_wifi.h"
#include "credential_store.h"
#include "access_manager.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "MQTT_MGR";

/* Device topic prefix */
static char s_topic_cmd[128]    = {0};
static char s_topic_event[128]  = {0};
static char s_topic_status[128] = {0};
static char s_topic_all[64]     = {0};

/* Handle incoming MQTT messages */
static void on_mqtt_message(const char *topic, const char *data, int data_len)
{
    /* Internal connection event */
    if (strcmp(topic, "$connected") == 0) {
        ESP_LOGI(TAG, "MQTT ready — publishing status");
        hal_mqtt_subscribe(s_topic_cmd);
        hal_mqtt_subscribe(s_topic_all);
        mqtt_manager_publish_status();
        return;
    }

    /* Multi-door sync — command for all doors */
    if (strcmp(topic, s_topic_all) == 0) {
        ESP_LOGI(TAG, "Broadcast command received: %s", data);
        if (strstr(data, "\"cmd\":\"open\"")) {
            access_token_t token = { .method = ACCESS_METHOD_MQTT };
            access_manager_check(&token);
        }
        return;
    }

    /* Device-specific command */
    if (strcmp(topic, s_topic_cmd) == 0) {
        ESP_LOGI(TAG, "Device command received: %s", data);

        if (strstr(data, "\"cmd\":\"open\"")) {
            ESP_LOGI(TAG, "Cloud open command — opening door");
            access_token_t token = { .method = ACCESS_METHOD_MQTT };
            access_manager_check(&token);
            return;
        }

        if (strstr(data, "\"cmd\":\"reset_wifi\"")) {
            ESP_LOGW(TAG, "WiFi reset command — clearing credentials");
            hal_wifi_reset_credentials();
            return;
        }

        return;
    }
}

/* ── Public API ── */

void mqtt_manager_init(const char *device_id)
{
    /* Build topic strings */
    snprintf(s_topic_cmd,    sizeof(s_topic_cmd),
             "adc100/%s/cmd", device_id);
    snprintf(s_topic_event,  sizeof(s_topic_event),
             "adc100/%s/event", device_id);
    snprintf(s_topic_status, sizeof(s_topic_status),
             "adc100/%s/status", device_id);
    snprintf(s_topic_all,    sizeof(s_topic_all),
             "adc100/doors/all");

    /* Initialize MQTT client */
    hal_mqtt_init(
        "mqtts://e1b91c9a41344a7280a17cf27e8b3c3d.s1.eu.hivemq.cloud:8883",
        "adc100",
        "!YANKFrnQH6wkrV",
        device_id
    );

    /* Set message callback */
    hal_mqtt_set_message_callback(on_mqtt_message);

    /* Subscribe to topics */
    hal_mqtt_subscribe(s_topic_cmd);
    hal_mqtt_subscribe(s_topic_all);

    /* Publish online status */
    mqtt_manager_publish_status();

    ESP_LOGI(TAG, "MQTT manager ready — device: %s", device_id);
}

void mqtt_manager_publish_event(const char *event_type,
                                const char *method,
                                const char *result)
{
    char payload[256] = {0};
    snprintf(payload, sizeof(payload),
             "{\"event\":\"%s\",\"method\":\"%s\",\"result\":\"%s\"}",
             event_type, method, result);

    hal_mqtt_publish(s_topic_event, payload);
}

void mqtt_manager_publish_status(void)
{
    char payload[128] = {0};
    snprintf(payload, sizeof(payload),
             "{\"status\":\"online\",\"credentials\":%d}",
             cred_store_count());

    hal_mqtt_publish(s_topic_status, payload);
}