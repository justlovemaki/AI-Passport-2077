#include "radio_ui.h"
#include "radio_diagnostics.h"
#include "freertos/FreeRTOS.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
static portMUX_TYPE state_mux=portMUX_INITIALIZER_UNLOCKED;
static radio_ui_snapshot_t state;
void radio_ui_reset_state(){portENTER_CRITICAL(&state_mux);state=radio_ui_snapshot_t{};portEXIT_CRITICAL(&state_mux);}
radio_ui_snapshot_t radio_ui_snapshot(){portENTER_CRITICAL(&state_mux);auto copy=state;portEXIT_CRITICAL(&state_mux);return copy;}
extern "C" unsigned radio_diagnostics_playback_state(void){portENTER_CRITICAL(&state_mux);unsigned value=static_cast<unsigned>(state.state);portEXIT_CRITICAL(&state_mux);return value;}
void radio_ui_set_station(std::size_t index,std::size_t count,const RadioStation &station){portENTER_CRITICAL(&state_mux);state.index=index;state.count=count;state.station=station;portEXIT_CRITICAL(&state_mux);}
void radio_ui_set_playback(RadioPlaybackState value,const char *detail){portENTER_CRITICAL(&state_mux);state.state=value;if(detail)snprintf(state.detail,sizeof(state.detail),"%s",detail);portEXIT_CRITICAL(&state_mux);}
void radio_ui_set_audio_levels(const uint8_t *levels,std::size_t count){portENTER_CRITICAL(&state_mux);memset(state.levels,0,sizeof(state.levels));if(levels)memcpy(state.levels,levels,std::min(count,sizeof(state.levels)));portEXIT_CRITICAL(&state_mux);}
void radio_ui_set_volume(uint8_t volume){portENTER_CRITICAL(&state_mux);state.volume=volume;portEXIT_CRITICAL(&state_mux);}
void radio_ui_set_location(const char *name){portENTER_CRITICAL(&state_mux);snprintf(state.location,sizeof(state.location),"%s",name?name:"");portEXIT_CRITICAL(&state_mux);}
void radio_ui_set_network(bool connected){portENTER_CRITICAL(&state_mux);state.network=connected;portEXIT_CRITICAL(&state_mux);}
void radio_ui_set_battery(int battery){portENTER_CRITICAL(&state_mux);state.battery=battery;portEXIT_CRITICAL(&state_mux);}
