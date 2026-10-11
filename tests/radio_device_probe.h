/* Opt-in bench probe. Never enabled in distributed firmware. */
#include "radio_app.h"
static void radio_probe_send(bsp_btn_t button,bsp_btn_ev_t event){input_event_t e={button,event};configASSERT(xQueueSend(input_queue,&e,pdMS_TO_TICKS(1000))==pdTRUE);}
static void radio_probe_key(bsp_btn_t button,bsp_btn_ev_t event){
    if(event==BSP_BTN_CLICK||event==BSP_BTN_DOUBLE||event==BSP_BTN_LONG){
        radio_probe_send(button,BSP_BTN_PRESS);
        if(event==BSP_BTN_LONG)radio_probe_send(button,event);
        radio_probe_send(button,BSP_BTN_RELEASE);
        if(event!=BSP_BTN_LONG)radio_probe_send(button,event);
    }else radio_probe_send(button,event);
    vTaskDelay(pdMS_TO_TICKS(650));
}
static void radio_probe_home(void){
    for(unsigned step=0;step<6&&navigation.page!=BADGE_HOME;step++){
        radio_probe_key(BSP_BTN_OK,BSP_BTN_LONG);
        if(return_menu.open){
            unsigned home=return_menu.plan.destination==BADGE_HOME?0:1;
            while(return_menu.selected!=home)radio_probe_key(BSP_BTN_DOWN,BSP_BTN_CLICK);
            radio_probe_key(BSP_BTN_OK,BSP_BTN_CLICK);
        }
    }
    configASSERT(navigation.page==BADGE_HOME);vTaskDelay(pdMS_TO_TICKS(9000));
}
static radio_probe_state_t radio_probe_state(void){radio_probe_state_t s;configASSERT(demo_radio_probe_state(&s));return s;}
static unsigned radio_probe_release(bsp_btn_t key,unsigned expected_station,unsigned expected_selected){
    radio_probe_send(key,BSP_BTN_PRESS);vTaskDelay(pdMS_TO_TICKS(30));
    int64_t begin=esp_timer_get_time();radio_probe_send(key,BSP_BTN_RELEASE);
    for(unsigned i=0;i<200;i++){radio_probe_state_t s=radio_probe_state();if(s.station==expected_station&&s.selected==expected_selected){unsigned ms=(esp_timer_get_time()-begin)/1000;ESP_LOGI("radio_probe","RELEASE response=%u ms",ms);return ms;}vTaskDelay(pdMS_TO_TICKS(5));}
    configASSERT(false);return 1000;
}
static void radio_probe_task(void *arg){
    (void)arg;vTaskDelay(pdMS_TO_TICKS(12000));
    if(!navigation.badge_mask){ESP_LOGW("radio_probe","SKIP: setup incomplete");vTaskDelete(NULL);return;}
    radio_probe_key(BSP_BTN_OK,BSP_BTN_CLICK);radio_probe_key(BSP_BTN_OK,BSP_BTN_CLICK);
    radio_probe_key(BSP_BTN_DOWN,BSP_BTN_CLICK);radio_probe_key(BSP_BTN_DOWN,BSP_BTN_CLICK);radio_probe_key(BSP_BTN_OK,BSP_BTN_CLICK);
    for(unsigned i=0;i<80&&!radio_probe_state().playing;i++)vTaskDelay(pdMS_TO_TICKS(500));
    radio_probe_state_t s=radio_probe_state();ESP_LOGI("radio_probe","BEGIN playing=%d stations=%u",s.playing,s.count);configASSERT(s.count>1);
    configASSERT(s.count>=57);
    const unsigned saved=s.station;
    const unsigned samples[]={7,s.count/2,s.count-1};
    for(unsigned sample=0;sample<3;sample++){
        const unsigned target=samples[sample];demo_radio_probe_select(target);
        vTaskDelay(pdMS_TO_TICKS(500));
        for(unsigned i=0;i<60&&!radio_probe_state().playing;i++)vTaskDelay(pdMS_TO_TICKS(500));
        s=radio_probe_state();
        ESP_LOGI("radio_probe","CURATED sample=%u index=%u count=%u playing=%d",sample,s.station,s.count,s.playing);
        configASSERT(s.station==target&&s.playing);vTaskDelay(pdMS_TO_TICKS(1500));
    }
    demo_radio_probe_select(saved);vTaskDelay(pdMS_TO_TICKS(500));s=radio_probe_state();
    unsigned maximum=0;
    for(unsigned i=0;i<8;i++){
        bsp_btn_t key=i%2?BSP_BTN_UP:BSP_BTN_DOWN;
        unsigned target=(s.station+s.count+(key==BSP_BTN_UP?-1:1))%s.count;
        unsigned ms=radio_probe_release(key,target,s.selected);if(ms>maximum)maximum=ms;
        radio_probe_send(key,i%2?BSP_BTN_DOUBLE:BSP_BTN_CLICK);vTaskDelay(pdMS_TO_TICKS(100));
        s=radio_probe_state();configASSERT(s.station==target);
    }
    radio_probe_send(BSP_BTN_UP,BSP_BTN_PRESS);radio_probe_key(BSP_BTN_UP,BSP_BTN_LONG);radio_probe_key(BSP_BTN_UP,BSP_BTN_RELEASE);
    radio_probe_state_t settings=radio_probe_state();configASSERT(settings.page==1&&settings.station==s.station&&settings.selected==0);
    for(unsigned i=0;i<6;i++){unsigned selected=(i+1)%3;unsigned ms=radio_probe_release(BSP_BTN_DOWN,s.station,selected);if(ms>maximum)maximum=ms;}
    radio_probe_key(BSP_BTN_OK,BSP_BTN_CLICK);configASSERT(radio_probe_state().page==2);
    radio_probe_release(BSP_BTN_UP,s.station,0);radio_probe_release(BSP_BTN_DOWN,s.station,0); /* Restore volume. */
    radio_probe_home();ESP_LOGI("radio_probe","MAX_RESPONSE=%u ms free=%lu",maximum,(unsigned long)esp_get_free_heap_size());
    configASSERT(maximum<250);
    ESP_LOGI("radio_probe","COMPLETE");vTaskDelete(NULL);
}
