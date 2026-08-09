/*
 * schedule_manager.c
 * ADC-100 Controller — NTP-based access scheduling
 */

#include "schedule_manager.h"
#include "hal_ntp.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <time.h>

static const char *TAG        = "SCHED_MGR";
#define NVS_NAMESPACE           "schedule"
#define NVS_KEY                 "cfg"

static schedule_t s_schedule = {
    .enabled      = false,
    .start_hour   = 8,
    .start_minute = 0,
    .end_hour     = 18,
    .end_minute   = 0,
};

static void save_to_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_KEY, &s_schedule, sizeof(s_schedule));
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Schedule saved to NVS");
}

static void load_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(s_schedule);
    nvs_get_blob(h, NVS_KEY, &s_schedule, &len);
    nvs_close(h);
    ESP_LOGI(TAG, "Schedule loaded — enabled:%d %02d:%02d-%02d:%02d",
             s_schedule.enabled,
             s_schedule.start_hour, s_schedule.start_minute,
             s_schedule.end_hour,   s_schedule.end_minute);
}

void schedule_manager_init(void)
{
    load_from_nvs();
}

void schedule_manager_set(const schedule_t *schedule)
{
    memcpy(&s_schedule, schedule, sizeof(s_schedule));
    save_to_nvs();
    ESP_LOGI(TAG, "Schedule updated — enabled:%d %02d:%02d-%02d:%02d",
             s_schedule.enabled,
             s_schedule.start_hour, s_schedule.start_minute,
             s_schedule.end_hour,   s_schedule.end_minute);
}

void schedule_manager_get(schedule_t *schedule)
{
    memcpy(schedule, &s_schedule, sizeof(s_schedule));
}

bool schedule_manager_is_allowed(void)
{
    if (!s_schedule.enabled) return true;

    if (!hal_ntp_is_synced()) {
        ESP_LOGW(TAG, "NTP not synced — denying access (schedule active)");
        return false;   /* ← غيّرنا true لـ false */
    }

    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    int now_minutes   = t.tm_hour * 60 + t.tm_min;
    int start_minutes = s_schedule.start_hour * 60 + s_schedule.start_minute;
    int end_minutes   = s_schedule.end_hour   * 60 + s_schedule.end_minute;

    bool allowed = (now_minutes >= start_minutes && now_minutes < end_minutes);

    ESP_LOGI(TAG, "Schedule check — now=%02d:%02d window=%02d:%02d-%02d:%02d allowed=%s",
             t.tm_hour, t.tm_min,
             s_schedule.start_hour, s_schedule.start_minute,
             s_schedule.end_hour,   s_schedule.end_minute,
             allowed ? "YES" : "NO");

    return allowed;
}