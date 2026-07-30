/*
 * hal_rs485.h
 * ADC-100 Controller
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#define RS485_FRAME_SIZE   20
#define RS485_START_BYTE   0xAA

/* Token types (Reader → Controller) */
#define RS485_TYPE_RFID    0x01
#define RS485_TYPE_PIN     0x02
#define RS485_TYPE_QR      0x03

/* Response types (Controller → Reader) */
#define RS485_TYPE_ACK     0x10
#define RS485_TYPE_GRANTED 0x20
#define RS485_TYPE_DENIED  0x30
#define RS485_TYPE_DURESS  0x40

typedef struct {
    uint8_t type;
    uint8_t data[64];
    uint8_t data_len;
} rs485_frame_t;

void hal_rs485_init(void);
bool hal_rs485_recv_frame(rs485_frame_t *frame, uint32_t timeout_ms);
void hal_rs485_send_response(uint8_t resp_type);