/*
 * mqtt_manager.c
 * ADC-100 Controller — MQTT business logic
 */

#include "mqtt_manager.h"
#include "hal_mqtt.h"
#include "hal_wifi.h"
#include "hal_ntp.h"
#include "hal_door.h"
#include "hal_relay.h"
#include "credential_store.h"
#include "access_manager.h"
#include "schedule_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "mbedtls/md.h"
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "MQTT_MGR";

#define FIRMWARE_VERSION        "1.0.0"
#define CMD_REPLAY_WINDOW_S     30
#define MQTT_CMD_SECRET         "ADC100_MQTT_SECRET_KEY_2024"

/* Device topic strings */
static char s_topic_cmd[128]    = {0};
static char s_topic_event[128]  = {0};
static char s_topic_status[128] = {0};
static char s_topic_hb[128]     = {0};
static char s_topic_all[64]     = {0};

/* MQTT reconnect counter */
static uint32_t s_mqtt_reconnects = 0;

/* Minimum free heap tracked since boot */
static uint32_t s_min_free_heap = UINT32_MAX;

/* ── JSON helpers ── */

static bool json_get_string(const char *json, const char *key,
                            char *out_buf, size_t out_len)
{
    char search[32] = {0};
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    while (*p == ' ') p++;
    if (*p != '"') return false;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return false;
    size_t len = end - p;
    if (len >= out_len) return false;
    memcpy(out_buf, p, len);
    out_buf[len] = '\0';
    return true;
}

static bool json_get_uint32(const char *json, const char *key, uint32_t *out)
{
    char search[32] = {0};
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    while (*p == ' ') p++;
    *out = (uint32_t)strtoul(p, NULL, 10);
    return true;
}

/* ── HMAC-SHA256 verification ── */

static bool verify_hmac(const char *cmd, uint32_t ts, const char *hmac_hex)
{
    char message[64] = {0};
    snprintf(message, sizeof(message), "%s:%" PRIu32, cmd, ts);

    uint8_t expected_bytes[32] = {0};
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx,
                           (const uint8_t *)MQTT_CMD_SECRET,
                           strlen(MQTT_CMD_SECRET));
    mbedtls_md_hmac_update(&ctx,
                           (const uint8_t *)message,
                           strlen(message));
    mbedtls_md_hmac_finish(&ctx, expected_bytes);
    mbedtls_md_free(&ctx);

    char expected_hex[65] = {0};
    for (int i = 0; i < 32; i++) {
        snprintf(&expected_hex[i * 2], 3, "%02x", expected_bytes[i]);
    }

    if (strlen(hmac_hex) != 64) return false;
    uint8_t diff = 0;
    for (int i = 0; i < 64; i++) {
        diff |= (uint8_t)hmac_hex[i] ^ (uint8_t)expected_hex[i];
    }
    return diff == 0;
}

/* ── Credential type parser ── */

static bool parse_cred_type(const char *type_str, cred_type_t *out)
{
    if (strcmp(type_str, "rfid") == 0) { *out = CRED_TYPE_RFID; return true; }
    if (strcmp(type_str, "pin")  == 0) { *out = CRED_TYPE_PIN;  return true; }
    if (strcmp(type_str, "qr")   == 0) { *out = CRED_TYPE_QR;   return true; }
    return false;
}

/* ── Credential command handler ── */

