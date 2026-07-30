/*
 * credential_store.h
 * ADC-100 Controller
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#define CRED_MAX_COUNT      200
#define CRED_DATA_MAX_LEN   64
#define CRED_LABEL_MAX_LEN  16

typedef enum {
    CRED_TYPE_RFID,
    CRED_TYPE_PIN,
    CRED_TYPE_QR,
} cred_type_t;

typedef struct {
    cred_type_t type;
    uint8_t     data[CRED_DATA_MAX_LEN];
    uint8_t     data_len;
    bool        is_duress;
    bool        is_active;
    char        label[CRED_LABEL_MAX_LEN];
} credential_t;

void cred_store_init(void);
bool cred_store_add(const credential_t *cred);
bool cred_store_remove(const char *label);
bool cred_store_lookup(cred_type_t type, const uint8_t *data,
                       uint8_t data_len, bool *is_duress);
int  cred_store_count(void);
void cred_store_seed_defaults(void);