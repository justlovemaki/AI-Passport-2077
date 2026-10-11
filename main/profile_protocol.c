#include "profile_protocol.h"
#include "profile_store.h"
#include "profile_edit.h"
#include "badge_profile.h"
#include "badge_power.h"
#include "badge_network.h"
#include "xiaozhi_app.h"
#include "passport_muse.h"
#include "muse_style.h"
#include "yao_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "mbedtls/platform_util.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
static SemaphoreHandle_t mutex;
static uint8_t binary[768];
static char encoded[1025],owner[40];
static bool transferring;
static int64_t last_activity;
enum {CONFIG_IDLE,CONFIG_EXPORT,CONFIG_IMPORT,CONFIG_RESTARTING};
#define CONFIG_NVS_BYTES 0x6000u
static unsigned config_mode;
static uint8_t *config_buffer;
static size_t config_received;
static uint8_t config_digest[32];
static char config_owner[40];
static esp_timer_handle_t config_restart_timer;
static const esp_partition_t *config_nvs(void){
    const esp_partition_t *p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_NVS,"nvs");
    return p&&p->address==0x9000&&p->size==CONFIG_NVS_BYTES?p:NULL;
}
static void config_reset(void){if(config_buffer)mbedtls_platform_zeroize(config_buffer,CONFIG_NVS_BYTES);free(config_buffer);config_buffer=NULL;config_mode=CONFIG_IDLE;config_received=0;mbedtls_platform_zeroize(config_digest,sizeof(config_digest));config_owner[0]=0;}
static void config_wait_for_restart(void){mbedtls_platform_zeroize(config_buffer,CONFIG_NVS_BYTES);free(config_buffer);config_buffer=NULL;config_mode=CONFIG_RESTARTING;config_received=0;mbedtls_platform_zeroize(config_digest,sizeof(config_digest));}
static bool device_id(char out[18]){uint8_t mac[6];if(esp_read_mac(mac,ESP_MAC_WIFI_STA)!=ESP_OK)return false;snprintf(out,18,"%02x:%02x:%02x:%02x:%02x:%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);return true;}
static int hex_digit(unsigned char value){return value>='0'&&value<='9'?value-'0':value>='a'&&value<='f'?value-'a'+10:value>='A'&&value<='F'?value-'A'+10:-1;}
static bool device_id_text(const char *value){if(!value||strlen(value)!=17)return false;for(unsigned i=0;i<17;i++)if(i%3==2?(value[i]!=':'):(hex_digit((unsigned char)value[i])<0))return false;return true;}
static bool digest_text(const char *text,uint8_t out[32]){
    if(!text||strlen(text)!=64)return false;
    for(unsigned i=0;i<32;i++){int a=hex_digit((unsigned char)text[i*2]),b=hex_digit((unsigned char)text[i*2+1]);if(a<0||b<0)return false;out[i]=(uint8_t)(a*16+b);}
    return true;
}
static bool config_partition_matches(const esp_partition_t *partition,const uint8_t expected[32]){
    mbedtls_sha256_context context;uint8_t digest[32];mbedtls_sha256_init(&context);
    int result=mbedtls_sha256_starts(&context,0);
    for(size_t offset=0;result==0&&offset<CONFIG_NVS_BYTES;offset+=sizeof(binary)){size_t size=CONFIG_NVS_BYTES-offset;if(size>sizeof(binary))size=sizeof(binary);esp_err_t e=esp_partition_read(partition,offset,binary,size);if(e!=ESP_OK){result=-1;break;}result=mbedtls_sha256_update(&context,binary,size);}
    if(result==0)result=mbedtls_sha256_finish(&context,digest);
    mbedtls_sha256_free(&context);return result==0&&!memcmp(digest,expected,32);
}
static void restart_timer(void *arg){(void)arg;esp_restart();}
static esp_err_t schedule_restart(void){return config_restart_timer?esp_timer_start_once(config_restart_timer,700000):ESP_ERR_INVALID_STATE;}
esp_err_t profile_protocol_init(void) {
    yao_location_init();
    mutex=xSemaphoreCreateMutex();if(!mutex)return ESP_ERR_NO_MEM;
    const esp_timer_create_args_t timer={.callback=restart_timer,.name="config_restart"};
    esp_err_t timer_result=esp_timer_create(&timer,&config_restart_timer);if(timer_result!=ESP_OK){vSemaphoreDelete(mutex);mutex=NULL;return timer_result;}
    nvs_handle_t n;uint8_t id=0;
    if(nvs_open("badge_cards",NVS_READONLY,&n)==ESP_OK){nvs_get_u8(n,"active",&id);nvs_close(n);}
    if(id<profile_store_count())profile_store_select(id);
    return ESP_OK;
}
bool profile_protocol_lock(void) {return mutex&&xSemaphoreTake(mutex,pdMS_TO_TICKS(5000))==pdTRUE;}
void profile_protocol_unlock(void) {xSemaphoreGive(mutex);}
static esp_err_t select_badge(unsigned id) {
    if(id>=profile_store_count())return ESP_ERR_INVALID_ARG;
    if(transferring||config_mode!=CONFIG_IDLE||!profile_store_ready())return ESP_ERR_INVALID_STATE;
    if(id==profile_store_badge())return ESP_OK;
    nvs_handle_t n;esp_err_t e=nvs_open("badge_cards",NVS_READWRITE,&n);
    if(e==ESP_OK){e=nvs_set_u8(n,"active",id);if(e==ESP_OK)e=nvs_commit(n);nvs_close(n);}
    return e==ESP_OK?profile_store_select(id):e;
}
static void expire(void) {
    int64_t age=esp_timer_get_time()-last_activity;
    if(transferring&&age>15000000) {profile_store_abort();transferring=false;}
    if(config_mode!=CONFIG_IDLE&&age>60000000)config_reset();
}
esp_err_t profile_protocol_select(unsigned id) {
    if(!profile_protocol_lock())return ESP_ERR_INVALID_STATE;
    expire();esp_err_t e=select_badge(id);profile_protocol_unlock();return e;
}
esp_err_t profile_protocol_edit(unsigned id,unsigned revision,unsigned field,const char *text){
    if(!profile_protocol_lock())return ESP_ERR_INVALID_STATE;
    expire();
    esp_err_t e=transferring||config_mode!=CONFIG_IDLE?ESP_ERR_INVALID_STATE:profile_edit_locked(id,revision,field,text);
    profile_protocol_unlock();return e;
}
static void catalog(cJSON *reply) {
    cJSON_AddNumberToObject(reply,"activeBadge",profile_store_badge());
    cJSON_AddNumberToObject(reply,"badgeCount",profile_store_count());
    cJSON *list=cJSON_AddArrayToObject(reply,"badges");
    for(unsigned i=0;i<profile_store_count();i++) {
        cJSON *row=cJSON_CreateObject();cJSON_AddNumberToObject(row,"id",i);
        cJSON_AddNumberToObject(row,"revision",profile_store_revision_for(i));
        cJSON_AddBoolToObject(row,"configured",profile_store_card_for(i)!=NULL);
        size_t length;const char *raw=profile_store_json_for(i,&length);cJSON *p=cJSON_ParseWithLength(raw,length);
        const char *keys[]={"name","brandName"};
        for(unsigned k=0;k<2;k++){cJSON *v=cJSON_GetObjectItemCaseSensitive(p,keys[k]);cJSON_AddStringToObject(row,keys[k],cJSON_IsString(v)?v->valuestring:"");}
        cJSON_Delete(p);cJSON_AddItemToArray(list,row);
    }
}
void profile_protocol_tick(void) {if(mutex&&xSemaphoreTake(mutex,0)==pdTRUE){expire();xSemaphoreGive(mutex);}}
static bool number(cJSON *o,const char *key,size_t max,size_t *value) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>max||floor(v->valuedouble)!=v->valuedouble) return false;
    *value=(size_t)v->valuedouble;return true;
}
static void muse_config_json(cJSON *reply) {
    bool token_set=false,paired=false;char host[16]={0};uint16_t port=0;
    passport_muse_config_status(&token_set,&paired);
    passport_muse_proxy_config(host,sizeof(host),&port);
    cJSON *muse=cJSON_AddObjectToObject(reply,"muse");
    cJSON_AddBoolToObject(muse,"tokenSet",token_set);
    cJSON_AddBoolToObject(muse,"paired",paired);
    cJSON_AddStringToObject(muse,"proxyHost",host);
    cJSON_AddNumberToObject(muse,"proxyPort",port);
    cJSON_AddNumberToObject(muse,"style",muse_style_get());
}
static bool valid_profile(cJSON *p,unsigned version) {
    if(!cJSON_IsObject(p)) return false;
    const char *keys[]={"name","department","title","employeeId","signature"};
    for(size_t i=0;i<5;i++) {
        cJSON *s=cJSON_GetObjectItemCaseSensitive(p,keys[i]);
        if(!cJSON_IsString(s)||strlen(s->valuestring)>128 || (i==0&&!s->valuestring[0])) return false;
        for(const unsigned char *c=(const unsigned char *)s->valuestring;*c;c++) if(*c<32||*c==127) return false;
    }
    if(version<2) return cJSON_GetArraySize(p)==5;
    const char *extra[]={"brandName","badgeCaption"};
    for(unsigned i=0;i<2;i++) {
        cJSON *v=cJSON_GetObjectItemCaseSensitive(p,extra[i]);
        if(!cJSON_IsString(v)||!v->valuestring[0]||strlen(v->valuestring)>128) return false;
        for(const unsigned char *c=(void *)v->valuestring;*c;c++) if(*c<32||*c==127)return false;
    }
    cJSON *theme=cJSON_GetObjectItemCaseSensitive(p,"theme");
    const char *colors[]={"background","panel","accent","text","muted"};
    if(!cJSON_IsObject(theme)||cJSON_GetArraySize(theme)!=5)return false;
    for(unsigned i=0;i<5;i++) {
        cJSON *v=cJSON_GetObjectItemCaseSensitive(theme,colors[i]);
        if(!cJSON_IsString(v)||strlen(v->valuestring)!=7||v->valuestring[0]!='#')return false;
        for(unsigned j=1;j<7;j++)if(!isxdigit((unsigned char)v->valuestring[j]))return false;
    }
    if(version>=4){
        cJSON *alias=cJSON_GetObjectItemCaseSensitive(p,"alias");
        if(!cJSON_IsString(alias)||strlen(alias->valuestring)>128)return false;
        for(const unsigned char *c=(void *)alias->valuestring;*c;c++)if(*c<32||*c==127)return false;
    }
    return cJSON_GetArraySize(p)==(version>=4?11:10) && cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(p,"logoCustom")) &&
        cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(p,"qrPresent"));
}
char *profile_protocol_request(const char *line,bool usb) {
    if(!mutex || xSemaphoreTake(mutex,pdMS_TO_TICKS(5000))!=pdTRUE)return NULL;
    expire();
    cJSON *q=cJSON_Parse(line),*reply=cJSON_CreateObject();
    if(!reply) {cJSON_Delete(q);xSemaphoreGive(mutex);return NULL;}
    size_t request_id=0;
    esp_err_t e=ESP_ERR_INVALID_ARG;
    const char *reason=NULL;
    if(!q||!number(q,"id",1000000000,&request_id)) goto done;
    cJSON *op=cJSON_GetObjectItemCaseSensitive(q,"op");
    if(!cJSON_IsString(op)) goto done;
    char client[40];cJSON *session=cJSON_GetObjectItemCaseSensitive(q,"session");
    const char *sid=cJSON_IsString(session)?session->valuestring:"legacy";
    if(strlen(sid)>32)goto done;
    snprintf(client,sizeof(client),"%c:%s",usb?'U':'H',sid);
    bool read_only=!strcmp(op->valuestring,"info")||!strcmp(op->valuestring,"read")||!strcmp(op->valuestring,"wifi_info")||!strcmp(op->valuestring,"wifi_scan_results")||!strcmp(op->valuestring,"badges")||!strcmp(op->valuestring,"xiaozhi_backend")||!strcmp(op->valuestring,"muse_config")||!strcmp(op->valuestring,"config_backup_info")||!strcmp(op->valuestring,"config_backup_read");
    if(!read_only)badge_power_activity();
    if(!read_only && transferring && strcmp(owner,client)) {e=ESP_ERR_INVALID_STATE;reason="upload_busy";goto done;}
    if(config_mode!=CONFIG_IDLE&&strcmp(config_owner,client)){e=ESP_ERR_INVALID_STATE;reason="backup_busy";goto done;}
    if(!strcmp(owner,client)||!strcmp(config_owner,client))last_activity=esp_timer_get_time();
    bool maintenance_op=!strcmp(op->valuestring,"config_backup_read")||!strcmp(op->valuestring,"config_backup_end")||!strcmp(op->valuestring,"config_restore_chunk")||!strcmp(op->valuestring,"config_restore_abort")||!strcmp(op->valuestring,"config_restore_commit");
    bool export_read=config_mode==CONFIG_EXPORT&&read_only;
    if(config_mode!=CONFIG_IDLE&&!maintenance_op&&!export_read){e=ESP_ERR_INVALID_STATE;reason="backup_busy";goto done;}
    size_t badge=profile_store_badge();
    if(cJSON_GetObjectItemCaseSensitive(q,"badge")&&!number(q,"badge",profile_store_count()-1,&badge))goto done;
    if(profile_store_count()>1&&!cJSON_GetObjectItemCaseSensitive(q,"badge")&&
       (!strcmp(op->valuestring,"begin")||!strcmp(op->valuestring,"clear")))goto done;
    if((!strcmp(op->valuestring,"begin")||!strcmp(op->valuestring,"clear"))&&cJSON_GetObjectItemCaseSensitive(q,"baseRevision")) {
        size_t base;if(!number(q,"baseRevision",UINT32_MAX,&base))goto done;
        if(base!=profile_store_revision_for(badge)){e=ESP_ERR_INVALID_STATE;reason="profile_changed";goto done;}
    }
    if((!strcmp(op->valuestring,"begin")||!strcmp(op->valuestring,"clear")||!strcmp(op->valuestring,"badge_select"))&&profile_store_refresh_pending()){
        e=ESP_ERR_INVALID_STATE;reason="display_pending";goto done;
    }
    if(!strcmp(op->valuestring,"config_backup_info")) {
        const esp_partition_t *p=config_nvs();char id[18];
        if(p&&device_id(id)){cJSON_AddNumberToObject(reply,"format",1);cJSON_AddStringToObject(reply,"device",id);cJSON_AddNumberToObject(reply,"nvsSize",CONFIG_NVS_BYTES);cJSON_AddNumberToObject(reply,"badgeCount",profile_store_count());e=ESP_OK;}
    } else if(!strcmp(op->valuestring,"config_backup_begin")) {
        const esp_partition_t *p=config_nvs();
        if(config_mode==CONFIG_IDLE&&!transferring&&p){config_buffer=malloc(CONFIG_NVS_BYTES);if(!config_buffer)e=ESP_ERR_NO_MEM;else {e=esp_partition_read(p,0,config_buffer,CONFIG_NVS_BYTES);if(e==ESP_OK){config_mode=CONFIG_EXPORT;config_received=CONFIG_NVS_BYTES;snprintf(config_owner,sizeof(config_owner),"%s",client);last_activity=esp_timer_get_time();}else config_reset();}}
    } else if(!strcmp(op->valuestring,"config_backup_read")) {
        size_t offset=0,count=0,n=0;
        if(config_mode==CONFIG_EXPORT&&number(q,"offset",CONFIG_NVS_BYTES,&offset)&&number(q,"count",sizeof(binary),&count)&&count&&offset<=CONFIG_NVS_BYTES&&count<=CONFIG_NVS_BYTES-offset&&mbedtls_base64_encode((unsigned char *)encoded,sizeof(encoded),&n,config_buffer+offset,count)==0){encoded[n]=0;cJSON_AddStringToObject(reply,"data",encoded);cJSON_AddNumberToObject(reply,"offset",offset+count);e=ESP_OK;}
    } else if(!strcmp(op->valuestring,"config_backup_end")) {
        if(config_mode==CONFIG_EXPORT){config_reset();e=ESP_OK;}
    } else if(!strcmp(op->valuestring,"config_restore_begin")) {
        cJSON *device=cJSON_GetObjectItemCaseSensitive(q,"device"),*sha=cJSON_GetObjectItemCaseSensitive(q,"sha256");size_t size=0;
        if(config_mode==CONFIG_IDLE&&!transferring&&cJSON_IsString(device)&&device_id_text(device->valuestring)&&cJSON_IsString(sha)&&digest_text(sha->valuestring,config_digest)&&number(q,"size",CONFIG_NVS_BYTES,&size)&&size==CONFIG_NVS_BYTES){config_buffer=malloc(CONFIG_NVS_BYTES);if(!config_buffer)e=ESP_ERR_NO_MEM;else {config_mode=CONFIG_IMPORT;config_received=0;snprintf(config_owner,sizeof(config_owner),"%s",client);last_activity=esp_timer_get_time();e=ESP_OK;}}
    } else if(!strcmp(op->valuestring,"config_restore_chunk")) {
        cJSON *s=cJSON_GetObjectItemCaseSensitive(q,"data");size_t offset=0,n=0;
        if(config_mode==CONFIG_IMPORT&&cJSON_IsString(s)&&strlen(s->valuestring)<=1024&&number(q,"offset",CONFIG_NVS_BYTES,&offset)&&offset==config_received&&mbedtls_base64_decode(binary,sizeof(binary),&n,(const unsigned char *)s->valuestring,strlen(s->valuestring))==0&&n>0&&n<=CONFIG_NVS_BYTES-config_received){memcpy(config_buffer+config_received,binary,n);config_received+=n;cJSON_AddNumberToObject(reply,"offset",config_received);e=ESP_OK;}
    } else if(!strcmp(op->valuestring,"config_restore_abort")) {
        if(config_mode==CONFIG_IMPORT){config_reset();e=ESP_OK;}
    } else if(!strcmp(op->valuestring,"config_restore_commit")) {
        uint8_t digest[32];const esp_partition_t *p=config_nvs();
        if(config_mode==CONFIG_IMPORT&&config_received==CONFIG_NVS_BYTES&&p&&mbedtls_sha256(config_buffer,CONFIG_NVS_BYTES,digest,0)==0&&!memcmp(digest,config_digest,32)){
            e=nvs_flash_deinit();if(e==ESP_OK)e=esp_partition_erase_range(p,0,CONFIG_NVS_BYTES);if(e==ESP_OK)e=esp_partition_write(p,0,config_buffer,CONFIG_NVS_BYTES);if(e==ESP_OK&&!config_partition_matches(p,config_digest))e=ESP_ERR_INVALID_CRC;if(e==ESP_OK)e=schedule_restart();if(e==ESP_OK)config_wait_for_restart();
        }
    } else if(!strcmp(op->valuestring,"info")) {
        yao_location_t place=yao_location_get();
        cJSON_AddItemToObject(reply,"yaoTime",yao_time_json(yao_time_now(),&place));
        cJSON_AddStringToObject(reply,"yaoLocationStatus",yao_location_status());
        unsigned version=profile_store_version_for(badge);
        cJSON_AddNumberToObject(reply,"protocol",2);
        cJSON_AddStringToObject(reply,"firmwareVersion",BADGE_VERSION);
        badge_power_status_t power=badge_power_status();
        cJSON *power_json=cJSON_AddObjectToObject(reply,"power");
        cJSON_AddBoolToObject(power_json,"screenOff",power.screen_off);
        cJSON_AddBoolToObject(power_json,"lightSleepConfigured",power.configured);
        cJSON_AddBoolToObject(power_json,"displayPerformanceLock",power.display_performance);
        cJSON_AddBoolToObject(power_json,"audioPerformanceLock",power.audio_performance);
        cJSON_AddBoolToObject(power_json,"audioOwner",power.audio_owner);
        cJSON_AddBoolToObject(power_json,"voiceListening",power.voice_listening);
        cJSON_AddBoolToObject(power_json,"audioSleeping",power.audio_sleeping);
        char backend[256];bool backend_custom=false;
        if(demo_xiaozhi_get_backend(backend,sizeof(backend),&backend_custom)==ESP_OK){
            cJSON *xz=cJSON_AddObjectToObject(reply,"xiaozhi");
            cJSON_AddStringToObject(xz,"backendUrl",backend);
            cJSON_AddBoolToObject(xz,"backendCustom",backend_custom);
        }
        muse_config_json(reply);
        cJSON_AddBoolToObject(reply,"profileReady",profile_store_ready());
        cJSON_AddBoolToObject(reply,"refreshPending",profile_store_refresh_pending());
        cJSON_AddNumberToObject(reply,"maxProfileVersion",4);
        cJSON_AddNumberToObject(reply,"maxBadgeLayout",2);
        cJSON_AddNumberToObject(reply,"profileVersion",profile_store_version_for(badge));
        cJSON_AddBoolToObject(reply,"qrPresent",profile_store_asset_for(badge,PROFILE_ASSET_QR)!=NULL);
        badge_network_json(reply);cJSON_AddNumberToObject(reply,"revision",profile_store_revision());
        cJSON_AddBoolToObject(reply,"custom",profile_store_card_for(badge)!=NULL);
        cJSON_AddNumberToObject(reply,"cardWidth",PROFILE_CARD_W);cJSON_AddNumberToObject(reply,"cardHeight",version>=4?PROFILE_V4_CARD_H:version>=3?PROFILE_V3_CARD_H:PROFILE_CARD_H);
        cJSON_AddNumberToObject(reply,"avatarWidth",version>=3?PROFILE_V3_AVATAR_W:PROFILE_AVATAR_W);cJSON_AddNumberToObject(reply,"avatarHeight",version>=4?PROFILE_V4_AVATAR_H:version>=3?PROFILE_V3_AVATAR_H:PROFILE_AVATAR_H);
        catalog(reply);cJSON_AddNumberToObject(reply,"badge",badge);cJSON_AddNumberToObject(reply,"badgeRevision",profile_store_revision_for(badge));
        size_t n;const char *s=profile_store_json_for(badge,&n);
        char json[PROFILE_JSON_MAX+1];memcpy(json,s,n);json[n]=0;
        cJSON *p=cJSON_Parse(json);if(!p) p=cJSON_CreateObject();cJSON_AddItemToObject(reply,"profile",p);
        e=ESP_OK;
    } else if(!strcmp(op->valuestring,"begin")) {
        cJSON *p=cJSON_GetObjectItemCaseSensitive(q,"profile");
        cJSON *format=cJSON_GetObjectItemCaseSensitive(q,"format");
        if(format&&(!cJSON_IsNumber(format)||(format->valuedouble!=2&&format->valuedouble!=3&&format->valuedouble!=4)))goto done;
        unsigned version=format?(unsigned)format->valueint:1;
        if(valid_profile(p,version)) {
            char *json=cJSON_PrintUnformatted(p);
            if(json) {e=profile_store_begin_format(badge,json,strlen(json),version,cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(p,"qrPresent")));free(json);transferring=e==ESP_OK;if(transferring){strcpy(owner,client);last_activity=esp_timer_get_time();}}
        }
    } else if(!strcmp(op->valuestring,"chunk")) {
        cJSON *s=cJSON_GetObjectItemCaseSensitive(q,"data");size_t offset,n=0;
        if(transferring&&cJSON_IsString(s)&&strlen(s->valuestring)<=1024&&number(q,"offset",PROFILE_V2_BYTES,&offset)&&
           mbedtls_base64_decode(binary,sizeof(binary),&n,(const unsigned char *)s->valuestring,strlen(s->valuestring))==0&&n>0) {
            e=profile_store_append(offset,binary,n);if(e==ESP_OK)cJSON_AddNumberToObject(reply,"offset",offset+n);
        }
    } else if(!strcmp(op->valuestring,"commit")) {
        if(transferring) {e=profile_store_commit();transferring=false;}
        if(e==ESP_OK)cJSON_AddNumberToObject(reply,"revision",profile_store_revision());
    } else if(!strcmp(op->valuestring,"abort")) {
        profile_store_abort();transferring=false;e=ESP_OK;
    } else if(!strcmp(op->valuestring,"clear")) {
        profile_store_abort();transferring=false;e=profile_store_clear_for(badge);
    } else if(!strcmp(op->valuestring,"badges")) {
        catalog(reply);cJSON_AddNumberToObject(reply,"revision",profile_store_revision());e=ESP_OK;
    } else if(!strcmp(op->valuestring,"badge_select")) {
        if(!cJSON_GetObjectItemCaseSensitive(q,"badge"))goto done;
        e=select_badge(badge);if(e==ESP_OK)catalog(reply);
    } else if(!strcmp(op->valuestring,"wifi_info")) {
        badge_network_json(reply);yao_location_t place=yao_location_get();cJSON_AddItemToObject(reply,"yaoTime",yao_time_json(yao_time_now(),&place));cJSON_AddStringToObject(reply,"yaoLocationStatus",yao_location_status());e=ESP_OK;
    } else if(!strcmp(op->valuestring,"xiaozhi_backend")) {
        char backend[256];bool backend_custom=false;
        e=demo_xiaozhi_get_backend(backend,sizeof(backend),&backend_custom);
        if(e==ESP_OK){cJSON_AddStringToObject(reply,"backendUrl",backend);cJSON_AddBoolToObject(reply,"backendCustom",backend_custom);}
    } else if(!strcmp(op->valuestring,"xiaozhi_backend_save")) {
        cJSON *url=cJSON_GetObjectItemCaseSensitive(q,"url");
        if(cJSON_IsString(url))e=demo_xiaozhi_save_backend(url->valuestring);
    } else if(!strcmp(op->valuestring,"muse_config")) {
        muse_config_json(reply);e=ESP_OK;
    } else if(!strcmp(op->valuestring,"muse_config_save")) {
        cJSON *token=cJSON_GetObjectItemCaseSensitive(q,"token");
        cJSON *host=cJSON_GetObjectItemCaseSensitive(q,"proxyHost");size_t port=0,style=0;
        if(cJSON_IsString(token)&&cJSON_IsString(host)&&number(q,"proxyPort",65535,&port)&&number(q,"style",MUSE_STYLE_COUNT-1,&style)){
            e=passport_muse_save_config(token->valuestring,host->valuestring,(uint16_t)port);
            if(e==ESP_OK&&!muse_style_set((unsigned)style))e=ESP_FAIL;
            if(e==ESP_OK)muse_config_json(reply);
        }
    } else if(!strcmp(op->valuestring,"wifi_scan")) {
        e=badge_network_scan();if(e==ESP_OK)badge_network_scan_json(reply);
    } else if(!strcmp(op->valuestring,"wifi_scan_results")) {
        badge_network_scan_json(reply);e=ESP_OK;
    } else if(!strcmp(op->valuestring,"wifi_save")) {
        cJSON *ssid=cJSON_GetObjectItemCaseSensitive(q,"ssid"),*password=cJSON_GetObjectItemCaseSensitive(q,"password");
        if(cJSON_IsString(ssid)&&cJSON_IsString(password))e=badge_network_save(ssid->valuestring,password->valuestring,
            cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(q,"open")),cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(q,"keepPassword")));
    } else if(!strcmp(op->valuestring,"wifi_delete")) {
        cJSON *ssid=cJSON_GetObjectItemCaseSensitive(q,"ssid");
        if(cJSON_IsString(ssid))e=badge_network_delete(ssid->valuestring);
    } else if(!strcmp(op->valuestring,"hotspot")) {
        if(usb)e=badge_network_toggle();
    } else if(!strcmp(op->valuestring,"read")) {
        cJSON *asset=cJSON_GetObjectItemCaseSensitive(q,"asset");size_t offset,count,n;
        const char *names[]={"card","avatar","brand","logo","qr"};int index=-1;
        if(cJSON_IsString(asset))for(int i=0;i<5;i++)if(!strcmp(asset->valuestring,names[i]))index=i;
        if(index>=0&&number(q,"offset",PROFILE_CARD_BYTES,&offset)&&number(q,"count",sizeof(binary),&count)&&count) {
            e=profile_store_read_for(badge,(profile_asset_t)index,offset,binary,count);
            if(e==ESP_OK&&mbedtls_base64_encode((unsigned char *)encoded,sizeof(encoded),&n,binary,count)==0) {
                encoded[n]=0;cJSON_AddStringToObject(reply,"data",encoded);
            } else e=ESP_ERR_INVALID_ARG;
        }
    }
 done:
    cJSON_AddNumberToObject(reply,"id",request_id);cJSON_AddBoolToObject(reply,"ok",e==ESP_OK);
    if(e!=ESP_OK)cJSON_AddStringToObject(reply,"error",esp_err_to_name(e));
    if(reason)cJSON_AddStringToObject(reply,"reason",reason);
    char *out=cJSON_PrintUnformatted(reply);cJSON_Delete(reply);cJSON_Delete(q);xSemaphoreGive(mutex);return out;
}
