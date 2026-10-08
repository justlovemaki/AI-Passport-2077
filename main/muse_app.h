#pragma once

#include "demo.h"

#ifdef __cplusplus
extern "C" {
#endif
void demo_muse_enter(void);
void demo_muse_exit(void);
void demo_muse_key(bsp_btn_t button, bsp_btn_ev_t event);
esp_err_t demo_muse_start(void);
esp_err_t demo_muse_stop(void);
#ifdef __cplusplus
}
#endif
