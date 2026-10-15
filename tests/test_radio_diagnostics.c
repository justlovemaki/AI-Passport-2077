#include "radio_diagnostics.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
int main(void){
    radio_diag_snapshot_t snapshot;
    radio_diagnostics_get(&snapshot);
    for(unsigned i=0;i<RADIO_DIAG_COUNT;i++)assert(snapshot.values[i]==0);
    radio_diagnostics_begin();
    radio_diagnostics_set(RADIO_DIAG_SAMPLE_RATE,48000);
    radio_diagnostics_add(RADIO_DIAG_HTTP_BYTES,1024);
    radio_diagnostics_add(RADIO_DIAG_HTTP_BYTES,128);
    radio_diagnostics_max(RADIO_DIAG_MAX_FEED_GAP_US,15000);
    radio_diagnostics_max(RADIO_DIAG_MAX_FEED_GAP_US,3);
    radio_diagnostics_max(RADIO_DIAG_MAX_FEED_GAP_US,16000);
    radio_diagnostics_min(RADIO_DIAG_MIN_FREE_HEAP,64000);
    radio_diagnostics_min(RADIO_DIAG_MIN_FREE_HEAP,70000);
    radio_diagnostics_min(RADIO_DIAG_MIN_FREE_HEAP,60000);
    radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_HTTP_IDLE_ERROR);
    radio_diagnostics_get(&snapshot);
    assert(snapshot.values[RADIO_DIAG_SESSION]==1&&snapshot.values[RADIO_DIAG_ACTIVE]==1);
    assert(snapshot.values[RADIO_DIAG_SAMPLE_RATE]==48000&&snapshot.values[RADIO_DIAG_HTTP_BYTES]==1152);
    assert(snapshot.values[RADIO_DIAG_MAX_FEED_GAP_US]==16000&&snapshot.values[RADIO_DIAG_ERROR]==RADIO_DIAG_HTTP_IDLE_ERROR);
    assert(snapshot.values[RADIO_DIAG_MIN_FREE_HEAP]==60000);
    radio_diagnostics_set(RADIO_DIAG_ACTIVE,0);radio_diagnostics_get(&snapshot);
    assert(snapshot.values[RADIO_DIAG_ACTIVE]==0&&snapshot.values[RADIO_DIAG_HTTP_BYTES]==1152);
    // A read after pause retains evidence; only a new stream resets counters.
    radio_diagnostics_begin();radio_diagnostics_get(&snapshot);
    assert(snapshot.values[RADIO_DIAG_SESSION]==2&&snapshot.values[RADIO_DIAG_ACTIVE]==1);
    for(unsigned i=2;i<RADIO_DIAG_COUNT;i++)assert(snapshot.values[i]==0);
    radio_diagnostics_set((radio_diag_metric_t)-1,123);
    radio_diagnostics_add(RADIO_DIAG_COUNT,123);
    radio_diagnostics_max(RADIO_DIAG_COUNT,123);
    radio_diagnostics_min(RADIO_DIAG_COUNT,123);
    radio_diagnostics_get(NULL);
    puts("Radio diagnostics: reset, aggregation, high-water marks, pause retention and invalid-input guards PASS");
}
