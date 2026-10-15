#include "radio_pcm_output.h"
#include "radio_pcm_ramp.h"
#include "radio_diagnostics.h"
#include "radio_pcm_test_stubs.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

static std::thread worker;
static std::mutex mutex;
static std::thread::id bsp_owner;
static std::vector<int16_t> transmitted;
static std::vector<bool> notifications;
static std::atomic<bool> running{true};
static std::atomic<uint8_t> volume{40};
static std::atomic<unsigned> writes{0};
static unsigned fail_write_at,applied_volume,volume_changes;
static uint32_t rate;
static bool fail_format,fail_task;
static void check_owner(){
    if(bsp_owner==std::thread::id{})bsp_owner=std::this_thread::get_id();
    assert(bsp_owner==std::this_thread::get_id());
}
int bsp_audio_set_format(uint32_t hz,uint8_t bits,uint8_t channels){
    std::lock_guard<std::mutex> lock(mutex);check_owner();assert(bits==16&&channels==1);rate=hz;return fail_format?ESP_FAIL:ESP_OK;
}
void bsp_audio_set_volume(uint8_t value){
    std::lock_guard<std::mutex> lock(mutex);check_owner();applied_volume=value;++volume_changes;
}
int bsp_audio_write(const void *data,std::size_t bytes){
    assert(bytes&&bytes%2==0);const unsigned count=++writes;
    {
        std::lock_guard<std::mutex> lock(mutex);check_owner();
        if(fail_write_at&&count>=fail_write_at)return ESP_FAIL;
        const auto *pcm=static_cast<const int16_t *>(data);transmitted.insert(transmitted.end(),pcm,pcm+bytes/2);
    }
    // Model a blocking sink, not network/decoder work inside the feeder.
    std::this_thread::sleep_for(std::chrono::microseconds(bytes/2*1000000/rate));
    return ESP_OK;
}
int64_t esp_timer_get_time(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
uint32_t esp_get_free_heap_size(){return 64000;}
unsigned uxTaskGetStackHighWaterMark(void *){return 2048;}
int xTaskCreate(void (*fn)(void *),const char *,unsigned stack,void *arg,unsigned priority,void *){
    assert(stack==4096&&priority==6&&!worker.joinable());
    if(fail_task)return 0;
    worker=std::thread([=]{fn(arg);});return pdPASS;
}
void vTaskDelay(unsigned ticks){std::this_thread::sleep_for(std::chrono::milliseconds(ticks));}
void vTaskDelete(void *){}
static bool current(void *){return running.load();}
static uint8_t wanted_volume(void *){return volume.load();}
static void status(bool buffering,void *){
    if(!running.load())return;
    std::lock_guard<std::mutex> lock(mutex);notifications.push_back(buffering);
}
static RadioPcmOutputConfig config(uint32_t hz=48000){return {hz,current,wanted_volume,status,nullptr};}
static void join(){if(worker.joinable())worker.join();}
static void reset(){
    join();transmitted.clear();notifications.clear();bsp_owner={};
    running=true;volume=40;writes=0;fail_write_at=applied_volume=volume_changes=0;rate=0;fail_format=fail_task=false;
    radio_diagnostics_begin();
}
template<class Predicate> static void wait_for(Predicate predicate){
    const int64_t deadline=esp_timer_get_time()+5000000;
    while(!predicate()){assert(esp_timer_get_time()<deadline);vTaskDelay(1);}
}
static bool notified(bool value){std::lock_guard<std::mutex> lock(mutex);return std::find(notifications.begin(),notifications.end(),value)!=notifications.end();}
int main(){
    // Envelope bounds, exact untouched steady PCM, chunk equivalence and signed extremes.
    for(uint32_t hz:{8000u,16000u,32000u,44100u,48000u}){
        RadioPcmRamp a(hz),b(hz);std::vector<int16_t> one(500),split(500);
        for(std::size_t i=0;i<one.size();i++)one[i]=i%2?INT16_MIN:INT16_MAX;
        split=one;const auto original=one;
        a.process(one.data(),one.size());b.process(split.data(),73);b.process(split.data()+73,split.size()-73);
        assert(one==split&&one.front()==0);
        assert(std::equal(one.begin()+a.length(),one.end(),original.begin()+a.length()));
        int16_t tail[320];const auto n=a.fade_out(tail,320);assert(n==hz/200&&tail[n-1]==0);
        for(std::size_t i=1;i<n;i++)assert(std::abs(static_cast<int>(tail[i])-tail[i-1])<=32768/static_cast<int>(n)+1);
        std::vector<int16_t> resume(300,INT16_MAX);a.process(resume.data(),resume.size());assert(resume.front()==0&&resume.back()==INT16_MAX);
    }
    assert(RadioPcmRamp(0).length()==1&&RadioPcmRamp(UINT32_MAX).length()==240);

    reset();auto *out=radio_pcm_output_open(config());assert(out);
    std::vector<int16_t> pcm(5000,10000);
    assert(!radio_pcm_output_write(out,pcm.data(),3));
    assert(radio_pcm_output_write(out,pcm.data(),pcm.size()*2));
    assert(radio_pcm_output_close(out,true));join();
    radio_diag_snapshot_t snapshot;radio_diagnostics_get(&snapshot);
    assert(snapshot.values[RADIO_DIAG_PCM_BYTES]==pcm.size()*2);
    assert(snapshot.values[RADIO_DIAG_MIN_FREE_HEAP]==64000&&snapshot.values[RADIO_DIAG_WRITER_STACK_FREE]==2048);
    assert(snapshot.values[RADIO_DIAG_ERROR]==RADIO_DIAG_OK);
    assert(transmitted.size()==pcm.size()+240+1152&&transmitted.front()==0&&transmitted.back()==0);
    assert(std::all_of(transmitted.begin()+240,transmitted.begin()+pcm.size(),[](int16_t s){return s==10000;}));
    assert(bsp_owner!=std::this_thread::get_id()); // Codec and writes belong to sink, not producer.

    reset();out=radio_pcm_output_open(config(44100));assert(out);
    std::vector<int16_t> frame(1152,5000),reserve(3456,5000);
    assert(radio_pcm_output_write(out,reserve.data(),reserve.size()*2));
    std::this_thread::sleep_for(std::chrono::milliseconds(40)); // Longer than BSP DMA reserve.
    for(unsigned i=0;i<12;i++){
        assert(radio_pcm_output_write(out,frame.data(),frame.size()*2));
        std::this_thread::sleep_for(std::chrono::milliseconds(13)); // Measured decode peak.
    }
    assert(radio_pcm_output_close(out,true));join();radio_diagnostics_get(&snapshot);
    assert(snapshot.values[RADIO_DIAG_PCM_BYTES]==(reserve.size()+12*frame.size())*2);
    assert(snapshot.values[RADIO_DIAG_REBUFFERS]==0);

    reset();out=radio_pcm_output_open(config());assert(out);
    assert(radio_pcm_output_write(out,pcm.data(),4096));
    wait_for([&]{return notified(true);}); // Actual starvation, not a queue-empty poll.
    volume=25;
    assert(radio_pcm_output_write(out,pcm.data(),2304));
    assert(radio_pcm_output_close(out,true));join();radio_diagnostics_get(&snapshot);
    assert(snapshot.values[RADIO_DIAG_PCM_BYTES]==6400&&snapshot.values[RADIO_DIAG_REBUFFERS]>=1);
    assert(snapshot.values[RADIO_DIAG_PCM_UNDERRUNS]>=1&&applied_volume==25&&volume_changes==2);
    assert(std::count(notifications.begin(),notifications.end(),false)>=2);

    reset();out=radio_pcm_output_open(config(44100));assert(out);
    assert(radio_pcm_output_write(out,pcm.data(),100)); // EOF shorter than prefill/ramp.
    assert(radio_pcm_output_close(out,true));join();radio_diagnostics_get(&snapshot);
    assert(snapshot.values[RADIO_DIAG_PCM_BYTES]==100&&transmitted.front()==0&&transmitted.back()==0);

    // Receive-only control keeps the real sink/codec allocation alive but
    // provides no media. It must not synthesize output or report fake underruns.
    reset();out=radio_pcm_output_open(config());assert(out);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    assert(!radio_pcm_output_close(out,false));join();radio_diagnostics_get(&snapshot);
    assert(writes==0&&snapshot.values[RADIO_DIAG_PCM_BYTES]==0&&snapshot.values[RADIO_DIAG_PCM_UNDERRUNS]==0&&notifications.empty());

    reset();fail_format=true;assert(!radio_pcm_output_open(config()));join();radio_diagnostics_get(&snapshot);
    assert(snapshot.values[RADIO_DIAG_ERROR]==RADIO_DIAG_FORMAT_ERROR);
    reset();fail_task=true;assert(!radio_pcm_output_open(config()));radio_diagnostics_get(&snapshot);
    assert(snapshot.values[RADIO_DIAG_ERROR]==RADIO_DIAG_MEMORY_ERROR);
    reset();fail_write_at=1;out=radio_pcm_output_open(config());assert(out);
    assert(radio_pcm_output_write(out,pcm.data(),4096));wait_for([&]{return radio_pcm_output_failed(out);});
    assert(!radio_pcm_output_close(out,false));join();radio_diagnostics_get(&snapshot);
    assert(snapshot.values[RADIO_DIAG_ERROR]==RADIO_DIAG_OUTPUT_ERROR&&snapshot.values[RADIO_DIAG_PCM_BYTES]==0);

    reset();out=radio_pcm_output_open(config());assert(out);
    running=false;join();running=true;
    assert(!radio_pcm_output_write(out,pcm.data(),2)); // A finished sink never accepts stale data.
    assert(!radio_pcm_output_close(out,false));

    reset();out=radio_pcm_output_open(config());assert(out);
    std::vector<int16_t> long_pcm(100000,3000);std::atomic<bool> accepted{true};
    std::thread producer([&]{accepted=radio_pcm_output_write(out,long_pcm.data(),long_pcm.size()*2);});
    wait_for([&]{return writes.load()>0;});running=false;producer.join();
    assert(!accepted);radio_pcm_output_close(out,false);join();radio_diagnostics_get(&snapshot);
    assert(snapshot.values[RADIO_DIAG_PCM_BYTES]<long_pcm.size()*2);
    assert(snapshot.values[RADIO_DIAG_PCM_QUEUED_BYTES]==0);
    puts("Radio PCM sink: codec ownership, independent paced output, envelopes, EOF drain, underrun recovery, volume, failures and cancellation/join PASS");
}
