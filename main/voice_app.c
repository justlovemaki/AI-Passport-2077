#include "voice_app.h"
#include "voice_ui.h"
#include "voice_stream.h"
#include "voice_activity.h"
#include "bsp_audio.h"
#include "badge_power.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "nvs.h"
#include "decoder/impl/esp_opus_dec.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static voice_navigation_t navigation; /* LVGL lock owns navigation and timer. */
static lv_timer_t *timer;
static QueueHandle_t queue;
static SemaphoreHandle_t done;
static bool started;
static atomic_bool stopping,volume_loaded;
static atomic_uint generation;
static atomic_int volume,playing,battery,error_code;
static voice_activity_t activity;
typedef struct {unsigned clip,generation;} request_t;
static const esp_partition_t *parts[2];
extern const uint8_t voice_bgm_start[] asm("_binary_voice_bgm_bin_start");
extern const uint8_t voice_bgm_end[] asm("_binary_voice_bgm_bin_end");
static const int16_t silence[1600];
static const char *errors[]={NULL,"正在启动","音效资源异常","音频启动失败","音效播放失败","音量保存失败"};
static void refresh(lv_timer_t *t) {
    (void)t;navigation.volume=atomic_load(&volume);
    voice_ui_render(&navigation,atomic_load(&playing),atomic_load(&battery),errors[atomic_load(&error_code)]);
}
static bool read_data(void *ctx,uint32_t offset,void *out,size_t size) {
    (void)ctx;uint8_t *p=out;
    while(size){unsigned partition;uint32_t local;size_t count;
        if(!voice_stream_span(offset,size,&partition,&local,&count)||!parts[partition]||esp_partition_read(parts[partition],local,p,count)!=ESP_OK)return false;
        offset+=count;p+=count;size-=count;}
    return true;
}
static bool read_clip_data(void *ctx,uint32_t offset,void *out,size_t size) {
    const voice_clip_t *clip=ctx;
    if(!clip)return false;
    if(clip->storage==VOICE_STORAGE_PARTITION)return read_data(NULL,offset,out,size);
    if(clip->storage!=VOICE_STORAGE_APP||offset>VOICE_EMBEDDED_BYTES||size>VOICE_EMBEDDED_BYTES-offset)return false;
    memcpy(out,voice_bgm_start+offset,size);return true;
}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static bool resources(void) {
    parts[0]=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x42,"voice_data");
    parts[1]=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x43,"voice_tail");
    uint8_t h[28];
    size_t bgm_bytes=(size_t)(voice_bgm_end-voice_bgm_start);
    return parts[0]&&parts[1]&&parts[0]->size==VOICE_FIRST_BYTES&&parts[1]->size==VOICE_TAIL_BYTES&&read_data(NULL,0,h,sizeof(h))&&
        !memcmp(h,"VKP1",4)&&u32(h+4)==1&&u32(h+8)==VOICE_PARTITION_CLIP_COUNT&&u32(h+12)==VOICE_PAYLOAD_BYTES&&u32(h+16)==VOICE_PARTITION_CATALOG_CRC&&
        esp_rom_crc32_le(0,h,24)==u32(h+24)&&bgm_bytes==VOICE_EMBEDDED_BYTES&&
        esp_rom_crc32_le(0,voice_bgm_start,bgm_bytes)==VOICE_EMBEDDED_CRC;
}

