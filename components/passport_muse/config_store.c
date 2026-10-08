#include "config_store.h"
#include "nvs.h"
#include <string.h>
static const char *NS = "passport_muse";
void config_store_init(void) {} /* main owns NVS; never erase or reinitialize it. */
bool config_get_str(const char *key, char *out, size_t cap) {
    if (cap) out[0] = 0;
    nvs_handle_t h; if (nvs_open(NS,NVS_READONLY,&h)!=ESP_OK) return false;
    esp_err_t e=nvs_get_str(h,key,out,&cap); nvs_close(h); return e==ESP_OK;
}
bool config_set_str(const char *key,const char *value) {
    nvs_handle_t h; if (nvs_open(NS,NVS_READWRITE,&h)!=ESP_OK) return false;
    esp_err_t e=nvs_set_str(h,key,value); if(e==ESP_OK)e=nvs_commit(h);
    nvs_close(h); return e==ESP_OK;
}
bool config_erase_key(const char *key) {
    nvs_handle_t h; if(nvs_open(NS,NVS_READWRITE,&h)!=ESP_OK)return false;
    esp_err_t e=nvs_erase_key(h,key); if(e==ESP_ERR_NVS_NOT_FOUND)e=ESP_OK;
    if(e==ESP_OK)e=nvs_commit(h);
    nvs_close(h); return e==ESP_OK;
}
config_key_lookup_t config_key_lookup(const char *key) {
    nvs_handle_t h; if(nvs_open(NS,NVS_READONLY,&h)!=ESP_OK)return CONFIG_KEY_NOT_FOUND;
    size_t cap=0; esp_err_t e=nvs_get_str(h,key,NULL,&cap); nvs_close(h);
    return e==ESP_OK?CONFIG_KEY_FOUND:e==ESP_ERR_NVS_NOT_FOUND?CONFIG_KEY_NOT_FOUND:CONFIG_KEY_LOOKUP_ERROR;
}
bool config_is_provisioned(void) { return config_setup_complete()&&config_key_lookup("access_token")==CONFIG_KEY_FOUND; }
bool config_setup_complete(void) { char s[8]; return config_get_str("setup_complete",s,sizeof(s))&&strcmp(s,"yes")==0; }
bool config_mark_setup_complete(void) { return config_set_str("setup_complete","yes"); }
bool config_clear_setup_complete(void) { return config_erase_key("setup_complete"); }
bool config_clear_pairing(void) {
    const char *keys[]={"access_token","refresh_token","username","api_url_v2","noise_host","setup_complete"};
    bool ok=true; for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++)ok=config_erase_key(keys[i])&&ok;
    return ok;
}
bool config_clear_setup(void) { return config_clear_pairing(); }
