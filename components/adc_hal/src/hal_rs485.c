/*
 * hal_rs485.c
 * ADC-100 Controller
 */

#include "hal_rs485.h"
#include "mcal_rs485.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "HAL_RS485";

static uint8_t calc_checksum(const uint8_t *buf)
{
    uint8_t cs = 0;
    for (int i = 0; i < 19; i++) cs ^= buf[i];
    return cs;
}

void hal_rs485_init(void)
{
    mcal_rs485_init();
    ESP_LOGI(TAG, "RS485 HAL ready");
}

bool hal_rs485_recv_frame(rs485_frame_t *frame, uint32_t timeout_ms)
{
    uint8_t buf[RS485_FRAME_SIZE] = {0};

    /* Hunt for start byte */
    uint8_t start = 0;
    int n = mcal_rs485_recv(&start, 1, timeout_ms);
    if (n <= 0 || start != RS485_START_BYTE) return false;

    /* Read remaining 19 bytes */
    buf[0] = RS485_START_BYTE;
    n = mcal_rs485_recv(&buf[1], RS485_FRAME_SIZE - 1, 50);
    if (n < RS485_FRAME_SIZE - 1) {
        ESP_LOGW(TAG, "Short frame (%d bytes)", n + 1);
        return false;
    }

    /* Validate checksum */
    uint8_t expected = calc_checksum(buf);
    if (buf[19] != expected) {
        ESP_LOGW(TAG, "Bad checksum (got 0x%02X, expected 0x%02X)",
                 buf[19], expected);
        return false;
    }

    frame->type     = buf[1];
    frame->data_len = buf[18];
    uint8_t copy_len = frame->data_len > 16 ? 16 : frame->data_len;
    memcpy(frame->data, &buf[2], copy_len);

    ESP_LOGI(TAG, "Frame received (type=0x%02X, len=%d)",
             frame->type, frame->data_len);
    return true;
}

void hal_rs485_send_response(uint8_t resp_type)
{
    uint8_t frame[RS485_FRAME_SIZE] = {0};
    frame[0]  = RS485_START_BYTE;
    frame[1]  = resp_type;
    frame[19] = 0;

    uint8_t cs = 0;
    for (int i = 0; i < 19; i++) cs ^= frame[i];
    frame[19] = cs;

    mcal_rs485_send(frame, RS485_FRAME_SIZE);
    ESP_LOGI(TAG, "Response sent: 0x%02X", resp_type);
}