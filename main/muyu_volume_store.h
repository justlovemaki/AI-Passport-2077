#pragma once
#include "nvs.h"
#include <stdint.h>
/* Worker-only NVS access. New volume_pct takes precedence; only NOT_FOUND
 * permits falling back to legacy volume. No erase, migration write or commit
 * occurs while loading. Store changes one key; the caller owns commit. */
esp_err_t muyu_volume_load(nvs_handle_t storage,uint8_t *percent);
esp_err_t muyu_volume_store(nvs_handle_t storage,uint8_t percent);
