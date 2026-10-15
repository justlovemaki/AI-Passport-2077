#define BADGE_NETWORK_HOST_TEST
#include "../main/badge_network.c"
#include "badge_clock.h"
#include <assert.h>

static int queued[4],queued_count;
static bool station_connecting,fail_ap,verify_worker;
static unsigned worker_case,probe_checks_left;
static int connects,scans,disconnects,servers;
static bool sntp_running;
static unsigned sntp_starts,sntp_restarts,sntp_stops,sntp_immediate;
static wifi_mode_t wifi_mode;
static int64_t clock_us=100000000;

SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void *)1;}
int xSemaphoreTake(SemaphoreHandle_t m,unsigned ticks){return pdTRUE;}
int xSemaphoreGive(SemaphoreHandle_t m){return pdTRUE;}
QueueHandle_t xQueueCreate(unsigned length,unsigned size){return (void *)1;}
int xQueueSend(QueueHandle_t q,const void *value,unsigned ticks){
    if(queued_count==4)return pdFALSE;
    queued[queued_count++]=*(const int *)value;return pdTRUE;
}
int xQueueSendToFront(QueueHandle_t q,const void *value,unsigned ticks){
    if(queued_count==4)return pdFALSE;
    memmove(queued+1,queued,queued_count*sizeof(int));queued[0]=*(const int *)value;queued_count++;return pdTRUE;
}
int xQueueReceive(QueueHandle_t q,void *value,unsigned ticks){
    if(!queued_count){
        /* Check before terminal worker cleanup closes the hotspot. */
        assert(verify_worker);
        if(worker_case==0)assert(state.active&&servers==1&&connects==0&&scans==0);
        else if(worker_case==1)assert(connects==1&&scans==0&&disconnects==1);
        else if(worker_case==2)assert(connects==1&&scans==1);
        else if(worker_case==3)assert(connects==1&&scans==0&&disconnects==0&&probe_at>clock_us);
        if(probe_checks_left){
            --probe_checks_left;clock_us+=100000;
            assert(badge_network_wake_probe()==ESP_OK);return pdFALSE;
        }
        atomic_store(&shutdown_requested,true);return pdFALSE;
    }
    *(int *)value=queued[0];queued_count--;memmove(queued,queued+1,queued_count*sizeof(int));return pdTRUE;
}
int xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){return pdPASS;}
void vTaskDelay(unsigned ticks){}
void vTaskDelete(void *task){}
int64_t esp_timer_get_time(void){return clock_us;}
void yao_location_network(bool online){}
void yao_location_poll(void){}
esp_err_t esp_wifi_init(const wifi_init_config_t *c){return ESP_OK;}
esp_err_t esp_wifi_set_storage(int storage){return ESP_OK;}
esp_err_t esp_wifi_set_mode(wifi_mode_t mode){wifi_mode=mode;return ESP_OK;}
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode){*mode=wifi_mode;return ESP_OK;}
esp_err_t esp_wifi_start(void){return ESP_OK;}
esp_err_t esp_wifi_stop(void){return ESP_OK;}
esp_err_t esp_wifi_connect(void){connects++;station_connecting=true;return ESP_OK;}
esp_err_t esp_wifi_disconnect(void){disconnects++;station_connecting=false;return ESP_OK;}
esp_err_t esp_wifi_set_config(int interface,wifi_config_t *c){
    if(interface==WIFI_IF_AP){
        /* Reproduce the SDK failure that previously made offline setup fail. */
        if(station_connecting||fail_ap)return ESP_FAIL;
        assert(c->ap.ssid_len>0&&c->ap.authmode==WIFI_AUTH_WPA2_PSK);
    }
    return ESP_OK;
}
esp_err_t esp_wifi_get_ps(wifi_ps_type_t *ps){*ps=WIFI_PS_MIN_MODEM;return ESP_OK;}
esp_err_t esp_wifi_set_ps(wifi_ps_type_t ps){return ESP_OK;}
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *c,bool block){scans++;return ESP_OK;}
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *count){*count=0;return ESP_OK;}
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *count,wifi_ap_record_t *records){*count=0;return ESP_OK;}
esp_err_t esp_wifi_clear_ap_list(void){return ESP_OK;}
esp_err_t esp_netif_init(void){return ESP_OK;}
esp_err_t esp_event_loop_create_default(void){return ESP_OK;}
void *esp_netif_create_default_wifi_sta(void){return (void *)1;}
void *esp_netif_create_default_wifi_ap(void){return (void *)1;}
esp_err_t esp_event_handler_register(esp_event_base_t base,int id,void (*handler)(void *,esp_event_base_t,int32_t,void *),void *arg){return ESP_OK;}
void esp_fill_random(void *data,size_t size){memset(data,0x42,size);}
esp_err_t esp_read_mac(uint8_t *mac,int type){memset(mac,0x42,6);return ESP_OK;}
bool esp_sntp_enabled(void){return sntp_running;}
void esp_sntp_setoperatingmode(int mode){assert(mode==ESP_SNTP_OPMODE_POLL);}
void esp_sntp_set_sync_mode(int mode){assert(mode==SNTP_SYNC_MODE_IMMED);sntp_immediate++;}
void esp_sntp_setservername(int index,const char *name){assert(index==0&&!strcmp(name,"ntp.aliyun.com"));}
void esp_sntp_init(void){sntp_running=true;sntp_starts++;}
bool esp_sntp_restart(void){assert(sntp_running);sntp_restarts++;return true;}
void esp_sntp_stop(void){sntp_running=false;sntp_stops++;}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *handle){return ESP_FAIL;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *data,size_t *size){return ESP_FAIL;}
esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *data,size_t *size){return ESP_FAIL;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size){return ESP_OK;}
esp_err_t nvs_set_str(nvs_handle_t h,const char *key,const char *data){return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){return ESP_OK;}
esp_err_t httpd_start(httpd_handle_t *h,const httpd_config_t *c){*h=(void *)1;servers++;return ESP_OK;}
esp_err_t httpd_stop(httpd_handle_t h){servers--;return ESP_OK;}
esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *uri){return ESP_OK;}
/* The HTTP transport is outside these tests. Handler references must still link. */
const uint8_t page_start[] asm("_binary_configure_html_gz_start")={0};
const uint8_t page_end[] asm("_binary_configure_html_gz_end")={0};
int httpd_req_to_sockfd(httpd_req_t *r){return -1;}
size_t httpd_req_get_hdr_value_len(httpd_req_t *r,const char *name){return 0;}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *name,char *out,size_t size){return ESP_FAIL;}
esp_err_t httpd_resp_send_err(httpd_req_t *r,int status,const char *text){return ESP_OK;}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *type){return ESP_OK;}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *name,const char *value){return ESP_OK;}
esp_err_t httpd_resp_send_chunk(httpd_req_t *r,const char *body,size_t size){return ESP_OK;}
esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *body){return ESP_OK;}
int httpd_req_recv(httpd_req_t *r,char *body,size_t size){return -1;}
char *profile_protocol_request(const char *body,bool usb){return NULL;}
void cJSON_AddBoolToObject(cJSON *o,const char *key,bool value){}
void cJSON_AddStringToObject(cJSON *o,const char *key,const char *value){}
void cJSON_AddNumberToObject(cJSON *o,const char *key,double value){}
void cJSON_AddItemToArray(cJSON *o,cJSON *item){}
void cJSON_AddItemToObject(cJSON *o,const char *key,cJSON *item){}

