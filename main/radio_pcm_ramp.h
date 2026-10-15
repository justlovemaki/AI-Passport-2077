#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

/* Five-millisecond integer envelopes only at start, discontinuity and stop.
 * Ordinary PCM after the envelope is bit-for-bit unchanged. */
class RadioPcmRamp {
    std::size_t length_,remaining_;
    int16_t last_=0;
public:
    explicit RadioPcmRamp(uint32_t rate):length_(std::clamp<uint32_t>(rate/200,1,240)),remaining_(length_){}
    std::size_t length() const{return length_;}
    void process(int16_t *pcm,std::size_t count){
        for(std::size_t i=0;i<count;i++){
            if(remaining_){
                pcm[i]=static_cast<int16_t>(static_cast<int32_t>(pcm[i])*static_cast<int32_t>(length_-remaining_)/static_cast<int32_t>(length_));
                --remaining_;
            }
            last_=pcm[i];
        }
    }
    std::size_t fade_out(int16_t *pcm,std::size_t capacity){
        const std::size_t count=std::min(length_,capacity);
        if(!count)return 0;
        for(std::size_t i=0;i<count;i++)pcm[i]=static_cast<int16_t>(static_cast<int32_t>(last_)*static_cast<int32_t>(count-1-i)/static_cast<int32_t>(count));
        last_=0;remaining_=length_;
        return count;
    }
};
