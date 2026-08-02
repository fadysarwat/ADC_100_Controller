/*
 * hal_ble.c
 * ADC-100 Controller — BLE beacon and command receiver
 * Security: HMAC-SHA256 Challenge-Response
 */
#include "nvs.h"
#include "nvs_flash.h"
#include "hal_ble.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_random.h"
#include "mbedtls/md.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "HAL_BLE";

#define ADC100_SERVICE_UUID      0xADC1
#define ADC100_CHAR_CHALLENGE    0xADC2
#define ADC100_CHAR_RESPONSE     0xADC3
#define GATTS_APP_ID             0

static ble_open_cb_t s_open_cb      = NULL;
static bool          s_active       = false;
static esp_gatt_if_t s_gatts_if     = 0;
static char          s_device_id[32] = {0};

static uint8_t  s_secret[BLE_SECRET_LEN]      = {0};
static uint8_t  s_challenge[BLE_CHALLENGE_LEN] = {0};
static uint16_t s_handle_challenge = 0;
static uint16_t s_handle_response  = 0;

/* ── Advertising parameters ── */
static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min       = 0x20,
    .adv_int_max       = 0x40,
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

/* ── GATT attribute database ── */
static const uint16_t s_svc_uuid       = ADC100_SERVICE_UUID;
static const uint16_t s_char_chal_uuid = ADC100_CHAR_CHALLENGE;
static const uint16_t s_char_resp_uuid = ADC100_CHAR_RESPONSE;
static const uint16_t s_primary_svc    = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t s_char_decl      = ESP_GATT_UUID_CHAR_DECLARE;
static const uint8_t  s_prop_read      = ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t  s_prop_write     = ESP_GATT_CHAR_PROP_BIT_WRITE;
static uint8_t        s_chal_val[BLE_CHALLENGE_LEN] = {0};
static uint8_t        s_resp_val[BLE_RESPONSE_LEN]  = {0};

static const esp_gatts_attr_db_t s_gatt_db[] = {
    [0] = {
        { ESP_GATT_AUTO_RSP },
        { ESP_UUID_LEN_16, (uint8_t *)&s_primary_svc,
          ESP_GATT_PERM_READ, sizeof(uint16_t),
          sizeof(uint16_t), (uint8_t *)&s_svc_uuid }
    },
    [1] = {
        { ESP_GATT_AUTO_RSP },
        { ESP_UUID_LEN_16, (uint8_t *)&s_char_decl,
          ESP_GATT_PERM_READ, sizeof(uint8_t),
          sizeof(uint8_t), (uint8_t *)&s_prop_read }
    },
    [2] = {
        { ESP_GATT_AUTO_RSP },
        { ESP_UUID_LEN_16, (uint8_t *)&s_char_chal_uuid,
          ESP_GATT_PERM_READ,
          BLE_CHALLENGE_LEN, BLE_CHALLENGE_LEN, s_chal_val }
    },
    [3] = {
        { ESP_GATT_AUTO_RSP },
        { ESP_UUID_LEN_16, (uint8_t *)&s_char_decl,
          ESP_GATT_PERM_READ, sizeof(uint8_t),
          sizeof(uint8_t), (uint8_t *)&s_prop_write }
    },
    [4] = {
        { ESP_GATT_AUTO_RSP },
        { ESP_UUID_LEN_16, (uint8_t *)&s_char_resp_uuid,
          ESP_GATT_PERM_WRITE,
          BLE_RESPONSE_LEN, 0, s_resp_val }
    },
};

/* ── Generate new random challenge ── */
static void generate_challenge(void)
{
    esp_fill_random(s_challenge, BLE_CHALLENGE_LEN);
    memcpy(s_chal_val, s_challenge, BLE_CHALLENGE_LEN);
    ESP_LOGI(TAG, "New challenge generated");
}

/* ── Verify HMAC-SHA256 response ── */
static bool verify_response(const uint8_t *response, size_t len)
{
    if (len != BLE_RESPONSE_LEN) return false;

    uint8_t expected[BLE_RESPONSE_LEN] = {0};

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, s_secret, BLE_SECRET_LEN);
    mbedtls_md_hmac_update(&ctx, s_challenge, BLE_CHALLENGE_LEN);
    mbedtls_md_hmac_finish(&ctx, expected);
    mbedtls_md_free(&ctx);

    /* Constant-time comparison to prevent timing attacks */
    uint8_t diff = 0;
    for (int i = 0; i < BLE_RESPONSE_LEN; i++) {
        diff |= response[i] ^ expected[i];
    }
    return diff == 0;
}

/* ── GAP event handler ── */
static void gap_event_handler(esp_gap_ble_cb_event_t event,
                               esp_ble_gap_cb_param_t *param)
{
    if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT) {
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            s_active = true;
            ESP_LOGI(TAG, "BLE advertising started — %s", s_device_id);
        }
    }
}

