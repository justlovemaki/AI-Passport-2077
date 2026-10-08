/* Explicit opt-in bench firmware. Only POWER_PROBE reads a short microphone
 * frame into RAM to check resume; no recording is stored or uploaded. */
#ifdef BADGE_XIAOZHI_POWER_PROBE
#include "esp_pm.h"
#include "lvgl.h"
#include "esp_wifi.h"
#include "bsp_audio.h"
extern void demo_xiaozhi_probe_microphone(void);
extern bool demo_xiaozhi_probe_microphone_done(void);
static uint32_t xz_probe_ticks(void){configASSERT(bsp_lvgl_lock(1000));uint32_t t=lv_tick_get();bsp_lvgl_unlock();return t;}
#endif
extern bool demo_xiaozhi_probe_ready(void);
#ifdef BADGE_XIAOZHI_CONVERSATION_PROBE
extern bool demo_xiaozhi_conversation_probe_done(void);
#endif
#ifdef BADGE_XIAOZHI_PLAYBACK_PROBE
extern bool demo_xiaozhi_playback_probe_done(void);
#endif
static void xz_probe_key(bsp_btn_t b,bsp_btn_ev_t e){input_event_t event={b,e};configASSERT(xQueueSend(input_queue,&event,pdMS_TO_TICKS(1000))==pdTRUE);vTaskDelay(pdMS_TO_TICKS(650));}
static void xz_probe_home(void){if(badge_power_screen_off()){badge_power_activity();for(unsigned i=0;i<30&&badge_power_screen_off();i++)vTaskDelay(pdMS_TO_TICKS(100));vTaskDelay(pdMS_TO_TICKS(500));}xz_probe_key(BSP_BTN_OK,BSP_BTN_LONG);xz_probe_key(BSP_BTN_DOWN,BSP_BTN_CLICK);xz_probe_key(BSP_BTN_OK,BSP_BTN_CLICK);vTaskDelay(pdMS_TO_TICKS(2000));}
#ifdef BADGE_XIAOZHI_CONVERSATION_PROBE
#include "esp_system.h"
#include "bsp_audio.h"
extern unsigned demo_xiaozhi_probe_volume(void);
static unsigned xz_probe_saved_volume(void){
    nvs_handle_t n;uint8_t value=255;configASSERT(nvs_open("xiaozhi",NVS_READONLY,&n)==ESP_OK);
    configASSERT(nvs_get_u8(n,"volume",&value)==ESP_OK);nvs_close(n);return value;
}
static void xz_probe_reenter(void){
    // Home retains the mini-app selection; navigate from the actual selection.
    xz_probe_key(BSP_BTN_OK,BSP_BTN_CLICK);xz_probe_key(BSP_BTN_OK,BSP_BTN_CLICK);
    configASSERT(navigation.page==BADGE_GAMES);
    for(unsigned i=0;i<6&&navigation.game_selected!=4;i++)xz_probe_key(BSP_BTN_DOWN,BSP_BTN_CLICK);
    configASSERT(navigation.game_selected==4);xz_probe_key(BSP_BTN_OK,BSP_BTN_CLICK);
    for(unsigned i=0;i<120&&!demo_xiaozhi_probe_ready();i++)vTaskDelay(pdMS_TO_TICKS(100));
    configASSERT(demo_xiaozhi_probe_ready()&&navigation.page==BADGE_PLAYING&&navigation.game_selected==4);
}
static void xz_probe_volume_persistence(void){
    // Checkpoint survives an explicit MCU restart. Only test-owned keys are erased.
    nvs_handle_t n;configASSERT(nvs_open("xz_vol_probe",NVS_READWRITE,&n)==ESP_OK);
    uint8_t original=255;esp_err_t result=nvs_get_u8(n,"original",&original);
    if(result==ESP_ERR_NVS_NOT_FOUND||(result==ESP_OK&&demo_xiaozhi_probe_volume()==original)){
        original=demo_xiaozhi_probe_volume();
        configASSERT(nvs_set_u8(n,"original",original)==ESP_OK&&nvs_commit(n)==ESP_OK);nvs_close(n);
        unsigned expected=original>=90?original-10:original+10;
        xz_probe_key(original>=90?BSP_BTN_DOWN:BSP_BTN_UP,BSP_BTN_CLICK);
        configASSERT(demo_xiaozhi_probe_volume()==expected&&bsp_audio_get_volume()==expected);
        xz_probe_home();configASSERT(xz_probe_saved_volume()==expected);
        ESP_LOGI("xz_probe","VOLUME_IDLE_APPLY_AND_SAVE_PASS");
        esp_restart();
    }
    configASSERT(result==ESP_OK&&original<=100);nvs_close(n);
    unsigned expected=original>=90?original-10:original+10;
    configASSERT(demo_xiaozhi_probe_volume()==expected&&bsp_audio_get_volume()==expected);
    ESP_LOGI("xz_probe","VOLUME_REBOOT_RESTORE_PASS");
    xz_probe_key(original>=90?BSP_BTN_UP:BSP_BTN_DOWN,BSP_BTN_CLICK);
    xz_probe_home();configASSERT(xz_probe_saved_volume()==original);
    // Deliberately alter the shared codec's cache: the app must restore its NVS preference.
    bsp_audio_set_volume(original==40?60:40);xz_probe_reenter();
    configASSERT(demo_xiaozhi_probe_volume()==original&&bsp_audio_get_volume()==original);
    configASSERT(nvs_open("xz_vol_probe",NVS_READWRITE,&n)==ESP_OK);
    configASSERT(nvs_erase_key(n,"original")==ESP_OK&&nvs_commit(n)==ESP_OK);nvs_close(n);
    ESP_LOGI("xz_probe","VOLUME_REENTER_RESTORE_PASS original_restored=1");
}
#endif
static void xz_probe_task(void *arg){
    (void)arg;vTaskDelay(pdMS_TO_TICKS(12000));
    bool blank=navigation.badge_mask==0;
    if(blank){
        // Enter the app on an unconfigured bench board without writing a fake
        // user profile. Quiesce the input owner before changing test navigation.
        atomic_store(&input_ready,false);vTaskDelay(pdMS_TO_TICKS(500));
        if(bsp_lvgl_lock(1000)){navigation.badge_mask=1;render_shell();bsp_lvgl_unlock();}
        atomic_store(&input_ready,true);
    }
    xz_probe_key(BSP_BTN_OK,BSP_BTN_CLICK);xz_probe_key(BSP_BTN_OK,BSP_BTN_CLICK);
    for(unsigned i=0;i<4;i++)xz_probe_key(BSP_BTN_DOWN,BSP_BTN_CLICK);
    xz_probe_key(BSP_BTN_OK,BSP_BTN_CLICK);
    // The app's opt-in codec bench uses synthetic PCM only. Wait for it first.
    for(unsigned i=0;i<360&&!demo_xiaozhi_probe_ready();i++)vTaskDelay(pdMS_TO_TICKS(500));
    if(!demo_xiaozhi_probe_ready()){xz_probe_home();ESP_LOGW("xz_probe","CODEC_TIMEOUT");vTaskDelete(NULL);return;}
#ifdef BADGE_XIAOZHI_POWER_PROBE
    uint8_t brightness=bsp_display_brightness();
    esp_pm_config_t pm;configASSERT(esp_pm_get_configuration(&pm)==ESP_OK&&pm.min_freq_mhz==40&&pm.max_freq_mhz==160&&pm.light_sleep_enable);
    configASSERT(CONFIG_BUTTON_PERIOD_TIME_MS==50);
    vTaskDelay(pdMS_TO_TICKS(17000));configASSERT(bsp_audio_is_sleeping());
    ESP_LOGI("xz_probe","AUDIO_15S_PASS");
    for(unsigned i=0;i<100&&!badge_power_screen_off();i++)vTaskDelay(pdMS_TO_TICKS(500));
    configASSERT(badge_power_screen_off());vTaskDelay(pdMS_TO_TICKS(1000));
    configASSERT(bsp_display_brightness()==0);
    uint32_t tick=xz_probe_ticks();vTaskDelay(pdMS_TO_TICKS(1200));configASSERT(xz_probe_ticks()==tick);
    wifi_ps_type_t ps;configASSERT(esp_wifi_get_ps(&ps)==ESP_OK&&ps==WIFI_PS_MAX_MODEM);
    ESP_LOGI("xz_probe","SCREEN_60S_PASS LVGL_TICK_FROZEN MAX_MODEM PASS");
    // Exercise the real input owner, including release and delayed click.
    input_event_t wake_events[]={{BSP_BTN_OK,BSP_BTN_PRESS},{BSP_BTN_OK,BSP_BTN_RELEASE},{BSP_BTN_OK,BSP_BTN_CLICK}};
    for(unsigned i=0;i<3;i++){configASSERT(xQueueSend(input_queue,&wake_events[i],pdMS_TO_TICKS(1000))==pdTRUE);vTaskDelay(pdMS_TO_TICKS(150));}
    vTaskDelay(pdMS_TO_TICKS(500));
    configASSERT(!badge_power_screen_off()&&bsp_display_brightness()==brightness);
    demo_xiaozhi_probe_microphone();
    for(unsigned i=0;i<30&&!demo_xiaozhi_probe_microphone_done();i++)vTaskDelay(pdMS_TO_TICKS(100));
    configASSERT(demo_xiaozhi_probe_microphone_done());
    ESP_LOGI("xz_probe","WAKE_PASS brightness=%u",brightness);
#else
#ifdef BADGE_XIAOZHI_CONVERSATION_PROBE
    xz_probe_volume_persistence();
#endif
    xz_probe_key(BSP_BTN_OK,BSP_BTN_CLICK); // Connect; playback is separately opt-in below.
#ifdef BADGE_XIAOZHI_CONVERSATION_PROBE
    for(unsigned i=0;i<200&&!demo_xiaozhi_conversation_probe_done();i++)vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI("xz_probe","AUTO_CONVERSATION=%s",demo_xiaozhi_conversation_probe_done()?"PASS":"NOT_COMPLETED");
#elif defined(BADGE_XIAOZHI_PLAYBACK_PROBE)
    for(unsigned i=0;i<180&&!demo_xiaozhi_playback_probe_done();i++)vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI("xz_probe","CLOUD_PLAYBACK=%s",demo_xiaozhi_playback_probe_done()?"PASS":"NOT_COMPLETED");
#else
    vTaskDelay(pdMS_TO_TICKS(30000));
#endif
#endif
    xz_probe_home();ESP_LOGI("xz_probe","RETURN_HOME free=%lu",(unsigned long)esp_get_free_heap_size());
    if(blank){atomic_store(&input_ready,false);vTaskDelay(pdMS_TO_TICKS(500));if(bsp_lvgl_lock(1000)){load_profile_ui();render_shell();bsp_lvgl_unlock();}atomic_store(&input_ready,true);}
    ESP_LOGI("xz_probe","COMPLETE");vTaskDelete(NULL);
}
