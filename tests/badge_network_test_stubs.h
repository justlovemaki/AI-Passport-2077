#pragma once
/* Minimal SDK boundary for exercising the real network worker without hardware. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "badge_network_rules.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_INVALID_ARG 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_NOT_FOUND 5
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
static inline const char *esp_err_to_name(int e){(void)e;return "test error";}
typedef struct cJSON {int unused;} cJSON;
static inline cJSON *cJSON_CreateObject(void){return NULL;}
static inline cJSON *cJSON_CreateArray(void){return NULL;}
void cJSON_AddBoolToObject(cJSON *o,const char *key,bool value);
void cJSON_AddStringToObject(cJSON *o,const char *key,const char *value);
void cJSON_AddNumberToObject(cJSON *o,const char *key,double value);
void cJSON_AddItemToArray(cJSON *o,cJSON *item);
void cJSON_AddItemToObject(cJSON *o,const char *key,cJSON *item);
typedef struct {bool active,connected;char ssid[33],ap_ssid[24],ap_password[13],ip[16],message[48];} badge_network_status_t;
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(x) (x)
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t m,unsigned ticks);
int xSemaphoreGive(SemaphoreHandle_t m);
QueueHandle_t xQueueCreate(unsigned length,unsigned size);
int xQueueSend(QueueHandle_t q,const void *value,unsigned ticks);
int xQueueSendToFront(QueueHandle_t q,const void *value,unsigned ticks);
int xQueueReceive(QueueHandle_t q,void *value,unsigned ticks);
int xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle);
void vTaskDelay(unsigned ticks);
void vTaskDelete(void *task);
int64_t esp_timer_get_time(void);
void yao_location_network(bool online);
void yao_location_poll(void);
typedef int wifi_mode_t;
typedef int wifi_ps_type_t;
enum {WIFI_MODE_STA,WIFI_MODE_AP,WIFI_MODE_APSTA};
enum {WIFI_PS_MIN_MODEM,WIFI_PS_MAX_MODEM,WIFI_PS_NONE};
enum {WIFI_AUTH_OPEN,WIFI_AUTH_WEP,WIFI_AUTH_WPA_PSK,WIFI_AUTH_WPA2_PSK,WIFI_AUTH_WPA_WPA2_PSK,WIFI_AUTH_ENTERPRISE,WIFI_AUTH_WPA3_PSK,WIFI_AUTH_WPA2_WPA3_PSK};
enum {WIFI_REASON_AUTH_EXPIRE,WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT,WIFI_REASON_AUTH_FAIL,WIFI_REASON_HANDSHAKE_TIMEOUT,WIFI_REASON_NO_AP_FOUND,WIFI_REASON_ASSOC_FAIL,WIFI_REASON_ASSOC_EXPIRE};
enum {WIFI_IF_STA,WIFI_IF_AP,WIFI_STORAGE_RAM,WPA3_SAE_PWE_BOTH,WIFI_SCAN_TYPE_ACTIVE,ESP_MAC_WIFI_STA};
typedef struct {struct {uint8_t ssid[32],password[64];int sae_pwe_h2e,listen_interval;} sta;struct {uint8_t ssid[32],password[64];int ssid_len,channel,max_connection,authmode;} ap;} wifi_config_t;
typedef struct {bool show_hidden;int scan_type;struct {struct {unsigned min,max;} active;} scan_time;} wifi_scan_config_t;
typedef struct {uint8_t ssid[33];int rssi,authmode;} wifi_ap_record_t;
typedef struct {int unused;} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() {0}
esp_err_t esp_wifi_init(const wifi_init_config_t *c);
esp_err_t esp_wifi_set_storage(int storage);
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_set_config(int interface,wifi_config_t *c);
esp_err_t esp_wifi_get_ps(wifi_ps_type_t *ps);
esp_err_t esp_wifi_set_ps(wifi_ps_type_t ps);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *c,bool block);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *count);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *count,wifi_ap_record_t *records);
esp_err_t esp_wifi_clear_ap_list(void);
esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);
void *esp_netif_create_default_wifi_sta(void);
void *esp_netif_create_default_wifi_ap(void);
typedef int esp_event_base_t;
enum {IP_EVENT=1,WIFI_EVENT,IP_EVENT_STA_GOT_IP,WIFI_EVENT_STA_DISCONNECTED};
typedef struct {struct {struct {unsigned addr;} ip;} ip_info;} ip_event_got_ip_t;
typedef struct {int reason;} wifi_event_sta_disconnected_t;
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) (unsigned)((ip)->addr),168u,1u,2u
esp_err_t esp_event_handler_register(esp_event_base_t base,int id,void (*handler)(void *,esp_event_base_t,int32_t,void *),void *arg);
void esp_fill_random(void *data,size_t size);
esp_err_t esp_read_mac(uint8_t *mac,int type);
#define ESP_SNTP_OPMODE_POLL 0
#define SNTP_SYNC_MODE_IMMED 0
bool esp_sntp_enabled(void);
void esp_sntp_setoperatingmode(int mode);
void esp_sntp_set_sync_mode(int mode);
void esp_sntp_setservername(int index,const char *name);
void esp_sntp_init(void);
bool esp_sntp_restart(void);
void esp_sntp_stop(void);
typedef int nvs_handle_t;
#define NVS_READWRITE 0
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *handle);
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *data,size_t *size);
esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *data,size_t *size);
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size);
esp_err_t nvs_set_str(nvs_handle_t h,const char *key,const char *data);
esp_err_t nvs_commit(nvs_handle_t h);
typedef void *httpd_handle_t;
typedef struct {size_t content_len;} httpd_req_t;
typedef struct {unsigned stack_size,max_open_sockets;bool lru_purge_enable;unsigned recv_wait_timeout,send_wait_timeout;} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() {0}
typedef struct {const char *uri;int method;esp_err_t (*handler)(httpd_req_t *);} httpd_uri_t;
enum {HTTP_GET,HTTP_POST,HTTPD_403_FORBIDDEN,HTTPD_400_BAD_REQUEST,HTTPD_500_INTERNAL_SERVER_ERROR};
esp_err_t httpd_start(httpd_handle_t *h,const httpd_config_t *c);
esp_err_t httpd_stop(httpd_handle_t h);
esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *uri);
int httpd_req_to_sockfd(httpd_req_t *r);
size_t httpd_req_get_hdr_value_len(httpd_req_t *r,const char *name);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *name,char *out,size_t size);
esp_err_t httpd_resp_send_err(httpd_req_t *r,int status,const char *text);
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *type);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *name,const char *value);
esp_err_t httpd_resp_send_chunk(httpd_req_t *r,const char *body,size_t size);
esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *body);
int httpd_req_recv(httpd_req_t *r,char *body,size_t size);
char *profile_protocol_request(const char *body,bool usb);
