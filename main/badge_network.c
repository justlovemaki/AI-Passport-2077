#ifdef BADGE_NETWORK_HOST_TEST
#include "badge_network_test_stubs.h"
#else
#include "badge_network.h"
#include "yao_time.h"
#include "profile_protocol.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#endif
#include "badge_network_rules.h"
#include <stdatomic.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
static SemaphoreHandle_t mutex;
static QueueHandle_t commands;
static badge_network_status_t state;
static struct {char ssid[33],password[65];} saved;
static badge_wifi_store_t wifi_store;
static bool ready,started,attempted;
static atomic_uint rf_started_ms;
static atomic_bool rf_started;
static atomic_bool shutdown_requested,shutdown_done;
static httpd_handle_t server;
static nvs_handle_t nvs;
static bool nvs_ready;
static atomic_bool disconnected;
/* Coalesce key press/click probes, including a probe already being processed. */
static atomic_bool wake_probe_pending;
static atomic_uint last_http;
static char web_token[33];
static char setup_password[13];
static unsigned retries;
static int64_t retry_at;
/* Worker-owned: navigation/wake events must not cancel a fresh STA attempt. */
static int64_t probe_at;
#define CONNECT_PROBE_GRACE_US 15000000LL
enum {CMD_CONNECT=1,CMD_TOGGLE,CMD_SCAN,CMD_START_SETUP,CMD_POWER,CMD_ENTROPY,CMD_CLOSE_AP,CMD_WAKE_PROBE,CMD_AUTOCONNECT};
static atomic_bool onboarding;
static atomic_int desired_ps;
static atomic_bool standby_ps;
static int applied_ps=-1;
static bool scanning;
static unsigned scan_revision;
static esp_err_t scan_error;
static size_t scan_count;
static badge_scan_entry_t scan_results[BADGE_SCAN_LIMIT];
static void scan_nearby(void);
static void lock(void){xSemaphoreTake(mutex,portMAX_DELAY);}
static void unlock(void){xSemaphoreGive(mutex);}
static bool valid_setup_password(const char *password){
    if(!password||strlen(password)!=12)return false;
    for(unsigned i=0;i<12;i++)if(!isxdigit((unsigned char)password[i]))return false;
    return true;
}
static void message(const char *s){lock();snprintf(state.message,sizeof(state.message),"%s",s);unlock();}
void badge_network_status(badge_network_status_t *out){if(!mutex){memset(out,0,sizeof(*out));return;}lock();*out=state;unlock();}
void badge_network_json(cJSON *reply){
    cJSON *w=cJSON_CreateObject();if(!w)return;
    badge_network_status_t s;badge_network_status(&s);
    cJSON_AddBoolToObject(w,"connected",s.connected);cJSON_AddBoolToObject(w,"apActive",s.active);
    wifi_ps_type_t ps;
    if(esp_wifi_get_ps(&ps)==ESP_OK)cJSON_AddStringToObject(w,"powerSave",ps==WIFI_PS_MAX_MODEM?"MAX_MODEM":ps==WIFI_PS_NONE?"NONE":"MIN_MODEM");
    cJSON_AddStringToObject(w,"ssid",s.ssid);cJSON_AddStringToObject(w,"ip",s.ip);
    cJSON_AddStringToObject(w,"apSsid",s.ap_ssid);cJSON_AddStringToObject(w,"apPassword",s.active?s.ap_password:"");
    cJSON_AddStringToObject(w,"message",s.message);
    bool password=false;if(mutex){lock();password=saved.password[0]!=0;unlock();}
    cJSON_AddBoolToObject(w,"passwordSet",password);
    cJSON *known_list=cJSON_CreateArray();
    if(known_list){
        lock();
        for(uint8_t i=0;i<wifi_store.count;i++){
            cJSON *item=cJSON_CreateObject();
            if(item){
                cJSON_AddStringToObject(item,"ssid",wifi_store.items[i].ssid);
                cJSON_AddBoolToObject(item,"hasPassword",wifi_store.items[i].password[0]!=0);
                cJSON_AddNumberToObject(item,"lastUsed",wifi_store.items[i].last_used);
                cJSON_AddItemToArray(known_list,item);
            }
        }
        unlock();
        cJSON_AddItemToObject(w,"knownWifis",known_list);
    }
    cJSON_AddItemToObject(reply,"wifi",w);
}
static void event(void *arg,esp_event_base_t base,int32_t id,void *data){
    if(atomic_load(&shutdown_requested))return;
    (void)arg;
    if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP){
        yao_location_network(true);
        ip_event_got_ip_t *e=data;lock();state.connected=true;
        snprintf(state.ip,sizeof(state.ip),IPSTR,IP2STR(&e->ip_info.ip));
        snprintf(state.message,sizeof(state.message),"已连接");unlock();atomic_store(&disconnected,false);
        uint32_t now_sec = (uint32_t)(esp_timer_get_time() / 1000000);
        lock();
        if(badge_wifi_store_touch(&wifi_store, saved.ssid, now_sec) && nvs_ready) {
            nvs_set_blob(nvs, "known_wifis", &wifi_store, sizeof(wifi_store));
            nvs_commit(nvs);
        }
        unlock();
        /* The local UTC clock keeps running offline. Every obtained IP starts
         * asynchronous calibration; never clear an existing time while waiting.
         * Tracked defaults disable SNTP's randomized startup delay. */
        if(!esp_sntp_enabled()){
            esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
            esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
            esp_sntp_setservername(0,"ntp.aliyun.com");
            esp_sntp_init();
        }else esp_sntp_restart();
    }else if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED){
        yao_location_network(false);
        wifi_event_sta_disconnected_t *dis=(wifi_event_sta_disconnected_t *)data;
        const char *reason_str="连接断开";
        if(dis){
            switch(dis->reason){
                case WIFI_REASON_AUTH_EXPIRE:
                case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
                case WIFI_REASON_AUTH_FAIL:
                case WIFI_REASON_HANDSHAKE_TIMEOUT:reason_str="密码错误或认证失败";break;
                case WIFI_REASON_NO_AP_FOUND:reason_str="未找到该Wi-Fi信号";break;
                case WIFI_REASON_ASSOC_FAIL:
                case WIFI_REASON_ASSOC_EXPIRE:reason_str="路由器拒绝关联";break;
                default:reason_str="Wi-Fi连接断开";break;
            }
        }
        lock();state.connected=false;state.ip[0]=0;snprintf(state.message,sizeof(state.message),"%s",reason_str);unlock();
        atomic_store(&disconnected,true);
    }
}
static esp_err_t prepare(void){
    if(ready)return ESP_OK;
    if(attempted)return ESP_ERR_INVALID_STATE;
    attempted=true;
    esp_err_t e=esp_netif_init();if(e!=ESP_OK)return e;
    e=esp_event_loop_create_default();if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return e;
    if(!esp_netif_create_default_wifi_sta()||!esp_netif_create_default_wifi_ap())return ESP_ERR_NO_MEM;
    wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();
    e=esp_wifi_init(&config);if(e!=ESP_OK)return e;
    e=esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,event,NULL);if(e!=ESP_OK)return e;
    e=esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,NULL);if(e!=ESP_OK)return e;
    e=esp_wifi_set_storage(WIFI_STORAGE_RAM);if(e!=ESP_OK)return e;
    e=esp_wifi_set_mode(WIFI_MODE_STA);if(e!=ESP_OK)return e;
    ready=true;return ESP_OK;
}
static esp_err_t start(void){
    esp_err_t e=prepare();if(e!=ESP_OK)return e;
    if(!started){e=esp_wifi_start();if(e==ESP_OK){started=true;atomic_store(&rf_started_ms,(unsigned)(esp_timer_get_time()/1000));atomic_store(&rf_started,true);}}return e;
}
static void connect_saved(void){
    yao_location_network(false);
    wifi_config_t c={0};
    c.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
    c.sta.listen_interval=20;
    lock();memcpy(c.sta.ssid,saved.ssid,strlen(saved.ssid));
    memcpy(c.sta.password,saved.password,strlen(saved.password));
    strcpy(state.ssid,saved.ssid);unlock();
    retries=0;atomic_store(&disconnected,false);
    if(!c.sta.ssid[0])return;
    esp_err_t e=start();
    if(e==ESP_OK){
        /* Ensure operating mode allows STA connection */
        wifi_mode_t current_mode;
        if(esp_wifi_get_mode(&current_mode)==ESP_OK && current_mode==WIFI_MODE_AP){
            esp_wifi_set_mode(WIFI_MODE_APSTA);
        }
        esp_wifi_disconnect();
        e=esp_wifi_set_config(WIFI_IF_STA,&c);
    }
    probe_at=esp_timer_get_time()+CONNECT_PROBE_GRACE_US;
    if(e==ESP_OK)e=esp_wifi_connect();
    message(e==ESP_OK?"正在连接...":"连接设置失败");retry_at=probe_at;
}
static void auto_roam_and_connect(void){
    if(wifi_store.count==0)return;
    probe_at=esp_timer_get_time()+CONNECT_PROBE_GRACE_US;
    ESP_LOGI("badge_network","Auto-roam check triggered (known count=%u)...", (unsigned)wifi_store.count);
    message("正在搜寻已知 Wi-Fi...");
    
    /* 1. First try: Scan the surrounding air */
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(50));
    scan_nearby();
    lock();
    int best = badge_wifi_match_best(&wifi_store, scan_results, scan_count);
    if(best >= 0 && best < (int)wifi_store.count) {
        ESP_LOGI("badge_network","Auto-roam selected known Wi-Fi: %s (matched from %u scanned APs)",
                 wifi_store.items[best].ssid, (unsigned)scan_count);
        memset(&saved, 0, sizeof(saved));
        strncpy(saved.ssid, wifi_store.items[best].ssid, sizeof(saved.ssid) - 1);
        strncpy(saved.password, wifi_store.items[best].password, sizeof(saved.password) - 1);
        if(nvs_ready) {
            nvs_set_blob(nvs, "station", &saved, sizeof(saved));
            nvs_commit(nvs);
        }
        unlock();
        connect_saved();
        return;
    }
    
    /* 2. Second try (Blind fallback): If scan didn't see the SSID (e.g. hidden SSID, router beacon slow,
          or active SoftAP interfering with channel scan), cycle directly to the NEXT known Wi-Fi! */
    ESP_LOGW("badge_network","No scanned match; cycling sequentially to next saved AP");
    uint8_t cur_idx = 0;
    for(uint8_t i = 0; i < wifi_store.count; i++) {
        if(strcmp(wifi_store.items[i].ssid, saved.ssid) == 0) {
            cur_idx = i;
            break;
        }
    }
    uint8_t next_idx = (cur_idx + 1) % wifi_store.count;
    ESP_LOGI("badge_network","Roaming fallback -> trying next saved AP: %s", wifi_store.items[next_idx].ssid);
    memset(&saved, 0, sizeof(saved));
    strncpy(saved.ssid, wifi_store.items[next_idx].ssid, sizeof(saved.ssid) - 1);
    strncpy(saved.password, wifi_store.items[next_idx].password, sizeof(saved.password) - 1);
    if(nvs_ready) {
        nvs_set_blob(nvs, "station", &saved, sizeof(saved));
        nvs_commit(nvs);
    }
    unlock();
    connect_saved();
}
/* The setup server is reachable only on the AP interface, never the joined LAN. */
static bool allowed(httpd_req_t *r){
    badge_network_status_t s;badge_network_status(&s);if(!s.active)return false;
    struct sockaddr_storage addr;socklen_t n=sizeof(addr);
    if(getsockname(httpd_req_to_sockfd(r),(struct sockaddr *)&addr,&n)!=0)return false;
    bool local=false;
    if(addr.ss_family==AF_INET)local=badge_address_is_ap((const uint8_t *)&((struct sockaddr_in *)&addr)->sin_addr,4);
    else if(addr.ss_family==AF_INET6)local=badge_address_is_ap((const uint8_t *)&((struct sockaddr_in6 *)&addr)->sin6_addr,16);
    if(!local)return false;
    char origin[96];size_t length=httpd_req_get_hdr_value_len(r,"Origin");
    if(length&&(length>=sizeof(origin)||httpd_req_get_hdr_value_str(r,"Origin",origin,sizeof(origin))!=ESP_OK||strcmp(origin,"http://192.168.4.1")))return false;
    atomic_store(&last_http,(unsigned)(esp_timer_get_time()/1000000));return true;
}
static esp_err_t reject(httpd_req_t *r){return httpd_resp_send_err(r,HTTPD_403_FORBIDDEN,"Open setup from the badge hotspot");}
extern const uint8_t page_start[] asm("_binary_configure_html_gz_start");
extern const uint8_t page_end[] asm("_binary_configure_html_gz_end");
static esp_err_t page(httpd_req_t *r){
    if(!allowed(r))return reject(r);
    httpd_resp_set_type(r,"text/html; charset=utf-8");httpd_resp_set_hdr(r,"Content-Encoding","gzip");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    for(const uint8_t *p=page_start;p<page_end;){size_t n=page_end-p;if(n>2048)n=2048;esp_err_t e=httpd_resp_send_chunk(r,(const char *)p,n);if(e!=ESP_OK)return e;p+=n;}
    return httpd_resp_send_chunk(r,NULL,0);
}
static esp_err_t session(httpd_req_t *r){
    if(!allowed(r))return reject(r);
    char json[64];snprintf(json,sizeof(json),"{\"token\":\"%s\"}",web_token);
    httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");
    return httpd_resp_sendstr(r,json);
}
static esp_err_t api(httpd_req_t *r){
    if(!allowed(r))return reject(r);
    char token[40];if(httpd_req_get_hdr_value_str(r,"X-Badge-Token",token,sizeof(token))!=ESP_OK||strcmp(token,web_token))return reject(r);
    if(r->content_len<=0||r->content_len>=4096)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid request size");
    char *body=malloc(r->content_len+1);if(!body)return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"No memory");
    size_t used=0;while(used<r->content_len){int n=httpd_req_recv(r,body+used,r->content_len-used);if(n<=0){free(body);return ESP_FAIL;}used+=n;}
    body[used]=0;char *reply=profile_protocol_request(body,false);memset(body,0,used);free(body);
    if(!reply)return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"Busy");
    httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");
    esp_err_t e=httpd_resp_sendstr(r,reply);free(reply);return e;
}
static esp_err_t open_server(void){
    httpd_config_t c=HTTPD_DEFAULT_CONFIG();c.stack_size=6144;c.max_open_sockets=3;c.lru_purge_enable=true;c.recv_wait_timeout=5;c.send_wait_timeout=5;
    esp_err_t e=httpd_start(&server,&c);if(e!=ESP_OK)return e;
    const httpd_uri_t paths[]={ {.uri="/",.method=HTTP_GET,.handler=page},
        {.uri="/session",.method=HTTP_GET,.handler=session},{.uri="/api",.method=HTTP_POST,.handler=api}};
    for(unsigned i=0;i<3;i++){e=httpd_register_uri_handler(server,&paths[i]);if(e!=ESP_OK){httpd_stop(server);server=NULL;return e;}}
    return ESP_OK;
}
static void close_ap(void){
    bool was_active=false;
    lock();was_active=state.active;state.active=false;state.ap_password[0]=0;unlock();
    if(server){httpd_stop(server);server=NULL;}
    if(ready)esp_wifi_set_mode(WIFI_MODE_STA);
    message("热点已关闭");
    if(was_active)ESP_LOGI("badge_network","SoftAP closed (automatic/user action)");
}
static void toggle_ap(void){
    badge_network_status_t s;badge_network_status(&s);if(s.active){close_ap();return;}
    esp_err_t e=start();if(e!=ESP_OK){message("Wi-Fi不可用");return;}
    uint32_t random[4];esp_fill_random(random,sizeof(random));
    if(!valid_setup_password(setup_password)){
        snprintf(setup_password,sizeof(setup_password),"%08lX%04lX",(unsigned long)random[0],(unsigned long)(random[1]&65535));
        if(nvs_ready&&nvs_set_str(nvs,"ap_password",setup_password)==ESP_OK)nvs_commit(nvs);
    }
    snprintf(web_token,sizeof(web_token),"%08lX%08lX%08lX%08lX",(unsigned long)random[0],(unsigned long)random[1],(unsigned long)random[2],(unsigned long)random[3]);
    wifi_config_t config={0};memcpy(config.ap.ssid,s.ap_ssid,strlen(s.ap_ssid));config.ap.ssid_len=strlen(s.ap_ssid);
    memcpy(config.ap.password,setup_password,12);config.ap.channel=1;config.ap.max_connection=2;config.ap.authmode=WIFI_AUTH_WPA2_PSK;
    /* esp_wifi_set_config can reject AP configuration while STA is connecting.
     * Offline setup must cancel that attempt, not wait for a router to appear. */
    if(!s.connected)(void)esp_wifi_disconnect();
    e=esp_wifi_set_mode(WIFI_MODE_APSTA);if(e==ESP_OK)e=esp_wifi_set_config(WIFI_IF_AP,&config);
    if(e==ESP_OK){lock();state.active=true;strcpy(state.ap_password,setup_password);unlock();atomic_store(&last_http,(unsigned)(esp_timer_get_time()/1000000));e=open_server();}
    if(e!=ESP_OK){ESP_LOGW("badge_network","SoftAP start failed: %s",esp_err_to_name(e));close_ap();message("热点开启失败");}else message("热点已就绪");
}
static void scan_nearby(void){
    badge_scan_entry_t results[BADGE_SCAN_LIMIT];size_t count=0;
    esp_err_t e=start();
    if(e==ESP_OK){
        wifi_scan_config_t config={.show_hidden=false,.scan_type=WIFI_SCAN_TYPE_ACTIVE};
        config.scan_time.active.min=40;config.scan_time.active.max=120;
        /* Only the network worker blocks; UI/USB/HTTP remain responsive. */
        e=esp_wifi_scan_start(&config,true);
        if(e==ESP_OK){
            uint16_t total=0;e=esp_wifi_scan_get_ap_num(&total);
            if(e==ESP_OK&&total){
                if(total>64)total=64;
                wifi_ap_record_t *records=calloc(total,sizeof(*records));
                if(!records)e=ESP_ERR_NO_MEM;
                else {
                    e=esp_wifi_scan_get_ap_records(&total,records);
                    for(unsigned i=0;e==ESP_OK&&i<total;i++){
                        badge_scan_entry_t entry={.rssi=records[i].rssi,.auth=records[i].authmode};
                        memcpy(entry.ssid,records[i].ssid,32);
                        if(strcmp(entry.ssid,state.ap_ssid))badge_scan_insert(results,&count,&entry);
                    }
                    free(records);
                }
            }
            esp_wifi_clear_ap_list();
        }
    }
    lock();scan_error=e;if(e==ESP_OK){memcpy(scan_results,results,count*sizeof(*results));scan_count=count;}
    scanning=false;scan_revision++;unlock();
}
esp_err_t badge_network_scan(void){
    if(!mutex||!commands)return ESP_ERR_INVALID_STATE;
    lock();if(scanning){unlock();return ESP_OK;}scanning=true;scan_error=ESP_OK;unlock();
    int command=CMD_SCAN;
    if(xQueueSend(commands,&command,0)!=pdTRUE){lock();scanning=false;scan_error=ESP_ERR_INVALID_STATE;unlock();return ESP_ERR_INVALID_STATE;}
    return ESP_OK;
}
void badge_network_scan_json(cJSON *reply){
    cJSON *list=cJSON_CreateArray();if(!list)return;
    lock();cJSON_AddBoolToObject(reply,"scanning",scanning);cJSON_AddNumberToObject(reply,"scanRevision",scan_revision);
    cJSON_AddStringToObject(reply,"scanError",scan_error==ESP_OK?"":esp_err_to_name(scan_error));
    for(size_t i=0;i<scan_count;i++){
        const badge_scan_entry_t *ap=&scan_results[i];const char *security="Other";bool supported=true;
        switch(ap->auth){
            case WIFI_AUTH_OPEN:security="OPEN";break;
            case WIFI_AUTH_WPA2_PSK:security="WPA2";break;
            case WIFI_AUTH_WPA_WPA2_PSK:security="WPA/WPA2";break;
            case WIFI_AUTH_WPA3_PSK:security="WPA3";break;
            case WIFI_AUTH_WPA2_WPA3_PSK:security="WPA2/WPA3";break;
            case WIFI_AUTH_ENTERPRISE:security="Enterprise";supported=false;break;
            case WIFI_AUTH_WEP:security="WEP";supported=false;break;
            case WIFI_AUTH_WPA_PSK:security="WPA";supported=false;break;
            default:supported=false;break;
        }
        cJSON *item=cJSON_CreateObject();if(!item)continue;
        cJSON_AddStringToObject(item,"ssid",ap->ssid);cJSON_AddNumberToObject(item,"rssi",ap->rssi);
        cJSON_AddStringToObject(item,"security",security);cJSON_AddBoolToObject(item,"open",ap->auth==WIFI_AUTH_OPEN);
        cJSON_AddBoolToObject(item,"supported",supported);
        bool is_known = badge_wifi_store_find(&wifi_store, ap->ssid) != NULL;
        cJSON_AddBoolToObject(item,"saved",is_known);
        cJSON_AddItemToArray(list,item);
    }
    unlock();cJSON_AddItemToObject(reply,"networks",list);
}
static void worker(void *arg){
    (void)arg;int command;bool setup_seen=false;
    for(;;){
        if(atomic_load(&shutdown_requested))break;
        if(xQueueReceive(commands,&command,pdMS_TO_TICKS(1000))==pdTRUE){
            if(atomic_load(&shutdown_requested))break;
            if(command==CMD_CONNECT)connect_saved();else if(command==CMD_SCAN)scan_nearby();
            else if(command==CMD_AUTOCONNECT){badge_network_status_t current;badge_network_status(&current);if(!current.active&&!current.connected&&!probe_at)connect_saved();}
            else if(command==CMD_START_SETUP){badge_network_status_t current;badge_network_status(&current);if(!current.active)toggle_ap();}
            else if(command==CMD_CLOSE_AP)close_ap();
            else if(command==CMD_TOGGLE)toggle_ap();
            else if(command==CMD_ENTROPY)(void)start();
            else if(command==CMD_WAKE_PROBE){
                badge_network_status_t cur;badge_network_status(&cur);
                /* Power policy selects modem sleep, not which page may connect.
                 * Press/click/wake bursts share the current attempt's grace. */
                if(!cur.connected && !cur.active && wifi_store.count > 0 && esp_timer_get_time()>=probe_at){
                    retries = 0; /* Reset retry counter so new attempts begin immediately */
                    auto_roam_and_connect();
                    retry_at = esp_timer_get_time() + 15000000LL;
                }
                atomic_store(&wake_probe_pending,false);
            }
        }
        int policy=badge_network_power_policy(atomic_load(&desired_ps),atomic_load(&standby_ps));
        if(started&&policy!=applied_ps){
            wifi_ps_type_t mode=policy==1?WIFI_PS_MAX_MODEM:policy==2?WIFI_PS_NONE:WIFI_PS_MIN_MODEM;
            if(esp_wifi_set_ps(mode)==ESP_OK){applied_ps=policy;ESP_LOGI("badge_network","Power policy: %s",policy==1?"MAX_MODEM":policy==2?"NONE":"MIN_MODEM");}
        }
        badge_network_status_t s;badge_network_status(&s);
        if(s.connected&&!atomic_load(&shutdown_requested))yao_location_poll();
        unsigned now=(unsigned)(esp_timer_get_time()/1000000);
        badge_setup_action_t setup=badge_setup_tick(&setup_seen,atomic_load(&onboarding),s.active,now-atomic_load(&last_http));
        if(setup==BADGE_SETUP_START)toggle_ap();
        if(setup==BADGE_SETUP_EXTEND)atomic_store(&last_http,now);
        if(setup==BADGE_SETUP_CLOSE)close_ap();
        /* Keep the setup channel stable. Explicit CMD_SCAN/CMD_CONNECT from
         * the configuration page still work; background retries/roaming wait. */
        badge_network_status(&s);
        if(atomic_load(&disconnected)&&s.ssid[0]&&!s.connected&&!s.active&&esp_timer_get_time()>retry_at){
            retries++;
            /* If we have multiple saved Wi-Fis:
               Retry the current network 3 times with progressive delays:
               - Attempt 1: 15 seconds
               - Attempt 2: 35 seconds
               - Attempt 3: 60 seconds
               Only on attempt 4 and beyond, switch/roam to other known Wi-Fis! */
            if(wifi_store.count > 1 && retries > 3){
                /* After rotating through all known networks (e.g. 3 attempts on saved AP + 1 roam round per other known AP),
                   if still not connected, enter full silent sleep (stop periodic background scans to save battery).
                   Pressing any key or waking display will instantly wake and probe! */
                uint32_t max_commute_attempts = 3 + (uint32_t)wifi_store.count;
                if(retries > max_commute_attempts) {
                    message("离线（按键重连）");
                    /* Set retry_at far into future so it doesn't spin; wake_probe will reset it on keypress */
                    retry_at = esp_timer_get_time() + 86400000000LL; /* 24 hours */
                } else {
                    auto_roam_and_connect();
                    retry_at = esp_timer_get_time() + 15000000LL;
                }
            } else {
                probe_at=esp_timer_get_time()+CONNECT_PROBE_GRACE_US;
                esp_wifi_connect();
                message("正在重新连接...");
                int64_t delay_us = (retries == 1) ? 15000000LL :
                                   (retries == 2) ? 35000000LL : 60000000LL;
                retry_at = esp_timer_get_time() + delay_us;
            }
        }
    }
    yao_location_network(false);
    close_ap();if(esp_sntp_enabled())esp_sntp_stop();if(started)esp_wifi_stop();
    atomic_store(&shutdown_done,true);vTaskDelete(NULL);
}
esp_err_t badge_network_shutdown(void){
    if(!commands)return ESP_OK;
    atomic_store(&shutdown_requested,true);int cmd=CMD_POWER;(void)xQueueSendToFront(commands,&cmd,0);
    int64_t until=esp_timer_get_time()+12000000;while(!atomic_load(&shutdown_done)&&esp_timer_get_time()<until)vTaskDelay(pdMS_TO_TICKS(20));
    return atomic_load(&shutdown_done)?ESP_OK:ESP_ERR_TIMEOUT;
}
esp_err_t badge_network_toggle(void){int cmd=CMD_TOGGLE;return commands&&!atomic_load(&shutdown_requested)&&xQueueSendToFront(commands,&cmd,0)==pdTRUE?ESP_OK:ESP_ERR_INVALID_STATE;}
bool badge_network_entropy_ready(void){return atomic_load(&rf_started)&&(unsigned)((unsigned)(esp_timer_get_time()/1000)-atomic_load(&rf_started_ms))>=100;}
esp_err_t badge_network_prepare_entropy(void){if(badge_network_entropy_ready())return ESP_OK;int cmd=CMD_ENTROPY;return commands&&xQueueSend(commands,&cmd,0)==pdTRUE?ESP_OK:ESP_ERR_INVALID_STATE;}
esp_err_t badge_network_start_setup(void){int cmd=CMD_START_SETUP;return commands&&!atomic_load(&shutdown_requested)&&xQueueSendToFront(commands,&cmd,0)==pdTRUE?ESP_OK:ESP_ERR_INVALID_STATE;}
esp_err_t badge_network_close_ap(void){int cmd=CMD_CLOSE_AP;return commands&&!atomic_load(&shutdown_requested)&&xQueueSend(commands,&cmd,0)==pdTRUE?ESP_OK:ESP_ERR_INVALID_STATE;}
esp_err_t badge_network_wake_probe(void){
    if(!commands||atomic_load(&shutdown_requested))return ESP_ERR_INVALID_STATE;
    if(atomic_exchange(&wake_probe_pending,true))return ESP_OK;
    int cmd=CMD_WAKE_PROBE;
    if(xQueueSend(commands,&cmd,0)==pdTRUE)return ESP_OK;
    atomic_store(&wake_probe_pending,false);
    return ESP_ERR_INVALID_STATE;
}
void badge_network_xiaozhi_power(bool active,bool busy){
    int desired=active?(busy?2:1):0;
    if(atomic_exchange(&desired_ps,desired)!=desired&&commands){int cmd=CMD_POWER;(void)xQueueSend(commands,&cmd,0);}
}
void badge_network_standby(bool asleep){
    if(atomic_exchange(&standby_ps,asleep)!=asleep&&commands){int cmd=CMD_POWER;(void)xQueueSend(commands,&cmd,0);}
}
void badge_network_onboarding(bool enabled){atomic_store(&onboarding,enabled);}
esp_err_t badge_network_save(const char *ssid,const char *password,bool open,bool keep){
    size_t sn=strlen(ssid),pn=strlen(password);
    if(!sn||sn>32||pn>63||(!open&&!keep&&pn<8))return ESP_ERR_INVALID_ARG;
    if(!mutex||!commands||!nvs_ready)return ESP_ERR_INVALID_STATE;
    lock();
    const char *pwd_to_use = password;
    if(keep){
        if(!strcmp(ssid,saved.ssid) && saved.password[0]){
            pwd_to_use = saved.password;
        } else {
            const badge_known_wifi_t *k = badge_wifi_store_find(&wifi_store, ssid);
            if(k && k->password[0]) {
                pwd_to_use = k->password;
            } else {
                unlock(); return ESP_ERR_INVALID_ARG;
            }
        }
    }
    typeof(saved) next={0};strcpy(next.ssid,ssid);
    if(!open)strcpy(next.password,pwd_to_use);
    esp_err_t e=nvs_set_blob(nvs,"station",&next,sizeof(next));
    if(e==ESP_OK){
        uint32_t now_sec = (uint32_t)(esp_timer_get_time() / 1000000);
        badge_wifi_store_upsert(&wifi_store, ssid, open?"":pwd_to_use, now_sec);
        nvs_set_blob(nvs, "known_wifis", &wifi_store, sizeof(wifi_store));
        e=nvs_commit(nvs);
    }
    if(e==ESP_OK){saved=next;strcpy(state.ssid,ssid);state.connected=false;state.ip[0]=0;strcpy(state.message,"已保存，正在连接...");}
    memset(&next,0,sizeof(next));unlock();
    if(e==ESP_OK){
        /* Upon saving a new Wi-Fi, immediately reset retries and signal worker to connect */
        retries=0;atomic_store(&disconnected,false);
        int cmd=CMD_CONNECT;if(xQueueSend(commands,&cmd,0)!=pdTRUE)e=ESP_ERR_INVALID_STATE;
    }return e;
}
esp_err_t badge_network_delete(const char *ssid){
    if(!ssid||!ssid[0]||!mutex||!nvs_ready)return ESP_ERR_INVALID_ARG;
    lock();
    bool changed=badge_wifi_store_delete(&wifi_store,ssid);
    if(changed){
        nvs_set_blob(nvs,"known_wifis",&wifi_store,sizeof(wifi_store));
        nvs_commit(nvs);
        /* If deleted current saved AP, clear it */
        if(strcmp(saved.ssid,ssid)==0){
            memset(&saved,0,sizeof(saved));
            if(wifi_store.count>0){
                strncpy(saved.ssid,wifi_store.items[0].ssid,sizeof(saved.ssid)-1);
                strncpy(saved.password,wifi_store.items[0].password,sizeof(saved.password)-1);
            }
            nvs_set_blob(nvs,"station",&saved,sizeof(saved));
            nvs_commit(nvs);
            strncpy(state.ssid,saved.ssid,sizeof(state.ssid)-1);
        }
    }
    unlock();
    return changed?ESP_OK:ESP_ERR_NOT_FOUND;
}
esp_err_t badge_network_init(void){
    mutex=xSemaphoreCreateMutex();commands=xQueueCreate(4,sizeof(int));if(!mutex||!commands)return ESP_ERR_NO_MEM;
    uint8_t mac[6]={0};esp_read_mac(mac,ESP_MAC_WIFI_STA);
    snprintf(state.ap_ssid,sizeof(state.ap_ssid),"Badge-%02X%02X%02X",mac[3],mac[4],mac[5]);
    strcpy(state.message,"未保存 Wi-Fi");
    nvs_ready=nvs_open("badge_network",NVS_READWRITE,&nvs)==ESP_OK;
    if(nvs_ready){
        size_t len=sizeof(saved);if(nvs_get_blob(nvs,"station",&saved,&len)!=ESP_OK||len!=sizeof(saved)||saved.ssid[32]||saved.password[64])memset(&saved,0,sizeof(saved));
        len=sizeof(setup_password);if(nvs_get_str(nvs,"ap_password",setup_password,&len)!=ESP_OK||len!=sizeof(setup_password)||!valid_setup_password(setup_password))setup_password[0]=0;
        size_t wlen=sizeof(wifi_store);
        if(nvs_get_blob(nvs,"known_wifis",&wifi_store,&wlen)!=ESP_OK||wlen!=sizeof(wifi_store)||wifi_store.count>BADGE_KNOWN_WIFI_MAX){
            memset(&wifi_store,0,sizeof(wifi_store));
            if(saved.ssid[0]){
                badge_wifi_store_upsert(&wifi_store,saved.ssid,saved.password,1);
                nvs_set_blob(nvs,"known_wifis",&wifi_store,sizeof(wifi_store));
                nvs_commit(nvs);
            }
        }
    }
    strcpy(state.ssid,saved.ssid);
    if(xTaskCreate(worker,"badge_network",6144,NULL,3,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    if(saved.ssid[0]){int cmd=CMD_AUTOCONNECT;xQueueSend(commands,&cmd,0);}return ESP_OK;
}
