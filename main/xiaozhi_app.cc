/* XiaoZhi protocol adapter, based on FoloToy/folo-ai-passport-xiaozhi (MIT).
 * See assets/xiaozhi/NOTICE and LICENSE. The badge owns Wi-Fi, UI and lifecycle.
 * All network, codec and NVS work belongs to this one worker. MQTT's callback
 * only assembles bounded messages; it never touches audio, UI or credentials.
 */
#include "xiaozhi_app.h"
#include "xiaozhi_ui.h"
#include "xiaozhi_wire.h"
#include "xiaozhi_buffer.h"
#include "xiaozhi_volume.h"
#include "xiaozhi_caption.h"
#include "xiaozhi_text.h"
#include "xiaozhi_dispatch.h"
#include "xiaozhi_tools.h"
#include "assets/yao_request.h"
#include "badge_control.h"
#include "../components/opus/libopus/include/opus.h"
#include "badge_power.h"
extern "C" {
#include "badge_network.h"
#include "yao_time.h"
#include "bsp_audio.h"
#include "bsp_display.h"
#include "badge_profile.h"
#include "lvgl.h"
}
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_transport_ssl.h"
#include "esp_transport_tcp.h"
#include "esp_transport_ws.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "mbedtls/aes.h"
#include "decoder/impl/esp_opus_dec.h"
#include "encoder/impl/esp_opus_enc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <atomic>
#include <string>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <new>
#ifdef BADGE_XIAOZHI_STABILITY_PROBE
#include "xiaozhi_probe_pcm.h"
static std::atomic<unsigned> stability_replies{0};
static std::atomic<unsigned> stability_codec_allocations{0};
extern "C" unsigned demo_xiaozhi_stability_replies(void){return stability_replies.load();}
extern "C" unsigned demo_xiaozhi_stability_allocations(void){return stability_codec_allocations.load();}
#endif
#ifdef BADGE_XIAOZHI_CONVERSATION_PROBE
#include "xiaozhi_probe_pcm.h"
static std::atomic<bool> conversation_probe_done{false};
extern "C" bool demo_xiaozhi_conversation_probe_done(void){return conversation_probe_done.load();}
#endif
#ifdef BADGE_XIAOZHI_DEVICE_PROBE
extern "C" bool voice_probe_decode_all(bool (*cancelled)(void));
static std::atomic<bool> probe_ready{false};
#ifdef BADGE_XIAOZHI_PLAYBACK_PROBE
static std::atomic<bool> playback_probe_done{false};
extern "C" bool demo_xiaozhi_playback_probe_done(void){return playback_probe_done.load();}
#endif
#ifdef BADGE_XIAOZHI_POWER_PROBE
static std::atomic<bool> probe_mic_request{false},probe_mic_done{false};
extern "C" void demo_xiaozhi_probe_microphone(void){probe_mic_done=false;probe_mic_request=true;}
extern "C" bool demo_xiaozhi_probe_microphone_done(void){return probe_mic_done.load();}
#endif
extern "C" bool demo_xiaozhi_probe_ready(void){return probe_ready.load();}
#endif

