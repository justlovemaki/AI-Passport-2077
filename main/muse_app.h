#pragma once

#include "demo.h"
#include "badge_navigation.h"

#ifdef __cplusplus
extern "C" {
#endif
void demo_muse_enter(void);
void demo_muse_exit(void);
void demo_muse_key(bsp_btn_t button, bsp_btn_ev_t event);
esp_err_t demo_muse_start(void);
esp_err_t demo_muse_stop(void);
bool demo_muse_return_context(badge_return_context_t *context);
#ifdef __cplusplus
}
#endif
