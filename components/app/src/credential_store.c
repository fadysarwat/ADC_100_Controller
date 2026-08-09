/*
 * credential_store.c
 * ADC-100 Controller
 */

#include "credential_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG        = "CRED_STORE";
static const char *NVS_NS     = "cred_store";
static const char *NVS_COUNT  = "count";
static const char *NVS_PREFIX = "cred_";

static int32_t s_count = 0;

/* Write a credential to NVS at index idx */
static bool nvs_write_cred(int idx, const credential_t *cred)
{
    nvs_handle_t h;
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_PREFIX, idx);

    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(h, key, cred, sizeof(credential_t));
    nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

/* Read a credential from NVS at index idx */
static bool nvs_read_cred(int idx, credential_t *cred)
{
    nvs_handle_t h;
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_PREFIX, idx);

    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t size = sizeof(credential_t);
    esp_err_t err = nvs_get_blob(h, key, cred, &size);
    nvs_close(h);
    return err == ESP_OK;
}

/* Save current credential count to NVS */
static void nvs_save_count(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, NVS_COUNT, s_count);
    nvs_commit(h);
    nvs_close(h);
}

/* ── Public API ── */

void cred_store_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, NVS_COUNT, &s_count);
        nvs_close(h);
    }

    ESP_LOGI(TAG, "Loaded %ld slots — %d active credentials",
             s_count, cred_store_count());

    if (s_count == 0) {
        cred_store_seed_defaults();
    }
}

bool cred_store_add(const credential_t *cred)
{
    if (s_count >= CRED_MAX_COUNT) {
        ESP_LOGW(TAG, "Credential store full");
        return false;
    }

    if (!nvs_write_cred(s_count, cred)) return false;
    s_count++;
    nvs_save_count();

    ESP_LOGI(TAG, "Credential added: %s (active: %d)",
             cred->label, cred_store_count());
    return true;
}

bool cred_store_remove(const char *label)
{
    /* ابحث عن الـ credential */
    int found_idx = -1;
    for (int i = 0; i < s_count; i++) {
        credential_t cred;
        if (!nvs_read_cred(i, &cred)) continue;
        if (strncmp(cred.label, label, CRED_LABEL_MAX_LEN) == 0) {
            found_idx = i;
            break;
        }
    }

    if (found_idx == -1) return false;

    /* Compact — بنشيل الـ slot ده ونحرك الباقي */
    for (int i = found_idx; i < s_count - 1; i++) {
        credential_t next;
        if (nvs_read_cred(i + 1, &next)) {
            nvs_write_cred(i, &next);
        }
    }

    /* مسح آخر slot */
    credential_t empty = {0};
    nvs_write_cred(s_count - 1, &empty);

    s_count--;
    nvs_save_count();

    ESP_LOGI(TAG, "Credential removed: %s (active: %d)", label, cred_store_count());
    return true;
}

bool cred_store_lookup(cred_type_t type, const uint8_t *data,
                       uint8_t data_len, bool *is_duress)
{
    for (int i = 0; i < s_count; i++) {
        credential_t cred;
        if (!nvs_read_cred(i, &cred)) continue;
        if (!cred.is_active) continue;
        if (cred.type != type) continue;
        if (cred.data_len != data_len) continue;
        if (memcmp(cred.data, data, data_len) == 0) {
            *is_duress = cred.is_duress;
            return true;
        }
    }
    return false;
}

int cred_store_count(void)
{
    /* عد الـ active credentials بس */
    int active = 0;
    for (int i = 0; i < s_count; i++) {
        credential_t cred;
        if (!nvs_read_cred(i, &cred)) continue;
        if (cred.is_active) active++;
    }
    return active;
}

void cred_store_seed_defaults(void)
{
    ESP_LOGI(TAG, "Seeding default credentials...");

    credential_t c1 = {
        .type      = CRED_TYPE_RFID,
        .data      = {0x6A, 0x48, 0xE6, 0x00},
        .data_len  = 4,
        .is_duress = false,
        .is_active = true,
        .label     = "card_default",
    };
    cred_store_add(&c1);

    credential_t c2 = {
        .type      = CRED_TYPE_PIN,
        .data      = {'1','2','3','4'},
        .data_len  = 4,
        .is_duress = false,
        .is_active = true,
        .label     = "pin_default",
    };
    cred_store_add(&c2);

    credential_t c3 = {
        .type      = CRED_TYPE_PIN,
        .data      = {'9','1','1','0'},
        .data_len  = 4,
        .is_duress = true,
        .is_active = true,
        .label     = "pin_duress",
    };
    cred_store_add(&c3);

    ESP_LOGI(TAG, "Default credentials seeded");
}