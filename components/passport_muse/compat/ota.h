#pragma once
#include <stdbool.h>
typedef enum { OTA_RESULT_APPLIED, OTA_RESULT_SKIPPED, OTA_RESULT_FAILED } ota_result_t;
typedef struct { ota_result_t result; const char *detail, *new_version, *running_version; } ota_event_t;
typedef void (*ota_status_cb)(const ota_event_t *, void *);
bool ota_is_enabled(void);
void ota_start(const char *,bool,ota_status_cb,void *);
