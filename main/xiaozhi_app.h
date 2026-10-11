#pragma once
#include "demo.h"
#include "badge_navigation.h"
#include "yao_service.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void demo_xiaozhi_enter(void);
bool demo_xiaozhi_set_volume(unsigned volume);
void demo_xiaozhi_exit(void);
void demo_xiaozhi_key(bsp_btn_t button,bsp_btn_ev_t event);
esp_err_t demo_xiaozhi_start(void);
esp_err_t demo_xiaozhi_stop(void);
bool demo_xiaozhi_return_context(badge_return_context_t *context);
/* Navigation task, before start; copies a compact confirmed reading, not JSON. */
bool demo_xiaozhi_set_reading(const yao_record_t *record);
// Navigation task only, outside the LVGL lock; never touches a running worker.
void demo_xiaozhi_connection_tick(void);
void demo_xiaozhi_release_connection(void);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
bool demo_xiaozhi_active(void);
unsigned demo_xiaozhi_saved_volume(void);
esp_err_t demo_xiaozhi_save_volume(unsigned value);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
bool demo_xiaozhi_idle(void);
esp_err_t demo_xiaozhi_get_backend(char *url,size_t size,bool *custom);
esp_err_t demo_xiaozhi_save_backend(const char *url);
#ifdef __cplusplus
}
#endif
