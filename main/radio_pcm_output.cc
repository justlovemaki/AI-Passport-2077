#include "radio_pcm_output.h"
#include "radio_pcm_ramp.h"
#include "radio_stream_buffer.h"
#include "radio_diagnostics.h"
#include "radio_buffer_budget.h"
#ifdef RADIO_PCM_HOST_TEST
#include "radio_pcm_test_stubs.h"
#else
extern "C" {
#include "bsp_audio.h"
}
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include <atomic>
#include <new>

struct RadioPcmOutput {
    explicit RadioPcmOutput(const RadioPcmOutputConfig &c):config(c){}
    RadioPcmOutputConfig config;
    RadioStreamBuffer<radio_buffers::kPcmBytes> pcm;
    std::atomic<bool> ready{false},done{false},stop{false},input_done{false},failed{false},played{false};
};
namespace {
constexpr std::size_t kSamples=320;
bool current(const RadioPcmOutput *out){return !out->stop.load()&&out->config.current(out->config.context);}
void failure(RadioPcmOutput *out,radio_diag_error_t error){
    radio_diagnostics_set(RADIO_DIAG_ERROR,error);out->failed.store(true);
}
bool write_samples(RadioPcmOutput *out,int16_t *pcm,std::size_t samples,bool media,int64_t &last_media_done){
    if(!samples)return true;
    const int64_t start=esp_timer_get_time();
    if(media&&last_media_done)radio_diagnostics_max(RADIO_DIAG_MAX_FEED_GAP_US,static_cast<uint32_t>(std::min<int64_t>(start-last_media_done,UINT32_MAX)));
    if(bsp_audio_write(pcm,samples*sizeof(int16_t))!=ESP_OK){failure(out,RADIO_DIAG_OUTPUT_ERROR);return false;}
    if(media){
        last_media_done=esp_timer_get_time();out->played.store(true);
        radio_diagnostics_add(RADIO_DIAG_PCM_FRAMES,1);
        radio_diagnostics_add(RADIO_DIAG_PCM_BYTES,samples*sizeof(int16_t));
    }
    return true;
}
void notify(RadioPcmOutput *out,bool buffering){
    if(out->config.buffering&&current(out))out->config.buffering(buffering,out->config.context);
}
void sink_task(void *arg){
    auto *out=static_cast<RadioPcmOutput *>(arg);
    if(bsp_audio_set_format(out->config.sample_rate,16,1)!=ESP_OK){
        failure(out,RADIO_DIAG_FORMAT_ERROR);
    }else{
        int volume=out->config.volume(out->config.context);bsp_audio_set_volume(volume);
        out->ready.store(true,std::memory_order_release);
        int16_t block[kSamples];
        RadioPcmRamp ramp(out->config.sample_rate);
        RadioPrebuffer prebuffer(4096,esp_timer_get_time(),100000);
        int64_t last_media_done=0,gap_started=0;
        bool silenced=true,buffering=true;
        while(!out->failed.load()&&current(out)){
            const std::size_t available=out->pcm.available();
            radio_diagnostics_set(RADIO_DIAG_PCM_QUEUED_BYTES,available);
            if(prebuffer.waiting(available,out->input_done.load(std::memory_order_acquire),esp_timer_get_time())){vTaskDelay(1);continue;}
            const std::size_t bytes=out->pcm.pop(reinterpret_cast<uint8_t *>(block),sizeof(block));
            if(!bytes){
                if(out->input_done.load(std::memory_order_acquire)&&!out->pcm.available())break;
                const int64_t now=esp_timer_get_time();
                if(!gap_started)gap_started=now;
                if(now-gap_started<1000){vTaskDelay(1);continue;}
                // Fade while the previously queued DMA audio still has headroom.
                // Auto-cleared DMA can then remain at zero without a hard edge.
                if(!silenced&&last_media_done){
                    const std::size_t count=ramp.fade_out(block,kSamples);
                    if(!write_samples(out,block,count,false,last_media_done))break;
                    silenced=true;radio_diagnostics_add(RADIO_DIAG_PCM_UNDERRUNS,1);
                }
                if(!buffering&&RadioPrebuffer::starved(now,last_media_done)){
                    buffering=true;radio_diagnostics_add(RADIO_DIAG_REBUFFERS,1);notify(out,true);
                }
                vTaskDelay(1);continue;
            }
            gap_started=0;
            const int wanted=out->config.volume(out->config.context);
            if(wanted!=volume){bsp_audio_set_volume(wanted);volume=wanted;}
            ramp.process(block,bytes/sizeof(int16_t));
            if(!write_samples(out,block,bytes/sizeof(int16_t),true,last_media_done))break;
            silenced=false;
            if(buffering){buffering=false;notify(out,false);}
        }
        if(out->played.load()&&!out->failed.load()){
            if(!silenced){const std::size_t count=ramp.fade_out(block,kSamples);write_samples(out,block,count,false,last_media_done);}
            // One MP3 frame of zeros lets the stop ramp leave the DMA ring before
            // an application exit closes the codec. No clock/register changes.
            std::memset(block,0,sizeof(block));
            for(std::size_t left=1152;left&&!out->failed.load();){
                const std::size_t count=std::min(left,kSamples);
                if(!write_samples(out,block,count,false,last_media_done))break;
                left-=count;
            }
        }
    }
    radio_diagnostics_set(RADIO_DIAG_PCM_QUEUED_BYTES,0);
    radio_diagnostics_min(RADIO_DIAG_MIN_FREE_HEAP,esp_get_free_heap_size());
    radio_diagnostics_set(RADIO_DIAG_WRITER_STACK_FREE,uxTaskGetStackHighWaterMark(nullptr));
    // No context access after completion: close may immediately release it.
    out->done.store(true,std::memory_order_release);vTaskDelete(nullptr);
}
}
RadioPcmOutput *radio_pcm_output_open(const RadioPcmOutputConfig &config){
    if(!config.current||!config.volume||config.sample_rate<8000||config.sample_rate>48000){radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_FORMAT_ERROR);return nullptr;}
    if(!config.current(config.context))return nullptr;
    auto *out=new (std::nothrow) RadioPcmOutput(config);
    if(!out){radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_MEMORY_ERROR);return nullptr;}
    // Only the bounded PCM feeder outranks decoding and LVGL; I2S waits yield.
    if(xTaskCreate(sink_task,"radio_pcm",4096,out,6,nullptr)!=pdPASS){
        radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_MEMORY_ERROR);delete out;return nullptr;
    }
    while(!out->ready.load(std::memory_order_acquire)&&!out->done.load(std::memory_order_acquire)&&config.current(config.context))vTaskDelay(1);
    radio_diagnostics_min(RADIO_DIAG_MIN_FREE_HEAP,esp_get_free_heap_size());
    if(!out->ready.load()||out->done.load()||out->failed.load()||!config.current(config.context)){radio_pcm_output_close(out,false);return nullptr;}
    return out;
}
bool radio_pcm_output_write(RadioPcmOutput *out,const void *pcm,std::size_t bytes){
    if(!out||!pcm||!bytes||bytes%sizeof(int16_t))return false;
    const auto *data=static_cast<const uint8_t *>(pcm);std::size_t sent=0;
    while(sent<bytes&&!out->done.load(std::memory_order_acquire)&&!out->failed.load()&&current(out)){
        const std::size_t count=out->pcm.push(data+sent,bytes-sent);sent+=count;
        if(!count)vTaskDelay(1);
    }
    return sent==bytes;
}
bool radio_pcm_output_played(const RadioPcmOutput *out){return out&&out->played.load();}
bool radio_pcm_output_failed(const RadioPcmOutput *out){return out&&out->failed.load();}
bool radio_pcm_output_close(RadioPcmOutput *out,bool drain){
    if(!out)return false;
    if(drain)out->input_done.store(true,std::memory_order_release);else out->stop.store(true);
    while(!out->done.load(std::memory_order_acquire))vTaskDelay(1);
    const bool played=out->played.load();delete out;return played;
}
