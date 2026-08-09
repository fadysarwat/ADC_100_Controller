/*
 * access_manager.c
 * ADC-100 Controller
 */

#include "access_manager.h"
#include "schedule_manager.h"
#include "credential_store.h"
#include "hal_relay.h"
#include "hal_led.h"
#include "hal_buzzer.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "ACCESS_MGR";

#define MAX_FAILURES       5
#define LED_ON_DURATION_MS 2000
#define AUTO_LOCK_MS       5000

static int   s_fail_count      = 0;
static int64_t s_led_reset_time  = 0;
static int64_t s_relay_open_time = 0;

/* Event callback — set by main to publish events to MQTT */
static void (*s_event_cb)(const char *event_type,
                          const char *method,
                          const char *result) = NULL;

void access_manager_set_event_callback(void (*cb)(const char *event_type,
                                                   const char *method,
                                                   const char *result))
{
    s_event_cb = cb;
}

/* Get method name as string */
static const char *method_str(access_method_t method)
{
    switch (method) {
        case ACCESS_METHOD_RFID:  return "RFID";
        case ACCESS_METHOD_PIN:   return "PIN";
        case ACCESS_METHOD_QR:    return "QR";
        case ACCESS_METHOD_TOUCH: return "TOUCH";
        case ACCESS_METHOD_BLE:   return "BLE";
        case ACCESS_METHOD_MQTT:  return "MQTT";
        default:                  return "UNKNOWN";
    }
}

/* Apply the result — control relay, LED, buzzer, event */
static void apply_result(access_result_t result, access_method_t method)
{
    switch (result) {
        case ACCESS_RESULT_GRANTED:
            ESP_LOGI(TAG, "ACCESS GRANTED");
            s_fail_count = 0;
            hal_relay_open();
            hal_led_green();
            hal_buzzer_granted();
            s_relay_open_time = esp_timer_get_time();
            s_led_reset_time  = esp_timer_get_time() + (LED_ON_DURATION_MS * 1000LL);
            if (s_event_cb) s_event_cb("access", method_str(method), "GRANTED");
            break;

        case ACCESS_RESULT_DURESS:
            ESP_LOGW(TAG, "DURESS OPEN — silent alert");
            s_fail_count = 0;
            hal_relay_open();
            hal_led_green();
            hal_buzzer_silent();
            s_relay_open_time = esp_timer_get_time();
            s_led_reset_time  = esp_timer_get_time() + (LED_ON_DURATION_MS * 1000LL);
            if (s_event_cb) s_event_cb("access", method_str(method), "DURESS");
            break;

        case ACCESS_RESULT_DENIED:
            ESP_LOGW(TAG, "ACCESS DENIED — failure %d/%d",
                     s_fail_count + 1, MAX_FAILURES);
            s_fail_count++;
            hal_led_red();
            hal_buzzer_denied();
            s_led_reset_time = esp_timer_get_time() + (LED_ON_DURATION_MS * 1000LL);
            if (s_event_cb) s_event_cb("access", method_str(method), "DENIED");
            break;

        case ACCESS_RESULT_SCHEDULE_DENIED:
            ESP_LOGW(TAG, "ACCESS DENIED — outside schedule window");
            hal_led_red();
            hal_buzzer_denied();
            s_led_reset_time = esp_timer_get_time() + (LED_ON_DURATION_MS * 1000LL);
            if (s_event_cb) s_event_cb("schedule", method_str(method), "DENIED_OUTSIDE_WINDOW");
            break;

        case ACCESS_RESULT_FIRE:
            ESP_LOGW(TAG, "FIRE — forcing door open");
            hal_relay_open();
            hal_led_red();
            hal_buzzer_alert();
            if (s_event_cb) s_event_cb("fire", "FIRE", "GRANTED");
            break;

        case ACCESS_RESULT_TAMPER:
            ESP_LOGW(TAG, "TAMPER detected");
            hal_led_red();
            hal_buzzer_alert();
            if (s_event_cb) s_event_cb("tamper", "TAMPER", "ALERT");
            break;

        default:
            break;
    }
}

/* ── Public API ── */

void access_manager_init(void)
{
    s_fail_count      = 0;
    s_led_reset_time  = 0;
    s_relay_open_time = 0;
    cred_store_init();
    schedule_manager_init();
    ESP_LOGI(TAG, "Access manager ready — %d credentials loaded",
             cred_store_count());
}

access_result_t access_manager_check(const access_token_t *token)
{
    /* Fire — يتجاوز الـ schedule دايماً */
    if (token->method == ACCESS_METHOD_TOUCH) {
        apply_result(ACCESS_RESULT_GRANTED, ACCESS_METHOD_TOUCH);
        return ACCESS_RESULT_GRANTED;
    }

    /* Schedule check — لكل الـ methods الباقية */
    if (!schedule_manager_is_allowed()) {
        apply_result(ACCESS_RESULT_SCHEDULE_DENIED, token->method);
        return ACCESS_RESULT_SCHEDULE_DENIED;
    }

    /* BLE open */
    if (token->method == ACCESS_METHOD_BLE) {
        apply_result(ACCESS_RESULT_GRANTED, ACCESS_METHOD_BLE);
        return ACCESS_RESULT_GRANTED;
    }

    /* MQTT open */
    if (token->method == ACCESS_METHOD_MQTT) {
        apply_result(ACCESS_RESULT_GRANTED, ACCESS_METHOD_MQTT);
        return ACCESS_RESULT_GRANTED;
    }

    /* Lockout check */
    if (s_fail_count >= MAX_FAILURES) {
        ESP_LOGW(TAG, "Device locked out after %d failures", s_fail_count);
        hal_led_red();
        hal_buzzer_denied();
        return ACCESS_RESULT_LOCKOUT;
    }

    /* Map access method to credential type */
    cred_type_t ctype;
    switch (token->method) {
        case ACCESS_METHOD_RFID: ctype = CRED_TYPE_RFID; break;
        case ACCESS_METHOD_PIN:  ctype = CRED_TYPE_PIN;  break;
        case ACCESS_METHOD_QR:   ctype = CRED_TYPE_QR;   break;
        default:
            apply_result(ACCESS_RESULT_DENIED, token->method);
            return ACCESS_RESULT_DENIED;
    }

    /* Lookup credential in NVS store */
    bool is_duress = false;
    bool found = cred_store_lookup(ctype, token->data,
                                   token->data_len, &is_duress);

    access_result_t result;
    if (found) {
        result = is_duress ? ACCESS_RESULT_DURESS : ACCESS_RESULT_GRANTED;
    } else {
        result = ACCESS_RESULT_DENIED;
    }

    apply_result(result, token->method);
    return result;
}

void access_manager_tick(void)
{
    int64_t now = esp_timer_get_time();

    /* Auto-lock relay after AUTO_LOCK_MS */
    if (s_relay_open_time != 0 &&
        hal_relay_is_open() &&
        (now - s_relay_open_time) >= (AUTO_LOCK_MS * 1000LL)) {
        hal_relay_close();
        s_relay_open_time = 0;
        ESP_LOGI(TAG, "Auto-lock triggered");
    }

    /* Reset LED to idle */
    if (s_led_reset_time != 0 && now >= s_led_reset_time) {
        s_led_reset_time = 0;
        hal_led_idle();
    }
}