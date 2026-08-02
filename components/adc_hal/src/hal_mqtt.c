/*
 * hal_mqtt.c
 * ADC-100 Controller — MQTT client
 */

#include "hal_mqtt.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "HAL_MQTT";

static esp_mqtt_client_handle_t s_client    = NULL;
static mqtt_message_cb_t        s_msg_cb    = NULL;
static bool                     s_connected = false;

/* MQTT event handler */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {

		case MQTT_EVENT_CONNECTED:
		    s_connected = true;
		    ESP_LOGI(TAG, "MQTT connected");
		    /* Notify app layer that connection is ready */
		    if (s_msg_cb) {
		        s_msg_cb("$connected", "", 0);
		    }
		    break;

        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "MQTT disconnected — will retry");
            break;

        case MQTT_EVENT_DATA:
            if (s_msg_cb && event->topic && event->data) {
                char topic[128] = {0};
                char data[256]  = {0};
                int  tlen = event->topic_len < 127 ? event->topic_len : 127;
                int  dlen = event->data_len  < 255 ? event->data_len  : 255;
                memcpy(topic, event->topic, tlen);
                memcpy(data,  event->data,  dlen);
                s_msg_cb(topic, data, dlen);
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            break;

        default:
            break;
    }
}

/* ── Public API ── */

void hal_mqtt_init(const char *broker_url, const char *username,
                   const char *password, const char *device_id)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri                    = broker_url,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.username                  = username,
        .credentials.authentication.password   = password,
        .credentials.client_id                 = device_id,
        .session.keepalive                     = 60,
        .network.reconnect_timeout_ms          = 5000,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);

    ESP_LOGI(TAG, "MQTT client started — broker: %s", broker_url);
}

bool hal_mqtt_is_connected(void)
{
    return s_connected;
}

void hal_mqtt_publish(const char *topic, const char *data)
{
    if (!s_connected || !s_client) {
        ESP_LOGW(TAG, "MQTT not connected — cannot publish");
        return;
    }
    esp_mqtt_client_publish(s_client, topic, data, 0, 1, 0);
    ESP_LOGI(TAG, "Published to %s: %s", topic, data);
}

void hal_mqtt_subscribe(const char *topic)
{
    if (!s_client) return;
    esp_mqtt_client_subscribe(s_client, topic, 1);
    ESP_LOGI(TAG, "Subscribed to %s", topic);
}

void hal_mqtt_set_message_callback(mqtt_message_cb_t cb)
{
    s_msg_cb = cb;
}