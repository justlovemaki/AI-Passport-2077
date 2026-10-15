#include "radio_diagnostics.h"
#include <stdatomic.h>
#include <stddef.h>
static atomic_uint_least32_t counters[RADIO_DIAG_COUNT];
static int valid(radio_diag_metric_t field){return (unsigned)field<RADIO_DIAG_COUNT;}
void radio_diagnostics_begin(void){
    for(unsigned i=1;i<RADIO_DIAG_COUNT;i++)atomic_store_explicit(&counters[i],0,memory_order_relaxed);
    atomic_store_explicit(&counters[RADIO_DIAG_MIN_FREE_HEAP],UINT32_MAX,memory_order_relaxed);
    atomic_fetch_add_explicit(&counters[RADIO_DIAG_SESSION],1,memory_order_relaxed);
    atomic_store_explicit(&counters[RADIO_DIAG_ACTIVE],1,memory_order_relaxed);
}
void radio_diagnostics_set(radio_diag_metric_t field,uint32_t value){
    if(valid(field))atomic_store_explicit(&counters[field],value,memory_order_relaxed);
}
void radio_diagnostics_add(radio_diag_metric_t field,uint32_t value){
    if(valid(field))atomic_fetch_add_explicit(&counters[field],value,memory_order_relaxed);
}
void radio_diagnostics_max(radio_diag_metric_t field,uint32_t value){
    if(!valid(field))return;
    uint_least32_t previous=atomic_load_explicit(&counters[field],memory_order_relaxed);
    while(previous<value&&!atomic_compare_exchange_weak_explicit(&counters[field],&previous,value,memory_order_relaxed,memory_order_relaxed)){}
}
void radio_diagnostics_min(radio_diag_metric_t field,uint32_t value){
    if(!valid(field))return;
    uint_least32_t previous=atomic_load_explicit(&counters[field],memory_order_relaxed);
    while(previous>value&&!atomic_compare_exchange_weak_explicit(&counters[field],&previous,value,memory_order_relaxed,memory_order_relaxed)){}
}
void radio_diagnostics_get(radio_diag_snapshot_t *out){
    if(!out)return;
    for(unsigned i=0;i<RADIO_DIAG_COUNT;i++)out->values[i]=(uint32_t)atomic_load_explicit(&counters[i],memory_order_relaxed);
    if(out->values[RADIO_DIAG_MIN_FREE_HEAP]==UINT32_MAX)out->values[RADIO_DIAG_MIN_FREE_HEAP]=0;
}
