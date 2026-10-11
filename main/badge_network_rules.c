#include "badge_network_rules.h"
#include <string.h>
int badge_network_power_policy(int voice_policy,bool standby){
    return voice_policy==2?2:(voice_policy==1||standby)?1:0;
}
badge_setup_action_t badge_setup_tick(bool *seen,bool onboarding,bool active,uint32_t idle) {
    (void)idle; /* Kept for API compatibility; ordinary setup no longer expires. */
    bool previous=*seen;*seen=onboarding;
    if(onboarding)return !previous&&!active?BADGE_SETUP_START:BADGE_SETUP_IDLE;
    if(previous)return BADGE_SETUP_EXTEND;
    /* In ordinary setup mode, do not auto-close by idle timer while user is configuring */
    return BADGE_SETUP_IDLE;
}
bool badge_address_is_ap(const uint8_t *address,size_t size) {
    const uint8_t ap[4]={192,168,4,1};
    const uint8_t mapped[12]={0,0,0,0,0,0,0,0,0,0,255,255};
    if(!address)return false;
    if(size==4)return memcmp(address,ap,4)==0;
    return size==16 && memcmp(address,mapped,12)==0 && memcmp(address+12,ap,4)==0;
}

void badge_scan_insert(badge_scan_entry_t *entries,size_t *count,const badge_scan_entry_t *entry) {
    if(!entry->ssid[0]||entry->ssid[32]||*count>BADGE_SCAN_LIMIT)return;
    size_t index=*count;
    for(size_t i=0;i<*count;i++)if(entries[i].auth==entry->auth&&!strcmp(entries[i].ssid,entry->ssid)){index=i;break;}
    if(index<*count){if(entries[index].rssi>=entry->rssi)return;}
    else if(*count<BADGE_SCAN_LIMIT)(*count)++;
    else {if(entry->rssi<=entries[*count-1].rssi)return;index=*count-1;}
    entries[index]=*entry;
    while(index&&entries[index].rssi>entries[index-1].rssi){badge_scan_entry_t tmp=entries[index-1];entries[index-1]=entries[index];entries[index]=tmp;index--;}
}

bool badge_wifi_store_upsert(badge_wifi_store_t *store, const char *ssid, const char *password, uint32_t timestamp) {
    if(!store || !ssid || !ssid[0] || strlen(ssid) > 32) return false;
    if(password && strlen(password) > 64) return false;
    for(uint8_t i = 0; i < store->count; i++) {
        if(strcmp(store->items[i].ssid, ssid) == 0) {
            if(password) {
                strncpy(store->items[i].password, password, sizeof(store->items[i].password) - 1);
                store->items[i].password[sizeof(store->items[i].password) - 1] = 0;
            }
            store->items[i].last_used = timestamp;
            return true;
        }
    }
    uint8_t target_idx = store->count;
    if(store->count < BADGE_KNOWN_WIFI_MAX) {
        store->count++;
    } else {
        uint8_t oldest_idx = 0;
        uint32_t oldest_time = store->items[0].last_used;
        for(uint8_t i = 1; i < BADGE_KNOWN_WIFI_MAX; i++) {
            if(store->items[i].last_used < oldest_time) {
                oldest_time = store->items[i].last_used;
                oldest_idx = i;
            }
        }
        target_idx = oldest_idx;
    }
    memset(&store->items[target_idx], 0, sizeof(badge_known_wifi_t));
    strncpy(store->items[target_idx].ssid, ssid, sizeof(store->items[target_idx].ssid) - 1);
    if(password) {
        strncpy(store->items[target_idx].password, password, sizeof(store->items[target_idx].password) - 1);
    }
    store->items[target_idx].last_used = timestamp;
    return true;
}

bool badge_wifi_store_delete(badge_wifi_store_t *store, const char *ssid) {
    if(!store || !ssid || !ssid[0] || store->count == 0) return false;
    int found_idx = -1;
    for(uint8_t i = 0; i < store->count; i++) {
        if(strcmp(store->items[i].ssid, ssid) == 0) {
            found_idx = (int)i;
            break;
        }
    }
    if(found_idx < 0) return false;
    for(uint8_t i = (uint8_t)found_idx; i < store->count - 1; i++) {
        store->items[i] = store->items[i + 1];
    }
    memset(&store->items[store->count - 1], 0, sizeof(badge_known_wifi_t));
    store->count--;
    return true;
}

const badge_known_wifi_t *badge_wifi_store_find(const badge_wifi_store_t *store, const char *ssid) {
    if(!store || !ssid || !ssid[0]) return NULL;
    for(uint8_t i = 0; i < store->count; i++) {
        if(strcmp(store->items[i].ssid, ssid) == 0) {
            return &store->items[i];
        }
    }
    return NULL;
}

bool badge_wifi_store_touch(badge_wifi_store_t *store, const char *ssid, uint32_t timestamp) {
    if(!store || !ssid || !ssid[0]) return false;
    for(uint8_t i = 0; i < store->count; i++) {
        if(strcmp(store->items[i].ssid, ssid) == 0) {
            store->items[i].last_used = timestamp;
            return true;
        }
    }
    return false;
}

int badge_wifi_match_best(const badge_wifi_store_t *store, const badge_scan_entry_t *scanned, size_t scan_count) {
    if(!store || store->count == 0 || !scanned || scan_count == 0) return -1;
    int best_known_idx = -1;
    int best_rssi = -1000;
    for(size_t s = 0; s < scan_count; s++) {
        for(uint8_t k = 0; k < store->count; k++) {
            if(strcmp(scanned[s].ssid, store->items[k].ssid) == 0) {
                if(scanned[s].rssi > best_rssi) {
                    best_rssi = scanned[s].rssi;
                    best_known_idx = (int)k;
                }
                break;
            }
        }
    }
    return best_known_idx;
}

