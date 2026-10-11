/* Opt-in navigation/voice handoff test; no profile or network credential writes. */
extern unsigned demo_xiaozhi_yao_probe(unsigned field);
static void yao_probe_key(bsp_btn_t button,bsp_btn_ev_t event){
    input_event_t e={button,BSP_BTN_PRESS};process_input(&e);
    if(event==BSP_BTN_LONG){e.event=event;process_input(&e);}
    e.event=BSP_BTN_RELEASE;process_input(&e);
    if(event!=BSP_BTN_LONG){e.event=event;process_input(&e);}
}
static void yao_device_probe_tick(void){
    static unsigned step,round=1,waits,baseline;static int64_t next=20000000;static yao_record_t saved;
    if(step>12||esp_timer_get_time()<next)return;
    badge_power_activity();
    next=esp_timer_get_time()+700000;
    if(step==0){
        badge_power_set_timeout(0); /* Test only; do not persist the timeout. */
        configASSERT(control_home());
        yao_probe_key(BSP_BTN_UP,BSP_BTN_CLICK);
        configASSERT(navigation.page==BADGE_GAMES);
        while(strcmp(BADGE_GAMES_REGISTRY[navigation.game_selected].id,"cyber-yao"))yao_probe_key(BSP_BTN_DOWN,BSP_BTN_CLICK);
        yao_probe_key(BSP_BTN_OK,BSP_BTN_CLICK);configASSERT(navigation.page==BADGE_PLAYING);
        yao_probe_key(BSP_BTN_DOWN,BSP_BTN_LONG);
        ESP_LOGI("yao_probe","APP_OPEN_PASS");
    }else if(step<=6){
        yao_probe_key(BSP_BTN_OK,BSP_BTN_CLICK);
        if(step==6){configASSERT(yao_latest(&saved));yao_result_t result;configASSERT(yao_calculate(saved.lines,&result));ESP_LOGI("yao_probe","SIX_CASTS_PASS id=%lu original=%u changed=%u",(unsigned long)saved.id,result.original->number,result.changed->number);}
    }else if(step==7){
        baseline=demo_xiaozhi_yao_probe(0);waits=0;
        yao_probe_key(BSP_BTN_OK,BSP_BTN_CLICK);configASSERT(navigation.page==BADGE_AI_CHAT);
        ESP_LOGI("yao_probe","DIRECT_HANDOFF_PASS round=%u",round);next=esp_timer_get_time()+3000000;
    }else if(step==8){
        if(demo_xiaozhi_yao_probe(0)-baseline<128000){
            if(++waits<100)return;
            ESP_LOGE("yao_probe","CLOUD_TIMEOUT round=%u audio=%u tts_stop=%u",round,demo_xiaozhi_yao_probe(0)-baseline,demo_xiaozhi_yao_probe(1));
        }else ESP_LOGI("yao_probe","CLOUD_AUDIO_PASS round=%u samples=%u",round,demo_xiaozhi_yao_probe(0)-baseline);
        next=esp_timer_get_time()+1500000;
    }else if(step==9){
        yao_probe_key(BSP_BTN_OK,BSP_BTN_LONG);
        configASSERT(return_menu.open&&return_menu.plan.destination==BADGE_PLAYING);
        while(return_menu.selected!=0)yao_probe_key(BSP_BTN_UP,BSP_BTN_CLICK);
        yao_probe_key(BSP_BTN_OK,BSP_BTN_CLICK);
        configASSERT(navigation.page==BADGE_PLAYING);
        yao_record_t after;configASSERT(yao_latest(&after)&&after.id==saved.id&&!memcmp(after.lines,saved.lines,6));
        ESP_LOGI("yao_probe","RETURN_SAME_RESULT_PASS");
        if(round<6){if(round%2==0)demo_xiaozhi_release_connection();round++;step=7;next=esp_timer_get_time()+3000000;return;}
    }else if(step==10){
        yao_probe_key(BSP_BTN_DOWN,BSP_BTN_CLICK);yao_probe_key(BSP_BTN_UP,BSP_BTN_CLICK);
        configASSERT(control_home());ESP_LOGI("yao_probe","HOME_PASS");
    }else if(step==11){ESP_LOGI("yao_probe","DONE audio=%u",demo_xiaozhi_yao_probe(0));step=13;return;}
    step++;
}
