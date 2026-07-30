/*
 * mcal_rs485.c
 * ADC-100 Controller
 */

#include "mcal_rs485.h"
#include "mcal_gpio.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "MCAL_RS485";

#define RS485_UART      UART_NUM_1
#define RS485_BAUD      9600
#define RS485_BUF_SIZE  1024

void mcal_rs485_init(void)
{
    gpio_config_t de_cfg = {
        .pin_bit_mask = 1ULL << PIN_RS485_DE,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&de_cfg);
    gpio_set_level(PIN_RS485_DE, 0);

    const uart_config_t cfg = {
        .baud_rate           = RS485_BAUD,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk          = UART_SCLK_DEFAULT,
    };

    uart_param_config(RS485_UART, &cfg);
    uart_set_pin(RS485_UART, PIN_RS485_DI, PIN_RS485_RO, -1, -1);
    uart_driver_install(RS485_UART, RS485_BUF_SIZE, RS485_BUF_SIZE, 0, NULL, 0);

    ESP_LOGI(TAG, "RS485 init done @ %d baud", RS485_BAUD);
}

esp_err_t mcal_rs485_send(const uint8_t *data, size_t len)
{
    gpio_set_level(PIN_RS485_DE, 1);
    vTaskDelay(pdMS_TO_TICKS(1));

    uart_write_bytes(RS485_UART, (const char *)data, len);
    uart_wait_tx_done(RS485_UART, pdMS_TO_TICKS(50));
    vTaskDelay(pdMS_TO_TICKS(1));

    gpio_set_level(PIN_RS485_DE, 0);

    /* Flush echo */
    uart_flush_input(RS485_UART);

    return ESP_OK;
}

int mcal_rs485_recv(uint8_t *buf, size_t max_len, uint32_t timeout_ms)
{
    return uart_read_bytes(RS485_UART, buf, max_len, pdMS_TO_TICKS(timeout_ms));
}