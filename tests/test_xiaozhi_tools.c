#include "xiaozhi_tools.h"
#include "yao_time.h"
#include "badge_control.h"
#include "badge_alarm_service.h"
cJSON *profile_edit_get(unsigned id){cJSON *o=cJSON_CreateObject();cJSON_AddNumberToObject(o,"number",id==5?1:id+1);return o;}
#include "voice_navigation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static size_t allocation_budget=SIZE_MAX;
static void *limited_alloc(size_t size){
    if(allocation_budget!=SIZE_MAX){if(size>allocation_budget)return NULL;allocation_budget-=size;}
    return malloc(size);
}
static uint32_t ticket;
static unsigned wake_count;
static uint32_t random_word;
uint32_t xz_tools_random(void){return random_word;}
bool yao_random_bits(uint32_t *out){*out=random_word;return true;}
static void wake_main(void){wake_count++;}
static cJSON *request(const char *method,const char *params){
    char buffer[2048];snprintf(buffer,sizeof(buffer),"{\"jsonrpc\":\"2.0\",\"id\":17,\"method\":\"%s\",\"params\":%s}",method,params);
    cJSON *q=cJSON_Parse(buffer);assert(q);
    if(!strcmp(method,"tools/list"))allocation_budget=1024;
    cJSON *r=xz_tools_handle(q,40,false,&ticket);allocation_budget=SIZE_MAX;
    cJSON_Delete(q);assert(r);char *wire=cJSON_PrintUnformatted(r);assert(wire);cJSON_Delete(r);r=cJSON_Parse(wire);cJSON_free(wire);assert(r);return r;
}
static cJSON *call(const char *name,const char *args){char p[1536];snprintf(p,sizeof(p),"{\"name\":\"%s\",\"arguments\":%s}",name,args);return request("tools/call",p);}
static cJSON *reading_get(const yao_record_t *snapshot,const char *args){
    char p[512];snprintf(p,sizeof(p),"{\"jsonrpc\":\"2.0\",\"id\":18,\"method\":\"tools/call\",\"params\":{\"name\":\"self.yao.get\",\"arguments\":%s}}",args);
    cJSON *q=cJSON_Parse(p);assert(q);cJSON *r=xz_tools_handle_reading(q,40,true,&ticket,snapshot);cJSON_Delete(q);assert(r);char *wire=cJSON_PrintUnformatted(r);assert(wire);cJSON_Delete(r);r=cJSON_Parse(wire);cJSON_free(wire);assert(r);return r;
}
static bool error(cJSON *r){return cJSON_HasObjectItem(r,"error")||cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r,"result"),"isError"));}
static cJSON *text_object(cJSON *r){cJSON *content=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r,"result"),"content");return cJSON_Parse(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(content,0),"text")->valuestring);}
int main(void){
    assert(badge_alarm_service_init());
    cJSON_Hooks hooks={limited_alloc,free};cJSON_InitHooks(&hooks);
    badge_control_publish(80,3,1,5,0);badge_control_enable(true);badge_control_set_wakeup(wake_main);
    uint32_t completion_ticket;badge_control_command_t pending_command={.kind=BC_BRIGHTNESS,.value=60},taken;
    assert(!badge_control_reserve(pending_command,&completion_ticket));
    assert(badge_control_peek(completion_ticket,&taken)&&badge_control_stays(taken.kind));
    assert(badge_control_result(completion_ticket)==0&&wake_count==0);
    badge_control_commit(completion_ticket,true);assert(wake_count==1);
    assert(badge_control_take(&taken));badge_control_publish(60,3,1,5,0);badge_control_finish(true);
    assert(badge_control_result(completion_ticket)==1&&badge_control_status().brightness==60);
    assert(!badge_control_reserve(pending_command,&completion_ticket));badge_control_commit(completion_ticket,false);assert(wake_count==1&&badge_control_result(completion_ticket)==-1);
    assert(!badge_control_reserve(pending_command,&completion_ticket));badge_control_cancel();assert(badge_control_result(completion_ticket)==-2);
    assert(!badge_control_stays(BC_RADIO)&&!badge_control_stays(BC_RADIO_PRESET));
    badge_control_set_wakeup(NULL);
    for(int status=-2;status<=1;status++){
        cJSON *reply=cJSON_CreateObject();xz_tools_complete(reply,status);
        const cJSON *result=cJSON_GetObjectItemCaseSensitive(reply,"result");
        assert(!strcmp(cJSON_GetObjectItemCaseSensitive(result,"execution")->valuestring,status==1?"completed":status==0?"pending":"failed"));
        assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result,"isError"))==(status<0));cJSON_Delete(reply);
    }
    badge_control_publish(80,3,1,5,0);badge_control_enable(true);
    cJSON *r=request("tools/list","{}");assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r,"result"),"tools"))==19);
    char *catalog=cJSON_PrintUnformatted(r);assert(catalog&&strlen(catalog)<7168);cJSON_free(catalog);cJSON_Delete(r);
    r=call("self.get_device_status","{}");cJSON *o=text_object(r);assert(cJSON_GetObjectItemCaseSensitive(o,"brightness")->valueint==80);assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(o,"network"),"connected")));cJSON_Delete(o);cJSON_Delete(r);
    badge_control_command_t c;
    r=call("self.apps.open","{\"app\":\"zen-muyu\"}");assert(!error(r)&&ticket);uint32_t first=ticket;cJSON_Delete(r);assert(!badge_control_take(&c));
    r=call("self.badge.show_qr","{}");assert(error(r)&&!ticket);cJSON_Delete(r);
    badge_control_commit(first,false);assert(!badge_control_take(&c));
    r=call("self.audio.play","{\"clip_id\":708}");assert(!error(r));first=ticket;cJSON_Delete(r);badge_control_commit(first,true);assert(badge_control_take(&c)&&c.kind==BC_PLAY_VOICE&&c.value==708);assert(!badge_control_take(&c));badge_control_finish(true);
    r=call("self.badge.switch","{\"number\":2}");assert(!error(r));first=ticket;cJSON_Delete(r);badge_control_enable(false);badge_control_enable(true);badge_control_commit(first,true);assert(!badge_control_take(&c));
    const char *badnums[]={"{\"number\":0}","{\"number\":6}","{\"number\":3}","{\"number\":1.5}","{\"number\":\"2\"}","{\"number\":2,\"number\":1}"};
    for(unsigned i=0;i<sizeof(badnums)/sizeof(*badnums);i++){r=call("self.badge.switch",badnums[i]);assert(error(r)&&!ticket);cJSON_Delete(r);}
    r=call("self.apps.open","{\"app\":\"leo-radio\"}");assert(error(r));cJSON_Delete(r);
    r=call("self.apps.open","{\"app\":\"muse\"}");assert(error(r));cJSON_Delete(r);
    r=call("self.display.set_brightness","{\"percent\":0}");assert(error(r));cJSON_Delete(r);
    r=call("self.display.set_brightness","{\"percent\":30}");assert(error(r));cJSON_Delete(r);
    r=call("self.display.set_brightness","{\"percent\":60,\"extra\":1}");assert(error(r));cJSON_Delete(r);
    r=call("self.display.set_brightness","{\"percent\":60}");assert(!error(r));first=ticket;cJSON_Delete(r);badge_control_commit(first,true);assert(badge_control_take(&c)&&c.kind==BC_BRIGHTNESS&&c.value==60);badge_control_finish(true);
    r=call("self.audio.search","{\"query\":\"\"}");assert(!error(r));o=text_object(r);assert(cJSON_GetObjectItemCaseSensitive(o,"total")->valueint==VOICE_CLIP_COUNT);assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(o,"matches"))==6);assert(cJSON_GetObjectItemCaseSensitive(o,"next_offset")->valueint==6);cJSON_Delete(o);cJSON_Delete(r);
    r=call("self.audio.search","{\"query\":\"__no_such_sound__\"}");o=text_object(r);assert(cJSON_GetObjectItemCaseSensitive(o,"total")->valueint==0);cJSON_Delete(o);cJSON_Delete(r);
    r=call("self.audio.search","{\"query\":\"\",\"offset\":708}");o=text_object(r);assert(!cJSON_HasObjectItem(o,"next_offset"));cJSON_Delete(o);cJSON_Delete(r);
    badge_control_publish(80,3,0,5,0);r=call("self.badge.show_qr","{}");assert(error(r));cJSON_Delete(r);
    r=call("unknown","{}");assert(error(r));cJSON_Delete(r);
    r=call("self.get_device_status","[]");assert(error(r));cJSON_Delete(r);
    badge_control_identity(0,"Alpha");badge_control_identity(1,"Beta");badge_control_runtime(87,false,0,"");
    r=call("self.get_device_status","{}");o=text_object(r);assert(cJSON_GetObjectItemCaseSensitive(o,"battery_percent")->valueint==87);assert(!strcmp(cJSON_GetObjectItemCaseSensitive(o,"active_badge_name")->valuestring,"Alpha"));cJSON_Delete(o);cJSON_Delete(r);
    r=call("self.badge.switch","{\"name\":\"Beta\"}");assert(!error(r));first=ticket;cJSON_Delete(r);badge_control_commit(first,true);assert(badge_control_take(&c)&&c.kind==BC_BADGE&&c.value==1&&!strcmp(c.text,"Beta"));badge_control_finish(true);
    badge_control_identity(0,"Beta");r=call("self.badge.switch","{\"name\":\"Beta\"}");assert(error(r));cJSON_Delete(r);
    r=call("self.badge.switch","{\"name\":\"Beta\",\"number\":1}");assert(error(r));cJSON_Delete(r);
    r=call("self.badge.switch","{\"name\":\"Missing\"}");assert(error(r));cJSON_Delete(r);
    r=call("self.xiaozhi.set_volume","{\"percent\":0}");assert(!error(r));first=ticket;cJSON_Delete(r);badge_control_commit(first,true);assert(badge_control_take(&c)&&c.kind==BC_XZ_VOLUME&&c.value==0);badge_control_finish(true);
    r=call("self.xiaozhi.set_volume","{\"percent\":101}");assert(error(r));cJSON_Delete(r);
    r=call("self.xiaozhi.set_volume","{\"percent\":5.5}");assert(error(r));cJSON_Delete(r);
    r=call("self.radio.search","{\"query\":\"中国之声\"}");o=text_object(r);assert(cJSON_GetObjectItemCaseSensitive(o,"total")->valueint==1);cJSON_Delete(o);cJSON_Delete(r);
    r=call("self.radio.search","{\"query\":\"\"}");o=text_object(r);assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(o,"matches"))==6);cJSON_Delete(o);cJSON_Delete(r);
    r=call("self.radio.play","{\"station_id\":0}");assert(error(r));cJSON_Delete(r); // disconnected
    r=call("self.reminder.set","{\"seconds\":5,\"text\":\"喝水\"}");assert(!error(r));first=ticket;cJSON_Delete(r);badge_control_commit(first,false);assert(!badge_control_take(&c));
    r=call("self.reminder.set","{\"seconds\":5,\"text\":\"喝水\"}");assert(!error(r));first=ticket;cJSON_Delete(r);badge_control_commit(first,true);assert(badge_control_take(&c)&&c.kind==BC_REMINDER_SET&&c.value==5&&!strcmp(c.text,"喝水"));badge_control_finish(true);
    uint32_t alarm_id;assert(badge_alarm_service_add(badge_alarm_test_now+5,0,0,"喝水",&alarm_id));
    r=call("self.reminder.get","{}");o=text_object(r);assert(cJSON_GetObjectItemCaseSensitive(o,"count")->valueint==1);assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"survives_reboot")));cJSON_Delete(o);cJSON_Delete(r);
    r=call("self.reminder.set","{\"seconds\":10,\"text\":\"another\"}");assert(!error(r));cJSON_Delete(r);badge_control_commit(ticket,false);
    r=call("self.reminder.cancel","{\"id\":1}");assert(!error(r));first=ticket;cJSON_Delete(r);badge_control_commit(first,true);assert(badge_control_take(&c)&&c.kind==BC_REMINDER_CANCEL&&c.value==1);badge_control_finish(true);
    r=call("self.reminder.cancel","{}");assert(error(r));cJSON_Delete(r);
    r=call("self.reminder.set","{\"time\":\"15:00\",\"weekdays\":[1,2,3,4,5,6,7],\"text\":\"喝水\"}");assert(!error(r));cJSON_Delete(r);badge_control_commit(ticket,true);assert(badge_control_take(&c)&&c.repeat==127&&c.minute==900&&c.value==0);badge_control_finish(true);
    const char *invalid_alarms[]={"{\"time\":\"25:00\",\"text\":\"bad\"}","{\"seconds\":2,\"time\":\"15:00\",\"text\":\"bad\"}","{\"time\":\"15:00\",\"weekdays\":[1,1],\"text\":\"bad\"}","{\"time\":\"15:00\",\"date\":42,\"text\":\"bad\"}","{\"time\":\"15:00\",\"weekdays\":[],\"text\":\"bad\"}"};
    for(unsigned i=0;i<sizeof(invalid_alarms)/sizeof(*invalid_alarms);i++){r=call("self.reminder.set",invalid_alarms[i]);assert(error(r));cJSON_Delete(r);}
    r=call("self.badge.get_profile","{}");assert(!error(r));cJSON_Delete(r);
    r=call("self.badge.update_profile","{\"number\":1,\"revision\":1,\"field\":\"title\",\"value\":\"产品设计师\"}");assert(!error(r));cJSON_Delete(r);badge_control_commit(ticket,true);assert(badge_control_take(&c)&&c.kind==BC_PROFILE_EDIT&&c.field==2&&!strcmp(c.text,"产品设计师")&&badge_control_stays(c.kind));badge_control_finish(true);
    r=call("self.badge.update_profile","{\"number\":1,\"revision\":1,\"field\":\"avatar\",\"value\":\"x\"}");assert(error(r));cJSON_Delete(r);
    r=call("self.system.action","{\"action\":\"home\"}");assert(!error(r));cJSON_Delete(r);badge_control_commit(ticket,true);assert(badge_control_take(&c)&&c.kind==BC_HOME&&!badge_control_stays(c.kind));badge_control_finish(true);
    r=call("self.system.action","{\"action\":\"shutdown\"}");assert(!error(r));cJSON_Delete(r);badge_control_commit(ticket,true);assert(badge_control_take(&c)&&c.kind==BC_SHUTDOWN&&!badge_control_stays(c.kind));badge_control_finish(true);
    badge_control_runtime(87,false,0,"");
    r=call("self.reminder.set","{\"seconds\":0,\"text\":\"bad\"}");assert(error(r));cJSON_Delete(r);
    r=call("self.reminder.set","{\"seconds\":86401,\"text\":\"bad\"}");assert(error(r));cJSON_Delete(r);
    r=call("self.reminder.set","{\"seconds\":10,\"text\":\"\"}");assert(error(r));cJSON_Delete(r);
    r=call("self.yao.cast","{\"request_id\":\"mcp-test\",\"question\":\"工作\"}");assert(!error(r)&&ticket==0);o=text_object(r);assert(cJSON_HasObjectItem(o,"readings")&&cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(o,"lines"))==6);cJSON_Delete(o);cJSON_Delete(r);
    r=call("self.apps.open","{\"app\":\"cyber-yao\"}");assert(!error(r)&&ticket);first=ticket;cJSON_Delete(r);badge_control_commit(first,true);assert(badge_control_take(&c)&&c.kind==BC_YAO);badge_control_finish(true);
    cJSON *guard=cJSON_Parse("{\"jsonrpc\":\"2.0\",\"id\":99,\"method\":\"tools/call\",\"params\":{\"name\":\"self.yao.cast\",\"arguments\":{\"request_id\":\"unwanted-recast\"}}}");
    r=xz_tools_handle_reading(guard,40,true,&ticket,NULL);assert(error(r)&&!ticket);cJSON_Delete(r);cJSON_Delete(guard);
    r=call("self.yao.get","{}");assert(!error(r)&&ticket==0);o=text_object(r);cJSON_Delete(r);
    char *snapshot=cJSON_PrintUnformatted(o);unsigned pinned=cJSON_GetObjectItemCaseSensitive(o,"result_id")->valueint;cJSON_Delete(o);
    yao_record_t pinned_record;assert(yao_lookup(pinned,&pinned_record));
    r=call("self.yao.cast","{\"request_id\":\"newer-result\"}");assert(!error(r));cJSON_Delete(r);
    r=reading_get(&pinned_record,"{}");assert(!error(r)&&ticket==0);o=text_object(r);
    char *returned=cJSON_PrintUnformatted(o);assert(returned&&!strcmp(returned,snapshot));cJSON_free(returned);
    assert((unsigned)cJSON_GetObjectItemCaseSensitive(o,"result_id")->valueint==pinned);
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(o,"original"),"yaoci"))==6);
    cJSON_Delete(o);cJSON_Delete(r);
    char pinned_args[64];snprintf(pinned_args,sizeof(pinned_args),"{\"result_id\":%u}",pinned);
    r=reading_get(&pinned_record,pinned_args);assert(!error(r));cJSON_Delete(r);
    yao_test_now+=3600;r=reading_get(&pinned_record,pinned_args);assert(!error(r));o=text_object(r);
    cJSON *prior=cJSON_Parse(snapshot);assert(prior);
    char *cast_before=cJSON_PrintUnformatted(cJSON_GetObjectItemCaseSensitive(prior,"cast_time")),*cast_after=cJSON_PrintUnformatted(cJSON_GetObjectItemCaseSensitive(o,"cast_time"));
    assert(cast_before&&cast_after&&!strcmp(cast_before,cast_after));
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(prior,"current_time"),"utc")->valuestring,cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(o,"current_time"),"utc")->valuestring));
    cJSON_free(cast_before);cJSON_free(cast_after);cJSON_Delete(prior);cJSON_Delete(o);cJSON_Delete(r);
    const char *invalid_readings[]={"null","[]","{\"unexpected\":1}","{\"result_id\":0}","{\"result_id\":\"1\"}","{\"result_id\":1,\"result_id\":1}"};
    for(unsigned i=0;i<sizeof(invalid_readings)/sizeof(*invalid_readings);i++){r=reading_get(&pinned_record,invalid_readings[i]);assert(error(r)&&ticket==0);cJSON_Delete(r);}
    /* The pinned record survives eviction from the four-entry history. */
    for(unsigned i=0;i<YAO_CACHE_COUNT+1;i++){
        char args[80];snprintf(args,sizeof(args),"{\"request_id\":\"evict-%u\"}",i);
        r=call("self.yao.cast",args);assert(!error(r));cJSON_Delete(r);
    }
    yao_record_t missing;assert(!yao_lookup(pinned,&missing));
    r=reading_get(&pinned_record,"{}");assert(!error(r));o=text_object(r);
    assert((unsigned)cJSON_GetObjectItemCaseSensitive(o,"result_id")->valueint==pinned);
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(o,"changed"),"yaoci"))==6);
    cJSON_Delete(o);cJSON_Delete(r);
    cJSON_free(snapshot);
    assert(!badge_control_take(&c));
    bool seen[VOICE_CLIP_COUNT]={0};unsigned distinct=0;int previous=-1;
    for(unsigned i=0;i<100;i++){
        random_word=i==0?0:i==1?UINT32_MAX:random_word*1664525u+1013904223u;
        r=call("self.audio.play_random","{}");assert(!error(r)&&ticket);o=text_object(r);
        int selected=cJSON_GetObjectItemCaseSensitive(o,"clip_id")->valueint;
        assert(selected>=0&&selected<VOICE_CLIP_COUNT&&selected!=previous);
        if(i==0)assert(selected==0);if(i==1)assert(selected==VOICE_CLIP_COUNT-1);
        assert(!strcmp(cJSON_GetObjectItemCaseSensitive(o,"title")->valuestring,voice_clips[selected].name));
        if(!seen[selected]){seen[selected]=true;distinct++;}previous=selected;
        first=ticket;cJSON_Delete(o);cJSON_Delete(r);assert(!badge_control_take(&c));
        badge_control_commit(first,true);assert(badge_control_take(&c)&&c.kind==BC_PLAY_VOICE&&c.value==(unsigned)selected);badge_control_finish(true);
    }
    assert(distinct>40);
    const char *bad_random[]={"{\"query\":1}","{\"query\":null}","{\"clip_id\":0}","{\"query\":\"__no_such_sound__\"}","{\"query\":\"\",\"query\":\"\"}"};
    for(unsigned i=0;i<sizeof(bad_random)/sizeof(*bad_random);i++){r=call("self.audio.play_random",bad_random[i]);assert(error(r)&&!ticket);cJSON_Delete(r);}
    cJSON *filter=cJSON_CreateObject();cJSON_AddStringToObject(filter,"query",voice_packs[10].name);char *filter_text=cJSON_PrintUnformatted(filter);cJSON_Delete(filter);
    for(unsigned i=0;i<12;i++){
        random_word=random_word*1664525u+1013904223u;r=call("self.audio.play_random",filter_text);assert(!error(r));o=text_object(r);
        assert(strstr(cJSON_GetObjectItemCaseSensitive(o,"category")->valuestring,voice_packs[10].name)||strstr(cJSON_GetObjectItemCaseSensitive(o,"title")->valuestring,voice_packs[10].name));
        first=ticket;cJSON_Delete(o);cJSON_Delete(r);badge_control_commit(first,false);assert(!badge_control_take(&c));
    }
    cJSON_free(filter_text);
    voice_navigation_t n;voice_navigation_init(&n);for(unsigned i=0;i<VOICE_CLIP_COUNT;i++){assert(voice_navigation_select(&n,i));assert(voice_navigation_ok(&n)==(int)i);}assert(!voice_navigation_select(&n,VOICE_CLIP_COUNT));
    puts("Assistant tools PASS: discovery, real network status, schema errors, missing data, 709 clip selections, bounded search, deferred acknowledgement, failed sends, stale tickets and single execution");return 0;
}
