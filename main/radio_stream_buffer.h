#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

/* One HTTP producer and one decoder consumer. Neither side waits or allocates.
 * Release/acquire publishes bytes before a reader sees them and retains each
 * slot until its previous bytes have been copied out. */
template<std::size_t Capacity>
class RadioStreamBuffer {
    static_assert(Capacity>0 && Capacity<(UINT32_MAX/2) && (Capacity&(Capacity-1))==0,
                  "power-of-two byte queue preserves indices across counter wrap");
    uint8_t data_[Capacity];
    std::atomic<uint32_t> read_{0},write_{0};
public:
    /* Consumer only: its read cursor cannot change between these loads. */
    std::size_t available() const {
        const uint32_t read=read_.load(std::memory_order_acquire);
        return write_.load(std::memory_order_acquire)-read;
    }
    std::size_t push(const uint8_t *data,std::size_t bytes) {
        const uint32_t write=write_.load(std::memory_order_relaxed);
        const uint32_t read=read_.load(std::memory_order_acquire);
        const std::size_t count=std::min<std::size_t>(bytes,Capacity-(write-read));
        if(!count)return 0;
        const std::size_t index=write%Capacity,first=std::min(count,Capacity-index);
        std::memcpy(data_+index,data,first);
        std::memcpy(data_,data+first,count-first);
        write_.store(write+count,std::memory_order_release);
        return count;
    }
    /* Consumer-only discard for bounded receive diagnostics, never playback. */
    std::size_t discard(std::size_t bytes){
        const uint32_t read=read_.load(std::memory_order_relaxed);
        const uint32_t write=write_.load(std::memory_order_acquire);
        const std::size_t count=std::min<std::size_t>(bytes,write-read);
        read_.store(read+count,std::memory_order_release);return count;
    }
    std::size_t pop(uint8_t *data,std::size_t bytes) {
        const uint32_t read=read_.load(std::memory_order_relaxed);
        const uint32_t write=write_.load(std::memory_order_acquire);
        const std::size_t count=std::min<std::size_t>(bytes,write-read);
        if(!count)return 0;
        const std::size_t index=read%Capacity,first=std::min(count,Capacity-index);
        std::memcpy(data,data_+index,first);
        std::memcpy(data+first,data_,count-first);
        read_.store(read+count,std::memory_order_release);
        return count;
    }
};

/* Initial prefill only. A transient empty queue after playback must not impose
 * another full refill: that turns a millisecond packet gap into an audible stop.
 * Low-bitrate streams also leave prefill after a bounded wait. */
class RadioPrebuffer {
    std::size_t target_;
    int64_t deadline_;
    bool open_=false;
public:
    RadioPrebuffer(std::size_t target,int64_t now,int64_t max_wait):target_(target),deadline_(now+max_wait){}
    bool waiting(std::size_t available,bool finished,int64_t now){
        if(!open_&&(finished||available>=target_||now>=deadline_))open_=true;
        return !open_;
    }
    static bool starved(int64_t now,int64_t last_pcm){return last_pcm>0&&now-last_pcm>=100000;}
};

/* Decoder input survives a zero-consumption call and the next network block.
 * A cached PCM frame may be output without consuming any new compressed bytes. */
class RadioDecoderInput {
    uint8_t *data_;
    std::size_t capacity_,size_=0;
public:
    RadioDecoderInput(uint8_t *data,std::size_t capacity):data_(data),capacity_(capacity){}
    uint8_t *data(){return data_;}
    uint8_t *tail(){return data_+size_;}
    std::size_t size() const{return size_;}
    std::size_t space() const{return capacity_-size_;}
    bool append(std::size_t bytes){if(bytes>space())return false;size_+=bytes;return true;}
    bool consume(std::size_t bytes){
        if(bytes>size_)return false;
        size_-=bytes;
        if(bytes&&size_)std::memmove(data_,data_+bytes,size_);
        return true;
    }
};
