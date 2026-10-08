/* Cyber badge shell. Input task owns navigation; button callbacks only enqueue. */
#include "badge_navigation.h"
#include "badge_power.h"
#include "badge_power_logic.h"
#include "badge_profile.h"
#include "profile_store.h"
#include "profile_usb.h"
#include "profile_protocol.h"
#include "badge_network.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include "badge_ui.h"
#include "game_registry.h"
#include "badge_control.h"
#include "voice_app.h"
#include "xiaozhi_app.h"
#include "xiaozhi_style.h"
#include "xiaozhi_wake.h"
#include "radio_app.h"
#include "yao_app.h"
#include "badge_reminder.h"
#include "badge_alarm_service.h"
#include "bsp_audio.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "badge_alert.h"
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>

static const char *TAG="cyber_badge";
typedef struct {bsp_btn_t button; bsp_btn_ev_t event;} input_event_t;
static QueueHandle_t input_queue;
static atomic_bool input_ready;
static void control_wakeup(void){const input_event_t signal={(bsp_btn_t)255,BSP_BTN_RELEASE};if(input_queue)xQueueSend(input_queue,&signal,0);}
static badge_navigation_t navigation;
static bool voice_suspended;
static bool on_voice_page(void){return navigation.page==BADGE_PLAYING&&
    !strcmp(BADGE_GAMES_REGISTRY[navigation.game_selected].id,"voice-keychain");}
static bool on_yao_page(void){return navigation.page==BADGE_PLAYING&&
    !strcmp(BADGE_GAMES_REGISTRY[navigation.game_selected].id,"cyber-yao");}
static bool resume_voice_audio(void){
    if(!voice_suspended)return true;
    if(xz_wake_stop()!=ESP_OK)return false;
    if(demo_voice_start()!=ESP_OK)return false;
    voice_suspended=false;return true;
}
static bool voice_idle_wake(void){
    if(!on_voice_page())return false;
    if(!xz_wake_enabled()){resume_voice_audio();return false;}
    if(!voice_suspended&&!demo_voice_audio_busy()&&demo_voice_stop()==ESP_OK){
        voice_suspended=true;ESP_LOGI(TAG,"Soundboard idle: resume local wake");
    }
    return voice_suspended;
}
static badge_return_menu_t return_menu;
static nvs_handle_t settings;
static bool settings_open, storage_ok, battery_ok, buttons_ok;
static int battery=-1;
static char unit[7]="------";
static unsigned profile_seen;
static unsigned control_qr_mask;
static badge_reminder_t reminder;
static bool reminder_closing;
static uint32_t reminder_current_id;
static void publish_control(void){badge_control_publish((navigation.brightness+1)*20,navigation.badge_mask,control_qr_mask,navigation.badge_count,navigation.active_badge);badge_control_runtime(battery,reminder.deadline!=0||reminder.ringing,badge_reminder_remaining(&reminder,esp_timer_get_time()),reminder.text);}

