#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    PASSPORT_MUSE_NEEDS_TOKEN, PASSPORT_MUSE_PAIRING, PASSPORT_MUSE_CONFIRM,
    PASSPORT_MUSE_CONNECTING, PASSPORT_MUSE_READY, PASSPORT_MUSE_LISTENING,
    PASSPORT_MUSE_WORKING, PASSPORT_MUSE_REPLY, PASSPORT_MUSE_ERROR,
    PASSPORT_MUSE_PHONE_SETUP,
} passport_muse_stage_t;
typedef struct {
    passport_muse_stage_t stage;
    bool sdk_token_set, paired, reply_is_answer;
    uint16_t level;
    uint32_t reply_revision;
    char name[64], device_name[32], detail[96], reply[2048];
} passport_muse_snapshot_t;
/* Host-owned shared Wi-Fi adapter. Implementations must be thread-safe. The
 * status callback may receive NULL output pointers and returns true only while
 * the station is connected. performance(true) temporarily disables modem
 * power saving during capture; false restores the normal shared policy. */
typedef struct {
    bool (*network_status)(char *ssid, size_t ssid_cap, int *rssi, bool *secure);
    void (*network_performance)(bool active);
    /* Called by the Muse worker, never under the LVGL lock. */
    void (*runtime_tick)(bool busy, bool audio_busy);
} passport_muse_host_ops_t;
esp_err_t passport_muse_set_host_ops(const passport_muse_host_ops_t *ops);
/* Atomically gates app startup while saving. An empty token preserves the
 * existing token; an empty host with port 0 clears the proxy. */
esp_err_t passport_muse_save_config(const char *token, const char *host, uint16_t port);
esp_err_t passport_muse_set_sdk_token(const char *token);
void passport_muse_config_status(bool *token_set, bool *paired);
esp_err_t passport_muse_set_proxy(const char *host, uint16_t port);
bool passport_muse_proxy_valid(const char *host, uint16_t port);
void passport_muse_proxy_config(char *host, size_t cap, uint16_t *port);
esp_err_t passport_muse_start(void);
bool passport_muse_stop(void);
void passport_muse_key(bool pressed, bool cancel);
void passport_muse_confirm(void);
// Explicit long-DOWN recovery. Clears only Muse account pairing, preserving
// SDK token, system Wi-Fi and the user's profile.
void passport_muse_repair(void);
void passport_muse_log_diagnostics(void);
void passport_muse_snapshot(passport_muse_snapshot_t *out);
/* Lightweight paging query; avoids copying the reply into button-task stack/RAM. */
bool passport_muse_reply_state(size_t at, uint32_t *revision, passport_muse_stage_t *stage);
size_t muse_hatch_reply_page(const char *text, size_t at, char *out, size_t cap);
// Serial diagnostics: only while the unconfigured Muse page is open. Starts
// and tears down BLE without advertising, pairing, credentials or cloud calls.
bool passport_muse_diagnostic_ble_cycle(void);
#ifdef __cplusplus
}
#endif
