#pragma once
#include "demo.h"
#include "badge_navigation.h"
#ifdef __cplusplus
extern "C" {
#endif
#ifdef BADGE_RADIO_DEVICE_PROBE
typedef struct {unsigned station,count,page,selected;int playing;} radio_probe_state_t;
bool demo_radio_probe_state(radio_probe_state_t *state);
void demo_radio_probe_select(unsigned station);
#endif
void demo_radio_enter(void);
void demo_radio_exit(void);
void demo_radio_key(bsp_btn_t button,bsp_btn_ev_t event);
esp_err_t demo_radio_start(void);
esp_err_t demo_radio_start_preset(unsigned preset);
#ifdef BADGE_CONTROL_DEVICE_PROBE
bool demo_radio_control_probe(unsigned preset);
#endif
esp_err_t demo_radio_stop(void);
bool demo_radio_back(void);
bool demo_radio_return_context(badge_return_context_t *context);
#ifdef __cplusplus
}
#endif