static void load_settings(void) {
    esp_err_t err=nvs_flash_init();
    if(err==ESP_OK) err=nvs_open("cyber_badge_v1",NVS_READWRITE,&settings);
    settings_open=err==ESP_OK;
    uint8_t brightness=3;
    if(settings_open) {
        err=nvs_get_u8(settings,"brightness",&brightness);
        if(err==ESP_ERR_NVS_NOT_FOUND) err=ESP_OK;
        if(err!=ESP_OK || brightness>4) {
            nvs_close(settings); settings_open=false;
            ESP_LOGW(TAG,"Invalid settings preserved; using defaults");
        } else navigation.brightness=brightness;
        if(settings_open){
            uint8_t choice=BADGE_SCREEN_TIMEOUT_DEFAULT;
            esp_err_t timeout_err=nvs_get_u8(settings,"screen_timeout",&choice);
            if(timeout_err==ESP_OK&&choice<BADGE_SCREEN_TIMEOUT_COUNT)navigation.screen_timeout=choice;
            else if(timeout_err!=ESP_ERR_NVS_NOT_FOUND)ESP_LOGW(TAG,"Invalid screen timeout preserved; using 60 seconds");
        }
    }
    badge_power_set_timeout(navigation.screen_timeout);
    storage_ok=settings_open;
    if(!storage_ok) ESP_LOGW(TAG,"Settings unavailable; no NVS erase attempted");
}
static void save_settings(void) {
    esp_err_t err=settings_open?nvs_set_u8(settings,"brightness",navigation.brightness):ESP_ERR_INVALID_STATE;
    if(err==ESP_OK) err=nvs_commit(settings);
    storage_ok=err==ESP_OK;
    if(!storage_ok) ESP_LOGW(TAG,"Brightness save failed: %s",esp_err_to_name(err));
}
static void load_profile_ui(void) {
    const uint8_t *cards[PROFILE_BADGE_COUNT]={0};unsigned mask=0;control_qr_mask=0;
    for(unsigned i=0;i<profile_store_count();i++){cards[i]=profile_store_card_for(i);if(cards[i])mask|=1u<<i;}
    for(unsigned i=0;i<PROFILE_BADGE_COUNT;i++){
        size_t n=0;const char *raw=profile_store_json_for(i,&n);cJSON *p=raw?cJSON_ParseWithLength(raw,n):NULL;
        const cJSON *name=cJSON_GetObjectItemCaseSensitive(p,"name");
        badge_control_identity(i,cJSON_IsString(name)?name->valuestring:"");cJSON_Delete(p);
    }
    badge_ui_set_badges(cards,profile_store_count());
    unsigned formats[PROFILE_BADGE_COUNT];for(unsigned i=0;i<PROFILE_BADGE_COUNT;i++)formats[i]=profile_store_version_for(i);
    badge_ui_set_badge_formats(formats);
    badge_navigation_badges(&navigation,profile_store_count(),profile_store_badge(),mask);
    for(unsigned i=0;i<profile_store_count();i++)if(profile_store_asset_for(i,PROFILE_ASSET_QR))control_qr_mask|=1u<<i;
    publish_control();
    badge_network_onboarding(mask==0);
    if(profile_store_version()>=4)badge_ui_set_tactical(profile_store_card(),profile_store_asset(PROFILE_ASSET_AVATAR));
    else if(profile_store_version()>=3)badge_ui_set_portrait(profile_store_card(),profile_store_asset(PROFILE_ASSET_AVATAR));
    else badge_ui_set_profile(profile_store_card());
    uint32_t colors[5];const uint32_t *palette=NULL;
    size_t length;const char *raw=profile_store_json(&length);
    cJSON *p=cJSON_ParseWithLength(raw,length),*theme=cJSON_GetObjectItemCaseSensitive(p,"theme");
    const char *keys[]={"background","panel","accent","text","muted"};
    if(cJSON_IsObject(theme)) {bool valid=true;for(unsigned i=0;i<5;i++) {
        cJSON *v=cJSON_GetObjectItemCaseSensitive(theme,keys[i]);
        if(!cJSON_IsString(v)||strlen(v->valuestring)!=7){valid=false;break;}
        colors[i]=(uint32_t)strtoul(v->valuestring+1,NULL,16);
    }if(valid)palette=colors;}
    badge_ui_set_custom(profile_store_asset(PROFILE_ASSET_BRAND),profile_store_asset(PROFILE_ASSET_LOGO),profile_store_asset(PROFILE_ASSET_QR),palette);
    cJSON_Delete(p);
}
static void update_status(void) {
    badge_network_status_t network;badge_network_status(&network);
        badge_ui_network(network.active,network.connected,network.ssid,network.ap_ssid,network.ap_password,network.ip,network.message);
    badge_ui_status(unit,battery,(uint32_t)(esp_timer_get_time()/1000000),storage_ok,buttons_ok);
}
static void render_shell(void) {
    const badge_game_t *g=BADGE_GAME_COUNT?&BADGE_GAMES_REGISTRY[navigation.game_selected]:NULL;
    navigation.ai_enabled=xz_wake_enabled();navigation.ai_volume=demo_xiaozhi_saved_volume();navigation.ai_style=xiaozhi_style_get();
    badge_ui_ai_status(navigation.ai_selected==4?"按 OK 切换并保存":navigation.ai_selected==3?xz_wake_phrase():xz_wake_status());
    badge_ui_render(&navigation,g?g->lifecycle.name:"暂无小程序",g?g->description:"",g?g->category:"",g?g->artwork:BADGE_GAME_ART_NONE);
    update_status();
}
static badge_input_t translate(const input_event_t *input) {
    badge_input_t shortcut=badge_ai_long_gesture(navigation.page,input->button==BSP_BTN_UP,input->button==BSP_BTN_DOWN,input->event==BSP_BTN_LONG);
    if(shortcut!=BADGE_NONE)return shortcut;
    if(input->button==BSP_BTN_OK)return badge_ok_gesture(navigation.page,input->event==BSP_BTN_LONG,
            input->event==BSP_BTN_CLICK||input->event==BSP_BTN_DOUBLE);
    if(input->button==BSP_BTN_UP)return badge_up_gesture(navigation.page,navigation.badge_mask!=0,
            input->event==BSP_BTN_LONG,input->event==BSP_BTN_CLICK||input->event==BSP_BTN_DOUBLE);
    if(input->event!=BSP_BTN_CLICK && input->event!=BSP_BTN_DOUBLE) return BADGE_NONE;
    if(input->button==BSP_BTN_DOWN) return BADGE_DOWN;
    return BADGE_NONE;
}
/* Direct mini-app -> AI handoff; no transient home screen. Navigation owns
 * both lifecycles and copies context before starting the voice worker. */
