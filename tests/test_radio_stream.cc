#include "radio_stream_buffer.h"
#include "radio_buffer_budget.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <thread>

static uint8_t byte_at(std::size_t index){return static_cast<uint8_t>(index%251);}
int main(){
    RadioStreamBuffer<8> small;
    uint8_t source[12],out[12];
    for(unsigned i=0;i<12;i++)source[i]=i;
    assert(small.pop(out,sizeof(out))==0);
    assert(small.push(source,12)==8&&small.available()==8);
    assert(small.push(source,1)==0); // Backpressure never overwrites unread bytes.
    assert(small.pop(out,5)==5&&!std::memcmp(source,out,5));
    assert(small.push(source+8,4)==4); // Physical ring wrap.
    assert(small.pop(out,12)==7&&!std::memcmp(source+5,out,7));
    assert(small.available()==0);
    assert(small.push(source,8)==8&&small.discard(3)==3);
    assert(small.pop(out,2)==2&&out[0]==3&&out[1]==4);
    assert(small.discard(99)==3&&small.available()==0&&small.discard(1)==0);

    /* A 100 ms HTTP wait used to exceed the 13 ms DMA window at 48 kHz.
     * Read-ahead of 4096 compressed bytes can supply 100 ms at 320 kbit/s.
     * This is a bounded jitter check, not an unlimited network guarantee. */
    RadioStreamBuffer<8192> jitter;
    std::array<uint8_t,4096> preload{};
    assert(jitter.push(preload.data(),preload.size())==preload.size());
    uint8_t tick[400];
    for(unsigned ms=0;ms<100;ms+=10)assert(jitter.pop(tick,sizeof(tick))==sizeof(tick));
    assert(jitter.available()==96);

    // Keep the independent PCM reserve, but do not consume the receiver heap
    // with a 32 KiB compressed queue before lazy MP3 allocation has occurred.
    assert(radio_buffers::kCompressedBytes==8192&&radio_buffers::kPcmBytes==8192);
    assert(radio_buffers::kDefaultReadBytes==256&&radio_buffers::kMaxReadBytes==1024);
    assert(radio_buffers::kOwnedBufferBytes<=25*1024);
    RadioStreamBuffer<radio_buffers::kCompressedBytes> source_reserve;
    std::array<uint8_t,radio_buffers::kCompressedBytes> compressed{};
    assert(source_reserve.push(compressed.data(),compressed.size())==compressed.size());
    for(unsigned ms=0;ms<1000;ms+=50)assert(source_reserve.pop(tick,sizeof(tick))==sizeof(tick));
    assert(source_reserve.available()==192); // Bounded one-second reserve at 64 kbit/s.
    RadioPrebuffer startup(radio_buffers::kStartupBytes,100,radio_buffers::kStartupWaitUs);
    assert(startup.waiting(1024,false,100+radio_buffers::kStartupWaitUs-1));
    assert(!startup.waiting(1024,false,100+radio_buffers::kStartupWaitUs));
    RadioPrebuffer filled(radio_buffers::kStartupBytes,100,radio_buffers::kStartupWaitUs);
    assert(!filled.waiting(2048,false,101)&&!filled.waiting(0,false,102));

    // Real concurrent producer/consumer, unequal fragments and repeated wraps.
    RadioStreamBuffer<8192> stream;
    constexpr std::size_t total=1024*1024+777;
    std::thread producer([&]{
        uint8_t block[1024];std::size_t offset=0;
        while(offset<total){
            const std::size_t want=std::min<std::size_t>(sizeof(block),total-offset);
            for(std::size_t i=0;i<want;i++)block[i]=byte_at(offset+i);
            const std::size_t sent=stream.push(block,want);offset+=sent;
            if(!sent)std::this_thread::yield();
        }
    });
    std::size_t received=0;uint8_t block[137];
    while(received<total){
        const std::size_t count=stream.pop(block,sizeof(block));
        for(std::size_t i=0;i<count;i++)assert(block[i]==byte_at(received+i));
        received+=count;if(!count)std::this_thread::yield();
    }
    producer.join();assert(stream.available()==0);

    // Prefill is paid once, not every time normal packet delivery leaves the
    // compressed queue briefly empty. This reproduces the old rebuffer loop.
    RadioPrebuffer prebuffer(4096,100,1000000);
    assert(prebuffer.waiting(0,false,100));
    assert(prebuffer.waiting(1024,false,1000));
    assert(!prebuffer.waiting(4096,false,2000));
    for(unsigned packet=0;packet<100;packet++){
        assert(!prebuffer.waiting(0,false,3000+packet*10000));
        assert(!prebuffer.waiting(128,false,3500+packet*10000));
    }
    RadioPrebuffer low_bitrate(4096,100,1000000);
    assert(low_bitrate.waiting(64,false,1000099));
    assert(!low_bitrate.waiting(64,false,1000100));
    RadioPrebuffer short_stream(4096,100,1000000);
    assert(!short_stream.waiting(17,true,101));
    assert(!RadioPrebuffer::starved(1000100,0));
    assert(!RadioPrebuffer::starved(1000099,900100));
    assert(RadioPrebuffer::starved(1000100,900100));

    uint8_t storage[8];RadioDecoderInput pending(storage,sizeof(storage));
    std::memcpy(pending.tail(),source,4);assert(pending.append(4));
    // A cached output frame consumes no compressed bytes: retry the same input.
    assert(pending.consume(0)&&pending.size()==4&&!std::memcmp(pending.data(),source,4));
    assert(pending.consume(2)&&pending.size()==2);
    // A no-output/no-consumption call needs fresh data, not a discarded tail.
    assert(pending.consume(0));std::memcpy(pending.tail(),source+4,4);
    assert(pending.append(4)&&pending.size()==6&&!std::memcmp(pending.data(),source+2,6));
    assert(!pending.append(3)&&pending.size()==6);
    assert(!pending.consume(7)&&pending.size()==6);
    assert(pending.consume(6)&&pending.size()==0&&pending.space()==sizeof(storage));
    puts("Radio stream: byte integrity/backpressure, initial-only bounded prefill, transient-empty regression, starvation indication and unconsumed input PASS");
}
