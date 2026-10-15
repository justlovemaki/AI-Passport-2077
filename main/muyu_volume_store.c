#include "muyu_volume_store.h"
#include "muyu_logic.h"
esp_err_t muyu_volume_load(nvs_handle_t storage,uint8_t *percent){
    if(!percent)return ESP_ERR_INVALID_ARG;
    uint8_t stored=0;bool legacy=false;
    esp_err_t result=nvs_get_u8(storage,"volume_pct",&stored);
    if(result==ESP_ERR_NVS_NOT_FOUND){legacy=true;result=nvs_get_u8(storage,"volume",&stored);}
    if(result!=ESP_OK)return result;
    return muyu_volume_decode(stored,legacy,percent)?ESP_OK:ESP_ERR_INVALID_ARG;
}
esp_err_t muyu_volume_store(nvs_handle_t storage,uint8_t percent){
    uint8_t checked;
    if(!muyu_volume_decode(percent,false,&checked))return ESP_ERR_INVALID_ARG;
    /* Keep the old index key untouched: interrupted migration and rollback
     * must never reinterpret a percentage as a legacy five-level index. */
    return nvs_set_u8(storage,"volume_pct",checked);
}