static bool return_to_yao(void){
    badge_power_display_tick(true,true);
    if(badge_power_screen_off())return false;
    if(demo_xiaozhi_stop()!=ESP_OK)return false;
    /* Without a cached longitude, let the pending geolocation TLS request use
     * the RAM held by the parked AI connection. Fresh locations keep reuse. */
    if(!yao_location_get().configured)demo_xiaozhi_release_connection();
    if(!bsp_lvgl_lock(1000))return false;
    demo_xiaozhi_exit();navigation.page=BADGE_PLAYING;demo_yao_enter();
    bsp_lvgl_unlock();demo_yao_start();return true;
}
static void interpret_yao(const yao_record_t *record){
    badge_network_status_t net;badge_network_status(&net);
    if(!net.connected){demo_yao_notice("请先在系统设置连接 Wi-Fi，卦象已保留");return;}
    bool prepared=demo_xiaozhi_set_reading(record);
    if(!prepared){demo_yao_notice("小智暂时忙，请稍后重试");return;}
    if(xz_wake_stop()!=ESP_OK){demo_xiaozhi_set_reading(NULL);return;}
    if(!bsp_lvgl_lock(1000)){demo_xiaozhi_set_reading(NULL);return;}
    demo_yao_stop();demo_yao_exit();navigation.ai_return=BADGE_PLAYING;navigation.page=BADGE_AI_CHAT;
    demo_xiaozhi_enter();bsp_lvgl_unlock();
    if(demo_xiaozhi_start()!=ESP_OK){demo_xiaozhi_set_reading(NULL);if(return_to_yao())demo_yao_notice("小智启动失败，卦象已保留");}
}
static void process_input(const input_event_t *input) {
    static badge_wake_filter_t wake_filter;
    bool asleep=badge_power_screen_off();
    badge_power_activity();
    /* Whenever user presses any button while Wi-Fi is disconnected, immediately probe network! */
    if(input->event==BSP_BTN_CLICK||input->event==BSP_BTN_PRESS){
        badge_network_wake_probe();
    }
    bool consumed=badge_wake_consumed(&wake_filter,(uint32_t)(esp_timer_get_time()/1000),asleep,
                          input->event==BSP_BTN_PRESS,input->event==BSP_BTN_RELEASE);
#ifdef BADGE_XIAOZHI_POWER_PROBE
    ESP_LOGI("xz_probe","INPUT button=%u event=%u asleep=%u consumed=%u",input->button,input->event,asleep,consumed);
#endif
    if(consumed)return;
    if(reminder.ringing){if(input->button==BSP_BTN_OK&&(input->event==BSP_BTN_CLICK||input->event==BSP_BTN_DOUBLE))reminder_closing=true;return;}
    badge_input_t key=translate(input);
    badge_return_menu_t prior_menu=return_menu;
    key=badge_return_menu_handle(&return_menu,navigation.page,key);
    if(prior_menu.open||return_menu.open) {
        if(!bsp_lvgl_lock(1000)){return_menu=prior_menu;return;}
        badge_ui_return_menu(return_menu.open?&return_menu:NULL);bsp_lvgl_unlock();
        if(key==BADGE_NONE)return;
    }
    if(navigation.page==BADGE_PLAYING && key==BADGE_BACK) {
        const badge_game_t *game=&BADGE_GAMES_REGISTRY[navigation.game_selected];
        if(game->back && game->back()) return;
    }
    if(navigation.page==BADGE_AI_CHAT&&key!=BADGE_BACK&&key!=BADGE_GO_HOME&&key!=BADGE_AI_OPEN_SETTINGS&&key!=BADGE_AI_CYCLE_STYLE){demo_xiaozhi_key(input->button,input->event);return;}
    if(navigation.page==BADGE_PLAYING && key!=BADGE_BACK && key!=BADGE_GO_HOME) {
        if(on_voice_page()&&input->event!=BSP_BTN_PRESS&&input->event!=BSP_BTN_RELEASE&&!resume_voice_audio())return;
        BADGE_GAMES_REGISTRY[navigation.game_selected].lifecycle.key(input->button,input->event);
        if(!strcmp(BADGE_GAMES_REGISTRY[navigation.game_selected].id,"cyber-yao")){
            yao_record_t record;if(demo_yao_take_interpretation(&record))interpret_yao(&record);
        }
        return;
    }
    badge_navigation_t previous=navigation;
    badge_action_t action=BADGE_IDLE;
    action=badge_navigation_handle(&navigation,key);
    if(action==BADGE_IDLE) return;
    if(action==BADGE_SLEEP_SAVE){
        esp_err_t e=settings_open?nvs_set_u8(settings,"screen_timeout",navigation.screen_timeout):ESP_ERR_INVALID_STATE;
        if(e==ESP_OK)e=nvs_commit(settings);
        storage_ok=e==ESP_OK;
        if(storage_ok)badge_power_set_timeout(navigation.screen_timeout);
        else {navigation=previous;ESP_LOGW(TAG,"Screen timeout save failed: %s",esp_err_to_name(e));}
    }
    if(action==BADGE_AI_TOGGLE){esp_err_t e=xz_wake_enable(!xz_wake_enabled());if(e!=ESP_OK)ESP_LOGW(TAG,"AI preference: %s",esp_err_to_name(e));}
    if(action==BADGE_AI_VOLUME){esp_err_t e=demo_xiaozhi_save_volume(navigation.ai_volume);if(e!=ESP_OK)ESP_LOGW(TAG,"AI volume: %s",esp_err_to_name(e));}
    if(action==BADGE_AI_STYLE&&!xiaozhi_style_set(navigation.ai_style))navigation=previous;
    /* The conversation timer observes the cached style. Keep its UI and worker
     * alive; the shell must not render over the active conversation screen. */
    if(action==BADGE_AI_STYLE&&navigation.page==BADGE_AI_CHAT)return;
    if(action==BADGE_LAUNCH||action==BADGE_AI_START){if(xz_wake_stop()!=ESP_OK){navigation=previous;return;}}
    if(action==BADGE_AI_STOP&&navigation.page==BADGE_PLAYING){
        navigation=previous;if(!return_to_yao())navigation=previous;return;
    }
    if(action==BADGE_AI_STOP){if(demo_xiaozhi_stop()!=ESP_OK){navigation=previous;return;}}
    if(action==BADGE_LAUNCH||action==BADGE_WIFI_START||action==BADGE_WIFI_TOGGLE)demo_xiaozhi_release_connection();
    if(action==BADGE_WIFI_START){badge_network_start_setup();return;}
    if(navigation.page==BADGE_WIFI&&previous.page!=BADGE_WIFI){badge_network_start_setup();}
    if(navigation.page!=BADGE_WIFI&&previous.page==BADGE_WIFI){badge_network_close_ap();}
    if(action==BADGE_SELECT) {
        esp_err_t e=profile_protocol_select(navigation.badge_selected);
        if(e!=ESP_OK){navigation=previous;ESP_LOGW(TAG,"Badge switch busy or failed: %s",esp_err_to_name(e));}
        if(e==ESP_OK&&bsp_lvgl_lock(1000)) {
            if(profile_protocol_lock()) {
                load_profile_ui();render_shell();profile_seen=profile_store_revision();profile_store_displayed(profile_seen);profile_protocol_unlock();
            }
            bsp_lvgl_unlock();
        }
        return;
    }
    if(action==BADGE_WIFI_TOGGLE) {badge_network_toggle();return;}
    if(action==BADGE_STOP) {
        if(xz_wake_stop()!=ESP_OK){navigation=previous;return;}
        const demo_entry_t *g=&BADGE_GAMES_REGISTRY[navigation.game_selected].lifecycle;
        esp_err_t err=g->stop?g->stop():ESP_OK;
        if(err!=ESP_OK) {
            navigation=previous;
            ESP_LOGE(TAG,"Game stop failed, long OK retries: %s",esp_err_to_name(err));
            return;
        }
    }
    if(!bsp_lvgl_lock(1000)) {navigation=previous; return;}
    if(action==BADGE_AI_START){
        badge_network_close_ap();
        badge_ui_destroy();demo_xiaozhi_enter();bsp_lvgl_unlock();esp_err_t e=demo_xiaozhi_start();ESP_LOGI(TAG,"AI start: %s",esp_err_to_name(e));return;
    }
    if(action==BADGE_AI_STOP){demo_xiaozhi_exit();badge_ui_create();}
    if(action==BADGE_LAUNCH) {
        const demo_entry_t *g=&BADGE_GAMES_REGISTRY[navigation.game_selected].lifecycle;
        badge_ui_destroy(); g->enter(); bsp_lvgl_unlock();
        esp_err_t err=g->start?g->start():ESP_OK;
        ESP_LOGI(TAG,"Game %s start: %s",BADGE_GAMES_REGISTRY[navigation.game_selected].id,esp_err_to_name(err));
        return;
    }
    if(action==BADGE_STOP) {
        voice_suspended=false;
        BADGE_GAMES_REGISTRY[navigation.game_selected].lifecycle.exit(); badge_ui_create();
        ESP_LOGI(TAG,"Returned from game to page %d",navigation.page);
    }
    render_shell(); bsp_lvgl_unlock();
    if(action==BADGE_BRIGHTNESS) bsp_display_backlight((navigation.brightness+1)*20);
    if(previous.page==BADGE_DISPLAY_SETTINGS && navigation.page!=BADGE_DISPLAY_SETTINGS) save_settings();
}
/* Only the navigation task stops applications. Never join the voice worker
 * from its own MCP callback, and never delete an active producer's UI. */