static void reset(void){
    state=(badge_network_status_t){0};strcpy(state.ap_ssid,"Badge-Test");
    memset(&saved,0,sizeof(saved));memset(&wifi_store,0,sizeof(wifi_store));
    ready=started=attempted=false;server=NULL;mutex=(void *)1;commands=(void *)1;
    setup_password[0]=0;queued_count=connects=scans=disconnects=servers=0;
    station_connecting=fail_ap=verify_worker=false;retries=0;retry_at=probe_at=0;applied_ps=-1;
    worker_case=probe_checks_left=0;clock_us=100000000;
    atomic_store(&standby_ps,false);
    sntp_running=false;sntp_starts=sntp_restarts=sntp_stops=sntp_immediate=0;
    atomic_store(&shutdown_requested,false);atomic_store(&shutdown_done,false);
    atomic_store(&disconnected,false);atomic_store(&wake_probe_pending,false);
    atomic_store(&onboarding,false);atomic_store(&desired_ps,0);
}
int main(void){
    reset();toggle_ap();assert(state.active&&servers==1&&connects==0); /* No saved Wi-Fi. */
    close_ap();assert(!state.active&&servers==0);
    reset();station_connecting=true;strcpy(saved.ssid,"Unavailable router");
    toggle_ap();assert(state.active&&disconnects==1&&!station_connecting&&connects==0);
    reset();state.connected=true;toggle_ap();assert(state.active&&disconnects==0); /* Keep joined LAN. */
    reset();fail_ap=true;toggle_ap();assert(!state.active&&servers==0&&!state.ap_password[0]);
    reset();
    for(unsigned i=0;i<100;i++)assert(badge_network_wake_probe()==ESP_OK);
    assert(queued_count==1);assert(badge_network_start_setup()==ESP_OK);
    assert(queued_count==2&&queued[0]==CMD_START_SETUP&&queued[1]==CMD_WAKE_PROBE);
    reset();queued_count=4;assert(badge_network_wake_probe()==ESP_ERR_INVALID_STATE);
    assert(!atomic_load(&wake_probe_pending));queued_count=0;assert(badge_network_wake_probe()==ESP_OK);
    /* Setup overtakes a boot auto-connect and a key-triggered roam. Neither
     * those queued requests nor an expired retry may scan/reconnect behind AP. */
    reset();strcpy(saved.ssid,"Unavailable router");strcpy(state.ssid,saved.ssid);
    assert(badge_wifi_store_upsert(&wifi_store,saved.ssid,"password",1));
    atomic_store(&disconnected,true);station_connecting=true;
    int cmd=CMD_AUTOCONNECT;assert(xQueueSend(commands,&cmd,0)==pdTRUE);
    assert(badge_network_wake_probe()==ESP_OK);assert(badge_network_start_setup()==ESP_OK);
    verify_worker=true;worker(NULL);assert(!atomic_load(&wake_probe_pending));
    /* Explicit configuration actions remain available while AP is active. */
    reset();toggle_ap();strcpy(saved.ssid,"New router");connect_saved();
    assert(state.active&&servers==1&&connects==1);scan_nearby();assert(scans==1);
    /* Normal pages, AI idle/busy and screen-off standby may all reconnect.
     * Repeated navigation events must not abort an in-flight STA attempt. */
    for(unsigned policy=0;policy<3;policy++){
        reset();strcpy(saved.ssid,"Saved router");strcpy(state.ssid,saved.ssid);
        assert(badge_wifi_store_upsert(&wifi_store,saved.ssid,"password",1));
        atomic_store(&desired_ps,policy);atomic_store(&standby_ps,policy==0);
        connect_saved();assert(probe_at>clock_us);
        worker_case=1;verify_worker=true;probe_checks_left=10;
        assert(badge_network_wake_probe()==ESP_OK);worker(NULL);
        // Once the grace expires, the same page/power policy can probe again.
        reset();strcpy(saved.ssid,"Saved router");
        assert(badge_wifi_store_upsert(&wifi_store,saved.ssid,"password",1));
        atomic_store(&desired_ps,policy);atomic_store(&standby_ps,policy==0);
        worker_case=2;verify_worker=true;
        assert(badge_network_wake_probe()==ESP_OK);worker(NULL);
    }
    // A key probe that overtakes boot connection must not be immediately undone
    // by a second CMD_AUTOCONNECT when the worker next drains the queue.
    reset();strcpy(saved.ssid,"Saved router");
    assert(badge_wifi_store_upsert(&wifi_store,saved.ssid,"password",1));
    assert(badge_network_wake_probe()==ESP_OK);cmd=CMD_AUTOCONNECT;
    assert(xQueueSend(commands,&cmd,0)==pdTRUE);
    worker_case=2;verify_worker=true;worker(NULL);
    reset();strcpy(state.ssid,"Saved router");
    assert(badge_wifi_store_upsert(&wifi_store,state.ssid,"password",1));
    atomic_store(&disconnected,true);cmd=CMD_POWER;
    assert(xQueueSend(commands,&cmd,0)==pdTRUE);
    worker_case=3;verify_worker=true;probe_checks_left=10;worker(NULL);
    puts("Wi-Fi background: page-independent power policies, navigation probe grace, boot deduplication and retry grace PASS");
    reset();atomic_store(&shutdown_requested,true);
    assert(badge_network_start_setup()==ESP_ERR_INVALID_STATE);
    puts("Network runtime: offline AP, connecting STA, queue priority/coalescing, retry isolation and explicit setup actions PASS");
    reset();
    char clock_text[6];badge_clock_format(0,clock_text);assert(!strcmp(clock_text,"--:--"));
    ip_event_got_ip_t ip={0};
    event(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&ip);
    assert(state.connected&&sntp_running&&sntp_starts==1&&sntp_immediate==1);
    assert(sntp_restarts==0&&sntp_stops==0);
    /* Rendering consumes local UTC, never network status. A disconnect does
     * not stop the clock/sync service, and offline midnight still rolls over. */
    int64_t utc=1704124799;
    badge_clock_format(utc,clock_text);assert(!strcmp(clock_text,"23:59"));
    event(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);
    assert(!state.connected&&sntp_running&&sntp_stops==0);
    badge_clock_format(utc+1,clock_text);assert(!strcmp(clock_text,"00:00"));
    badge_clock_format(utc+3601,clock_text);assert(!strcmp(clock_text,"01:00"));
    event(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&ip);
    assert(state.connected&&sntp_restarts==1&&sntp_starts==1&&sntp_stops==0);
    /* Merely obtaining an IP is not a successful sync. Display the existing
     * local time while SNTP is waiting or the server cannot be reached. */
    badge_clock_format(utc+3601,clock_text);assert(!strcmp(clock_text,"01:00"));
    puts("Clock/network: first-IP calibration, immediate mode, offline display/rollover and reconnect calibration PASS");
    return 0;
}
