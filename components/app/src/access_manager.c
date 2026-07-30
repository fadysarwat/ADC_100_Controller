/*
 * access_manager.c
 * ADC-100 Controller
 */

#include "access_manager.h"
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

#define MAX_FAILURES        5
#define LED_ON_DURATION_MS  2000
#define AUTO_LOCK_MS        5000

static int     s_fail_count     = 0;
static int64_t s_led_reset_time = 0;
static int64_t s_relay_open_time = 0;

/* Apply the result — control relay, LED, buzzer */
static void apply_result(access_result_t result)
{
    switch (result) {

        case ACCESS_RESULT_GRANTED:
            ESP_LOGI(TAG, "ACCESS GRANTED");
            s_fail_count = 0;
            hal_relay_open();
            hal_led_green();
            hal_buzzer_granted();
            s_relay_open_time = esp_timer_get_time();
            s_led_reset_time  = esp_timer_get_time() +
                                (LED_ON_DURATION_MS * 1000LL);
            break;

        case ACCESS_RESULT_DURESS:
            /* Open the door silently — trigger alert upstream */
            ESP_LOGW(TAG, "DURESS OPEN — silent alert");
            s_fail_count = 0;
            hal_relay_open();
            hal_led_green();
            hal_buzzer_silent();
            s_relay_open_time = esp_timer_get_time();
            s_led_reset_time  = esp_timer_get_time() +
                                (LED_ON_DURATION_MS * 1000LL);
            break;

        case ACCESS_RESULT_DENIED:
            ESP_LOGW(TAG, "ACCESS DENIED — failure %d/%d",
                     s_fail_count + 1, MAX_FAILURES);
            s_fail_count++;
            hal_led_red();
            hal_buzzer_denied();
            s_led_reset_time = esp_timer_get_time() +
                               (LED_ON_DURATION_MS * 1000LL);
            break;

        case ACCESS_RESULT_FIRE:
            /* Fire input — force open regardless of credentials */
            ESP_LOGW(TAG, "FIRE — forcing door open");
            hal_relay_open();
            hal_led_red();
            hal_buzzer_alert();
            break;

        case ACCESS_RESULT_TAMPER:
            ESP_LOGW(TAG, "TAMPER detected");
            hal_led_red();
            hal_buzzer_alert();
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

    ESP_LOGI(TAG, "Access manager ready — %d credentials loaded",
             cred_store_count());
}

access_result_t access_manager_check(const access_token_t *token)
{
    /* Touch button — open immediately, no credential check */
    if (token->method == ACCESS_METHOD_TOUCH) {
        apply_result(ACCESS_RESULT_GRANTED);
        return ACCESS_RESULT_GRANTED;
    }

    /* BLE open — open immediately, auth handled at BLE layer */
    if (token->method == ACCESS_METHOD_BLE) {
        apply_result(ACCESS_RESULT_GRANTED);
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
            apply_result(ACCESS_RESULT_DENIED);
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

    apply_result(result);
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
    if (s_led_reset_time != 0 &&
        now >= s_led_reset_time) {
        s_led_reset_time = 0;
        hal_led_idle();
    }
}