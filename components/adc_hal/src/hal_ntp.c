/*
 * hal_ntp.c
 * ADC-100 Controller — NTP time sync
 */

#include "hal_ntp.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <string.h>

static const char *TAG = "HAL_NTP";

#define NTP_SERVER      "pool.ntp.org"
#define TIMEZONE        "CET-1CEST,M3.5.0,M10.5.0/3"   /* Slovenia */
#define NTP_SYNC_TIMEOUT_MS  10000   /* 10 ثواني max انتظار */

static bool s_synced = false;

static void ntp_sync_cb(struct timeval *tv)
{
    s_synced = true;
    time_t now = tv->tv_sec;
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "NTP synced — local time: %s", buf);
}

void hal_ntp_init(void)
{
    ESP_LOGI(TAG, "Starting NTP sync — server: %s", NTP_SERVER);

    /* Set timezone */
    setenv("TZ", TIMEZONE, 1);
    tzset();

    /* Configure SNTP */
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER);
    sntp_set_time_sync_notification_cb(ntp_sync_cb);
    esp_sntp_init();

    /* انتظر الـ sync لحد NTP_SYNC_TIMEOUT_MS */
    int elapsed = 0;
    while (!s_synced && elapsed < NTP_SYNC_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(100));
        elapsed += 100;
    }

    if (s_synced) {
        ESP_LOGI(TAG, "NTP sync complete");
    } else {
        ESP_LOGW(TAG, "NTP sync timeout — will retry in background");
    }
}

bool hal_ntp_is_synced(void)
{
    return s_synced;
}

uint32_t hal_ntp_get_time(void)
{
    if (!s_synced) return 0;
    return (uint32_t)time(NULL);
}