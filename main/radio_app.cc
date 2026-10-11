extern "C" {
#include "radio_app.h"
#include "bsp_display.h"
#include "lvgl.h"
}
#include "radio_ui.h"
#include "radio_controls.h"
#include "radio_presets.h"
#include <cstring>
static radio_controls_t controls;
static lv_timer_t *timer;
static bool started;
static radio_quick_input_t quick_input;
static void refresh(lv_timer_t *){auto state=radio_ui_snapshot();controls.volume=state.volume;radio_ui_render(controls,state,radio_player_timer_seconds());}
extern "C" void demo_radio_enter(){quick_input={};radio_ui_reset_state();radio_controls_init(&controls,40,0);radio_ui_create();refresh(nullptr);}
static esp_err_t start_radio(int preset){
    started=radio_player_init(preset);if(!bsp_lvgl_lock(1000))return ESP_ERR_TIMEOUT;
    controls.city=radio_player_city();timer=lv_timer_create(refresh,150,nullptr);refresh(nullptr);bsp_lvgl_unlock();return started?ESP_OK:ESP_FAIL;
}
extern "C" esp_err_t demo_radio_start(){return start_radio(-1);}
extern "C" esp_err_t demo_radio_start_preset(unsigned preset){return start_radio(static_cast<int>(preset));}
#ifdef BADGE_CONTROL_DEVICE_PROBE
extern "C" bool demo_radio_control_probe(unsigned preset){return started&&preset<RADIO_PRESET_COUNT&&!strcmp(radio_player_station(radio_player_station_index()).url,RADIO_PRESETS[preset].url);}
#endif
extern "C" esp_err_t demo_radio_stop(){
    if(started&&!radio_player_shutdown(8000))return ESP_ERR_TIMEOUT;
    started=false;if(!bsp_lvgl_lock(1000))return ESP_ERR_TIMEOUT;
    if(timer){lv_timer_delete(timer);timer=nullptr;}bsp_lvgl_unlock();return ESP_OK;
}
extern "C" void demo_radio_exit(){radio_ui_destroy();}
extern "C" bool demo_radio_return_context(badge_return_context_t *context){
    if(!bsp_lvgl_lock(100))return false;
    *context={};context->internal=radio_controls_can_back(&controls);
    context->detail="退出后停止播放";
    context->resume=radio_ui_snapshot().state==RadioPlaybackState::Playing?"继续收听":"继续当前页面";
    bsp_lvgl_unlock();return true;
}
extern "C" bool demo_radio_back(){if(!bsp_lvgl_lock(1000))return true;bool consumed=radio_controls_back(&controls);if(consumed)refresh(nullptr);bsp_lvgl_unlock();return consumed;}
extern "C" void demo_radio_key(bsp_btn_t button,bsp_btn_ev_t event){
    if(!started)return;
    bool direction_click=false;
    if(button!=BSP_BTN_OK){
        radio_input_event_t mapped=event==BSP_BTN_PRESS?RADIO_PRESS:event==BSP_BTN_RELEASE?RADIO_RELEASE:event==BSP_BTN_LONG?RADIO_LONG:event==BSP_BTN_DOUBLE?RADIO_DOUBLE:RADIO_CLICK;
        direction_click=radio_direction_click(&quick_input,button==BSP_BTN_UP?0:1,mapped);
        if(!direction_click&&event!=BSP_BTN_LONG)return;
    }else if(event!=BSP_BTN_CLICK&&event!=BSP_BTN_DOUBLE)return;
    if(button==BSP_BTN_DOWN&&event==BSP_BTN_LONG){radio_player_set_playing(false);return;}
    if(!bsp_lvgl_lock(100))return;
    radio_action_t action=RADIO_NOTHING;
    if(button==BSP_BTN_UP&&event==BSP_BTN_LONG)radio_controls_settings(&controls);
    else if(direction_click||event==BSP_BTN_CLICK||event==BSP_BTN_DOUBLE){
        if(button==BSP_BTN_OK)action=radio_controls_ok(&controls);
        else action=radio_controls_move(&controls,button==BSP_BTN_UP?-1:1);
    }
    const auto next=controls;bsp_lvgl_unlock();
    switch(action){
    case RADIO_CHANNEL_UP:radio_player_select_relative(-1);break;
    case RADIO_CHANNEL_DOWN:radio_player_select_relative(1);break;
    case RADIO_TOGGLE:radio_player_toggle();break;
    case RADIO_VOLUME_CHANGED:radio_player_set_volume(next.volume);break;
    case RADIO_CITY_CHANGED:radio_player_set_city(next.city);break;
    case RADIO_TIMER_CHANGED:radio_player_set_timer(RADIO_TIMER_MINUTES[next.timer]);break;
    default:break;
    }
    if(bsp_lvgl_lock(100)){refresh(nullptr);bsp_lvgl_unlock();}
}

#ifdef BADGE_RADIO_DEVICE_PROBE
extern "C" void demo_radio_probe_select(unsigned station){radio_player_set_station(station);}
extern "C" bool demo_radio_probe_state(radio_probe_state_t *state){
    if(!started||!bsp_lvgl_lock(100))return false;
    *state={static_cast<unsigned>(radio_player_station_index()),static_cast<unsigned>(radio_player_station_count()),static_cast<unsigned>(controls.page),controls.selected,radio_ui_snapshot().state==RadioPlaybackState::Playing};
    bsp_lvgl_unlock();return true;
}
#endif