#ifdef BADGE_XIAOZHI_DEVICE_PROBE
/* Exercise every retained clip with the same codec and frame limits as playback. */
bool voice_probe_decode_all(bool (*cancelled)(void)){
    if(!resources())return false;
    uint8_t *packet=malloc(1500),*pcm=malloc(1920);void *decoder=NULL;bool ok=packet&&pcm;
    esp_opus_dec_cfg_t cfg=ESP_OPUS_DEC_CONFIG_DEFAULT();cfg.sample_rate=16000;cfg.channel=1;cfg.frame_duration=ESP_OPUS_DEC_FRAME_DURATION_60_MS;
    if(ok)ok=esp_opus_dec_open(&cfg,sizeof(cfg),&decoder)==ESP_AUDIO_ERR_OK&&decoder;
    unsigned frames=0;
    for(unsigned i=0;ok&&i<VOICE_CLIP_COUNT;i++){
        uint32_t cursor=0;esp_opus_dec_reset(decoder);const voice_clip_t *clip=&voice_clips[i];
        for(;;){if(cancelled()){ok=false;break;}int n=voice_stream_next(read_clip_data,(void *)clip,clip->offset,clip->length,&cursor,packet);if(!n)break;if(n<0){ok=false;break;}
            esp_audio_dec_in_raw_t raw={.buffer=packet,.len=n};esp_audio_dec_out_frame_t output={.buffer=pcm,.len=1920};esp_audio_dec_info_t info={0};
            if(esp_opus_dec_decode(decoder,&raw,&output,&info)!=ESP_AUDIO_ERR_OK||raw.consumed!=(unsigned)n||!output.decoded_size||output.decoded_size>1920){ok=false;ESP_LOGE("xz_probe","Voice codec failed clip=%u frame=%u",i,frames);break;}frames++;if(frames%50==0)vTaskDelay(pdMS_TO_TICKS(1));
        }
        if(i%20==0)vTaskDelay(pdMS_TO_TICKS(1));
    }
    if(decoder){esp_opus_dec_close(decoder);}free(packet);free(pcm);
    ESP_LOGI("xz_probe","VOICE_ALL clips=%u frames=%u success=%d",VOICE_CLIP_COUNT,frames,ok);return ok;
}
#endif
static void cancel(void){atomic_fetch_add(&generation,1);}
static esp_err_t enqueue(unsigned clip){
    unsigned ticket=atomic_fetch_add(&generation,1)+1;
    if(!ticket)ticket=atomic_fetch_add(&generation,1)+1;
    request_t r={clip,ticket};voice_activity_submit(&activity,ticket);
    if(xQueueOverwrite(queue,&r)==pdTRUE)return ESP_OK;
    voice_activity_complete(&activity,ticket);return ESP_FAIL;
}
bool demo_voice_audio_busy(void){return started&&voice_activity_busy(&activity);}
static void worker(void *arg) {
    (void)arg;void *decoder=NULL;nvs_handle_t storage=0;bool storage_open=false,storage_valid=false;
    badge_power_enter();
    uint8_t *packet=malloc(1500);
    int16_t *pcm=malloc(960*sizeof(*pcm));
    uint8_t saved=40;
    if(nvs_open("voice_key_v1",NVS_READWRITE,&storage)==ESP_OK){storage_open=true;esp_err_t e=nvs_get_u8(storage,"volume",&saved);storage_valid=(e==ESP_OK||e==ESP_ERR_NVS_NOT_FOUND)&&saved<=100;}
    if(!storage_valid)saved=40;
    if(!atomic_exchange(&volume_loaded,true))atomic_store(&volume,saved);
    bool ready=resources();
    if(!ready)atomic_store(&error_code,2);
    else if(!packet||!pcm||bsp_audio_init()!=ESP_OK||bsp_audio_set_format(16000,16,1)!=ESP_OK) {ready=false;atomic_store(&error_code,3);}
    else {esp_opus_dec_cfg_t cfg=ESP_OPUS_DEC_CONFIG_DEFAULT();cfg.sample_rate=16000;cfg.channel=1;cfg.frame_duration=ESP_OPUS_DEC_FRAME_DURATION_60_MS;ready=esp_opus_dec_open(&cfg,sizeof(cfg),&decoder)==ESP_AUDIO_ERR_OK&&decoder;atomic_store(&error_code,ready?0:3);}
    bool battery_ready=bsp_battery_init()==ESP_OK;
    atomic_store(&battery,battery_ready?bsp_battery_soc():-1);
    ESP_LOGI("voice","Ready: resources/audio=%d clips=%u heap=%lu",ready,VOICE_CLIP_COUNT,(unsigned long)esp_get_free_heap_size());
    TickType_t last_save=xTaskGetTickCount(),last_battery=last_save;
    atomic_store(&activity.ready,true);
    while(!atomic_load(&stopping)) {
        badge_power_audio_tick(false);
        request_t r;
        if(xQueueReceive(queue,&r,pdMS_TO_TICKS(badge_power_screen_off()?1000:50))==pdTRUE) {
          if(ready&&!atomic_load(&stopping)&&r.generation==atomic_load(&generation)) {
            badge_power_audio_tick(true);
            const voice_clip_t *clip=&voice_clips[r.clip];uint32_t cursor=0;bool failed=false;int applied_volume=-1;
#ifdef BADGE_TOOL_CLOUD_PROBE
            unsigned probe_frames=0;
#endif
            esp_opus_dec_reset(decoder);atomic_store(&playing,(int)r.clip);atomic_store(&error_code,0);
            while(!atomic_load(&stopping)&&r.generation==atomic_load(&generation)) {
                int bytes=voice_stream_next(read_clip_data,(void *)clip,clip->offset,clip->length,&cursor,packet);
                if(bytes==0)break;
                if(bytes<0){failed=true;break;}
                esp_audio_dec_in_raw_t raw={.buffer=packet,.len=(uint32_t)bytes};
                esp_audio_dec_out_frame_t output={.buffer=(uint8_t *)pcm,.len=1920};esp_audio_dec_info_t info={0};
                esp_audio_err_t decoded=esp_opus_dec_decode(decoder,&raw,&output,&info);
                int requested_volume=atomic_load(&volume);
                if(requested_volume!=applied_volume){bsp_audio_set_volume(requested_volume);applied_volume=requested_volume;}
                if(decoded!=ESP_AUDIO_ERR_OK||raw.consumed!=(uint32_t)bytes||!output.decoded_size||output.decoded_size>1920||bsp_audio_write(pcm,output.decoded_size)!=ESP_OK){failed=true;break;}
#ifdef BADGE_TOOL_CLOUD_PROBE
                if(!probe_frames++)ESP_LOGI("tool_cloud","VOICE_PCM clip=%u",r.clip);
#endif
            }
            if(bsp_audio_write(silence,sizeof(silence))!=ESP_OK)failed=true;
            atomic_store(&playing,-1);if(failed)atomic_store(&error_code,4);
          }
          voice_activity_complete(&activity,r.generation);
        }
        TickType_t now=xTaskGetTickCount();int current=atomic_load(&volume);
        if(current!=saved&&now-last_save>=pdMS_TO_TICKS(3000)) {
            if(storage_valid&&nvs_set_u8(storage,"volume",current)==ESP_OK&&nvs_commit(storage)==ESP_OK)saved=current;
            else atomic_store(&error_code,5);
            last_save=now;
        }
        if(!badge_power_screen_off()&&now-last_battery>=pdMS_TO_TICKS(30000)){
            if(!battery_ready)battery_ready=bsp_battery_init()==ESP_OK;
            int next_battery=battery_ready?bsp_battery_soc():-1;
            if(next_battery>=0)atomic_store(&battery,next_battery);
            last_battery=now;
        }
    }
    int current=atomic_load(&volume);
    if(storage_valid&&current!=saved&&(nvs_set_u8(storage,"volume",current)!=ESP_OK||nvs_commit(storage)!=ESP_OK))ESP_LOGW("voice","Volume save failed; NVS preserved");
    if(storage_open)nvs_close(storage);
    if(decoder)esp_opus_dec_close(decoder);
    free(packet);free(pcm);
    atomic_store(&playing,-1);
    badge_power_leave();
    ESP_LOGI("voice","Stopped; stack remaining=%u",(unsigned)uxTaskGetStackHighWaterMark(NULL));
    xSemaphoreGive(done);vTaskDelete(NULL);
}
void demo_voice_enter(void) {
    atomic_store(&volume_loaded,false);voice_navigation_init(&navigation);atomic_store(&volume,40);atomic_store(&playing,-1);atomic_store(&battery,-1);atomic_store(&error_code,1);
    voice_ui_create();refresh(NULL);timer=lv_timer_create(refresh,100,NULL);
}
void demo_voice_exit(void){if(timer){lv_timer_delete(timer);timer=NULL;}voice_ui_destroy();}
esp_err_t demo_voice_start(void) {
    if(started)return ESP_ERR_INVALID_STATE;
    voice_activity_init(&activity);
    atomic_store(&stopping,false);cancel();queue=xQueueCreate(1,sizeof(request_t));done=xSemaphoreCreateBinary();
    if(!queue||!done||xTaskCreate(worker,"voice_player",16384,NULL,4,NULL)!=pdPASS){atomic_store(&error_code,3);demo_voice_stop();return ESP_ERR_NO_MEM;}
    started=true;return ESP_OK;
}
esp_err_t demo_voice_stop(void) {
    atomic_store(&stopping,true);cancel();
    if(started){if(xSemaphoreTake(done,pdMS_TO_TICKS(3000))!=pdTRUE)return ESP_ERR_TIMEOUT;started=false;}
    if(queue){vQueueDelete(queue);queue=NULL;}if(done){vSemaphoreDelete(done);done=NULL;}return ESP_OK;
}
bool demo_voice_back(void) {
    if(!bsp_lvgl_lock(100))return true;
    bool consumed=voice_navigation_back(&navigation);cancel();refresh(NULL);bsp_lvgl_unlock();return consumed;
}
esp_err_t demo_voice_play(unsigned clip) {
    if(!started||!queue||atomic_load(&stopping)||clip>=VOICE_CLIP_COUNT)return ESP_ERR_INVALID_STATE;
    if(!bsp_lvgl_lock(1000))return ESP_ERR_TIMEOUT;
    bool valid=voice_navigation_select(&navigation,clip);refresh(NULL);bsp_lvgl_unlock();
    if(!valid)return ESP_ERR_INVALID_ARG;
    return enqueue(clip);
}
#ifdef BADGE_CONTROL_DEVICE_PROBE
bool demo_voice_control_probe(unsigned clip){
    if(!bsp_lvgl_lock(1000))return false;
    bool ok=navigation.view==VOICE_CLIPS&&voice_packs[navigation.pack].first+navigation.clip==clip&&atomic_load(&error_code)==0&&atomic_load(&playing)==-1;
    bsp_lvgl_unlock();return ok;
}
#endif
void demo_voice_key(bsp_btn_t button,bsp_btn_ev_t event) {
    if(!queue||atomic_load(&stopping)||event==BSP_BTN_PRESS||event==BSP_BTN_RELEASE)return;
    if(!bsp_lvgl_lock(100))return;
    if(event==BSP_BTN_LONG){if(button==BSP_BTN_UP)voice_navigation_volume(&navigation);if(button==BSP_BTN_DOWN)cancel();}
    else if(event==BSP_BTN_CLICK||event==BSP_BTN_DOUBLE) {
        if(button==BSP_BTN_UP||button==BSP_BTN_DOWN){int direction=button==BSP_BTN_UP?-1:1;voice_navigation_move(&navigation,navigation.view==VOICE_VOLUME?-direction:direction);atomic_store(&volume,navigation.volume);}
        else if(button==BSP_BTN_OK){int clip=voice_navigation_ok(&navigation);if(clip>=0)enqueue((unsigned)clip);}
    }
    refresh(NULL);bsp_lvgl_unlock();
}
