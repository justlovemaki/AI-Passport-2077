#pragma once
#include <stdbool.h>
#include "demo.h"
#include "badge_navigation.h"
void demo_voice_enter(void);
void demo_voice_exit(void);
void demo_voice_key(bsp_btn_t button,bsp_btn_ev_t event);
esp_err_t demo_voice_start(void);
esp_err_t demo_voice_stop(void);
esp_err_t demo_voice_play(unsigned clip);
/* Navigation owner only. Includes startup and queued audio, not just playback. */
bool demo_voice_audio_busy(void);
#ifdef BADGE_CONTROL_DEVICE_PROBE
bool demo_voice_control_probe(unsigned clip);
#endif
bool demo_voice_back(void);
bool demo_voice_return_context(badge_return_context_t *context);
