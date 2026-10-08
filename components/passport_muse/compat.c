#include "factory_test.h"
#include "ota.h"
#include "led_status.h"
#include "muse_state.h"
#include "noise_tunnel_internal.h"
#include "bsp_battery.h"
#include "host/ble_gatt.h"
#include "esp_heap_caps.h"
// This C3 adapter has no tunnel queue and uses in-place RX decryption. Reserving
// the tunnel's 16 KiB here stalls the request queue as soon as I2S starts: the
// queued PCM then times out without ever being sent. Keep 8 KiB for Wi-Fi/TLS;
// req_pump_tx also checks the actual contiguous TLS record allocation.
#define VOICE_TX_DMA_RESERVE (8 * 1024)
bool noise_tx_has_dma_headroom(size_t *available){
    size_t n=heap_caps_get_free_size(MALLOC_CAP_DMA);
    if(available)*available=n;
    return n>=VOICE_TX_DMA_RESERVE;
}
bool noise_tx_has_dma_headroom_reclaiming(size_t reclaimable){
    return heap_caps_get_free_size(MALLOC_CAP_DMA)+reclaimable>=VOICE_TX_DMA_RESERVE;
}
bool noise_tx_has_contiguous_dma_headroom(void){
    return heap_caps_get_largest_free_block(MALLOC_CAP_DMA)>=2*1024;
}
static const struct ble_gatt_svc_def empty_services[]={{0}};
const struct ble_gatt_svc_def *factory_test_svcs(void){ return empty_services; }
void factory_test_on_disconnect(void){}
bool ota_is_enabled(void){return false;}
void ota_start(const char *u,bool f,ota_status_cb cb,void *p){
    (void)u;(void)f; const ota_event_t e={OTA_RESULT_SKIPPED,"Use Passport OS firmware update",NULL,NULL}; if(cb)cb(&e,p);
}
bool led_status_display_info(int *w,int *h){*w=240;*h=320;return false;}
int led_status_display_bits(void){return 16;}
muse_power_t muse_state_power(void){muse_power_t p={.battery_pct=bsp_battery_soc()};return p;}
bool noise_tunnel_on_session_up(const noise_tunnel_emit_t *e){(void)e;return true;}
void noise_tunnel_maybe_reopen(const noise_tunnel_emit_t *e){(void)e;}
void noise_tunnel_on_session_down(void){}
void noise_tunnel_on_inbound(const uint8_t *d,size_t n){(void)d;(void)n;}
int noise_tunnel_pump_tx(const noise_tunnel_emit_t *e){(void)e;return 0;}
void noise_tunnel_tick(const noise_tunnel_emit_t *e){(void)e;}