static void handle_credential_cmd(const char *cmd, const char *data)
{
    /* clear_credentials */
    if (strcmp(cmd, "clear_credentials") == 0) {
        int count = cred_store_count();
        for (int i = 0; i < count; i++) {
            char label[32];
            snprintf(label, sizeof(label), "cred_%d", i);
            cred_store_remove(label);
        }
        ESP_LOGW(TAG, "All credentials cleared");
        hal_mqtt_publish(s_topic_event,
                         "{\"event\":\"credential\",\"cmd\":\"clear\",\"result\":\"OK\"}");
        return;
    }

    /* delete_credential — محتاج label بس */
    if (strcmp(cmd, "delete_credential") == 0) {
        char label[CRED_LABEL_MAX_LEN] = {0};
        if (!json_get_string(data, "label", label, sizeof(label))) {
            ESP_LOGW(TAG, "Credential cmd: missing label");
            return;
        }
        if (cred_store_remove(label)) {
            ESP_LOGI(TAG, "Credential removed: %s", label);
            char payload[128] = {0};
            snprintf(payload, sizeof(payload),
                     "{\"event\":\"credential\",\"cmd\":\"delete\","
                     "\"label\":\"%s\",\"result\":\"OK\",\"total\":%d}",
                     label, cred_store_count());
            hal_mqtt_publish(s_topic_event, payload);
        } else {
            ESP_LOGW(TAG, "Credential not found: %s", label);
            hal_mqtt_publish(s_topic_event,
                             "{\"event\":\"credential\",\"cmd\":\"delete\",\"result\":\"NOT_FOUND\"}");
        }
        return;
    }

    /* add_credential */
    char type_str[8]  = {0};
    char cred_data[CRED_DATA_MAX_LEN + 1] = {0};
    char label[CRED_LABEL_MAX_LEN]        = {0};
    char duress_str[8] = {0};

    if (!json_get_string(data, "cred_type", type_str, sizeof(type_str))) {
        ESP_LOGW(TAG, "Credential cmd: missing cred_type");
        return;
    }
    if (!json_get_string(data, "cred_data", cred_data, sizeof(cred_data))) {
        ESP_LOGW(TAG, "Credential cmd: missing cred_data");
        return;
    }
    if (!json_get_string(data, "label", label, sizeof(label))) {
        snprintf(label, sizeof(label), "cred_%d", cred_store_count());
    }

    cred_type_t ctype;
    if (!parse_cred_type(type_str, &ctype)) {
        ESP_LOGW(TAG, "Credential cmd: unknown type: %s", type_str);
        return;
    }

    if (strcmp(cmd, "add_credential") == 0) {
        json_get_string(data, "duress", duress_str, sizeof(duress_str));
        bool is_duress = (strcmp(duress_str, "true") == 0);

        credential_t cred = {0};
        cred.type      = ctype;
        cred.data_len  = (uint8_t)strlen(cred_data);
        cred.is_duress = is_duress;
        cred.is_active = true;
        memcpy(cred.data,  cred_data, cred.data_len);
        memcpy(cred.label, label,     sizeof(cred.label) - 1);

        if (cred_store_add(&cred)) {
            ESP_LOGI(TAG, "Credential added: %s type=%s duress=%d",
                     label, type_str, is_duress);
            char payload[128] = {0};
            snprintf(payload, sizeof(payload),
                     "{\"event\":\"credential\",\"cmd\":\"add\","
                     "\"label\":\"%s\",\"result\":\"OK\",\"total\":%d}",
                     label, cred_store_count());
            hal_mqtt_publish(s_topic_event, payload);
        } else {
            ESP_LOGW(TAG, "Credential add failed: store full?");
            hal_mqtt_publish(s_topic_event,
                             "{\"event\":\"credential\",\"cmd\":\"add\",\"result\":\"FAIL\"}");
        }
        return;
    }
}

/* ── Schedule command handler ── */

static void handle_schedule_cmd(const char *data)
{
    char enabled_str[8] = {0};
    char start[6]       = {0};
    char end[6]         = {0};

    if (!json_get_string(data, "enabled", enabled_str, sizeof(enabled_str))) {
        ESP_LOGW(TAG, "Schedule: missing enabled field");
        return;
    }
    if (!json_get_string(data, "start", start, sizeof(start))) {
        ESP_LOGW(TAG, "Schedule: missing start field");
        return;
    }
    if (!json_get_string(data, "end", end, sizeof(end))) {
        ESP_LOGW(TAG, "Schedule: missing end field");
        return;
    }

    schedule_t sched = {0};
    sched.enabled = (strcmp(enabled_str, "true") == 0);
    sscanf(start, "%hhu:%hhu", &sched.start_hour, &sched.start_minute);
    sscanf(end,   "%hhu:%hhu", &sched.end_hour,   &sched.end_minute);

    schedule_manager_set(&sched);

    ESP_LOGI(TAG, "Schedule set — enabled:%d %02d:%02d-%02d:%02d",
             sched.enabled,
             sched.start_hour, sched.start_minute,
             sched.end_hour,   sched.end_minute);

    hal_mqtt_publish(s_topic_event,
                     "{\"event\":\"schedule\",\"result\":\"UPDATED\"}");
}