static bool control_stop_current(void) {
    badge_power_display_tick(true,true);
    if(badge_power_screen_off())return false;
    if(xz_wake_stop()!=ESP_OK)return false;
    if(navigation.page==BADGE_AI_CHAT){
        if(demo_xiaozhi_stop()!=ESP_OK)return false;
    }else if(navigation.page==BADGE_PLAYING){
        const demo_entry_t *g=&BADGE_GAMES_REGISTRY[navigation.game_selected].lifecycle;
        if(g->stop&&g->stop()!=ESP_OK)return false;
    }
    return true;
}
static bool control_home(void) {
    if(!control_stop_current()||!bsp_lvgl_lock(1000))return false;
    if(navigation.page==BADGE_AI_CHAT){demo_xiaozhi_exit();badge_ui_create();}
    else if(navigation.page==BADGE_PLAYING){
        BADGE_GAMES_REGISTRY[navigation.game_selected].lifecycle.exit();
        voice_suspended=false;badge_ui_create();
    }
    badge_ui_return_menu(NULL);return_menu=(badge_return_menu_t){0};
    navigation.page=BADGE_HOME;render_shell();bsp_lvgl_unlock();return true;
}
static void shutdown_step(const char *step,esp_err_t e){
    if(e!=ESP_OK)ESP_LOGW(TAG,"Shutdown %s failed: %s; continuing to stop remaining peripherals",step,esp_err_to_name(e));
}
/* Terminal operation. Navigation owns every producer before suspending buses. */
static bool control_shutdown(void){
    /* Refuse shutdown while USB/web is committing profile data. */
    if(!profile_protocol_lock())return false;
    if(!profile_store_ready()){profile_protocol_unlock();return false;}
    profile_protocol_unlock();
    if(!control_stop_current())return false;
    demo_xiaozhi_release_connection();
    atomic_store(&input_ready,false);
    if(badge_network_shutdown()!=ESP_OK){ESP_LOGE(TAG,"Network shutdown failed; restarting to restore service");esp_restart();}
    /* Freeze configuration writes until deep sleep; reboot recreates the lock. */
    if(!profile_protocol_lock()||!profile_store_ready()){ESP_LOGE(TAG,"Profile write during shutdown; restarting");esp_restart();}
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if(bsp_button_prepare_deep_sleep()!=ESP_OK){ESP_LOGE(TAG,"Wake button setup failed; restarting");esp_restart();}
    esp_err_t battery_sleep=bsp_battery_sleep(),audio_sleep=bsp_audio_sleep();
    ESP_LOGI(TAG,"Shutdown suspend battery=%s audio=%s",esp_err_to_name(battery_sleep),esp_err_to_name(audio_sleep));
    shutdown_step("I2S",bsp_audio_prepare_deep_sleep());
    shutdown_step("I2C",bsp_i2c_prepare_deep_sleep());
    if(!bsp_lvgl_lock(1000)){ESP_LOGE(TAG,"Display shutdown lock failed");esp_restart();}
    shutdown_step("LCD",bsp_display_prepare_deep_sleep());
    ESP_LOGI(TAG,"Shutdown: deep sleep, UP key wake, reminders retained");
#ifdef BADGE_ASSISTANT_DEVICE_PROBE
    esp_sleep_enable_timer_wakeup(5000000);
#endif
    esp_deep_sleep_start();esp_restart();return false;
}
#ifdef BADGE_CONTROL_DEVICE_PROBE
static bool control_probe_fail_prepare,control_probe_fail_start;
#endif
static bool execute_control_impl(badge_control_command_t c) {
    if(!demo_xiaozhi_active())return false;
    if(c.kind==BC_XZ_VOLUME)return demo_xiaozhi_set_volume(c.value);
    if(c.kind==BC_REMINDER_SET){uint32_t id=0;int64_t due=c.value?badge_alarm_service_now()+c.value:c.when;
        bool ok=badge_alarm_service_add(due,c.minute,c.repeat,c.text,&id);if(ok)badge_control_set_created_id(id);return ok;}
    if(c.kind==BC_REMINDER_CANCEL)return badge_alarm_service_cancel(c.value);
    if(c.kind==BC_HOME)return control_home();
    if(c.kind==BC_SHUTDOWN)return control_shutdown();
    if(c.kind==BC_PROFILE_EDIT){
        esp_err_t e=profile_protocol_edit(c.value,c.revision,c.field,c.text);
        if(e!=ESP_OK){ESP_LOGW(TAG,"Profile edit rejected: %s",esp_err_to_name(e));return false;}
        if(bsp_lvgl_lock(1000)){
            if(profile_protocol_lock()){load_profile_ui();profile_seen=profile_store_revision();profile_store_displayed(profile_seen);profile_protocol_unlock();}
            bsp_lvgl_unlock();
        }publish_control();return true;
    }
    if(c.kind==BC_BRIGHTNESS){
        navigation.brightness=c.value/20-1;bsp_display_backlight(c.value);save_settings();publish_control();return storage_ok;
    }
    const char *target=c.kind==BC_YAO?"cyber-yao":c.kind==BC_MUSE?"muse":c.kind==BC_MUYU?"zen-muyu":(c.kind==BC_RADIO||c.kind==BC_RADIO_PRESET)?"leo-radio":c.kind==BC_VOICE||c.kind==BC_PLAY_VOICE?"voice-keychain":NULL;
    size_t index=0;if(target){for(;index<BADGE_GAME_COUNT;index++)if(!strcmp(BADGE_GAMES_REGISTRY[index].id,target))break;if(index==BADGE_GAME_COUNT)return false;}
    /* Recheck mutable profile data before stopping the conversation. */
    if(c.kind==BC_QR||c.kind==BC_BADGE){
        if(!profile_protocol_lock())return false;
        bool valid=profile_store_ready()&&(c.kind==BC_QR?profile_store_asset(PROFILE_ASSET_QR)!=NULL:profile_store_card_for(c.value)!=NULL);
        if(valid&&c.kind==BC_BADGE&&c.text[0]){
            size_t n;const char *raw=profile_store_json_for(c.value,&n);cJSON *p=cJSON_ParseWithLength(raw,n),*name=cJSON_GetObjectItemCaseSensitive(p,"name");
            valid=cJSON_IsString(name)&&!strcmp(name->valuestring,c.text);cJSON_Delete(p);
        }
        profile_protocol_unlock();if(!valid)return false;
    }
    // Join producers while the current conversation screen remains visible.
    // Destroy/create/render the destination inside ONE LVGL lock, so no home
    // or old-profile frame can be flushed during an assistant transition.
    if(!control_stop_current())return false;
    if(target)demo_xiaozhi_release_connection();
#ifdef BADGE_CONTROL_DEVICE_PROBE
    if(control_probe_fail_prepare){control_probe_fail_prepare=false;goto resume_conversation;}
#endif
    if(c.kind==BC_BADGE&&profile_protocol_select(c.value)!=ESP_OK)goto resume_conversation;
    if(!bsp_lvgl_lock(1000))goto resume_conversation;
    if(!profile_protocol_lock()){bsp_lvgl_unlock();goto resume_conversation;}
    load_profile_ui();profile_seen=profile_store_revision();profile_store_displayed(profile_seen);profile_protocol_unlock();
    badge_ui_return_menu(NULL);return_menu=(badge_return_menu_t){0};
    demo_xiaozhi_exit();
    if(target){
        navigation.game_selected=index;navigation.playing_return=BADGE_HOME;navigation.page=BADGE_PLAYING;
        const demo_entry_t *g=&BADGE_GAMES_REGISTRY[index].lifecycle;g->enter();bsp_lvgl_unlock();
        esp_err_t e;
#ifdef BADGE_CONTROL_DEVICE_PROBE
        if(control_probe_fail_start){control_probe_fail_start=false;e=ESP_ERR_NO_MEM;}
        else
#endif
        e=c.kind==BC_RADIO_PRESET?demo_radio_start_preset(c.value):(g->start?g->start():ESP_OK);
        if(e==ESP_OK&&c.kind==BC_PLAY_VOICE)e=demo_voice_play(c.value);
        if(e!=ESP_OK){ESP_LOGE(TAG,"Assistant app launch failed: %s",esp_err_to_name(e));control_home();return false;}
    }else{navigation.page=c.kind==BC_QR?BADGE_QR:BADGE_HOME;navigation.qr_return=BADGE_HOME;badge_ui_create();render_shell();bsp_lvgl_unlock();}
    return true;
resume_conversation:
    // No screen was deleted. Restore its producer so a transient profile/lock
    // failure cannot leave the visible conversation permanently unresponsive.
    {esp_err_t e=demo_xiaozhi_start();if(e!=ESP_OK){ESP_LOGE(TAG,"Assistant resume failed: %s",esp_err_to_name(e));control_home();}}
    return false;
}
#ifdef BADGE_YAO_DEVICE_PROBE
#include "../tests/yao_device_probe.h"
#endif
static bool execute_control(badge_control_command_t c){
#ifdef BADGE_CONTROL_DEVICE_PROBE
    unsigned homes=badge_ui_probe_home_count();
#endif
    bool ok=execute_control_impl(c);
#ifdef BADGE_CONTROL_DEVICE_PROBE
    if(ok){
        unsigned expected=c.kind==BC_BADGE?1:0;
        configASSERT(badge_ui_probe_home_count()-homes==expected);
        if(c.kind==BC_BADGE)configASSERT(badge_ui_probe_home_badge()==c.value);
        ESP_LOGI("control_probe","DIRECT_PAGE_PASS action=%u home_renders=%u",c.kind,expected);
    }
#endif
    return ok;
}
#ifdef BADGE_CONTROL_DEVICE_PROBE
#include "../tests/control_device_probe.h"
#endif
#ifdef BADGE_XIAOZHI_CONNECT_PROBE
#include "../tests/xiaozhi_connect_probe.h"
#endif
#ifdef BADGE_ASSISTANT_DEVICE_PROBE
#include "profile_edit.h"
#include "../tests/assistant_device_probe.h"
#endif
#ifdef BADGE_XIAOZHI_REUSE_PROBE
#include "../tests/xiaozhi_reuse_probe.h"
#endif
#ifdef BADGE_WAKE_DEVICE_PROBE
#include "../tests/wake_device_probe.h"
#endif
#ifdef BADGE_XIAOZHI_STABILITY_PROBE
extern unsigned demo_xiaozhi_stability_replies(void);
extern unsigned demo_xiaozhi_stability_allocations(void);
static void stability_probe_tick(void){
    static unsigned phase,volume;static bool saved_wake;static int64_t started;
    int64_t now=esp_timer_get_time();
    if(!phase&&now>12000000){
        badge_power_set_timeout(0); /* Probe only: keep USB reachable, no NVS write. */
        saved_wake=xz_wake_enabled();configASSERT(xz_wake_enable(true)==ESP_OK);
        volume=demo_xiaozhi_saved_volume();configASSERT(control_home());
        input_event_t key={BSP_BTN_UP,BSP_BTN_LONG};process_input(&key);
        configASSERT(navigation.page==BADGE_AI_CHAT);started=now;phase=1;
    }else if(phase==1&&(demo_xiaozhi_stability_replies()>=5||now-started>240000000)){
        unsigned replies=demo_xiaozhi_stability_replies();bool chat=navigation.page==BADGE_AI_CHAT;
        configASSERT(control_home());configASSERT(demo_xiaozhi_saved_volume()==volume);
        configASSERT(demo_xiaozhi_stability_allocations()==1);
        ESP_LOGI("stability_bench","BENCH_DONE replies=%u chat=%u volume_preserved=1 codec_allocations=1",replies,chat);phase=2;started=now;
    }else if(phase==2&&now-started>6000000){
        configASSERT(xz_wake_listening());ESP_LOGI("stability_bench","BACKGROUND_WAKE_AFTER_CLOUD_PASS");
        configASSERT(xz_wake_enable(saved_wake)==ESP_OK);phase=3;
    }
}
#endif
#ifdef BADGE_TOOL_CLOUD_PROBE
#include "../tests/tool_cloud_probe.h"
#endif

