/* Real service handshakes, no microphone upload. Opt-in production-disabled probe. */
extern unsigned demo_xiaozhi_reuse_probe(unsigned field);
static void reuse_open(void){
    configASSERT(control_home());
    configASSERT(bsp_lvgl_lock(1000));navigation.page=BADGE_AI_CHAT;navigation.ai_return=BADGE_HOME;
    badge_ui_destroy();demo_xiaozhi_enter();bsp_lvgl_unlock();
    configASSERT(demo_xiaozhi_start()==ESP_OK);
}
static void reuse_probe_tick(void){
    static unsigned phase,goal=1,volume;
    static bool original;
    static int64_t next=12000000,deadline;
    int64_t now=esp_timer_get_time();
    if(phase==99||now<next)return;
    next=now+500000;
    if(phase==0){
        original=xz_wake_enabled();volume=demo_xiaozhi_saved_volume();
        configASSERT(control_home());configASSERT(xz_wake_enable(true)==ESP_OK);
        reuse_open();deadline=now+45000000;phase=1;return;
    }
    if(phase==1||phase==3||phase==5||phase==7||phase==9){
        configASSERT(now<deadline);
        if(demo_xiaozhi_reuse_probe(2)<goal)return;
        configASSERT(demo_xiaozhi_reuse_probe(0)==1);
        ESP_LOGI("reuse_bench","HANDSHAKE=%u transports=%u discovery=%u heap=%lu",goal,
            demo_xiaozhi_reuse_probe(1),demo_xiaozhi_reuse_probe(0),(unsigned long)esp_get_free_heap_size());
        configASSERT(control_home());configASSERT(demo_xiaozhi_reuse_probe(3));
        if(phase==1||phase==3){
            configASSERT(demo_xiaozhi_reuse_probe(1)==1);
            if(goal<3){phase=2;next=now+6000000;}
            else {phase=4;next=now+6000000;}
        }else if(phase==5){
            configASSERT(demo_xiaozhi_reuse_probe(1)==2);
            configASSERT(demo_xiaozhi_reuse_probe(4));phase=6;next=now+6000000;
        }else if(phase==7){
            configASSERT(demo_xiaozhi_reuse_probe(1)==3);
            phase=8;next=now+123000000;
        }else{
            configASSERT(demo_xiaozhi_reuse_probe(1)==4);
            demo_xiaozhi_release_connection();configASSERT(xz_wake_enable(original)==ESP_OK);
            configASSERT(demo_xiaozhi_saved_volume()==volume);
            ESP_LOGI("reuse_bench","BENCH_DONE volume_preserved=1 heap=%lu",(unsigned long)esp_get_free_heap_size());phase=99;
        }
    }else if(phase==2){
        configASSERT(demo_xiaozhi_reuse_probe(3));
        ESP_LOGI("reuse_bench","PARKED_WAKE status=%s heap=%lu",xz_wake_status(),(unsigned long)esp_get_free_heap_size());
        ++goal;reuse_open();deadline=now+45000000;phase=3;
    }else if(phase==4){
        // Exercise the actual mini-app navigation release path, then idle wake.
        configASSERT(control_home());configASSERT(bsp_lvgl_lock(1000));
        navigation.page=BADGE_GAMES;navigation.game_selected=2;render_shell();bsp_lvgl_unlock();
        input_event_t key={BSP_BTN_OK,BSP_BTN_CLICK};process_input(&key);
        configASSERT(on_voice_page());configASSERT(!demo_xiaozhi_reuse_probe(3));
        ESP_LOGI("reuse_bench","MINI_APP_RELEASE_PASS");phase=10;next=now+5000000;
    }else if(phase==10){
        configASSERT(voice_suspended);
        ++goal;reuse_open();deadline=now+45000000;phase=5;
    }else if(phase==6||phase==8){
        configASSERT(!demo_xiaozhi_reuse_probe(3));
        ESP_LOGI("reuse_bench", "%s",phase==6?"DISCONNECT_RELEASE_PASS":"TTL_RELEASE_PASS");
        ++goal;reuse_open();deadline=now+45000000;phase=phase==6?7:9;
    }
}
