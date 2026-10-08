/* Opt-in lifecycle probe. Real microphone frames stay local. Synthetic trigger
 * exercises navigation; cloud auto-connect is disabled in this build. */
extern unsigned xz_wake_probe_frames(void);
extern void xz_wake_probe_trigger(void);
extern void xz_wake_probe_fail_once(void);
#include "voice_catalog.h"
static uint8_t wake_saved_radio[3];static bool wake_saved_radio_present[3],wake_saved_radio_name_present;
static char wake_saved_radio_name[64];static const char *wake_radio_keys[]={"volume","city","station"};
static void wake_probe_radio_settings(bool restore){
    nvs_handle_t h;configASSERT(nvs_open("badge_radio_v1",NVS_READWRITE,&h)==ESP_OK);
    for(unsigned i=0;i<3;i++){
        if(restore){if(wake_saved_radio_present[i])configASSERT(nvs_set_u8(h,wake_radio_keys[i],wake_saved_radio[i])==ESP_OK);else nvs_erase_key(h,wake_radio_keys[i]);}
        else wake_saved_radio_present[i]=nvs_get_u8(h,wake_radio_keys[i],&wake_saved_radio[i])==ESP_OK;
    }
    if(restore){if(wake_saved_radio_name_present)configASSERT(nvs_set_str(h,"station_name",wake_saved_radio_name)==ESP_OK);else nvs_erase_key(h,"station_name");configASSERT(nvs_commit(h)==ESP_OK);}
    else {size_t len=sizeof(wake_saved_radio_name);wake_saved_radio_name_present=nvs_get_str(h,"station_name",wake_saved_radio_name,&len)==ESP_OK;}
    nvs_close(h);
}
static void wake_probe_tick(void){
    static unsigned phase,frames,volume,app,baseline,voice_cycles;
    static bool original;
    static int64_t next=12000000;
    if(phase==99||esp_timer_get_time()<next)return;
    next=esp_timer_get_time()+3000000;
    switch(phase){
    case 0:
        configASSERT(BADGE_GAME_COUNT>=3);badge_power_set_timeout(2);wake_probe_radio_settings(false);
        original=xz_wake_enabled();volume=demo_xiaozhi_saved_volume();
        configASSERT(control_home());configASSERT(xz_wake_enable(true)==ESP_OK);
        xz_wake_init();configASSERT(xz_wake_enabled());
        xz_wake_probe_fail_once();frames=xz_wake_probe_frames();next=esp_timer_get_time()+7000000;
        ESP_LOGI("wake_bench","ENABLE_PERSIST_PASS");phase=1;break;
    case 1:
        configASSERT(xz_wake_listening()&&xz_wake_probe_frames()>frames+10);frames=xz_wake_probe_frames();
        ESP_LOGI("wake_bench","START_FAILURE_AUTO_RECOVERY_PASS");
        ESP_LOGI("wake_bench","MIC_FRAMES_PASS frames=%u free=%lu",frames,(unsigned long)esp_get_free_heap_size());
        next=esp_timer_get_time()+63000000;phase=2;break;
    case 2:
        configASSERT(badge_power_screen_off());configASSERT(xz_wake_probe_frames()>frames+500);
        ESP_LOGI("wake_bench","SCREEN_OFF_RX_PASS");xz_wake_probe_trigger();phase=3;break;
    case 3:
        configASSERT(navigation.page==BADGE_AI_CHAT&&demo_xiaozhi_active());
        configASSERT(!badge_power_screen_off()&&bsp_display_brightness()>0);
        configASSERT(demo_xiaozhi_saved_volume()==volume);
        ESP_LOGI("wake_bench","WAKE_CHAT_BRIGHTNESS_VOLUME_PASS");
        configASSERT(control_home());phase=4;break;
    case 4:
        configASSERT(!demo_xiaozhi_active());frames=xz_wake_probe_frames();
        configASSERT(bsp_lvgl_lock(1000));navigation.page=BADGE_GAMES;navigation.game_selected=app;render_shell();bsp_lvgl_unlock();
        {input_event_t key={BSP_BTN_OK,BSP_BTN_CLICK};process_input(&key);}
        configASSERT(navigation.page==BADGE_PLAYING);frames=xz_wake_probe_frames();phase=5;break;
    case 5:
        if(app==2){
            configASSERT(on_voice_page()&&voice_suspended&&xz_wake_probe_frames()>frames+5);
            ESP_LOGI("wake_bench","VOICE_LIST_IDLE_WAKE_PASS");
            unsigned clip=0;while(clip<VOICE_CLIP_COUNT&&voice_clips[clip].samples<16000*7)clip++;
            configASSERT(clip<VOICE_CLIP_COUNT&&resume_voice_audio());frames=xz_wake_probe_frames();
            configASSERT(demo_voice_play(clip)==ESP_OK&&demo_voice_audio_busy());phase=20;break;
        }
        configASSERT(xz_wake_probe_frames()<=frames+2);
        ESP_LOGI("wake_bench","APP_AUDIO_EXCLUSION_PASS app=%u",app);
        configASSERT(control_home());phase=6;break;
    case 6:
        configASSERT(xz_wake_probe_frames()>frames+5);
        ESP_LOGI("wake_bench","APP_RETURN_LISTEN_PASS app=%u",app);
        if(++app<BADGE_GAME_COUNT)phase=4;else{yao_location_voice_active(true);phase=30;}break;
    case 30:
        /* Compare like-for-like heaps: geolocation TLS is unrelated to the
         * detector and can transiently occupy 20+ KiB between samples. */
        if(yao_location_worker_running())break;
        vTaskDelay(pdMS_TO_TICKS(20));
        configASSERT(xz_wake_enable(false)==ESP_OK);baseline=esp_get_free_heap_size();app=0;phase=7;break;
    case 7:
        configASSERT(xz_wake_enable(true)==ESP_OK);frames=xz_wake_probe_frames();phase=8;break;
    case 8:
        configASSERT(xz_wake_probe_frames()>frames+5);configASSERT(xz_wake_enable(false)==ESP_OK);
        ESP_LOGI("wake_bench","HEAP_CHECK cycle=%u baseline=%u free=%lu",app,baseline,(unsigned long)esp_get_free_heap_size());
        configASSERT(esp_get_free_heap_size()+4096>=baseline);
        if(++app<5)phase=7;else{
            configASSERT(demo_xiaozhi_saved_volume()==volume);
            configASSERT(xz_wake_enable(original)==ESP_OK);
            ESP_LOGI("wake_bench","REPEAT_STOP_HEAP_PASS cycles=5 baseline=%u free=%lu",baseline,(unsigned long)esp_get_free_heap_size());
            wake_probe_radio_settings(true);yao_location_voice_active(false);badge_power_set_timeout(0);ESP_LOGI("wake_bench","BENCH_DONE");phase=99;
        }break;
    case 20:{
        configASSERT(on_voice_page()&&demo_voice_audio_busy()&&!voice_suspended);
        configASSERT(xz_wake_probe_frames()<=frames+2);
        input_event_t stop={BSP_BTN_DOWN,BSP_BTN_LONG};process_input(&stop);
        ESP_LOGI("wake_bench","VOICE_PLAY_EXCLUSION_PASS");phase=21;break;
    }
    case 21:
        configASSERT(on_voice_page()&&voice_suspended&&xz_wake_probe_frames()>frames+5);
        ESP_LOGI("wake_bench","VOICE_STOP_RESUME_WAKE_PASS");xz_wake_probe_trigger();phase=22;break;
    case 22:{
        configASSERT(navigation.page==BADGE_AI_CHAT&&demo_xiaozhi_active()&&!voice_suspended);
        unsigned clip=0;for(unsigned i=1;i<VOICE_CLIP_COUNT;i++)if(voice_clips[i].samples<voice_clips[clip].samples)clip=i;
        configASSERT(execute_control((badge_control_command_t){.kind=BC_PLAY_VOICE,.value=clip}));
        frames=xz_wake_probe_frames();next=esp_timer_get_time()+4000000;phase=23;break;
    }
    case 23:
        configASSERT(on_voice_page()&&voice_suspended&&xz_wake_probe_frames()>frames+5);
        ESP_LOGI("wake_bench","VOICE_FINISH_WAKE_CYCLE_PASS cycle=%u free=%lu",++voice_cycles,(unsigned long)esp_get_free_heap_size());
        if(voice_cycles<3){xz_wake_probe_trigger();phase=22;}else{configASSERT(control_home());phase=6;}
        break;
    }
}