/* ── Secure command handler ── */

static void handle_secure_cmd(const char *data)
{
    char cmd[32]      = {0};
    char hmac_hex[65] = {0};
    uint32_t ts       = 0;

    if (!json_get_string(data, "cmd", cmd, sizeof(cmd))) {
        ESP_LOGW(TAG, "Anti-replay: missing cmd field");
        return;
    }
    if (!json_get_uint32(data, "ts", &ts)) {
        ESP_LOGW(TAG, "Anti-replay: missing ts field");
        return;
    }
    if (!json_get_string(data, "hmac", hmac_hex, sizeof(hmac_hex))) {
        ESP_LOGW(TAG, "Anti-replay: missing or invalid hmac field");
        return;
    }

    /* Timestamp check */
    uint32_t now_s = hal_ntp_get_time();
    if (now_s == 0) {
        ESP_LOGW(TAG, "NTP not synced — rejecting command");
        return;
    }
    if (now_s > ts + CMD_REPLAY_WINDOW_S) {
        ESP_LOGW(TAG, "Anti-replay: ts too old (ts=%" PRIu32 " now=%" PRIu32 ")", ts, now_s);
        return;
    }
    if (ts > now_s + CMD_REPLAY_WINDOW_S) {
        ESP_LOGW(TAG, "Anti-replay: ts in future (ts=%" PRIu32 " now=%" PRIu32 ")", ts, now_s);
        return;
    }

    /* HMAC check */
    if (!verify_hmac(cmd, ts, hmac_hex)) {
        ESP_LOGW(TAG, "Anti-replay: invalid HMAC — command rejected");
        return;
    }

    ESP_LOGI(TAG, "Secure command verified: %s", cmd);

    if (strcmp(cmd, "open") == 0) {
        access_token_t token = { .method = ACCESS_METHOD_MQTT };
        access_manager_check(&token);
        return;
    }

    if (strcmp(cmd, "set_schedule") == 0) {
        handle_schedule_cmd(data);
        return;
    }

    if (strcmp(cmd, "add_credential")    == 0 ||
        strcmp(cmd, "delete_credential") == 0 ||
        strcmp(cmd, "clear_credentials") == 0) {
        handle_credential_cmd(cmd, data);
        return;
    }

    if (strcmp(cmd, "reset_wifi") == 0) {
        ESP_LOGW(TAG, "WiFi reset command — clearing credentials");
        hal_wifi_reset_credentials();
        return;
    }

    ESP_LOGW(TAG, "Unknown command: %s", cmd);
}

/* ── MQTT message handler ── */

static void on_mqtt_message(const char *topic, const char *data, int data_len)
{
    if (strcmp(topic, "$connected") == 0) {
        s_mqtt_reconnects++;
        ESP_LOGI(TAG, "MQTT ready (reconnects: %" PRIu32 ") — publishing status",
                 s_mqtt_reconnects);
        hal_mqtt_subscribe(s_topic_cmd);
        hal_mqtt_subscribe(s_topic_all);
        mqtt_manager_publish_status();
        return;
    }

    if (strcmp(topic, s_topic_all) == 0) {
        ESP_LOGI(TAG, "Broadcast command received: %s", data);
        handle_secure_cmd(data);
        return;
    }

    if (strcmp(topic, s_topic_cmd) == 0) {
        ESP_LOGI(TAG, "Device command received: %s", data);
        handle_secure_cmd(data);
        return;
    }
}

