#include "profile_protocol.h"
#include "profile_store.h"
#include "profile_edit.h"
#include "badge_power.h"
#include "badge_network.h"
#include "xiaozhi_app.h"
#include "passport_muse.h"
#include "muse_style.h"
#include "yao_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "nvs.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
static SemaphoreHandle_t mutex;
static uint8_t binary[768];
static char encoded[1025],owner[40];
static bool transferring;
static int64_t last_activity;
esp_err_t profile_protocol_init(void) {
    yao_location_init();
    mutex=xSemaphoreCreateMutex();if(!mutex)return ESP_ERR_NO_MEM;
    nvs_handle_t n;uint8_t id=0;
    if(nvs_open("badge_cards",NVS_READONLY,&n)==ESP_OK){nvs_get_u8(n,"active",&id);nvs_close(n);}
    if(id<profile_store_count())profile_store_select(id);
    return ESP_OK;
}
bool profile_protocol_lock(void) {return mutex&&xSemaphoreTake(mutex,pdMS_TO_TICKS(5000))==pdTRUE;}
void profile_protocol_unlock(void) {xSemaphoreGive(mutex);}
static esp_err_t select_badge(unsigned id) {
    if(id>=profile_store_count())return ESP_ERR_INVALID_ARG;
    if(transferring||!profile_store_ready())return ESP_ERR_INVALID_STATE;
    if(id==profile_store_badge())return ESP_OK;
    nvs_handle_t n;esp_err_t e=nvs_open("badge_cards",NVS_READWRITE,&n);
    if(e==ESP_OK){e=nvs_set_u8(n,"active",id);if(e==ESP_OK)e=nvs_commit(n);nvs_close(n);}
    return e==ESP_OK?profile_store_select(id):e;
}
static void expire(void) {
    if(transferring&&esp_timer_get_time()-last_activity>15000000) {profile_store_abort();transferring=false;}
}
esp_err_t profile_protocol_select(unsigned id) {
    if(!profile_protocol_lock())return ESP_ERR_INVALID_STATE;
    expire();esp_err_t e=select_badge(id);profile_protocol_unlock();return e;
}
esp_err_t profile_protocol_edit(unsigned id,unsigned revision,unsigned field,const char *text){
    if(!profile_protocol_lock())return ESP_ERR_INVALID_STATE;
    expire();
    esp_err_t e=transferring?ESP_ERR_INVALID_STATE:profile_edit_locked(id,revision,field,text);
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
#include <stdio.h>
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
    bool read_only=!strcmp(op->valuestring,"info")||!strcmp(op->valuestring,"read")||!strcmp(op->valuestring,"wifi_info")||!strcmp(op->valuestring,"wifi_scan_results")||!strcmp(op->valuestring,"badges")||!strcmp(op->valuestring,"xiaozhi_backend")||!strcmp(op->valuestring,"muse_config");
    if(!read_only)badge_power_activity();
    if(!read_only && transferring && strcmp(owner,client)) {e=ESP_ERR_INVALID_STATE;reason="upload_busy";goto done;}
    if(!strcmp(owner,client))last_activity=esp_timer_get_time();
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
    if(!strcmp(op->valuestring,"info")) {
        yao_location_t place=yao_location_get();
        cJSON_AddItemToObject(reply,"yaoTime",yao_time_json(yao_time_now(),&place));
        cJSON_AddStringToObject(reply,"yaoLocationStatus",yao_location_status());
        unsigned version=profile_store_version_for(badge);
        cJSON_AddNumberToObject(reply,"protocol",2);
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
