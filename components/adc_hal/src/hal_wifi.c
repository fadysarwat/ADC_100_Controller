/*
 * hal_wifi.c
 * ADC-100 Controller — WiFi manager with custom HTML provisioning
 */

#include "hal_wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "HAL_WIFI";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define MAX_RETRY           5
#define NVS_WIFI_NS         "wifi_creds"
#define AP_SSID             "ADC100-Setup"
#define AP_PASSWORD         "adc100setup"

static EventGroupHandle_t s_wifi_event_group;
static wifi_state_t       s_state   = WIFI_STATE_DISCONNECTED;
static int                s_retries = 0;
static httpd_handle_t     s_server  = NULL;

/* ── HTML page ── */
static const char *HTML_PAGE =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ADC-100 Setup</title>"
    "<style>"
    "body{font-family:Arial,sans-serif;background:#0A0E1A;color:#fff;"
    "display:flex;justify-content:center;align-items:center;height:100vh;margin:0}"
    ".box{background:#131929;padding:30px;border-radius:12px;width:300px;"
    "border:1px solid #00C2FF}"
    "h2{color:#00C2FF;text-align:center;margin-bottom:20px}"
    "input{width:100%;padding:10px;margin:8px 0;border-radius:6px;"
    "border:1px solid #00C2FF;background:#0A0E1A;color:#fff;box-sizing:border-box}"
    "button{width:100%;padding:12px;background:#00C2FF;color:#0A0E1A;"
    "border:none;border-radius:6px;font-weight:bold;cursor:pointer;margin-top:10px}"
    "button:hover{background:#0066FF}"
    ".logo{text-align:center;color:#00C2FF;font-size:12px;margin-bottom:15px}"
    "</style></head><body><div class='box'>"
    "<div class='logo'>INNOVOLTX</div>"
    "<h2>NexGate Setup</h2>"
    "<form action='/save' method='POST'>"
    "<input type='text' name='ssid' placeholder='WiFi Name (SSID)' required><br>"
    "<input type='password' name='pass' placeholder='WiFi Password' required><br>"
    "<button type='submit'>Connect</button>"
    "</form></div></body></html>";

static const char *HTML_SUCCESS =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<title>ADC-100 Setup</title>"
    "<style>body{font-family:Arial,sans-serif;background:#0A0E1A;color:#fff;"
    "display:flex;justify-content:center;align-items:center;height:100vh;margin:0}"
    ".box{background:#131929;padding:30px;border-radius:12px;width:300px;"
    "border:1px solid #00E5A0;text-align:center}"
    "h2{color:#00E5A0}p{color:#8A9BB5}"
    "</style></head><body><div class='box'>"
    "<h2>Connected!</h2>"
    "<p>Credentials saved. Device will restart and connect to your WiFi.</p>"
    "</div></body></html>";

/* ── URL decoder ── */
static void url_decode(char *dst, const char *src, size_t dst_len)
{
    char *d = dst;
    const char *s = src;
    size_t remaining = dst_len - 1;

    while (*s && remaining > 0) {
        if (*s == '%' && *(s+1) && *(s+2)) {
            char hex[3] = { *(s+1), *(s+2), 0 };
            *d++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else if (*s == '+') {
            *d++ = ' ';
            s++;
        } else {
            *d++ = *s++;
        }
        remaining--;
    }
    *d = '\0';
}

/* ── NVS helpers ── */
static bool nvs_save_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_WIFI_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", pass);
    nvs_commit(h);
    nvs_close(h);
    return true;
}

static bool nvs_load_wifi(char *ssid, char *pass, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_WIFI_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t s = len;
    bool ok = (nvs_get_str(h, "ssid", ssid, &s) == ESP_OK);
    s = len;
    ok = ok && (nvs_get_str(h, "pass", pass, &s) == ESP_OK);
    nvs_close(h);
    return ok;
}

/* ── HTTP handlers ── */
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_PAGE, strlen(HTML_PAGE));
    return ESP_OK;
}