static void input_worker(void *arg) {
    (void)arg;
    input_event_t input;
    uint32_t last_status=0,last_battery=0;
    for(;;) {
        demo_xiaozhi_connection_tick();
#if defined(BADGE_XIAOZHI_CONNECT_PROBE) && !defined(BADGE_ASSISTANT_DEVICE_PROBE)
        if(atomic_load(&input_ready))connect_probe_tick();
#endif
#ifdef BADGE_ASSISTANT_DEVICE_PROBE
        if(atomic_load(&input_ready))assistant_device_probe_tick();
#endif
#ifdef BADGE_YAO_DEVICE_PROBE
        if(atomic_load(&input_ready))yao_device_probe_tick();
#endif
        bool got_input=xQueueReceive(input_queue,&input,pdMS_TO_TICKS(badge_power_screen_off()?1000:250))==pdTRUE;
        if(got_input&&atomic_load(&input_ready)&&input.button!=(bsp_btn_t)255)process_input(&input);
        if(atomic_load(&input_ready))badge_power_display_tick(reminder.ringing,false);
        if(atomic_load(&input_ready)){
            publish_control();badge_control_command_t command;
            if(badge_control_take(&command)){badge_power_activity();badge_power_display_tick(true,true);bool ok=!badge_power_screen_off()&&execute_control(command);badge_control_finish(ok);ESP_LOGI(TAG,"Assistant action=%u value=%u result=%s",command.kind,command.value,ok?"OK":"FAILED");}
        }
        if(atomic_load(&input_ready)){
            if(reminder_closing&&badge_alert_dismiss()&&badge_alarm_service_ack(reminder_current_id)){
                reminder_closing=false;reminder_current_id=0;badge_reminder_clear(&reminder);publish_control();
            }
            badge_alarm_t alarm;
            if(!reminder.ringing&&badge_alarm_service_poll(&alarm)){
                badge_power_activity();
                if(control_home()&&badge_alert_start(alarm.text)){
                    reminder_current_id=alarm.id;snprintf(reminder.text,sizeof(reminder.text),"%s",alarm.text);reminder.ringing=true;publish_control();ESP_LOGI(TAG,"Reminder fired id=%u",(unsigned)alarm.id);
                }
            }
        }
#ifdef BADGE_CONTROL_DEVICE_PROBE
        if(atomic_load(&input_ready))control_probe_tick();
#endif
#ifdef BADGE_WAKE_DEVICE_PROBE
        if(atomic_load(&input_ready))wake_probe_tick();
#endif
#ifdef BADGE_XIAOZHI_REUSE_PROBE
        if(atomic_load(&input_ready))reuse_probe_tick();
#endif
#ifdef BADGE_XIAOZHI_STABILITY_PROBE
        if(atomic_load(&input_ready))stability_probe_tick();
#endif
#ifdef BADGE_TOOL_CLOUD_PROBE
        if(atomic_load(&input_ready))cloud_probe_tick();
#endif
        if(atomic_load(&input_ready)){
            if(navigation.page==BADGE_AI_CHAT&&xz_wake_enabled()&&demo_xiaozhi_idle()){
                badge_page_t origin=navigation.ai_return;
                if(origin==BADGE_PLAYING){return_to_yao();}
                else if(control_home()&&bsp_lvgl_lock(1000)){navigation.page=origin;render_shell();bsp_lvgl_unlock();}
            }
            bool voice_quiet=voice_idle_wake();
            bool quiet=(navigation.page!=BADGE_PLAYING||voice_quiet)&&navigation.page!=BADGE_AI_CHAT&&!reminder.ringing;
            if(quiet&&xz_wake_take_trigger()){
                badge_power_display_tick(true,true);
                if(badge_power_screen_off())continue;
                badge_page_t origin=voice_quiet?navigation.playing_return:navigation.page;
                bool stopped=voice_quiet?control_home():xz_wake_stop()==ESP_OK;
                if(stopped&&bsp_lvgl_lock(1000)){
                    navigation.ai_return=origin;navigation.page=BADGE_AI_CHAT;badge_ui_return_menu(NULL);return_menu=(badge_return_menu_t){0};
                    badge_ui_destroy();demo_xiaozhi_enter();bsp_lvgl_unlock();
                    esp_err_t start_result=demo_xiaozhi_start();ESP_LOGI(TAG,"Voice wake AI start: %s",esp_err_to_name(start_result));
                }
            }else xz_wake_tick(quiet);
        }
        if(!atomic_load(&input_ready)) continue;
        /* A committed profile still needs its UI references refreshed while
         * the display sleeps. Ask the power owner to wake LVGL, otherwise the
         * flash bank reuse barrier would reject every subsequent upload. */
        if(profile_store_refresh_pending())badge_power_activity();
        badge_power_display_tick(reminder.ringing,false);
        if(badge_power_screen_off()) continue;
        unsigned revision=profile_store_revision();
        if(revision!=profile_seen && bsp_lvgl_lock(100)) {
            if(profile_protocol_lock()) {
                load_profile_ui();
                if(navigation.page!=BADGE_PLAYING&&navigation.page!=BADGE_AI_CHAT) render_shell();
                if(return_menu.open)badge_ui_return_menu(&return_menu);
                profile_seen=profile_store_revision();profile_store_displayed(profile_seen);
                profile_protocol_unlock();
            }
            bsp_lvgl_unlock();
        }
        uint32_t now=(uint32_t)(esp_timer_get_time()/1000000);
        if(now-last_battery>=30) {
            if(!battery_ok)battery_ok=bsp_battery_init()==ESP_OK;
            int next=battery_ok?bsp_battery_soc():-1;if(next>=0)battery=next;
            last_battery=now;
        }
        if(now!=last_status){
            if(on_yao_page())demo_yao_tick();
            if(bsp_lvgl_lock(100)){if(navigation.page==BADGE_AI_SETTINGS)render_shell();update_status();bsp_lvgl_unlock();}
            last_status=now;
        }
    }
}
static void on_key(bsp_btn_t btn,bsp_btn_ev_t event,void *arg) {
    (void)arg;
    if(!atomic_load(&input_ready)) return;
#ifdef BADGE_XIAOZHI_POWER_PROBE
    ESP_LOGI("xz_probe","PHYSICAL button=%u event=%u",btn,event);
#endif
    const input_event_t input={btn,event};
    (void)xQueueSend(input_queue,&input,0);
}
#ifdef BADGE_RADIO_DEVICE_PROBE
#include "../tests/radio_device_probe.h"
#endif
#ifdef BADGE_XIAOZHI_DEVICE_PROBE
#include "../tests/xiaozhi_device_probe.h"
#endif
#ifdef BADGE_BSP_POWER_PROBE
#include "../tests/bsp_power_device_probe.h"
#endif
void app_main(void) {
    ESP_LOGI(TAG,"Arasaka Personnel Badge " BADGE_VERSION " starting");
    if(badge_power_init()!=ESP_OK)ESP_LOGW(TAG,"Power management unavailable");
    bsp_i2c_init();
    /* Put the physically powered codec into standby even before the first app
     * opens it. No audio task exists yet; later owners use init() to resume. */
    esp_err_t audio_idle=bsp_audio_init();
    if(audio_idle==ESP_OK)audio_idle=bsp_audio_sleep();
    if(audio_idle!=ESP_OK)ESP_LOGW(TAG,"Initial audio standby: %s",esp_err_to_name(audio_idle));
    if(bsp_display_init()!=ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG,"Display initialization failed"); return;
    }
