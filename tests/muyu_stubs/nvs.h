#pragma once
/* Minimal boundary for the real Muyu volume storage adapter. */
#include "esp_err.h"
#include <stdint.h>
typedef unsigned nvs_handle_t;
#define ESP_ERR_NVS_NOT_FOUND 100
#define ESP_ERR_NVS_TYPE_MISMATCH 101
esp_err_t nvs_get_u8(nvs_handle_t storage,const char *key,uint8_t *out);
esp_err_t nvs_set_u8(nvs_handle_t storage,const char *key,uint8_t value);
