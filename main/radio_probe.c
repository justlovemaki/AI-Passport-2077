#include "radio_probe.h"
#include <stdatomic.h>
#include <stddef.h>
static atomic_uint_least32_t request;
#define RUNNING UINT32_MAX
bool radio_probe_valid_read_size(unsigned bytes){return bytes==256||bytes==1024;}
bool radio_probe_reserve(bool available,bool paused,bool stream_busy,unsigned seconds,unsigned read_bytes){
    if(!available||!paused||stream_busy||seconds<5||seconds>20||!radio_probe_valid_read_size(read_bytes))return false;
    uint_least32_t empty=0,value=(seconds<<16)|read_bytes;
    return atomic_compare_exchange_strong_explicit(&request,&empty,value,memory_order_release,memory_order_relaxed);
}
bool radio_probe_take(radio_probe_request_t *out){
    if(!out)return false;
    uint_least32_t value=atomic_load_explicit(&request,memory_order_acquire);
    if(!value||value==RUNNING)return false;
    if(!atomic_compare_exchange_strong_explicit(&request,&value,RUNNING,memory_order_acq_rel,memory_order_relaxed))return false;
    out->seconds=(unsigned)(value>>16);out->read_bytes=(unsigned)(value&0xffff);return true;
}
bool radio_probe_busy(void){return atomic_load_explicit(&request,memory_order_acquire)!=0;}
void radio_probe_finish(void){atomic_store_explicit(&request,0,memory_order_release);}
void radio_probe_cancel(void){radio_probe_finish();}