#ifdef BADGE_BSP_POWER_PROBE
    bsp_power_device_probe();
#endif
    badge_navigation_init(&navigation,BADGE_GAME_COUNT);
    load_settings();if(!badge_alarm_service_init())ESP_LOGE(TAG,"Reminder storage unavailable; data preserved");xiaozhi_style_init();xz_wake_init();xz_wake_on_trigger(control_wakeup);xz_wake_on_prepare(demo_xiaozhi_release_connection);
    esp_err_t profile_err=profile_store_init();
    if(profile_err!=ESP_OK)ESP_LOGE(TAG,"Profile storage unavailable: %s (existing partitions preserved)",esp_err_to_name(profile_err));
    profile_seen=profile_store_revision();
    profile_protocol_init();badge_network_init();
    bsp_display_backlight((navigation.brightness+1)*20);
    uint8_t mac[6];
    if(esp_read_mac(mac,ESP_MAC_WIFI_STA)==ESP_OK) snprintf(unit,sizeof(unit),"%02X%02X%02X",mac[3],mac[4],mac[5]);
    battery_ok=bsp_battery_init()==ESP_OK;
    battery=battery_ok?bsp_battery_soc():-1;
    input_queue=xQueueCreate(16,sizeof(input_event_t));
    badge_control_set_wakeup(control_wakeup);
    if(input_queue && xTaskCreate(input_worker,"badge_input",6144,NULL,5,NULL)==pdPASS) {
        buttons_ok=bsp_button_init(on_key,NULL)==ESP_OK;
    } else {
        if(input_queue) vQueueDelete(input_queue);
        input_queue=NULL;
        ESP_LOGE(TAG,"Input task allocation failed");
    }
    if(bsp_lvgl_lock(1000)) {
        if(profile_protocol_lock()) {
            load_profile_ui();badge_ui_create();render_shell();
            profile_seen=profile_store_revision();profile_store_displayed(profile_seen);
            profile_protocol_unlock();
        }
        bsp_lvgl_unlock();
        atomic_store(&input_ready,buttons_ok);
    }
    ESP_LOGI(TAG,"Ready: home=badge games=%u buttons=%d battery=%d soc=%d storage=%d",
        (unsigned)BADGE_GAME_COUNT,buttons_ok,battery_ok,battery,storage_ok);
    ESP_LOGI(TAG,"Profile storage: %s; USB configuration: %s",esp_err_to_name(profile_err),esp_err_to_name(profile_usb_start()));
#ifdef BADGE_RADIO_DEVICE_PROBE
    xTaskCreate(radio_probe_task,"radio_probe",4096,NULL,2,NULL);
#endif
#ifdef BADGE_XIAOZHI_DEVICE_PROBE
    xTaskCreate(xz_probe_task,"xz_probe",4096,NULL,2,NULL);
#endif
}