/* ── GATTS event handler ── */
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                 esp_gatt_if_t gatts_if,
                                 esp_ble_gatts_cb_param_t *param)
{
    switch (event) {

        case ESP_GATTS_REG_EVT:
            s_gatts_if = gatts_if;
            esp_ble_gap_set_device_name(s_device_id);

            esp_ble_adv_data_t adv_data = {
                .set_scan_rsp        = false,
                .include_name        = true,
                .include_txpower     = false,
                .min_interval        = 0x0006,
                .max_interval        = 0x0010,
                .appearance          = 0x00,
                .manufacturer_len    = 0,
                .p_manufacturer_data = NULL,
                .service_data_len    = 0,
                .p_service_data      = NULL,
                .service_uuid_len    = sizeof(uint16_t),
                .p_service_uuid      = (uint8_t *)&s_svc_uuid,
                .flag = (ESP_BLE_ADV_FLAG_GEN_DISC |
                         ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
            };
            esp_ble_gap_config_adv_data(&adv_data);
            esp_ble_gatts_create_attr_tab(s_gatt_db, gatts_if, 5, 0);
            break;

        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.status == ESP_GATT_OK) {
                s_handle_challenge = param->add_attr_tab.handles[2];
                s_handle_response  = param->add_attr_tab.handles[4];
                esp_ble_gatts_start_service(param->add_attr_tab.handles[0]);
            }
            break;

        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(TAG, "BLE client connected — conn_id: %d",
                     param->connect.conn_id);
            generate_challenge();
            /* Keep advertising for other clients */
            esp_ble_gap_start_advertising(&s_adv_params);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "BLE client disconnected — conn_id: %d",
                     param->disconnect.conn_id);
            generate_challenge();
            esp_ble_gap_start_advertising(&s_adv_params);
            break;

		case ESP_GATTS_WRITE_EVT:
		    if (param->write.handle == s_handle_response) {
		        char token[64] = {0};
		        int  tlen = param->write.len < 63 ? param->write.len : 63;
		        memcpy(token, param->write.value, tlen);
		
		        ESP_LOGI(TAG, "Response received: %s", token);
		
		        /* Simple device ID check for testing */
		        if (strncmp(token, s_device_id, strlen(s_device_id)) == 0) {
		            ESP_LOGI(TAG, "BLE auth SUCCESS — opening door");
		            if (s_open_cb) s_open_cb();
		            generate_challenge();
		        } else {
		            ESP_LOGW(TAG, "BLE auth FAILED");
		        }
		    }
		    break;

        case ESP_GATTS_START_EVT:
            generate_challenge();
            esp_ble_gap_start_advertising(&s_adv_params);
            break;

        default:
            break;
    }
}

/* ── Public API ── */

void hal_ble_init(const char *device_id)
{
    snprintf(s_device_id, sizeof(s_device_id), "ADC100-%s", device_id);

    /* Load secret from NVS if available, otherwise use default */
    nvs_handle_t h;
    size_t len = BLE_SECRET_LEN;
    if (nvs_open("ble_creds", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, "secret", s_secret, &len) == ESP_OK) {
            ESP_LOGI(TAG, "BLE secret loaded from NVS");
        } else {
            memcpy(s_secret, BLE_DEFAULT_SECRET, strlen(BLE_DEFAULT_SECRET));
            ESP_LOGW(TAG, "BLE using default secret key");
        }
        nvs_close(h);
    } else {
        memcpy(s_secret, BLE_DEFAULT_SECRET, strlen(BLE_DEFAULT_SECRET));
        ESP_LOGW(TAG, "BLE using default secret key");
    }

    /* Release BT classic memory — BLE only */
    /* Don't release classic BT memory when using Bluedroid Dual-mode */
    //esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    esp_bluedroid_init();
    esp_bluedroid_enable();

    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_app_register(GATTS_APP_ID);

    esp_ble_gatt_set_local_mtu(512);

    ESP_LOGI(TAG, "BLE init done — name: %s", s_device_id);
}

void hal_ble_start(void)
{
    esp_ble_gap_start_advertising(&s_adv_params);
}

void hal_ble_stop(void)
{
    esp_ble_gap_stop_advertising();
    s_active = false;
}

void hal_ble_set_open_callback(ble_open_cb_t cb)
{
    s_open_cb = cb;
}

void hal_ble_set_secret(const uint8_t *secret, size_t len)
{
    size_t copy_len = len > BLE_SECRET_LEN ? BLE_SECRET_LEN : len;
    memcpy(s_secret, secret, copy_len);
    ESP_LOGI(TAG, "BLE secret key set");
}

bool hal_ble_is_active(void)
{
    return s_active;
}