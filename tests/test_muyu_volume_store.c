#include "muyu_volume_store.h"
#include "muyu_logic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool have_percent,have_legacy,fail_set;
static uint8_t percent_value,legacy_value;
static esp_err_t percent_error,legacy_error;
static unsigned percent_reads,legacy_reads,writes;
esp_err_t nvs_get_u8(nvs_handle_t storage,const char *key,uint8_t *out){
    assert(storage==1);
    if(!strcmp(key,"volume_pct")){
        percent_reads++;if(percent_error)return percent_error;
        if(!have_percent)return ESP_ERR_NVS_NOT_FOUND;
        *out=percent_value;return ESP_OK;
    }
    assert(!strcmp(key,"volume"));legacy_reads++;if(legacy_error)return legacy_error;
    if(!have_legacy)return ESP_ERR_NVS_NOT_FOUND;
    *out=legacy_value;return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t storage,const char *key,uint8_t value){
    assert(storage==1&&!strcmp(key,"volume_pct"));writes++;
    if(fail_set)return ESP_FAIL;
    have_percent=true;percent_value=value;return ESP_OK;
}
static void reset(void){have_percent=have_legacy=fail_set=false;percent_value=legacy_value=0;percent_error=legacy_error=ESP_OK;percent_reads=legacy_reads=writes=0;}
int main(void){
    uint8_t restored=MUYU_VOLUME_DEFAULT;
    reset();assert(muyu_volume_load(1,&restored)==ESP_ERR_NVS_NOT_FOUND&&restored==40&&writes==0);
    for(unsigned i=0;i<5;i++){
        reset();have_legacy=true;legacy_value=i;restored=255;
        assert(muyu_volume_load(1,&restored)==ESP_OK&&restored==i*20&&percent_reads==1&&legacy_reads==1&&writes==0);
    }
    for(unsigned i=0;i<=100;i+=5){
        reset();have_percent=have_legacy=true;percent_value=i;legacy_value=4;restored=255;
        assert(muyu_volume_load(1,&restored)==ESP_OK&&restored==i&&legacy_reads==0&&writes==0);
    }
    reset();have_percent=have_legacy=true;percent_value=103;legacy_value=2;restored=40;
    assert(muyu_volume_load(1,&restored)==ESP_ERR_INVALID_ARG&&restored==40&&legacy_reads==0&&writes==0);
    percent_error=ESP_ERR_NVS_TYPE_MISMATCH;
    assert(muyu_volume_load(1,&restored)==ESP_ERR_NVS_TYPE_MISMATCH&&legacy_reads==0);
    reset();have_legacy=true;legacy_value=5;restored=40;
    assert(muyu_volume_load(1,&restored)==ESP_ERR_INVALID_ARG&&restored==40);
    legacy_error=ESP_ERR_NVS_TYPE_MISMATCH;
    assert(muyu_volume_load(1,&restored)==ESP_ERR_NVS_TYPE_MISMATCH);
    reset();assert(muyu_volume_load(1,NULL)==ESP_ERR_INVALID_ARG&&percent_reads==0);
    assert(muyu_volume_store(1,103)==ESP_ERR_INVALID_ARG&&writes==0);
    assert(muyu_volume_store(1,63)==ESP_ERR_INVALID_ARG&&writes==0);
    // Interrupted first migration still falls back to the intact old index.
    reset();have_legacy=true;legacy_value=3;fail_set=true;
    assert(muyu_volume_store(1,65)==ESP_FAIL&&!have_percent&&legacy_value==3);
    assert(muyu_volume_load(1,&restored)==ESP_OK&&restored==60);
    fail_set=false;assert(muyu_volume_store(1,65)==ESP_OK&&legacy_value==3);
    assert(muyu_volume_load(1,&restored)==ESP_OK&&restored==65);
    assert(muyu_volume_store(1,100)==ESP_OK&&legacy_value==3);
    assert(muyu_volume_load(1,&restored)==ESP_OK&&restored==100);
    assert(muyu_volume_store(1,0)==ESP_OK);
    assert(muyu_volume_load(1,&restored)==ESP_OK&&restored==0);
    puts("Muyu volume storage: legacy mapping, new-key priority, corrupt/type guards, failed migration, mute/100 and rollback key preservation PASS");
}