/* ── Public API ── */

void mqtt_manager_init(const char *device_id)
{
    snprintf(s_topic_cmd,    sizeof(s_topic_cmd),    "adc100/%s/cmd",       device_id);
    snprintf(s_topic_event,  sizeof(s_topic_event),  "adc100/%s/event",     device_id);
    snprintf(s_topic_status, sizeof(s_topic_status), "adc100/%s/status",    device_id);
    snprintf(s_topic_hb,     sizeof(s_topic_hb),     "adc100/%s/heartbeat", device_id);
    snprintf(s_topic_all,    sizeof(s_topic_all),    "adc100/doors/all");

    hal_mqtt_init(
        "mqtts://e1b91c9a41344a7280a17cf27e8b3c3d.s1.eu.hivemq.cloud:8883",
        "adc100",
        "!YANKFrnQH6wkrV",
        device_id
    );

    hal_mqtt_set_message_callback(on_mqtt_message);
    hal_mqtt_subscribe(s_topic_cmd);
    hal_mqtt_subscribe(s_topic_all);
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

void mqtt_manager_publish_heartbeat(void)
{
    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    int rssi = 0;
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }

    char ip_str[16] = "0.0.0.0";
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        }
    }

    uint32_t free_heap = esp_get_free_heap_size();
    if (free_heap < s_min_free_heap) {
        s_min_free_heap = free_heap;
    }

    esp_reset_reason_t reason = esp_reset_reason();
    const char *restart_reason;
    switch (reason) {
        case ESP_RST_POWERON:   restart_reason = "power_on";   break;
        case ESP_RST_SW:        restart_reason = "software";   break;
        case ESP_RST_PANIC:     restart_reason = "panic";      break;
        case ESP_RST_WDT:       restart_reason = "watchdog";   break;
        case ESP_RST_BROWNOUT:  restart_reason = "brownout";   break;
        case ESP_RST_DEEPSLEEP: restart_reason = "deep_sleep"; break;
        default:                restart_reason = "unknown";    break;
    }

    schedule_t sched = {0};
    schedule_manager_get(&sched);

    const char *door_state   = hal_door_is_open()   ? "open"   : "closed";
    const char *relay_state  = hal_relay_is_open()  ? "open"   : "closed";
    const char *tamper_state = hal_door_is_tamper() ? "active" : "normal";
    const char *fire_state   = hal_door_is_fire()   ? "active" : "normal";

    char payload[600] = {0};
    snprintf(payload, sizeof(payload),
             "{"
             "\"fw\":\"%s\","
             "\"uptime\":%" PRIu32 ","
             "\"rssi\":%d,"
             "\"ip\":\"%s\","
             "\"free_heap\":%" PRIu32 ","
             "\"min_heap\":%" PRIu32 ","
             "\"restart_reason\":\"%s\","
             "\"mqtt_reconnects\":%" PRIu32 ","
             "\"door\":\"%s\","
             "\"relay\":\"%s\","
             "\"tamper\":\"%s\","
             "\"fire\":\"%s\","
             "\"credentials\":%d,"
             "\"schedule_enabled\":%s,"
             "\"schedule_start\":\"%02d:%02d\","
             "\"schedule_end\":\"%02d:%02d\""
             "}",
             FIRMWARE_VERSION,
             uptime_s, rssi, ip_str,
             free_heap, s_min_free_heap,
             restart_reason, s_mqtt_reconnects,
             door_state, relay_state, tamper_state, fire_state,
             cred_store_count(),
             sched.enabled ? "true" : "false",
             sched.start_hour, sched.start_minute,
             sched.end_hour,   sched.end_minute);

    hal_mqtt_publish(s_topic_hb, payload);

    ESP_LOGI(TAG, "Heartbeat sent — uptime=%" PRIu32 "s rssi=%d heap=%" PRIu32
             " reconnects=%" PRIu32,
             uptime_s, rssi, free_heap, s_mqtt_reconnects);
}