static esp_err_t save_handler(httpd_req_t *req)
{
    char buf[256]     = {0};
    char ssid_enc[64] = {0};
    char pass_enc[64] = {0};
    char ssid[64]     = {0};
    char pass[64]     = {0};

    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;

    /* Parse URL-encoded POST body */
    httpd_query_key_value(buf, "ssid", ssid_enc, sizeof(ssid_enc));
    httpd_query_key_value(buf, "pass", pass_enc, sizeof(pass_enc));

    /* Decode special characters */
    url_decode(ssid, ssid_enc, sizeof(ssid));
    url_decode(pass, pass_enc, sizeof(pass));

    ESP_LOGI(TAG, "Received credentials — SSID: %s", ssid);

    /* Save to NVS */
    nvs_save_wifi(ssid, pass);

    /* Send success page */
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_SUCCESS, strlen(HTML_SUCCESS));

    /* Restart after short delay */
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

/* ── Start AP + Web Server ── */
static void start_provisioning_ap(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_config = {
        .ap = {
            .ssid           = AP_SSID,
            .ssid_len       = strlen(AP_SSID),
            .password       = AP_PASSWORD,
            .max_connection = 4,
            .authmode       = WIFI_AUTH_WPA2_PSK,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();

    /* Start HTTP server */
    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    config.max_uri_handlers  = 8;
    config.stack_size        = 8192;
    config.max_open_sockets  = 4;
    config.backlog_conn      = 5;
    httpd_start(&s_server, &config);

    httpd_uri_t root = {
        .uri     = "/",
        .method  = HTTP_GET,
        .handler = root_handler,
    };
    httpd_uri_t save = {
        .uri     = "/save",
        .method  = HTTP_POST,
        .handler = save_handler,
    };
    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &save);

    s_state = WIFI_STATE_PROVISIONING;
    ESP_LOGI(TAG, "Setup AP started — connect to '%s' and open 192.168.4.1", AP_SSID);
}

/* ── WiFi event handler ── */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        s_state = WIFI_STATE_CONNECTING;

    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retries < MAX_RETRY) {
            esp_wifi_connect();
            s_retries++;
            ESP_LOGW(TAG, "Retry %d/%d...", s_retries, MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            s_state = WIFI_STATE_FAILED;
            ESP_LOGE(TAG, "WiFi connection failed");
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected — IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retries = 0;
        s_state   = WIFI_STATE_CONNECTED;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* ── Public API ── */

void hal_wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                &wifi_event_handler, NULL);

    /* Check if credentials saved in NVS */
    char ssid[64] = {0};
    char pass[64] = {0};

    if (nvs_load_wifi(ssid, pass, sizeof(ssid))) {
        ESP_LOGI(TAG, "Credentials found — connecting to: %s", ssid);

        wifi_config_t wifi_config = {0};
        strncpy((char *)wifi_config.sta.ssid,     ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));

        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        esp_wifi_start();
    } else {
        ESP_LOGI(TAG, "No credentials — starting provisioning AP");
        start_provisioning_ap();
    }
}

wifi_state_t hal_wifi_get_state(void)
{
    return s_state;
}

bool hal_wifi_is_connected(void)
{
    return s_state == WIFI_STATE_CONNECTED;
}

void hal_wifi_wait_connected(void)
{
    if (s_state == WIFI_STATE_PROVISIONING) {
        ESP_LOGI(TAG, "Waiting for provisioning...");
        while (s_state == WIFI_STATE_PROVISIONING) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        return;
    }

    xEventGroupWaitBits(s_wifi_event_group,
                        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                        pdFALSE, pdFALSE,
                        portMAX_DELAY);

    if (hal_wifi_is_connected()) {
        ESP_LOGI(TAG, "WiFi ready");
    } else {
        ESP_LOGE(TAG, "WiFi failed — running offline");
    }
}

void hal_wifi_reset_credentials(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_WIFI_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "WiFi credentials cleared — restarting");
    esp_restart();
}