namespace {
constexpr const char *TAG="xiaozhi";
constexpr const char *OTA="https://api.tenclass.net/xiaozhi/ota/";
constexpr unsigned MAX_MESSAGE=2048;
std::atomic<bool> stopping{false},background_idle{false};
std::atomic<unsigned> clicks{0},wanted_volume{40};
std::atomic<xz_state_t> return_state{XZ_IDLE};
SemaphoreHandle_t done;
yao_record_t *launch_reading=nullptr;
#ifdef BADGE_XIAOZHI_CONNECT_PROBE
std::atomic<unsigned> connect_probe_state{0},connect_probe_fault{0},connect_probe_frames{0},connect_probe_catalog{0};
#endif
#ifdef BADGE_YAO_DEVICE_PROBE
std::atomic<unsigned> yao_probe_samples{0},yao_probe_tts_stop{0};
#endif
TaskHandle_t voice_worker=nullptr;
std::atomic<bool> started{false};
const char *str(const cJSON *o,const char *key){const auto *v=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsString(v)?v->valuestring:"";}
int num(const cJSON *o,const char *key,int fallback){const auto *v=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsNumber(v)?v->valueint:fallback;}
struct Json {cJSON *p=nullptr; explicit Json(cJSON *v):p(v){} ~Json(){cJSON_Delete(p);} Json(const Json&)=delete;};
struct JsonText {char *p=nullptr;~JsonText(){cJSON_free(p);}JsonText()=default;JsonText(const JsonText&)=delete;};
std::string json_text(cJSON *p){char *s=cJSON_PrintUnformatted(p);std::string result=s?s:"";cJSON_free(s);return result;}
bool clean_header(const std::string &s){return s.size()<768&&s.find('\r')==std::string::npos&&s.find('\n')==std::string::npos;}
bool valid_backend_url(const char *url){
    if(!url)return false;
    size_t n=strlen(url);
    if(!n)return true;
    if(n>255)return false;
    if(strncmp(url,"https://",8)&&strncmp(url,"http://",7))return false;
    for(const unsigned char *p=(const unsigned char *)url;*p;p++)if(*p<=32||*p==127)return false;
    return true;
}
std::string backend_url(){
    char url[256]={0};
    nvs_handle_t n;size_t len=sizeof(url);
    if(nvs_open("xiaozhi",NVS_READONLY,&n)==ESP_OK){
        esp_err_t e=nvs_get_str(n,"backend_url",url,&len);nvs_close(n);
        if(e==ESP_OK&&valid_backend_url(url)&&url[0])return url;
    }
    return OTA;
}
struct Message {unsigned size;char text[MAX_MESSAGE+1];};
// Foreground worker owns this RAM-only cache. Navigation may expire it only
// after joining that worker. Credentials never enter NVS or badge profiles.
struct ServiceConfig {
    std::string url,token,endpoint,client,user,password,topic;
    unsigned version=1;int64_t saved=0;
};
ServiceConfig service_config;
#ifdef BADGE_XIAOZHI_REUSE_PROBE
std::atomic<unsigned> probe_discovery{0},probe_connections{0},probe_handshakes{0};
#endif

#ifdef BADGE_TOOL_CLOUD_PROBE
#include "cloud_probe_pcm.h"
static std::atomic<unsigned> cloud_sequence{0},cloud_completed{0};
#endif
class Session {
public:
    xz_snapshot_t view{};
    xz_face_selector_t face_selector{};
    std::string mac,uuid,url,token,endpoint,client,user,password,topic,session;
    yao_record_t *reading=nullptr;
    bool reading_pending=false;
    bool context_turn=false,tools_advertised=false,context_upload=false;
    size_t context_offset=0;int64_t context_next=0;
    unsigned version=1;
    esp_transport_handle_t parent=nullptr,ws=nullptr;
    esp_mqtt_client_handle_t mqtt=nullptr;
    QueueHandle_t messages=nullptr;
    Message *assembly=nullptr,*incoming=nullptr;
    std::atomic<bool> connected{false},failed{false},closing{false};
    std::atomic<xz_fault_t> fault{XZ_FAULT_NONE};
    std::atomic<unsigned> diagnostic_state{XZ_IDLE};
    unsigned retries=0;int64_t retry_at=0;
#ifdef BADGE_XIAOZHI_STABILITY_PROBE
    unsigned stability_turn=0;size_t stability_offset=0;
    int64_t stability_frame=0;bool stability_injected=false,stability_silence=false;
#endif
    unsigned assembled=0,total=0;
    int udp=-1;
    uint8_t nonce[16]{},key[16]{};
    uint32_t tx=0,rx=0;
    OpusEncoder *encoder=nullptr;
    OpusDecoder *decoder=nullptr;
    void *codec_arena=nullptr;
    size_t codec_arena_bytes=0;
    size_t codec_queue_offset=0;
    xz_audio_buffer_t *audio_queue=nullptr;
    unsigned received_packets=0,played_packets=0,late_writes=0,udp_missing=0,applied_volume=101;
    int64_t last_write_end=0,max_write_gap=0;
    int16_t *pcm=nullptr;
    uint8_t *packet=nullptr;
    int input_bytes=0,output_bytes=0;
    int64_t listen_since=0,last_incoming=0,last_draw=0,last_audio=0,tts_stop=0;
    bool hello=false,accept_audio=false,played=false,conversation=false;
#ifdef BADGE_TOOL_CLOUD_PROBE
    unsigned cloud_turn=0,cloud_offset=0;int64_t cloud_sent=0,cloud_frame=0;bool cloud_waiting=false;
#endif
    int64_t resume_at=0,ignore_until=0,idle_since=0;
    unsigned observed_clicks=0;
    xz_volume_t volume_preference{};
    bool volume_loaded=false;
    std::atomic<bool> parked{false};
    int64_t parked_at=0;
    bool power_owned=false;
#ifdef BADGE_XIAOZHI_CONVERSATION_PROBE
    unsigned probe_turn=0,probe_stt=0;
    size_t probe_offset=0;
    int64_t probe_next_frame=0;
#endif
    void leave_power(){if(power_owned){(void)badge_power_leave();power_owned=false;}}
    ~Session(){close();leave_power();free(codec_arena);free(pcm);free(packet);free(reading);}
    void render(bool force=false){if(badge_power_screen_off())return;int64_t now=esp_timer_get_time();int64_t interval=(view.state==XZ_LISTENING||view.state==XZ_SPEAKING)?50000:200000;if(!force&&now-last_draw<interval)return;view.volume=wanted_volume.load();if(xiaozhi_ui_post(&view))last_draw=now;}
    void state(xz_state_t s,const char *detail){
#ifdef BADGE_XIAOZHI_CONNECT_PROBE
        connect_probe_state=(unsigned)s;
#endif
        if(view.state!=s)ESP_LOGI(TAG,"State %u -> %u heap=%lu largest=%lu stack=%u",(unsigned)view.state,(unsigned)s,(unsigned long)esp_get_free_heap_size(),(unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),(unsigned)uxTaskGetStackHighWaterMark(nullptr));
        return_state=s;view.state=s;diagnostic_state=s;xz_text_copy(view.detail,sizeof(view.detail),detail);view.level=0;view.level_until_ms=0;if(s==XZ_LISTENING||s==XZ_READY){view.emotion=XZ_FACE_NEUTRAL;view.face_variant=0;xz_face_new_turn(&face_selector);}render(true);
#ifdef BADGE_XIAOZHI_DEVICE_PROBE
        ESP_LOGI("xz_probe","STATE=%u detail=%s code=%s free=%lu",(unsigned)s,detail,view.code,(unsigned long)esp_get_free_heap_size());
#endif
    }
    bool fail(xz_fault_t reason){
        if(stopping||closing)return false;
        xz_fault_t expected=XZ_FAULT_NONE;
        if(fault.compare_exchange_strong(expected,reason)){
#ifdef BADGE_XIAOZHI_CONNECT_PROBE
            connect_probe_fault=(unsigned)reason;
#endif
            ESP_LOGW(TAG,"Session fault=%u state=%u heap=%lu largest=%lu min=%lu",(unsigned)reason,diagnostic_state.load(),
                (unsigned long)esp_get_free_heap_size(),(unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                (unsigned long)esp_get_minimum_free_heap_size());
        }
        failed=true;return false;
    }
    bool memory_failure(const char *stage,size_t requested=0){
        ESP_LOGE(TAG,"Memory allocation failed stage=%s bytes=%u heap=%lu largest=%lu min=%lu",
            stage,(unsigned)requested,(unsigned long)esp_get_free_heap_size(),
            (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
            (unsigned long)esp_get_minimum_free_heap_size());
        return fail(XZ_FAULT_MEMORY);
    }
    const char *failure_text(){
        badge_network_status_t net{};badge_network_status(&net);
        if(net.active)return "热点未关闭影响通信，按 OK 重试";
        if(!net.connected)return "Wi-Fi 未连接，请在设置中配置";
        switch(fault.load()){
        case XZ_FAULT_MEMORY:return "语音内存不足，按 OK 重试";
        case XZ_FAULT_QUEUE:return "消息过于密集，按 OK 重试";
        case XZ_FAULT_TIMEOUT:return "服务响应超时，按 OK 重试";
        case XZ_FAULT_PROTOCOL:return "服务数据异常，按 OK 重试";
        case XZ_FAULT_AUDIO:return "音频处理失败，按 OK 重试";
        default:return "语音连接已断开，按 OK 重试";
        }
    }
    void error(const char *detail){conversation=false;state(XZ_ERROR,detail);}
    bool schedule_retry(xz_fault_t reason){
        int64_t delay=xz_retry_delay(reason,retries);
        if(!delay||stopping)return false;
        retry_at=esp_timer_get_time()+delay;++retries;
        state(XZ_CONNECTING,"连接中断，正在重新连接");
        ESP_LOGI(TAG,"Reconnect scheduled attempt=%u delay_ms=%lld",retries,(long long)(delay/1000));
        return true;
    }
    bool establish(){
        badge_network_xiaozhi_power(true,true);
        badge_network_status_t net{};badge_network_status(&net);
        if(!net.connected){close();fault=XZ_FAULT_NONE;fail(XZ_FAULT_TRANSPORT);error("请先在工牌系统设置中连接 Wi-Fi");return false;}
        // Retain MQTT/TLS only; every entry obtains a new conversation/UDP key.
        if(mqtt&&connected&&!failed){
            if(connect()){ESP_LOGI(TAG,"Connection reused (fresh audio session)");return true;}
            ESP_LOGI(TAG,"Retained connection unavailable; reconnecting");
        }
        close();fault=XZ_FAULT_NONE;failed=false;
        bool cached=xz_cache_fresh(service_config.saved,esp_timer_get_time(),600000000);
        if(configure()&&!stopping&&connect())return true;
        // Refresh revoked/expired cached credentials once, without a retry loop.
        if(cached&&!stopping&&fault!=XZ_FAULT_MEMORY){
            service_config=ServiceConfig{};close();fault=XZ_FAULT_NONE;failed=false;
            if(configure()&&!stopping&&connect())return true;
        }
        bool activation=view.state==XZ_ACTIVATION;
        if(!activation&&!stopping){if(fault==XZ_FAULT_NONE)fail(XZ_FAULT_TRANSPORT);error(failure_text());}
        close();return false;
    }
    void restore_volume(){
        uint8_t stored=255;nvs_handle_t n;
        esp_err_t result=nvs_open("xiaozhi",NVS_READONLY,&n);
        if(result==ESP_OK){result=nvs_get_u8(n,"volume",&stored);nvs_close(n);}
        if(result!=ESP_OK&&result!=ESP_ERR_NVS_NOT_FOUND)ESP_LOGW(TAG,"Volume restore failed: %s",esp_err_to_name(result));
        unsigned value=xz_volume_valid(stored,bsp_audio_get_volume());
        wanted_volume=value;volume_preference.saved=value;volume_preference.value=value;volume_loaded=true;
        sync_volume(false,false);
    }
    void sync_volume(bool safe,bool flush){
        if(!volume_loaded)return;
        int64_t now=esp_timer_get_time();
        xz_volume_observe(&volume_preference,wanted_volume.load(),now);
        if(applied_volume!=volume_preference.value){bsp_audio_set_volume(volume_preference.value);applied_volume=volume_preference.value;}
        if(!xz_volume_due(&volume_preference,now,safe,flush))return;
        nvs_handle_t n;esp_err_t result=nvs_open("xiaozhi",NVS_READWRITE,&n);
        if(result==ESP_OK){result=nvs_set_u8(n,"volume",volume_preference.value);if(result==ESP_OK)result=nvs_commit(n);nvs_close(n);}
        xz_volume_result(&volume_preference,now,result==ESP_OK);
        if(result!=ESP_OK)ESP_LOGW(TAG,"Volume save failed: %s",esp_err_to_name(result));
#ifdef BADGE_XIAOZHI_DEVICE_PROBE
        else ESP_LOGI("xz_probe","VOLUME_SAVED=%u",volume_preference.saved);
#endif
    }
    bool identity(){
        uint8_t m[6];if(esp_read_mac(m,ESP_MAC_WIFI_STA)!=ESP_OK)return false;char text[40];snprintf(text,sizeof(text),"%02x:%02x:%02x:%02x:%02x:%02x",m[0],m[1],m[2],m[3],m[4],m[5]);mac=text;
        nvs_handle_t n;if(nvs_open("xiaozhi",NVS_READWRITE,&n)!=ESP_OK)return false;
        size_t len=sizeof(text);esp_err_t e=nvs_get_str(n,"uuid",text,&len);
        if(e==ESP_ERR_NVS_NOT_FOUND){uint8_t r[16];esp_fill_random(r,sizeof(r));r[6]=(r[6]&15)|64;r[8]=(r[8]&63)|128;snprintf(text,sizeof(text),"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",r[0],r[1],r[2],r[3],r[4],r[5],r[6],r[7],r[8],r[9],r[10],r[11],r[12],r[13],r[14],r[15]);e=nvs_set_str(n,"uuid",text);if(e==ESP_OK)e=nvs_commit(n);}
        nvs_close(n);if(e!=ESP_OK||strlen(text)!=36)return false;uuid=text;return true;
    }
    int http(const char *address,const std::string &body,std::string &response){
        esp_http_client_config_t cfg{};cfg.url=address;cfg.timeout_ms=4500;cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.buffer_size=512;cfg.buffer_size_tx=512;cfg.disable_auto_redirect=true;
        auto h=esp_http_client_init(&cfg);if(!h)return -1;
        esp_http_client_set_method(h,HTTP_METHOD_POST);
        esp_http_client_set_header(h,"Activation-Version","1");esp_http_client_set_header(h,"Device-Id",mac.c_str());esp_http_client_set_header(h,"Client-Id",uuid.c_str());esp_http_client_set_header(h,"Accept-Language","zh-CN");esp_http_client_set_header(h,"User-Agent","folo-ai-passport-c3/2.4.2 badge/" BADGE_VERSION);esp_http_client_set_header(h,"Content-Type","application/json");
        int status=-1;
        if(!stopping&&esp_http_client_open(h,body.size())==ESP_OK&&esp_http_client_write(h,body.data(),body.size())==(int)body.size()&&esp_http_client_fetch_headers(h)>=0){
            status=esp_http_client_get_status_code(h);char chunk[512];int64_t end=esp_timer_get_time()+6000000;
            while(!stopping&&response.size()<=8192&&esp_timer_get_time()<end){int n=esp_http_client_read(h,chunk,sizeof(chunk));if(n<0){status=-1;break;}if(n==0){if(!esp_http_client_is_complete_data_received(h))status=-1;break;}response.append(chunk,n);}if(response.size()>8192||stopping)status=-1;
        }
        esp_http_client_cleanup(h);return status;
    }
    bool configure(){
        if(xz_cache_fresh(service_config.saved,esp_timer_get_time(),600000000)){
            url=service_config.url;token=service_config.token;endpoint=service_config.endpoint;
            client=service_config.client;user=service_config.user;password=service_config.password;
            topic=service_config.topic;version=service_config.version;
            ESP_LOGI(TAG,"Service configuration reused");return true;
        }
        service_config=ServiceConfig{};
        state(XZ_CONNECTING,"正在获取小智服务地址");
#ifdef BADGE_XIAOZHI_REUSE_PROBE
        ++probe_discovery;
#endif
        Json root(cJSON_CreateObject());cJSON_AddNumberToObject(root.p,"version",2);cJSON_AddStringToObject(root.p,"uuid",uuid.c_str());cJSON_AddNumberToObject(root.p,"flash_size",8*1024*1024);cJSON_AddStringToObject(root.p,"mac_address",mac.c_str());
        auto board=cJSON_AddObjectToObject(root.p,"board");cJSON_AddStringToObject(board,"type","folo-ai-passport-c3");cJSON_AddStringToObject(board,"name","folo-ai-passport-c3");cJSON_AddStringToObject(board,"manufacturer","folo");cJSON_AddStringToObject(board,"mac",mac.c_str());
        auto app=cJSON_AddObjectToObject(root.p,"application");cJSON_AddStringToObject(app,"name","folo-ai-passport-xiaozhi");cJSON_AddStringToObject(app,"version","2.4.2");
        std::string response;std::string discovery=backend_url();int status=http(discovery.c_str(),json_text(root.p),response);
        if(status!=200){
            ESP_LOGW(TAG,"Discovery HTTP status=%d",status);
            badge_network_status_t net{};badge_network_status(&net);
            if(net.active)error("配网热点开启中\n影响服务发现，按 OK 重试");
            else if(!net.connected)error("Wi-Fi 已断开\n请在设置中重新连接");
            else if(status>0)error("小智服务响应异常\n请稍后重试");
            else error("小智服务连接超时\n请检查网络连接");
            return false;
        }
        Json reply(cJSON_ParseWithLength(response.data(),response.size()));if(!reply.p){error("小智服务返回数据异常");return false;}
        auto activation=cJSON_GetObjectItemCaseSensitive(reply.p,"activation");
        if(*str(activation,"code")){
            xz_text_copy(view.code,sizeof(view.code),str(activation,"code"));
            state(XZ_ACTIVATION,"打开 xiaozhi.me\n控制台添加设备，输入下方码");
            // Only the user can bind their account. Retry explicitly after binding.
            std::string ignored;http("https://api.tenclass.net/xiaozhi/ota/activate","{}",ignored);
            return false;
        }
        view.code[0]=0;auto websocket=cJSON_GetObjectItemCaseSensitive(reply.p,"websocket");auto broker=cJSON_GetObjectItemCaseSensitive(reply.p,"mqtt");
        // Prefer the official MQTT/UDP transport when provided, as upstream does.
        endpoint=str(broker,"endpoint");client=str(broker,"client_id");user=str(broker,"username");password=str(broker,"password");topic=str(broker,"publish_topic");
        url=str(websocket,"url");token=str(websocket,"token");version=num(websocket,"version",1);
        if(endpoint.empty()||client.empty()||topic.empty()){
            endpoint.clear();xz_endpoint_t parsed{};
            if(!xz_endpoint(url.c_str(),&parsed)||version<1||version>3||!clean_header(token)){error("小智服务地址或协议不受支持");return false;}
        }
        service_config={url,token,endpoint,client,user,password,topic,version,esp_timer_get_time()};
        return true;
    }
    static void mqtt_event(void *arg,esp_event_base_t,int32_t id,void *data){
        auto &s=*static_cast<Session *>(arg);auto *e=static_cast<esp_mqtt_event_handle_t>(data);
        if(id==MQTT_EVENT_CONNECTED)s.connected=true;
        else if(id==MQTT_EVENT_DISCONNECTED||id==MQTT_EVENT_ERROR){
            s.connected=false;
            if(!s.closing&&!stopping){
                if(id==MQTT_EVENT_ERROR&&e->error_handle)ESP_LOGW(TAG,"MQTT error type=%d esp=%d tls=%d socket=%d",(int)e->error_handle->error_type,(int)e->error_handle->esp_tls_last_esp_err,e->error_handle->esp_tls_stack_err,e->error_handle->esp_transport_sock_errno);
                s.fail(XZ_FAULT_TRANSPORT);
            }
        }
        else if(id==MQTT_EVENT_DATA){
            // Hidden pages receive no captions, audio or tool calls. MQTT owns
            // keepalive. Fragment tails from the hidden interval are discarded.
            if(s.parked||stopping){s.assembled=0;s.total=0;return;}
            if(e->current_data_offset!=0&&s.total==0)return;
            if(e->current_data_offset==0){s.assembled=0;s.total=e->total_data_len;}
            if(!s.assembly||e->data_len<0||e->current_data_offset<0||s.total>MAX_MESSAGE||s.total==0||(unsigned)e->current_data_offset!=s.assembled||(unsigned)e->data_len>s.total-s.assembled){s.fail(XZ_FAULT_PROTOCOL);return;}
            memcpy(s.assembly->text+s.assembled,e->data,e->data_len);s.assembled+=e->data_len;
            if(s.assembled==s.total){
                // Four bounded control messages tolerate a TTS/LLM caption burst
                // without reserving four maximum-sized JSON buffers permanently.
                s.assembly->text[s.total]=0;char *copy=static_cast<char *>(malloc(s.total+1));
                if(!copy){s.memory_failure("mqtt_message",s.total+1);return;}memcpy(copy,s.assembly->text,s.total+1);
                if(xQueueSend(s.messages,&copy,0)!=pdTRUE){free(copy);s.fail(XZ_FAULT_QUEUE);}
            }
        }
    }
    bool connect(){
        state(XZ_CONNECTING,"正在建立加密语音连接");failed=false;hello=false;last_incoming=esp_timer_get_time();
        if(!reserve_codec())return false;
        if(!endpoint.empty()&&!mqtt){
            if(endpoint.size()>200||client.size()>256||user.size()>512||password.size()>768||topic.size()>256)return false;
            messages=xQueueCreate(4,sizeof(char *));assembly=static_cast<Message *>(calloc(1,sizeof(Message)));if(!messages||!assembly)return memory_failure("mqtt_buffers",sizeof(Message));
            std::string uri="mqtts://"+endpoint;
            esp_mqtt_client_config_t cfg{};cfg.broker.address.uri=uri.c_str();cfg.broker.verification.crt_bundle_attach=esp_crt_bundle_attach;cfg.credentials.client_id=client.c_str();cfg.credentials.username=user.c_str();cfg.credentials.authentication.password=password.c_str();cfg.session.keepalive=60;cfg.network.timeout_ms=4500;cfg.network.disable_auto_reconnect=true;cfg.task.stack_size=4096;cfg.task.priority=3;cfg.buffer.size=1024;cfg.buffer.out_size=1024;
            mqtt=esp_mqtt_client_init(&cfg);if(!mqtt)return memory_failure("mqtt_client");
#ifdef BADGE_XIAOZHI_REUSE_PROBE
            ++probe_connections;
#endif
            if(esp_mqtt_client_register_event(mqtt,MQTT_EVENT_ANY,mqtt_event,this)!=ESP_OK||esp_mqtt_client_start(mqtt)!=ESP_OK)return false;
            int64_t end=esp_timer_get_time()+8000000;while(!stopping&&!failed&&!connected&&esp_timer_get_time()<end)vTaskDelay(pdMS_TO_TICKS(25));
            if(!connected){
                badge_network_status_t net{};badge_network_status(&net);
                if(net.active)error("配网热点开启中\n影响连接握手，按 OK 重试");
                else if(!net.connected)error("Wi-Fi 已断开\n请在设置中重新连接");
                else error("语音服务连接超时\n请按 OK 重试");
                return false;
            }
        }else if(!mqtt){
            incoming=static_cast<Message *>(malloc(sizeof(Message)));if(!incoming)return memory_failure("websocket_buffer",sizeof(Message));
            xz_endpoint_t parsed{};if(!xz_endpoint(url.c_str(),&parsed))return false;
            parent=parsed.tls?esp_transport_ssl_init():esp_transport_tcp_init();if(!parent)return false;if(parsed.tls)esp_transport_ssl_crt_bundle_attach(parent,esp_crt_bundle_attach);
            ws=esp_transport_ws_init(parent);if(!ws)return false;esp_transport_ws_set_path(ws,parsed.path);
            std::string headers="Protocol-Version: "+std::to_string(version)+"\r\nDevice-Id: "+mac+"\r\nClient-Id: "+uuid+"\r\n";
            if(!token.empty())headers+=std::string("Authorization: ")+(token.find(' ')==std::string::npos?"Bearer ":"")+token+"\r\n";
            if(esp_transport_ws_set_headers(ws,headers.c_str())!=ESP_OK||esp_transport_connect(ws,parsed.host,parsed.port,4500)<0){return false;}connected=true;
        }
        parked=false;
        Json h(cJSON_CreateObject());cJSON_AddStringToObject(h.p,"type","hello");cJSON_AddNumberToObject(h.p,"version",mqtt?3:version);cJSON_AddStringToObject(h.p,"transport",mqtt?"udp":"websocket");auto features=cJSON_AddObjectToObject(h.p,"features");cJSON_AddBoolToObject(features,"mcp",true);auto params=cJSON_AddObjectToObject(h.p,"audio_params");cJSON_AddStringToObject(params,"format","opus");cJSON_AddNumberToObject(params,"sample_rate",16000);cJSON_AddNumberToObject(params,"channels",1);cJSON_AddNumberToObject(params,"frame_duration",60);
        if(!send_text(json_text(h.p)))return false;
        int64_t end=esp_timer_get_time()+10000000;while(!stopping&&!failed&&!xz_session_ready(hello,tools_advertised)&&esp_timer_get_time()<end){poll();vTaskDelay(pdMS_TO_TICKS(10));}
        if(!xz_session_ready(hello,tools_advertised)||failed){
            if(!stopping&&!failed){ESP_LOGW(TAG,"Handshake timeout hello=%u tools=%u",hello,tools_advertised);fail(XZ_FAULT_TIMEOUT);}
            return false;
        }
#ifdef BADGE_XIAOZHI_REUSE_PROBE
        ++probe_handshakes;
#endif
#if !defined(BADGE_XIAOZHI_PLAYBACK_PROBE) && !defined(BADGE_TOOL_CLOUD_PROBE) && !defined(BADGE_XIAOZHI_REUSE_PROBE)
        conversation=true;
#endif
        state(XZ_READY,"连接成功，准备开始聆听");return true;
    }
    bool send_text(const std::string &text,bool shutdown=false){
        return send_text(text.data(),text.size(),shutdown);
    }
    bool send_text(const char *text,size_t length,bool shutdown=false){
        if((stopping&&!shutdown)||!text||!length)return false;
        bool ok=mqtt?esp_mqtt_client_publish(mqtt,topic.c_str(),text,length,0,0)>=0:ws&&esp_transport_ws_send_raw(ws,static_cast<ws_transport_opcodes_t>(WS_TRANSPORT_OPCODES_FIN|WS_TRANSPORT_OPCODES_TEXT),text,length,1000)==(int)length;if(!ok)fail(XZ_FAULT_TRANSPORT);return ok;
    }
    __attribute__((noinline)) bool send_catalog(Json &wrapper,size_t &length){
        // The worker already owns a 28 KiB stack. Borrow 8 KiB only during
        // serialization/transmission, after tool construction and before Opus.
        // Keeping this on the heap starves the AES DMA allocator during TLS.
        char text[8192];
        bool printed=cJSON_PrintPreallocated(wrapper.p,text,sizeof(text),false);
        cJSON_Delete(wrapper.p);wrapper.p=nullptr;
        if(!printed)return memory_failure("catalog_output_limit",sizeof(text));
        length=strlen(text);
        return send_text(text,length);
    }
    bool send_initial_context(){
        if(!reading_pending||!reading)return true;
        badge_network_xiaozhi_power(true,true);
        Json j(cJSON_CreateObject());
        if(!j.p||!cJSON_AddStringToObject(j.p,"type","listen")||!cJSON_AddStringToObject(j.p,"session_id",session.c_str())||
           !cJSON_AddStringToObject(j.p,"state","start")||!cJSON_AddStringToObject(j.p,"mode","manual"))return fail(XZ_FAULT_MEMORY);
        conversation=true;accept_audio=true;tts_stop=0;ignore_until=0;context_turn=true;
        yao_result_t result{};char original[32],changed[32];
        if(!yao_calculate(reading->lines,&result))return fail(XZ_FAULT_PROTOCOL);
        yao_full_name(result.original,original,sizeof(original));yao_full_name(result.changed,changed,sizeof(changed));
        snprintf(view.heard,sizeof(view.heard),"解读：%s → %s",original,changed);
        state(XZ_THINKING,"正在解读本次卦象");last_incoming=esp_timer_get_time();
        if(!send_text(json_text(j.p)))return false;
        context_upload=true;context_offset=0;context_next=listen_since=esp_timer_get_time();
#ifdef BADGE_YAO_DEVICE_PROBE
        ESP_LOGI("yao_probe","CONTEXT_SENT snapshot_bytes=%u tools=%u",(unsigned)sizeof(*reading),tools_advertised);
#endif
        reading_pending=false; // Keep the compact confirmed record pinned through this visit.
        return true;
    }
    bool upload_context_request(){
        if(!context_upload||esp_timer_get_time()<context_next)return true;
        if(context_offset==sizeof(yao_request_opus)){
            context_upload=false;last_incoming=esp_timer_get_time();return control("listen","stop");
        }
        if(context_offset+2>sizeof(yao_request_opus))return fail(XZ_FAULT_AUDIO);
        unsigned bytes=yao_request_opus[context_offset]|(unsigned)yao_request_opus[context_offset+1]<<8;context_offset+=2;
        if(!bytes||bytes>1500||context_offset+bytes>sizeof(yao_request_opus))return fail(XZ_FAULT_AUDIO);
        memcpy(packet+16,yao_request_opus+context_offset,bytes);context_offset+=bytes;
        context_next=esp_timer_get_time()+60000;badge_power_audio_activity();
        return send_encoded_packet(bytes);
    }
    bool control(const char *type,const char *state_value=nullptr,bool shutdown=false){Json j(cJSON_CreateObject());cJSON_AddStringToObject(j.p,"type",type);cJSON_AddStringToObject(j.p,"session_id",session.c_str());if(state_value)cJSON_AddStringToObject(j.p,"state",state_value);if(!strcmp(type,"listen")&&state_value&&!strcmp(state_value,"start"))cJSON_AddStringToObject(j.p,"mode","auto");return send_text(json_text(j.p),shutdown);}
    bool reserve_codec(){
        if(codec_arena)return true;
        int enc=opus_encoder_get_size(1),dec=opus_decoder_get_size(1);
        if(enc<=0||dec<=0)return false;
        if(!xz_audio_arena_layout(enc,dec,&codec_queue_offset,&codec_arena_bytes))return fail(XZ_FAULT_AUDIO);
        codec_arena=malloc(codec_arena_bytes);
#ifdef BADGE_XIAOZHI_STABILITY_PROBE
        if(codec_arena)stability_codec_allocations++;
#endif
        ESP_LOGI(TAG,"Codec arena=%u encoder=%d decoder=%d queue_offset=%u allocated=%u free=%lu",(unsigned)codec_arena_bytes,enc,dec,(unsigned)codec_queue_offset,codec_arena!=nullptr,(unsigned long)esp_get_free_heap_size());
        return codec_arena?true:memory_failure("opus_arena",codec_arena_bytes);
    }
    void free_codecs(){audio_queue=nullptr;encoder=nullptr;decoder=nullptr;view.level=0;}
    void release_codec_arena(){free_codecs();free(codec_arena);codec_arena=nullptr;codec_arena_bytes=0;codec_queue_offset=0;}
    void reclaim_for_retry(const char *reason){
        ESP_LOGI(TAG,"Active reclaim begin reason=%s heap=%lu largest=%u",reason?reason:"-",(unsigned long)esp_get_free_heap_size(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        state(XZ_CONNECTING,"正在回收语音资源");
        close();
        release_codec_arena();
        free(pcm);pcm=nullptr;free(packet);packet=nullptr;
        (void)bsp_audio_sleep();badge_network_xiaozhi_power(false,false);
        vTaskDelay(pdMS_TO_TICKS(120));
        ESP_LOGI(TAG,"Active reclaim end reason=%s heap=%lu largest=%u",reason?reason:"-",(unsigned long)esp_get_free_heap_size(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
    bool listen(){
        context_turn=false;context_upload=false;
        badge_network_xiaozhi_power(true,true);
        accept_audio=false;tts_stop=0;free_codecs();
        esp_err_t audio_result=bsp_audio_init();
        if(audio_result==ESP_OK)audio_result=bsp_audio_set_format(16000,16,1);
        if(audio_result!=ESP_OK){ESP_LOGE(TAG,"Microphone init: %s",esp_err_to_name(audio_result));fail(XZ_FAULT_AUDIO);error("麦克风初始化失败");return false;}
        if(!reserve_codec()){error("语音内存不足，请退出后重试");return false;}
        encoder=static_cast<OpusEncoder *>(codec_arena);
        int result=opus_encoder_init(encoder,16000,1,OPUS_APPLICATION_VOIP);
        if(result==OPUS_OK)result=opus_encoder_ctl(encoder,OPUS_SET_BITRATE(16000));
        if(result==OPUS_OK)result=opus_encoder_ctl(encoder,OPUS_SET_COMPLEXITY(0));
        if(result==OPUS_OK)result=opus_encoder_ctl(encoder,OPUS_SET_VBR(1));
        ESP_LOGI(TAG,"Encoder status=%d free=%lu",result,(unsigned long)esp_get_free_heap_size());
        if(result!=OPUS_OK){encoder=nullptr;fail(XZ_FAULT_AUDIO);error("语音编码初始化失败");return false;}
        input_bytes=1920;output_bytes=1500;
        // Discard the few old DMA frames before opening the microphone uplink.
#if defined(BADGE_XIAOZHI_STABILITY_PROBE)
        stability_offset=0;stability_frame=esp_timer_get_time();++stability_turn;
        ESP_LOGI("stability_bench","LISTEN turn=%u heap=%lu largest=%lu",stability_turn,(unsigned long)esp_get_free_heap_size(),(unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#elif defined(BADGE_TOOL_CLOUD_PROBE)
        cloud_offset=0;cloud_frame=esp_timer_get_time();
#elif defined(BADGE_XIAOZHI_CONVERSATION_PROBE)
        probe_offset=0;probe_next_frame=esp_timer_get_time();probe_turn++;
        ESP_LOGI("xz_probe","AUTO_LISTEN turn=%u free=%lu",probe_turn,(unsigned long)esp_get_free_heap_size());
#else
        for(unsigned i=0;i<2&&!stopping;i++){
            esp_err_t e=bsp_audio_read(pcm,1920);
            if(e!=ESP_OK){ESP_LOGE(TAG,"Microphone pre-read: %s",esp_err_to_name(e));return fail(XZ_FAULT_AUDIO);}
        }
#endif
        badge_power_audio_activity();
        if(!control("listen","start"))return false;
        listen_since=esp_timer_get_time();last_incoming=listen_since;state(XZ_LISTENING,"请直接说话，说完自动发送");return true;
    }
    bool capture(){
#if defined(BADGE_XIAOZHI_CONNECT_PROBE)
        memset(pcm,0,input_bytes);vTaskDelay(pdMS_TO_TICKS(60));connect_probe_frames++;
#elif defined(BADGE_XIAOZHI_STABILITY_PROBE)
        int64_t wait=stability_frame-esp_timer_get_time();if(wait>1000)vTaskDelay(pdMS_TO_TICKS(wait/1000));
        stability_frame+=60000;memset(pcm,0,input_bytes);
        bool silence=stability_turn==2&&esp_timer_get_time()-listen_since<22000000;
        if(!silence){
            if(stability_turn==2&&!stability_silence){stability_silence=true;configASSERT(!background_idle.load());ESP_LOGI("stability_bench","ACTIVE_SILENCE_22S_PASS");}
            size_t count=std::min<size_t>(input_bytes,sizeof(xz_probe_pcm)-stability_offset);
            if(count){memcpy(pcm,xz_probe_pcm+stability_offset,count);stability_offset+=count;}
        }
#elif defined(BADGE_TOOL_CLOUD_PROBE)
        int64_t wait=cloud_frame-esp_timer_get_time();if(wait>1000)vTaskDelay(pdMS_TO_TICKS(wait/1000));cloud_frame+=60000;
        for(int i=0;i<input_bytes/2;i+=2){int16_t v=0;if(cloud_offset<cloud_pcm_sizes[cloud_turn-1]){v=cloud_pcm[cloud_pcm_offsets[cloud_turn-1]+cloud_offset++];}pcm[i]=v;pcm[i+1]=v;}
#elif defined(BADGE_XIAOZHI_CONVERSATION_PROBE)
        int64_t wait=probe_next_frame-esp_timer_get_time();if(wait>1000)vTaskDelay(pdMS_TO_TICKS(wait/1000));
        probe_next_frame+=60000;memset(pcm,0,input_bytes);
        size_t probe_bytes=std::min<size_t>(input_bytes,sizeof(xz_probe_pcm)-probe_offset);
        if(probe_bytes){memcpy(pcm,xz_probe_pcm+probe_offset,probe_bytes);probe_offset+=probe_bytes;}
#else
        if(bsp_audio_read(pcm,input_bytes)!=ESP_OK)return fail(XZ_FAULT_AUDIO);
#endif
        badge_power_audio_activity();
        uint64_t level=0;for(int i=0;i<input_bytes/2;i++)level+=std::abs((int)pcm[i]);view.level=std::min<unsigned>(100,level/(input_bytes/2)/45);
        render(); // Hand off fresh input energy before the more expensive encoder.
        int encoded=opus_encode(encoder,pcm,960,packet+16,1500);
        if(encoded<=0||encoded>1500)return fail(XZ_FAULT_AUDIO);
        if(stopping)return false;
        return send_encoded_packet(encoded);
    }
    bool send_encoded_packet(unsigned encoded){
        if(mqtt){memcpy(packet,nonce,16);xz_udp_header(packet,encoded,(esp_timer_get_time()-listen_since)/1000,++tx);if(!crypt(packet+16,encoded,packet,packet+16))return false;return send(udp,packet,16+encoded,0)==(int)(16+encoded);}
        unsigned offset=16;
        if(version==2){offset=0;memset(packet,0,16);packet[1]=2;packet[12]=(encoded>>24)&255;packet[13]=(encoded>>16)&255;packet[14]=(encoded>>8)&255;packet[15]=encoded&255;}
        if(version==3){offset=12;packet[12]=packet[13]=0;packet[14]=encoded>>8;packet[15]=encoded&255;}
        size_t bytes=16+encoded-offset;return esp_transport_ws_send_raw(ws,static_cast<ws_transport_opcodes_t>(WS_TRANSPORT_OPCODES_FIN|WS_TRANSPORT_OPCODES_BINARY),reinterpret_cast<char *>(packet+offset),bytes,1000)==(int)bytes;
    }
    bool crypt(const uint8_t *in,size_t length,const uint8_t *iv,uint8_t *out){uint8_t counter[16],stream[16]{};memcpy(counter,iv,16);size_t offset=0;mbedtls_aes_context aes;mbedtls_aes_init(&aes);int e=mbedtls_aes_setkey_enc(&aes,key,128);if(!e)e=mbedtls_aes_crypt_ctr(&aes,length,&offset,counter,stream,in,out);mbedtls_aes_free(&aes);return e==0;}
    bool prepare_decoder(){
        if(decoder)return true;
        if(!reserve_codec())return false;
        decoder=static_cast<OpusDecoder *>(codec_arena);
        int result=opus_decoder_init(decoder,16000,1);
        if(result!=OPUS_OK){decoder=nullptr;return fail(XZ_FAULT_AUDIO);}
        encoder=nullptr;
        audio_queue=reinterpret_cast<xz_audio_buffer_t *>(static_cast<uint8_t *>(codec_arena)+codec_queue_offset);
        memset(audio_queue,0,sizeof(*audio_queue));
        if(bsp_audio_init()!=ESP_OK||bsp_audio_set_format(16000,16,1)!=ESP_OK)return fail(XZ_FAULT_AUDIO);
        applied_volume=101;ESP_LOGI(TAG,"Decoder ready free=%lu",(unsigned long)esp_get_free_heap_size());
        return true;
    }
    void audio(const uint8_t *data,size_t length){
        if(!accept_audio||!length||stopping)return;
        played=true;badge_power_audio_activity();
        if(!prepare_decoder())return;
        int samples=opus_decode(decoder,data,length,pcm,1920,0);
        if(samples<=0||samples>1920){fail(XZ_FAULT_AUDIO);return;}
        unsigned volume=wanted_volume.load();if(volume!=applied_volume){bsp_audio_set_volume(volume);applied_volume=volume;}
        int64_t start=esp_timer_get_time();
        if(last_write_end){int64_t gap=start-last_write_end;if(gap>max_write_gap)max_write_gap=gap;if(gap>20000)late_writes++;}
        if(bsp_audio_write(pcm,samples*sizeof(int16_t))!=ESP_OK){fail(XZ_FAULT_AUDIO);return;}
        last_write_end=esp_timer_get_time();played_packets++;view.played_ms+=(unsigned)samples/16;
#ifdef BADGE_YAO_DEVICE_PROBE
        if(context_turn)yao_probe_samples.fetch_add(samples);
#endif
        unsigned energy=0;for(int i=0;i<samples;i++)energy+=std::abs((int)pcm[i]);
        view.level=std::min(100u,energy/(unsigned)samples/45);
        // Six 240-sample DMA buffers at 16 kHz retain at most 90 ms of output.
        view.level_until_ms=lv_tick_get()+100;
        render(); // Coalesce directly after output; never wait for an LVGL lock.
    }
    void receive_audio(const uint8_t *data,size_t length){
        if(!accept_audio||stopping)return;
        if(!prepare_decoder())return; // Allocate the large codec before the small queue.
        last_audio=esp_timer_get_time();
        if(!audio_queue||!xz_audio_push(audio_queue,data,length,last_audio)){ESP_LOGE(TAG,"Audio queue rejected size=%u samples=%u used=%u free=%lu",(unsigned)length,xz_opus_samples(data,length),audio_queue?audio_queue->used:0,(unsigned long)esp_get_free_heap_size());if(!audio_queue)memory_failure("playback_queue",sizeof(xz_audio_buffer_t));else fail(XZ_FAULT_QUEUE);return;}
        received_packets++;
    }
    void play_buffered(){
        if(xz_audio_ready(audio_queue,esp_timer_get_time(),tts_stop!=0)){
            size_t bytes=xz_audio_pop(audio_queue,packet+16,1500);
            if(bytes)audio(packet+16,bytes);
        }
    }
    void mcp(cJSON *payload){
        const bool catalog=!strcmp(str(payload,"method"),"tools/list");
#ifdef BADGE_XIAOZHI_CONNECT_PROBE
        if(!strcmp(str(payload,"method"),"tools/list"))ESP_LOGI("connect_probe","DISCOVERY_BEGIN state=%u encoder=%u free=%lu largest=%lu",(unsigned)view.state,encoder!=nullptr,(unsigned long)esp_get_free_heap_size(),(unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#endif
        // Keep the pre-handshake codec arena reserved. Freeing it for discovery
        // lets small network/UI allocations split the only contiguous 24 KB block.
        // The bounded catalog no longer embeds a reading snapshot.
        if(!strcmp(str(payload,"method"),"tools/list"))badge_network_xiaozhi_power(true,true);
        badge_network_status_t net{};badge_network_status(&net);uint32_t ticket=0;
        Json result(context_turn?xz_tools_handle_reading(payload,wanted_volume.load(),net.connected,&ticket,reading):xz_tools_handle(payload,wanted_volume.load(),net.connected,&ticket));if(!result.p){if(cJSON_HasObjectItem(payload,"id"))memory_failure("tool_result");return;}
#ifdef BADGE_YAO_DEVICE_PROBE
        if(!strcmp(str(cJSON_GetObjectItemCaseSensitive(payload,"params"),"name"),"self.yao.get")){
            const cJSON *body=cJSON_GetObjectItemCaseSensitive(result.p,"result");
            Json record(cJSON_Parse(str(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(body,"content"),0),"text")));
            const cJSON *a=cJSON_GetObjectItemCaseSensitive(record.p,"original"),*b=cJSON_GetObjectItemCaseSensitive(record.p,"changed");
            configASSERT(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(a,"yaoci"))==6&&cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(b,"yaoci"))==6);
            ESP_LOGI("yao_probe","FULL_TEXT_REPLY_PASS original=%s changed=%s lines=6+6",str(a,"name"),str(b,"name"));
        }
#endif
#ifdef BADGE_TOOL_CLOUD_PROBE
        ESP_LOGI("tool_cloud_private","MCP request=%s response=%s",json_text(payload).c_str(),json_text(result.p).c_str());
        ESP_LOGI("tool_cloud","MCP received method=%s queued=%u error=%u",str(payload,"method"),ticket!=0,cJSON_HasObjectItem(result.p,"error")||cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(result.p,"result"),"isError")));
#endif
        badge_control_command_t command{};
        if(badge_control_peek(ticket,&command)&&badge_control_stays(command.kind)){
            // Wake the navigation owner immediately. It never joins this worker
            // for in-place actions, so completion can be awaited without a cycle.
            badge_control_commit(ticket,true);int completion=0;int64_t end=esp_timer_get_time()+2000000;
            while(!stopping&&(completion=badge_control_result(ticket))==0&&esp_timer_get_time()<end)vTaskDelay(pdMS_TO_TICKS(5));
            xz_tools_complete(result.p,completion);ticket=0;
            if(command.kind==BC_XZ_VOLUME&&completion==1)sync_volume(false,false);
        }
        Json wrapper(cJSON_CreateObject());cJSON_AddStringToObject(wrapper.p,"type","mcp");cJSON_AddStringToObject(wrapper.p,"session_id",session.c_str());
        if(!cJSON_AddItemToObject(wrapper.p,"payload",result.p)){badge_control_commit(ticket,false);return;}
        result.p=nullptr; // Transfer ownership; avoid duplicating the tools catalog in scarce RAM.
        size_t response_length=0;bool sent=false;
        if(catalog)sent=send_catalog(wrapper,response_length);
        else{
            // cJSON returns NULL on OOM; no throwing std::string copy.
            JsonText response;response.p=cJSON_PrintUnformatted(wrapper.p);
            cJSON_Delete(wrapper.p);wrapper.p=nullptr;
            if(!response.p||!response.p[0]){badge_control_commit(ticket,false);memory_failure("tool_response_json");return;}
            response_length=strlen(response.p);sent=send_text(response.p,response_length);
        }
        badge_control_commit(ticket,sent);
#ifdef BADGE_YAO_DEVICE_PROBE
        if(catalog)ESP_LOGI("yao_probe","CATALOG_SENT bytes=%u ok=%u free=%lu",(unsigned)response_length,sent,(unsigned long)esp_get_free_heap_size());
#endif
        if(sent&&!strcmp(str(payload,"method"),"tools/list")){
            tools_advertised=true;
#ifdef BADGE_XIAOZHI_CONNECT_PROBE
            connect_probe_catalog++;ESP_LOGI("connect_probe","DISCOVERY_DONE bytes=%u free=%lu",(unsigned)response_length,(unsigned long)esp_get_free_heap_size());
#endif
        }
    }
    void json(const char *data,size_t size){
        Json root(cJSON_ParseWithLength(data,size));if(!root.p)return;const char *type=str(root.p,"type");last_incoming=esp_timer_get_time();
#ifdef BADGE_YAO_DEVICE_PROBE
        const cJSON *mp=cJSON_GetObjectItemCaseSensitive(root.p,"payload");
        ESP_LOGI("yao_probe","RX type=%s state=%s method=%s tool=%s",type,str(root.p,"state"),str(mp,"method"),str(cJSON_GetObjectItemCaseSensitive(mp,"params"),"name"));
        if(!strcmp(type,"alert"))ESP_LOGW("yao_probe","ALERT message=%s",str(root.p,"message"));
#endif
        // A retired conversation must not deliver captions or MCP commands to
        // the new page. Hello establishes the only valid session identifier.
        const char *sid=str(root.p,"session_id");
#ifdef BADGE_TOOL_CLOUD_PROBE
        ESP_LOGI("tool_cloud","RX type=%s hello=%u sid_present=%u sid_matches=%u method=%s",type,hello,*sid!=0,session==sid,str(cJSON_GetObjectItemCaseSensitive(root.p,"payload"),"method"));
#endif
        if(!xz_message_allowed(type,str(cJSON_GetObjectItemCaseSensitive(root.p,"payload"),"method"),hello,*sid!=0,session==sid))return;
        if(!strcmp(type,"hello")){
            if(hello)return; // Duplicate hello must not leak/replace a live UDP socket.
            session=str(root.p,"session_id");if(session.size()>128){fail(XZ_FAULT_PROTOCOL);return;}
            auto params=cJSON_GetObjectItemCaseSensitive(root.p,"audio_params");const char *format=str(params,"format");if((*format&&strcmp(format,"opus"))||num(params,"channels",1)!=1){fail(XZ_FAULT_PROTOCOL);return;}
            if(mqtt){auto u=cJSON_GetObjectItemCaseSensitive(root.p,"udp");const char *host=str(u,"server");int port=num(u,"port",0);if(!*host||strlen(host)>192||port<1||port>65535||!xz_hex16(str(u,"key"),key)||!xz_hex16(str(u,"nonce"),nonce)){fail(XZ_FAULT_PROTOCOL);return;}addrinfo hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_DGRAM;addrinfo *addr=nullptr;char p[8];snprintf(p,sizeof(p),"%d",port);if(getaddrinfo(host,p,&hints,&addr)!=0){fail(XZ_FAULT_TRANSPORT);return;}udp=socket(addr->ai_family,addr->ai_socktype,addr->ai_protocol);if(udp<0||::connect(udp,addr->ai_addr,addr->ai_addrlen)!=0)fail(XZ_FAULT_TRANSPORT);freeaddrinfo(addr);if(failed)return;timeval timeout{0,1000};setsockopt(udp,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));}
            else if(strcmp(str(root.p,"transport"),"websocket")){fail(XZ_FAULT_PROTOCOL);return;}
            hello=true;
        }else if(!strcmp(type,"tts")){
            // Auto mode ends microphone upload when the server starts a reply.
            // Paused sessions and the short interrupt tail must not restart it.
            if(!xz_tts_allowed(conversation,view.state,esp_timer_get_time(),ignore_until))return;
            const char *s=str(root.p,"state");if(!strcmp(s,"start")){free_codecs();view.text[0]=0;view.played_ms=view.caption_start_ms=0;received_packets=played_packets=late_writes=udp_missing=0;last_write_end=max_write_gap=0;accept_audio=true;tts_stop=0;badge_network_xiaozhi_power(true,true);state(XZ_SPEAKING,"小智正在回答");}
            else if(!strcmp(s,"stop")){tts_stop=esp_timer_get_time();
#ifdef BADGE_YAO_DEVICE_PROBE
                if(context_turn){yao_probe_tts_stop=1;conversation=false;}
#endif
            }
            else if(!strcmp(s,"sentence_start")){
#ifdef BADGE_YAO_DEVICE_PROBE
                if(context_turn)ESP_LOGI("yao_probe","CAPTION=%s",str(root.p,"text"));
#endif
#ifdef BADGE_TOOL_CLOUD_PROBE
                ESP_LOGI("tool_cloud_private","CAPTION=%s",str(root.p,"text"));
                ESP_LOGI("tool_cloud","CAPTION turn=%u literal_percent_s=%u",cloud_turn,strstr(str(root.p,"text"),"%s")!=nullptr);
#endif
                if(xz_caption_is_tool_progress(str(root.p,"text"))){ESP_LOGI(TAG,"Tool progress caption omitted");return;}
                view.caption_start_ms=view.played_ms+(audio_queue?audio_queue->samples/16:0);xz_caption_normalize(view.text,sizeof(view.text),str(root.p,"text"),xiaozhi_font_has_glyph);render(true);}
        }else if(!strcmp(type,"stt")){
            if(xz_tts_allowed(conversation,view.state,esp_timer_get_time(),ignore_until)&&*str(root.p,"text")){
                if(!context_turn)xz_caption_normalize(view.heard,sizeof(view.heard),str(root.p,"text"),xiaozhi_font_has_glyph);
#ifdef BADGE_XIAOZHI_CONVERSATION_PROBE
                probe_stt++;ESP_LOGI("xz_probe","AUTO_STT turn=%u bytes=%u",probe_turn,(unsigned)strlen(view.heard));
#endif
                if(view.state==XZ_LISTENING){view.text[0]=0;free_codecs();accept_audio=true;state(XZ_THINKING,"已听清，正在准备回复");}
                else render(true);
            }
        }
        else if(!strcmp(type,"llm")){
            if(xz_tts_allowed(conversation,view.state,esp_timer_get_time(),ignore_until)&&*str(root.p,"emotion")){
                view.emotion=xz_face_emotion(str(root.p,"emotion"));
                view.face_variant=xz_face_select(&face_selector,view.emotion,esp_random());render(true);
            }
        }
        else if(!strcmp(type,"mcp"))mcp(cJSON_GetObjectItemCaseSensitive(root.p,"payload"));
        else if(!strcmp(type,"goodbye")){fail(XZ_FAULT_SERVER_END);}
        else if(!strcmp(type,"alert")){xz_caption_normalize(view.text,sizeof(view.text),str(root.p,"message"),xiaozhi_font_has_glyph);conversation=false;accept_audio=false;state(XZ_READY,"服务提示，按 OK 重试或返回");}
    }
    void poll(){
        if(stopping)return;
        if(mqtt){
            // Drain control traffic first. Audio is accepted only between TTS start/stop.
            char *message=nullptr;
            while(!stopping&&xQueueReceive(messages,&message,0)==pdTRUE){json(message,strlen(message));free(message);}
            // Drain network bursts before any blocking I2S write. Keep room for
            // the maximum packet; leave excess packets in the socket mailbox.
            for(unsigned i=0;udp>=0&&i<16&&!stopping&&!failed&&xz_audio_room(audio_queue);i++){
                int n=recv(udp,packet,1517,MSG_DONTWAIT);if(n<=0)break;
                uint32_t sequence;
                if(xz_udp_audio(packet,n,rx,&sequence)&&crypt(packet+16,n-16,packet,packet+16)){
                    if(accept_audio&&received_packets&&sequence>rx+1)udp_missing+=sequence-rx-1;
                    rx=sequence;last_incoming=esp_timer_get_time();receive_audio(packet+16,n-16);
                }
            }
        }else if(ws){
          for(unsigned budget=0;budget<16&&!stopping&&!failed&&xz_audio_room(audio_queue);budget++){
            int ready=esp_transport_poll_read(ws,1);if(ready<0){fail(XZ_FAULT_TRANSPORT);return;}if(!ready)return;
            int n=esp_transport_read(ws,incoming->text,MAX_MESSAGE,500);if(n<0){fail(XZ_FAULT_TRANSPORT);return;}if(!n)return;
            unsigned length=esp_transport_ws_get_read_payload_len(ws);int opcode=esp_transport_ws_get_read_opcode(ws);if(length>MAX_MESSAGE){fail(XZ_FAULT_PROTOCOL);return;}
            unsigned used=n;while(used<length&&!stopping){int part=esp_transport_read(ws,incoming->text+used,length-used,500);if(part<=0){fail(XZ_FAULT_TRANSPORT);return;}used+=part;}
            if(!esp_transport_ws_get_fin_flag(ws)){fail(XZ_FAULT_PROTOCOL);return;} // Fail closed on unsupported fragmented messages.
            if(opcode==WS_TRANSPORT_OPCODES_TEXT){incoming->text[used]=0;json(incoming->text,used);}
            else if(opcode==WS_TRANSPORT_OPCODES_BINARY){const uint8_t *p;size_t bytes;if(!xz_ws_audio(version,reinterpret_cast<uint8_t *>(incoming->text),used,&p,&bytes)){fail(XZ_FAULT_PROTOCOL);return;}last_incoming=esp_timer_get_time();receive_audio(p,bytes);}
            else if(opcode==WS_TRANSPORT_OPCODES_CLOSE)fail(XZ_FAULT_TRANSPORT);
          }
        }
    }
    bool park(){
        // The navigation owner disables tools before requesting stop. The
        // worker completes all audio/UI/power work before transferring ownership.
        bool retain=mqtt&&connected&&!failed&&hello;
        parked=true;conversation=false;accept_audio=false;
        if(retain){
            bool ended=control("listen","stop",true);
            ended=control("abort",nullptr,true)&&ended;
            ended=control("goodbye",nullptr,true)&&ended;
            retain=ended;
        }
        free_codecs();free(codec_arena);codec_arena=nullptr;
        sync_volume(true,true);volume_loaded=false;
        if(udp>=0){::close(udp);udp=-1;}
        hello=false;session.clear();tx=rx=0;
        memset(key,0,sizeof(key));memset(nonce,0,sizeof(nonce));
        free(pcm);pcm=nullptr;free(packet);packet=nullptr;
        leave_power();
        parked_at=esp_timer_get_time();
        // An in-flight callback may finish queueing once; resume drains it while
        // still parked. The queue stays bounded and never runs in the background.
        if(retain)ESP_LOGI(TAG,"Connection parked heap=%lu",(unsigned long)esp_get_free_heap_size());
        return retain;
    }
    void close(){
        // Tool discovery belongs to the MQTT/TLS transport, not each audio hello.
        tools_advertised=false;
        closing=true;context_upload=false;badge_control_cancel();
        // A transport retry must not give the contiguous codec block back to
        // the general heap while this worker's 28 KiB stack remains allocated.
        // Park/destruction release it only when leaving the conversation.
        conversation=false;accept_audio=false;free_codecs();sync_volume(true,true);
        if(udp>=0){::close(udp);udp=-1;}
        if(mqtt){esp_mqtt_client_stop(mqtt);esp_mqtt_client_destroy(mqtt);mqtt=nullptr;}
        if(ws){esp_transport_close(ws);esp_transport_destroy(ws);ws=nullptr;}free(incoming);incoming=nullptr;
        if(parent){esp_transport_destroy(parent);parent=nullptr;}
        if(messages){char *message=nullptr;while(xQueueReceive(messages,&message,0)==pdTRUE)free(message);vQueueDelete(messages);messages=nullptr;}free(assembly);assembly=nullptr;connected=false;hello=false;tx=rx=0;
        // Release service credentials with the mini app; do not write them to badge profiles.
        token.clear();password.clear();parked=false;closing=false;
    }
    void run(){
        view={};observed_clicks=0;retries=0;retry_at=0;
#ifdef BADGE_TOOL_CLOUD_PROBE
        cloud_turn=0;cloud_waiting=false;
#endif
        resume_at=ignore_until=idle_since=tts_stop=0;last_draw=0;
        if(messages){char *message=nullptr;while(xQueueReceive(messages,&message,0)==pdTRUE)free(message);}
        applied_volume=101;restore_volume();view.volume=wanted_volume.load();
        pcm=static_cast<int16_t *>(malloc(3840));packet=static_cast<uint8_t *>(malloc(1517));
        if(!pcm||!packet||!identity()){error("小智启动失败，请返回后重试");return;}
#ifdef BADGE_XIAOZHI_DEVICE_PROBE
#ifdef BADGE_XIAOZHI_VERIFY_ALL_VOICES
        bool decoded=voice_probe_decode_all([]{return stopping.load();});if(stopping)return;configASSERT(decoded);
#endif
        badge_network_start_setup();
        for(unsigned i=0;i<100;i++){badge_network_status_t net{};badge_network_status(&net);if(net.active)break;vTaskDelay(pdMS_TO_TICKS(50));}
        configASSERT(bsp_audio_init()==ESP_OK&&bsp_audio_set_format(16000,16,1)==ESP_OK);
        ESP_LOGI("xz_probe","AP_AUDIO_READY free=%lu largest=%u",(unsigned long)esp_get_free_heap_size(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        {
            void *bench_encoder=nullptr,*bench_decoder=nullptr;
            esp_opus_enc_config_t cfg=ESP_OPUS_ENC_CONFIG_DEFAULT();cfg.sample_rate=16000;cfg.channel=1;cfg.bitrate=16000;cfg.frame_duration=ESP_OPUS_ENC_FRAME_DURATION_60_MS;cfg.application_mode=ESP_OPUS_ENC_APPLICATION_VOIP;cfg.complexity=0;cfg.enable_vbr=true;
            int result=esp_opus_enc_open(&cfg,sizeof(cfg),&bench_encoder);ESP_LOGI("xz_probe","ENCODER_OPEN result=%d free=%lu",result,(unsigned long)esp_get_free_heap_size());configASSERT(result==ESP_AUDIO_ERR_OK&&bench_encoder);
            esp_audio_enc_in_frame_t in{};in.buffer=reinterpret_cast<uint8_t *>(pcm);in.len=1920;esp_audio_enc_out_frame_t out{};out.buffer=packet+16;out.len=1500;
            for(unsigned i=0;i<960;i++)pcm[i]=(i%32<16)?4000:-4000;
            int64_t begin=esp_timer_get_time();for(unsigned i=0;i<30;i++){configASSERT(esp_opus_enc_process(bench_encoder,&in,&out)==ESP_AUDIO_ERR_OK);vTaskDelay(pdMS_TO_TICKS(1));}
            ESP_LOGI("xz_probe","ENCODE_60MS average_us=%lld bytes=%lu",(long long)((esp_timer_get_time()-begin)/30),(unsigned long)out.encoded_bytes);
            esp_opus_enc_close(bench_encoder);bench_encoder=nullptr;
            esp_opus_dec_cfg_t d=ESP_OPUS_DEC_CONFIG_DEFAULT();d.sample_rate=16000;d.channel=1;d.frame_duration=ESP_OPUS_DEC_FRAME_DURATION_120_MS;configASSERT(esp_opus_dec_open(&d,sizeof(d),&bench_decoder)==ESP_AUDIO_ERR_OK&&bench_decoder);
            esp_audio_dec_in_raw_t raw{};raw.buffer=packet+16;raw.len=out.encoded_bytes;esp_audio_dec_out_frame_t frame{};frame.buffer=reinterpret_cast<uint8_t *>(pcm);frame.len=3840;esp_audio_dec_info_t info{};configASSERT(esp_opus_dec_decode(bench_decoder,&raw,&frame,&info)==ESP_AUDIO_ERR_OK&&frame.decoded_size==1920);
            esp_opus_dec_close(bench_decoder);ESP_LOGI("xz_probe","CODECS_PASS free=%lu",(unsigned long)esp_get_free_heap_size());
        }
#ifdef BADGE_XIAOZHI_POWER_PROBE
        uint8_t prior_brightness=bsp_display_brightness();
        bsp_display_backlight(0);configASSERT(bsp_display_suspend()==ESP_OK);
        configASSERT(bsp_display_resume()==ESP_OK&&bsp_display_brightness()==80);
        bsp_display_backlight(prior_brightness);ESP_LOGI("xz_probe","BRIGHTNESS_FALLBACK_PASS");
        for(unsigned cycle=0;cycle<3;cycle++){
            bsp_audio_set_volume(63);configASSERT(bsp_audio_sleep()==ESP_OK&&bsp_audio_is_sleeping());
            configASSERT(bsp_audio_wake()==ESP_OK&&!bsp_audio_is_sleeping()&&bsp_audio_get_volume()==63);
            configASSERT(bsp_audio_read(pcm,1920)==ESP_OK);
        }
        bsp_audio_set_volume(wanted_volume.load());ESP_LOGI("xz_probe","AUDIO_RESTORE_3_CYCLES_PASS");
#endif
        probe_ready=true;
#endif
        badge_power_enter();power_owned=true;
        state(XZ_IDLE,"说完自动发送");
        ESP_LOGI(TAG,"Ready free=%lu",(unsigned long)esp_get_free_heap_size());
#ifdef BADGE_XIAOZHI_PLAYBACK_PROBE
        bool playback_probe_sent=false;
#endif
        while(!stopping){
            bool busy=view.state==XZ_CONNECTING||view.state==XZ_LISTENING||view.state==XZ_THINKING||view.state==XZ_SPEAKING;
            sync_volume(!busy,false);
            badge_power_tick(busy,view.state==XZ_LISTENING||view.state==XZ_SPEAKING);
#ifdef BADGE_XIAOZHI_POWER_PROBE
            if(probe_mic_request.exchange(false)){
                configASSERT(!badge_power_screen_off()&&bsp_display_brightness()>0&&view.state==XZ_IDLE);
                configASSERT(!bsp_audio_is_sleeping());
                configASSERT(bsp_audio_init()==ESP_OK&&bsp_audio_set_format(16000,16,1)==ESP_OK);
                configASSERT(bsp_audio_read(pcm,1920)==ESP_OK); // RAM only, never saved or uploaded.
                badge_power_audio_activity();
                ESP_LOGI("xz_probe","MIC_WAKE_PASS bytes=1920 brightness=%u",bsp_display_brightness());probe_mic_done=true;
            }
#endif
            if(retry_at&&esp_timer_get_time()>=retry_at&&!stopping){
                retry_at=0;
                if(!establish()&&view.state!=XZ_ACTIVATION&&!stopping)schedule_retry(fault.load());
            }
            unsigned pressed=clicks.load();if(pressed!=observed_clicks){observed_clicks=pressed;
                xz_action_t action=xz_ok_action(view.state,hello&&!failed);
                if(action==XZ_PAUSE){context_upload=false;conversation=false;accept_audio=false;tts_stop=0;free_codecs();control("listen","stop");control("abort");state(XZ_READY,"对话已暂停");}
                else if(action==XZ_START||action==XZ_INTERRUPT){
                    context_upload=false;conversation=true;accept_audio=false;tts_stop=0;free_codecs();
                    ignore_until=action==XZ_INTERRUPT?esp_timer_get_time()+500000:0;resume_at=ignore_until;
                    if(action==XZ_INTERRUPT){control("abort");}
                    state(XZ_READY,"准备聆听，请直接说话");
                }
                else if(action==XZ_CONNECT){
                    retry_at=0;retries=0;
                    reclaim_for_retry("ok_retry");
                    pcm=static_cast<int16_t *>(malloc(3840));packet=static_cast<uint8_t *>(malloc(1517));
                    if(!pcm||!packet||!identity()){error("小智启动失败，请返回后重试");return;}
                    fault=XZ_FAULT_NONE;failed=false;
                    establish();observed_clicks=clicks.load();
                }
            }
            if(hello&&!stopping){
#ifndef BADGE_TOOL_CLOUD_PROBE
                if(reading_pending&&view.state==XZ_READY){if(tools_advertised&&!send_initial_context())fail(XZ_FAULT_PROTOCOL);}
                else if(xz_auto_listen(conversation,view.state,esp_timer_get_time(),resume_at)&&!listen())fail(XZ_FAULT_PROTOCOL);
#else
                if(cloud_waiting&&esp_timer_get_time()-cloud_sent>35000000){cloud_waiting=false;conversation=false;tts_stop=0;free_codecs();state(XZ_READY,"测试等待超时");ESP_LOGI("tool_cloud","TURN_TIMEOUT turn=%u",cloud_turn);}
                if(!cloud_waiting&&view.state==XZ_READY&&cloud_turn==0&&cloud_sequence<4){
                    cloud_turn=++cloud_sequence;cloud_sent=esp_timer_get_time();cloud_waiting=true;conversation=true;
                    if(!listen()){fail(XZ_FAULT_PROTOCOL);}
                    ESP_LOGI("tool_cloud","TURN_SENT=%u",cloud_turn);
                }
#endif
#ifdef BADGE_XIAOZHI_PLAYBACK_PROBE
                if(!playback_probe_sent&&view.state==XZ_READY){
                    Json test(cJSON_CreateObject());cJSON_AddStringToObject(test.p,"type","listen");
                    cJSON_AddStringToObject(test.p,"session_id",session.c_str());cJSON_AddStringToObject(test.p,"state","detect");
                    cJSON_AddStringToObject(test.p,"text","你好小智");
                    conversation=true;badge_network_xiaozhi_power(true,true);state(XZ_THINKING,"等待测试回复");
                    send_text(json_text(test.p));playback_probe_sent=true;
                    // A synthetic Opus silence packet establishes the UDP return
                    // path through NAT, normally established by microphone uplink.
                    if(mqtt){
                        vTaskDelay(pdMS_TO_TICKS(200));
                        const uint8_t silence[]={0xf8,0xff,0xfe};memcpy(packet,nonce,16);
                        xz_udp_header(packet,sizeof(silence),0,++tx);
                        if(!crypt(silence,sizeof(silence),packet,packet+16)||send(udp,packet,19,0)!=19)fail(XZ_FAULT_PROTOCOL);
                    }
                }
#endif
                played=false;
                if(context_upload&&!upload_context_request())fail(XZ_FAULT_TRANSPORT);
                if(view.state==XZ_LISTENING&&!capture())fail(XZ_FAULT_TRANSPORT);
                poll();
                if(!failed)play_buffered();
                // MQTT control and UDP audio can arrive in different orders.
                // Drain the final packets instead of cutting the last syllable.
                if(xz_tts_drained(tts_stop,last_audio,esp_timer_get_time())&&(!audio_queue||!audio_queue->count)){
#ifdef BADGE_XIAOZHI_STABILITY_PROBE
                    unsigned completed=++stability_replies;
                    ESP_LOGI("stability_bench","REPLY=%u packets=%u/%u heap=%lu largest=%lu stack=%u",completed,played_packets,received_packets,(unsigned long)esp_get_free_heap_size(),(unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),(unsigned)uxTaskGetStackHighWaterMark(nullptr));
                    if(completed>=5)conversation=false;
                    if(completed==3&&!stability_injected){stability_injected=true;fail(XZ_FAULT_TRANSPORT);ESP_LOGI("stability_bench","INJECT_TRANSPORT_FAILURE");}
#endif
                    ESP_LOGI(TAG,"Playback packets=%u/%u udp_missing=%u late_writes=%u max_gap_us=%lld buffer_peak=%u",played_packets,received_packets,udp_missing,late_writes,(long long)max_write_gap,audio_queue?audio_queue->peak:0);
#ifdef BADGE_XIAOZHI_PLAYBACK_PROBE
                    ESP_LOGI("xz_probe","PLAYBACK_RESULT decoded=%u received=%u max_gap_us=%lld free=%lu",played_packets,received_packets,(long long)max_write_gap,(unsigned long)esp_get_free_heap_size());
                    playback_probe_done=played_packets>10&&played_packets==received_packets;conversation=false;
#endif
#ifdef BADGE_XIAOZHI_CONVERSATION_PROBE
                    ESP_LOGI("xz_probe","AUTO_REPLY turn=%u packets=%u/%u gaps=%u max_us=%lld",probe_turn,played_packets,received_packets,udp_missing,(long long)max_write_gap);
                    if(probe_turn>=2){conversation=false;conversation_probe_done=probe_stt>=2&&played_packets>10&&played_packets==received_packets;}
#endif
#ifdef BADGE_TOOL_CLOUD_PROBE
                    cloud_waiting=false;conversation=false;cloud_completed=cloud_turn;ESP_LOGI("tool_cloud","TURN_DONE=%u",cloud_turn);
#endif
                    retries=0;tts_stop=0;accept_audio=false;free_codecs();sync_volume(true,false);resume_at=esp_timer_get_time();state(XZ_READY,conversation?"回复结束，继续聆听":"对话已暂停");
                }
                if(view.state==XZ_LISTENING&&esp_timer_get_time()-listen_since>30000000){free_codecs();control("listen","stop");accept_audio=true;state(XZ_THINKING,"本轮录音已自动结束，等待回答");last_incoming=esp_timer_get_time();}
                if(conversation&&esp_timer_get_time()-last_incoming>90000000)fail(XZ_FAULT_TIMEOUT);
                if(failed){
                    bool active=conversation;xz_fault_t reason=fault.load();close();
                    if(reason==XZ_FAULT_SERVER_END)state(XZ_IDLE,"本轮对话已结束，按 OK 继续");
                    else if(!(active&&schedule_retry(reason)))error(failure_text());
                }
            }
            bool idle=!conversation&&(view.state==XZ_IDLE||view.state==XZ_READY);
            if(!idle)idle_since=0;else if(!idle_since)idle_since=esp_timer_get_time();
            background_idle=xz_background_idle(view.state,conversation,idle_since,esp_timer_get_time());
            render();vTaskDelay(pdMS_TO_TICKS(badge_power_screen_off()?50:view.state==XZ_LISTENING||view.state==XZ_SPEAKING||view.state==XZ_THINKING||played?1:50));
        }
    }
};
// Only the navigation task accesses parked_session except the final handoff
// before giving done. started stays true until navigation joins the worker.
Session *parked_session=nullptr;
void reclaim_idle_connection(const char *reason,bool force=false){
    size_t free8=heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    bool low=largest<49152||free8<98304;
    if((force||low)&&parked_session){
        ESP_LOGI(TAG,"Idle reclaim begin reason=%s force=%u heap=%u largest=%u",reason?reason:"-",force?1:0,(unsigned)free8,(unsigned)largest);
        delete parked_session;parked_session=nullptr;
        (void)bsp_audio_sleep();badge_network_xiaozhi_power(false,false);
        vTaskDelay(pdMS_TO_TICKS(120));
        ESP_LOGI(TAG,"Idle reclaim end reason=%s heap=%lu largest=%u",reason?reason:"-",(unsigned long)esp_get_free_heap_size(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }else if(force){
        ESP_LOGI(TAG,"Idle reclaim audio-only reason=%s heap=%u largest=%u",reason?reason:"-",(unsigned)free8,(unsigned)largest);
        (void)bsp_audio_sleep();badge_network_xiaozhi_power(false,false);
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}
void worker(void *arg){
    auto *s=static_cast<Session *>(arg);
    s->run();
    if(s->park())parked_session=s;else delete s;
    ESP_LOGI(TAG,"Stopped free=%lu stack=%u",(unsigned long)esp_get_free_heap_size(),(unsigned)uxTaskGetStackHighWaterMark(nullptr));
    xSemaphoreGive(done);
    // The joiner deletes this task synchronously. Self-deletion defers the
    // 28 KiB stack to Idle and can make an immediate re-entry fail allocation.
    for(;;)vTaskSuspend(nullptr);
}
}
#ifdef BADGE_XIAOZHI_CONNECT_PROBE
extern "C" unsigned demo_xiaozhi_connect_probe(unsigned f){return f==0?connect_probe_state.load():f==1?connect_probe_fault.load():f==2?connect_probe_catalog.load():connect_probe_frames.load();}
#endif
extern "C" void demo_xiaozhi_enter(){
    return_state=XZ_IDLE;
    badge_network_status_t net{};badge_network_status(&net);
    if(net.active){
        badge_network_close_ap();
        ESP_LOGI(TAG,"Entering XiaoZhi: automatically closed setup hotspot");
    }
    xiaozhi_ui_create();xz_snapshot_t s{};s.volume=wanted_volume.load();
    if(net.active)xz_text_copy(s.detail,sizeof(s.detail),"已自动关闭配网热点\n说完自动发送");
    else xz_text_copy(s.detail,sizeof(s.detail),"说完自动发送");
    xiaozhi_ui_render(&s);
}
extern "C" void demo_xiaozhi_exit(){xiaozhi_ui_destroy();}
#ifdef BADGE_YAO_DEVICE_PROBE
extern "C" unsigned demo_xiaozhi_yao_probe(unsigned field){return field?yao_probe_tts_stop.load():yao_probe_samples.load();}
#endif
#if defined(BADGE_XIAOZHI_DEVICE_PROBE) || defined(BADGE_CONTROL_DEVICE_PROBE) || defined(BADGE_TOOL_CLOUD_PROBE)
extern "C" unsigned demo_xiaozhi_probe_volume(void){return wanted_volume.load();}
#endif
// Audio outranks the shared LVGL task (priority 4); I2S waits yield CPU back to UI.
extern "C" bool demo_xiaozhi_set_reading(const yao_record_t *record){
    if(started)return false;
    if(record){yao_result_t result{};if(!record->id||!yao_calculate(record->lines,&result))return false;}
    auto *copy=record?static_cast<yao_record_t *>(malloc(sizeof(*record))):nullptr;
    if(record&&!copy)return false;
    if(copy)*copy=*record;
    free(launch_reading);launch_reading=copy;return true;
}
extern "C" esp_err_t demo_xiaozhi_start(){
#ifdef BADGE_XIAOZHI_DEVICE_PROBE
probe_ready=false;
#endif
if(started)return ESP_ERR_INVALID_STATE;
badge_network_status_t net_check{};badge_network_status(&net_check);
bool ap_was_active=net_check.active;
if(ap_was_active){
    badge_network_close_ap();
    ESP_LOGI(TAG,"Starting XiaoZhi: closed active setup hotspot");
}
demo_xiaozhi_connection_tick();
reclaim_idle_connection("startup",false);
yao_location_voice_active(true);
if(yao_location_worker_running()){
    xz_snapshot_t waiting{};waiting.state=XZ_CONNECTING;waiting.volume=wanted_volume.load();
    if(ap_was_active)xz_text_copy(waiting.detail,sizeof(waiting.detail),"已关闭热点，准备语音连接");
    else xz_text_copy(waiting.detail,sizeof(waiting.detail),"正在准备语音连接");
    xiaozhi_ui_post(&waiting);
    ESP_LOGI(TAG,"Waiting for location HTTP cleanup before voice allocation");
    int64_t deadline=esp_timer_get_time()+10000000;
    while(yao_location_worker_running()&&esp_timer_get_time()<deadline)vTaskDelay(pdMS_TO_TICKS(20));
    if(yao_location_worker_running()){
        yao_location_voice_active(false);waiting.state=XZ_ERROR;
        xz_text_copy(waiting.detail,sizeof(waiting.detail),"后台网络忙，请返回后重试");xiaozhi_ui_post(&waiting);
        return ESP_ERR_TIMEOUT;
    }
    // The HTTP worker self-deletes; let Idle reclaim its stack before our 28 KiB allocation.
    vTaskDelay(pdMS_TO_TICKS(20));
}
badge_control_enable(false);stopping=false;background_idle=false;
#if defined(BADGE_WAKE_DEVICE_PROBE) || defined(BADGE_CONTROL_DEVICE_PROBE)
clicks=0; // Lifecycle probes never initiate a microphone/cloud session.
#else
clicks=1;
#endif
done=xSemaphoreCreateBinary();if(!done){yao_location_voice_active(false);return ESP_ERR_NO_MEM;}
Session *session=parked_session?parked_session:new(std::nothrow) Session;
if(!session){vSemaphoreDelete(done);done=nullptr;yao_location_voice_active(false);return ESP_ERR_NO_MEM;}
parked_session=nullptr;

#ifdef BADGE_XIAOZHI_CONNECT_PROBE
connect_probe_state=0;connect_probe_fault=0;
#endif
session->context_upload=false;session->context_turn=false;
free(session->reading);session->reading=launch_reading;launch_reading=nullptr;
session->reading_pending=session->reading!=nullptr;
ESP_LOGI(TAG,"Startup reading=%u snapshot_bytes=%u heap=%lu largest=%lu",session->reading_pending,
         session->reading?(unsigned)sizeof(*session->reading):0u,(unsigned long)esp_get_free_heap_size(),
         (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
// Reserve the other large block before the task stack and its small runtime
// allocations can fragment it. Hold it across every retry in this visit.
if(!session->reserve_codec()){
    session->reclaim_for_retry("reserve_failed");
    reclaim_idle_connection("reserve_failed",true);
    if(!session->reserve_codec()){
        session->error("语音内存不足，按 OK 重试");
        delete session;vSemaphoreDelete(done);done=nullptr;yao_location_voice_active(false);
        return ESP_ERR_NO_MEM;
    }
}
if(xTaskCreate(worker,"xiaozhi",28672,session,5,&voice_worker)!=pdPASS){
    voice_worker=nullptr;session->memory_failure("voice_task",28672);
    session->error("语音内存不足，请退出后重试");
    delete session;vSemaphoreDelete(done);done=nullptr;yao_location_voice_active(false);return ESP_ERR_NO_MEM;
}
started=true;badge_control_enable(true);return ESP_OK;}
extern "C" esp_err_t demo_xiaozhi_stop(){badge_control_enable(false);stopping=true;if(started){if(xSemaphoreTake(done,pdMS_TO_TICKS(10000))!=pdTRUE)return ESP_ERR_TIMEOUT;vTaskDelete(voice_worker);voice_worker=nullptr;started=false;}
#ifdef BADGE_XIAOZHI_DEVICE_PROBE
probe_ready=false;
#endif
if(done){vSemaphoreDelete(done);done=nullptr;}yao_location_voice_active(false);return ESP_OK;}
extern "C" bool demo_xiaozhi_set_volume(unsigned volume){if(!started||stopping||volume>100)return false;wanted_volume.store(volume);return true;}
extern "C" void demo_xiaozhi_key(bsp_btn_t button,bsp_btn_ev_t event){if(!started||stopping||event!=BSP_BTN_CLICK)return;if(button==BSP_BTN_OK)clicks.fetch_add(1);else{unsigned v=wanted_volume.load();wanted_volume=button==BSP_BTN_UP?std::min(100u,v+10):v>10?v-10:0;}}

extern "C" bool demo_xiaozhi_return_context(badge_return_context_t *context){
    xz_state_t state=return_state.load();
    *context={};
    context->confirm=xz_return_confirmation(state);
    context->detail="退出后结束当前对话";context->resume="继续对话";
    return true;
}
extern "C" bool demo_xiaozhi_active(void){return started.load()&&!stopping.load();}
extern "C" unsigned demo_xiaozhi_saved_volume(void){
    if(started)return wanted_volume.load();
    nvs_handle_t n;uint8_t value=40;
    if(nvs_open("xiaozhi",NVS_READONLY,&n)==ESP_OK){nvs_get_u8(n,"volume",&value);nvs_close(n);}
    return value<=100?value:40;
}
extern "C" esp_err_t demo_xiaozhi_save_volume(unsigned value){
    if(value>100)return ESP_ERR_INVALID_ARG;
    if(started){wanted_volume=value;return ESP_OK;}
    nvs_handle_t n;esp_err_t e=nvs_open("xiaozhi",NVS_READWRITE,&n);
    if(e==ESP_OK){e=nvs_set_u8(n,"volume",value);if(e==ESP_OK)e=nvs_commit(n);nvs_close(n);}
    return e;
}

extern "C" bool demo_xiaozhi_idle(void){return background_idle.load();}
#ifdef BADGE_TOOL_CLOUD_PROBE
extern "C" unsigned demo_xiaozhi_cloud_completed(void){return cloud_completed.load();}
#endif
extern "C" void demo_xiaozhi_release_connection(void){
    if(started)return;
    reclaim_idle_connection("api_release",true);
}
extern "C" esp_err_t demo_xiaozhi_get_backend(char *out,size_t size,bool *custom){
    if(!out||!size)return ESP_ERR_INVALID_ARG;
    out[0]=0;if(custom)*custom=false;
    nvs_handle_t n;char url[256]={0};size_t len=sizeof(url);
    if(nvs_open("xiaozhi",NVS_READONLY,&n)==ESP_OK){
        esp_err_t e=nvs_get_str(n,"backend_url",url,&len);nvs_close(n);
        if(e==ESP_OK&&valid_backend_url(url)&&url[0]){
            snprintf(out,size,"%s",url);if(custom)*custom=true;return ESP_OK;
        }
    }
    snprintf(out,size,"%s",OTA);return ESP_OK;
}
extern "C" esp_err_t demo_xiaozhi_save_backend(const char *url){
    if(started)return ESP_ERR_INVALID_STATE;
    if(!valid_backend_url(url))return ESP_ERR_INVALID_ARG;
    nvs_handle_t n;esp_err_t e=nvs_open("xiaozhi",NVS_READWRITE,&n);
    if(e!=ESP_OK)return e;
    if(url&&url[0])e=nvs_set_str(n,"backend_url",url);
    else {e=nvs_erase_key(n,"backend_url");if(e==ESP_ERR_NVS_NOT_FOUND)e=ESP_OK;}
    if(e==ESP_OK)e=nvs_commit(n);
    nvs_close(n);
    if(e==ESP_OK){
        service_config=ServiceConfig{};
        reclaim_idle_connection("backend_change",true);
    }
    return e;
}
extern "C" void demo_xiaozhi_connection_tick(void){
    if(started)return;
    int64_t now=esp_timer_get_time();
    if(parked_session&&!xz_connection_keep(parked_session->parked_at,now,
            parked_session->connected&&!parked_session->failed,esp_get_free_heap_size())){
        demo_xiaozhi_release_connection();ESP_LOGI(TAG,"Idle connection released");
    }
    if(service_config.saved&&!xz_cache_fresh(service_config.saved,now,600000000))service_config=ServiceConfig{};
}
#ifdef BADGE_XIAOZHI_REUSE_PROBE
extern "C" unsigned demo_xiaozhi_reuse_probe(unsigned field){
    if(field==0)return probe_discovery.load();
    if(field==1)return probe_connections.load();
    if(field==2)return probe_handshakes.load();
    if(field==3)return parked_session!=nullptr;
    if(field==4&&parked_session&&parked_session->mqtt){esp_mqtt_client_disconnect(parked_session->mqtt);return 1;}
    return 0;
}
#endif
