#include "xiaozhi_tools.h"
#include "yao_service.h"
#include "badge_alarm_service.h"
#include "profile_edit.h"
#include "badge_control.h"
#include "badge_profile.h"
#include "voice_catalog.h"
#include "radio_tool_catalog.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#ifdef BADGE_CONTROL_HOST_TEST
extern uint32_t xz_tools_random(void);
#else
#include "esp_random.h"
#define xz_tools_random esp_random
#endif
static int last_random_clip=-1;
static unsigned clip_pack(unsigned clip){unsigned p=0;while(p+1<VOICE_PACK_COUNT&&clip>=voice_packs[p+1].first)p++;return p;}

static const char *string(const cJSON *o,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsString(v)?v->valuestring:"";}
static cJSON *rpc_error(cJSON *reply,int code,const char *message){cJSON *e=cJSON_AddObjectToObject(reply,"error");cJSON_AddNumberToObject(e,"code",code);cJSON_AddStringToObject(e,"message",message);return reply;}
static cJSON *content(cJSON *reply,const char *text,bool error){cJSON *r=cJSON_AddObjectToObject(reply,"result"),*a=cJSON_AddArrayToObject(r,"content"),*item=cJSON_CreateObject();cJSON_AddBoolToObject(r,"isError",error);cJSON_AddStringToObject(item,"type","text");cJSON_AddStringToObject(item,"text",text);cJSON_AddItemToArray(a,item);return reply;}
static cJSON *object_content(cJSON *reply,cJSON *o){char *text=cJSON_PrintUnformatted(o);content(reply,text?text:"Out of memory",text==NULL);cJSON_free(text);cJSON_Delete(o);return reply;}
void xz_tools_complete(cJSON *reply,int completion){
    cJSON_DeleteItemFromObjectCaseSensitive(reply,"result");
    if(completion==1&&badge_control_created_id()){char done[96];snprintf(done,sizeof(done),"提醒已保存，id=%u。可查询提醒确认。",(unsigned)badge_control_created_id());content(reply,done,false);}
    else if(completion==1)content(reply,"指令已执行。可以继续下一条操作；需要具体数值时查询设备状态或提醒。",false);
    else if(completion==0)content(reply,"指令仍在处理中，尚未确认完成。请查询状态，暂勿重复提交或执行下一条控制操作。",false);
    else content(reply,"指令未能完成。请查询当前状态后再决定下一步，不要宣称操作成功。",true);
    cJSON_AddStringToObject(cJSON_GetObjectItemCaseSensitive(reply,"result"),"execution",completion==1?"completed":completion==0?"pending":"failed");
}
static bool integer(const cJSON *args,const char *key,int min,int max,unsigned *value){const cJSON *n=cJSON_GetObjectItemCaseSensitive(args,key);if(!cJSON_IsNumber(n)||n->valuedouble<min||n->valuedouble>max||n->valuedouble!=(int)n->valuedouble)return false;*value=(unsigned)n->valueint;return true;}
static bool keys(const cJSON *args,const char *first,const char *second){if(!args)return !first;if(!cJSON_IsObject(args))return false;unsigned a=0,b=0;for(const cJSON *v=args->child;v;v=v->next){if(first&&!strcmp(v->string,first)){if(a++)return false;}else if(second&&!strcmp(v->string,second)){if(b++)return false;}else return false;}return true;}
static bool allowed(const cJSON *args,const char *const *names,unsigned count){
    if(!cJSON_IsObject(args))return false;
    unsigned seen=0;for(const cJSON *v=args->child;v;v=v->next){unsigned i=0;while(i<count&&strcmp(v->string,names[i]))i++;if(i==count||(seen&(1u<<i)))return false;seen|=1u<<i;}return true;
}
static bool contains(const char *text,const char *query){for(;*text;text++){const unsigned char *a=(const unsigned char *)text,*b=(const unsigned char *)query;while(*b&&*a&&tolower(*a)==tolower(*b)){a++;b++;}if(!*b)return true;}return !*query;}
static const char definitions[]=
"[{\"name\":\"self.get_device_status\",\"description\":\"查询真实电量、网络、亮度、当前工牌及全部已配置工牌编号姓名；volume仅指小智。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}},{\"name\":\"self.apps.open\",\"description\":\"仅打开木鱼zen-muyu、电台leo-radio、音效voice-keychain、摇卦cyber-yao、Muse muse。指定播放直接用play工具，禁止先open。结束对话，组合任务最后调用。语音起卦用yao.cast。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"app\":{\"type\":\"string\",\"enum\":[\"zen-muyu\",\"leo-radio\",\"voice-keychain\",\"cyber-yao\",\"muse\"]}},\"required\":[\"app\"],\"additionalProperties\":false}},{\"name\":\"self.audio.search\",\"description\":\"按标题或分类搜索音效，空query分页，每页6条。指定播放必须先取clip_id，多候选请询问；随机请求用play_random，不取首条冒充随机。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\",\"maxLength\":32},\"offset\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":" VOICE_CLIP_COUNT_JSON "}},\"required\":[\"query\"],\"additionalProperties\":false}},{\"name\":\"self.audio.play\",\"description\":\"播放搜索得到的clip_id，已包含打开应用，结束对话；组合任务最后调用，随机请求用play_random。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"clip_id\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":" VOICE_CLIP_MAX_JSON "}},\"required\":[\"clip_id\"],\"additionalProperties\":false}},{\"name\":\"self.audio.play_random\",\"description\":\"随机/随便/换一段音效用本工具，设备随机且多候选不连续重复。query省略=全部，可填分类或标题，不填随机二字。无需search/open，结束对话并播放，\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\",\"maxLength\":32}},\"additionalProperties\":false}},{\"name\":\"self.badge.show_qr\",\"description\":\"显示当前工牌二维码，结束对话。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}},{\"name\":\"self.badge.switch\",\"description\":\"按number或name切换，两者选一；先查状态取得真实编号姓名，重名询问编号。结束对话。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"number\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":5},\"name\":{\"type\":\"string\",\"maxLength\":128}},\"additionalProperties\":false}},{\"name\":\"self.display.set_brightness\",\"description\":\"保存屏幕亮度，保持对话；调亮/暗一点增减20，范围20..100。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"percent\":{\"type\":\"integer\",\"enum\":[20,40,60,80,100]}},\"required\":[\"percent\"],\"additionalProperties\":false}},{\"name\":\"self.xiaozhi.set_volume\",\"description\":\"仅保存小智回复音量，0静音，不改其它程序。未指定程序默认小智，其它程序暂不支持语音调音量。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"percent\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100}},\"additionalProperties\":false,\"required\":[\"percent\"]}},{\"name\":\"self.radio.search\",\"description\":\"搜索精选电台，空query分页，每页6条；播放前先取station_id，多候选询问。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\",\"maxLength\":32},\"offset\":{\"type\":\"integer\",\"minimum\":0}},\"additionalProperties\":false,\"required\":[\"query\"]}},{\"name\":\"self.radio.play\",\"description\":\"播放搜索得到的station_id，已含打开应用，结束对话；组合任务最后调用。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"station_id\":{\"type\":\"integer\",\"minimum\":0}},\"additionalProperties\":false,\"required\":[\"station_id\"]}},{\"name\":\"self.reminder.set\",\"description\":\"创建最多8条持久提醒；先get查北京时间与已有提醒，避免重复。seconds倒计时或time钟点二选一。time为HH:mm，可加date一次性日期或weekdays重复星期(1周一..7周日，每日全选)。不加日期为下次该钟点。校时后恢复，关机不响，重复过期10分钟跳过；completed才成功。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"seconds\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":86400},\"time\":{\"type\":\"string\",\"pattern\":\"^[0-2][0-9]:[0-5][0-9]$\"},\"date\":{\"type\":\"string\",\"pattern\":\"^20[0-9]{2}-[0-9]{2}-[0-9]{2}$\"},\"weekdays\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":7},\"minItems\":1,\"maxItems\":7,\"uniqueItems\":true},\"text\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":32}},\"additionalProperties\":false,\"required\":[\"text\"]}},{\"name\":\"self.reminder.get\",\"description\":\"查询全部提醒ID、北京时间、剩余秒数与重复星期；设置前查询，取消用真实ID。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}},{\"name\":\"self.reminder.cancel\",\"description\":\"按get返回的id取消一条提醒，保持对话。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":2147483647}},\"additionalProperties\":false,\"required\":[\"id\"]}},{\"name\":\"self.yao.cast\",\"description\":\"用户明确起卦时调用，返回真实卦象和原文。request_id唯一英文数字标识，重试保持相同标识和问题；追问用yao.get，不重新起卦。最近四卦可重放。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"request_id\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":48},\"question\":{\"type\":\"string\",\"maxLength\":96}},\"additionalProperties\":false,\"required\":[\"request_id\"]}},{\"name\":\"self.yao.get\",\"description\":\"解读刚才的卦象必须先调用取完整结果，不得编造或另起卦。页面解读省略result_id取确认结果；普通对话省略取最近一卦。仅本次开机最近四卦，缺失告知用户。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"result_id\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":4294967295}},\"additionalProperties\":false}},{\"name\":\"self.badge.get_profile\",\"description\":\"读取工牌姓名name、部门department、职位title及revision；省略number取当前工牌。修改前必须读取，editable=false请在网页重新保存。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"number\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":5}},\"additionalProperties\":false}},{\"name\":\"self.badge.update_profile\",\"description\":\"修改一个文字字段并保存，保持对话；number与revision必须来自刚读取的资料。name最多20字，department/title最多24字，屏幕两行超长省略。失败先重读，勿宣称成功。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"number\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":5},\"revision\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":2147483647},\"field\":{\"type\":\"string\",\"enum\":[\"name\",\"department\",\"title\"]},\"value\":{\"type\":\"string\",\"maxLength\":24}},\"additionalProperties\":false,\"required\":[\"number\",\"revision\",\"field\",\"value\"]}},{\"name\":\"self.system.action\",\"description\":\"home返回首页；shutdown仅用户明确要求关机时使用，进入深度休眠，上键唤醒，关机期间不提醒。结束对话，组合任务最后调用。\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"home\",\"shutdown\"]}},\"additionalProperties\":false,\"required\":[\"action\"]}}]";
static bool random_clip(const char *query,unsigned *clip){
    unsigned count=0;bool previous_matches=false;
    for(unsigned i=0;i<VOICE_CLIP_COUNT;i++){
        if(!contains(voice_clips[i].name,query)&&!contains(voice_packs[clip_pack(i)].name,query))continue;
        count++;if((int)i==last_random_clip)previous_matches=true;
    }
    if(!count)return false;
    bool exclude_previous=count>1&&previous_matches;
    unsigned bound=count-(exclude_previous?1:0);
    unsigned choice=(unsigned)(((uint64_t)xz_tools_random()*bound)>>32);
    for(unsigned i=0;i<VOICE_CLIP_COUNT;i++){
        if(exclude_previous&&(int)i==last_random_clip)continue;
        if(!contains(voice_clips[i].name,query)&&!contains(voice_packs[clip_pack(i)].name,query))continue;
        if(choice--==0){*clip=i;return true;}
    }
    return false;
}
cJSON *xz_tools_handle_reading(const cJSON *request,unsigned volume,bool connected,uint32_t *ticket,const yao_record_t *snapshot){
    const cJSON *params=cJSON_GetObjectItemCaseSensitive(request,"params");
    const cJSON *id=cJSON_GetObjectItemCaseSensitive(request,"id");
    if(strcmp(string(request,"jsonrpc"),"2.0")||(!cJSON_IsString(id)&&!cJSON_IsNumber(id)))return xz_tools_handle(request,volume,connected,ticket);
    if(!strcmp(string(request,"method"),"tools/call")&&!strcmp(string(params,"name"),"self.yao.get")&&snapshot&&snapshot->id){
        *ticket=0;cJSON *reply=cJSON_CreateObject();if(!reply)return NULL;
        cJSON_AddStringToObject(reply,"jsonrpc","2.0");cJSON_AddItemToObject(reply,"id",cJSON_Duplicate(id,true));
        const cJSON *args=cJSON_GetObjectItemCaseSensitive(params,"arguments");
        if(args&&!keys(args,"result_id",NULL))return rpc_error(reply,-32602,"Invalid arguments");
        cJSON *record=yao_record_json(snapshot);if(!record){cJSON_Delete(reply);return NULL;}
        const cJSON *wanted=cJSON_GetObjectItemCaseSensitive(args,"result_id"),*actual=cJSON_GetObjectItemCaseSensitive(record,"result_id");
        bool matches=cJSON_IsNumber(actual)&&(!wanted||(cJSON_IsNumber(wanted)&&wanted->valuedouble==actual->valuedouble));
        if(!matches){cJSON_Delete(record);return content(reply,"当前正在解读页面确认的卦象，请省略 result_id 查询本次结果，不要使用旧编号。",true);}
        /* Full text comes from immutable data; current_time is generated now. */
        return object_content(reply,record);
    }
    if(!strcmp(string(request,"method"),"tools/call")&&!strcmp(string(params,"name"),"self.yao.cast")){
        *ticket=0;const cJSON *id=cJSON_GetObjectItemCaseSensitive(request,"id");
        if(!id)return NULL;
        cJSON *reply=cJSON_CreateObject();if(!reply)return NULL;
        cJSON_AddStringToObject(reply,"jsonrpc","2.0");cJSON_AddItemToObject(reply,"id",cJSON_Duplicate(id,true));
        return content(reply,"当前是已有卦象的解读，禁止重新起卦。请使用传入原文或 self.yao.get 查询。",true);
    }
    return xz_tools_handle(request,volume,connected,ticket);
}
cJSON *xz_tools_handle(const cJSON *request,unsigned volume,bool connected,uint32_t *ticket){
    *ticket=0;const cJSON *id=cJSON_GetObjectItemCaseSensitive(request,"id");if(!id)return NULL;
    cJSON *reply=cJSON_CreateObject();if(!reply)return NULL;cJSON_AddStringToObject(reply,"jsonrpc","2.0");
    cJSON_AddItemToObject(reply,"id",cJSON_IsString(id)||cJSON_IsNumber(id)?cJSON_Duplicate(id,true):cJSON_CreateNull());
    if(!cJSON_IsObject(request)||(!cJSON_IsString(id)&&!cJSON_IsNumber(id))||strcmp(string(request,"jsonrpc"),"2.0"))return rpc_error(reply,-32600,"Invalid JSON-RPC request");
    const char *method=string(request,"method");const cJSON *params=cJSON_GetObjectItemCaseSensitive(request,"params");
    if(!strcmp(method,"initialize")){cJSON *r=cJSON_AddObjectToObject(reply,"result");cJSON_AddStringToObject(r,"protocolVersion","2024-11-05");cJSON_AddObjectToObject(cJSON_AddObjectToObject(r,"capabilities"),"tools");cJSON *server=cJSON_AddObjectToObject(r,"serverInfo");cJSON_AddStringToObject(server,"name","AI Passport");cJSON_AddStringToObject(server,"version",BADGE_VERSION);return reply;}
    if(!strcmp(method,"tools/list")){
        if(*string(params,"cursor"))return rpc_error(reply,-32602,"Unknown cursor");
        /* The catalog is immutable JSON. Avoid a temporary tree with hundreds
         * of allocations while retaining the contiguous audio codec arena. */
        cJSON *list=cJSON_CreateStringReference(definitions),*result=NULL;
        /* Read-only flash-backed raw JSON; IsReference prevents freeing it. */
        if(list)list->type=(list->type&~cJSON_String)|cJSON_Raw;
        if(!list||!(result=cJSON_AddObjectToObject(reply,"result"))){cJSON_Delete(list);cJSON_Delete(reply);return NULL;}
        if(!cJSON_AddItemToObject(result,"tools",list)){cJSON_Delete(list);cJSON_Delete(reply);return NULL;}
        return reply;
    }
    if(strcmp(method,"tools/call"))return rpc_error(reply,-32601,"Unknown method");
    if(!cJSON_IsObject(params))return rpc_error(reply,-32602,"Tool parameters must be an object");
    const char *name=string(params,"name");const cJSON *args=cJSON_GetObjectItemCaseSensitive(params,"arguments");
    if(!strcmp(name,"self.yao.cast")||!strcmp(name,"self.yao.get")){const char *error=NULL;cJSON *o=yao_tool(name,args,&error);return o?object_content(reply,o):content(reply,error?error:"Out of memory; retry the same request_id",true);}
    badge_control_command_t command={0};unsigned value=0;
    if(!strcmp(name,"self.get_device_status")){
        if(!keys(args,NULL,NULL))goto invalid;
        badge_control_status_t s=badge_control_status();cJSON *o=cJSON_CreateObject();cJSON_AddNumberToObject(cJSON_AddObjectToObject(o,"audio_speaker"),"volume",volume);cJSON_AddBoolToObject(cJSON_AddObjectToObject(o,"network"),"connected",connected);cJSON_AddStringToObject(o,"volume_scope","xiaozhi");cJSON_AddNumberToObject(o,"battery_percent",s.battery);cJSON_AddStringToObject(o,"active_badge_name",s.names[s.active<5?s.active:0]);cJSON_AddNumberToObject(o,"brightness",s.brightness);cJSON_AddNumberToObject(o,"active_badge",s.active+1);cJSON_AddBoolToObject(o,"qr_present",(s.qr_mask&(1u<<s.active))!=0);cJSON_AddNumberToObject(o,"last_action_result",s.last_result);
        cJSON *a=cJSON_AddArrayToObject(o,"configured_badges");for(unsigned i=0;i<s.count&&i<5;i++)if(s.mask&(1u<<i))cJSON_AddItemToArray(a,cJSON_CreateNumber(i+1));cJSON *cards=cJSON_AddArrayToObject(o,"badge_names");for(unsigned i=0;i<s.count&&i<5;i++)if(s.mask&(1u<<i)){cJSON *b=cJSON_CreateObject();cJSON_AddNumberToObject(b,"number",i+1);cJSON_AddStringToObject(b,"name",s.names[i]);cJSON_AddItemToArray(cards,b);}return object_content(reply,o);
    }else if(!strcmp(name,"self.reminder.get")){
        if(!keys(args,NULL,NULL))goto invalid;
        return object_content(reply,badge_alarm_service_json());
    }else if(!strcmp(name,"self.radio.search")){
        const cJSON *q=cJSON_GetObjectItemCaseSensitive(args,"query");if(!keys(args,"query","offset")||!cJSON_IsString(q)||strlen(q->valuestring)>96)goto invalid;
        unsigned characters=0;for(const unsigned char *p=(const unsigned char *)q->valuestring;*p;p++)if((*p&0xc0)!=0x80)characters++;
        if(characters>32)goto invalid;
        if(cJSON_HasObjectItem(args,"offset")&&!integer(args,"offset",0,radio_tool_count(),&value))goto invalid;
        cJSON *o=cJSON_CreateObject(),*list=cJSON_AddArrayToObject(o,"matches");unsigned found=0,shown=0;
        for(unsigned i=0;i<radio_tool_count();i++){if(!contains(radio_tool_name(i),q->valuestring))continue;if(found++<value||shown>=6)continue;cJSON *b=cJSON_CreateObject();cJSON_AddNumberToObject(b,"station_id",i);cJSON_AddStringToObject(b,"name",radio_tool_name(i));cJSON_AddItemToArray(list,b);shown++;}
        cJSON_AddNumberToObject(o,"total",found);if(value+shown<found)cJSON_AddNumberToObject(o,"next_offset",value+shown);return object_content(reply,o);
    }else if(!strcmp(name,"self.audio.search")){
        const cJSON *q=cJSON_GetObjectItemCaseSensitive(args,"query");if(!keys(args,"query","offset")||!cJSON_IsString(q)||strlen(q->valuestring)>128)goto invalid;
        unsigned characters=0;for(const unsigned char *p=(const unsigned char *)q->valuestring;*p;p++)if((*p&0xc0)!=0x80)characters++;
        if(characters>32)goto invalid;
        if(cJSON_GetObjectItemCaseSensitive(args,"offset")&&!integer(args,"offset",0,VOICE_CLIP_COUNT,&value))goto invalid;
        cJSON *o=cJSON_CreateObject(),*list=cJSON_AddArrayToObject(o,"matches");unsigned found=0,shown=0,pack=0;
        for(unsigned i=0;i<VOICE_CLIP_COUNT;i++){
            while(pack+1<VOICE_PACK_COUNT&&i>=voice_packs[pack+1].first)pack++;
            if(!contains(voice_clips[i].name,q->valuestring)&&!contains(voice_packs[pack].name,q->valuestring))continue;
            if(found++<value||shown>=6)continue;
            cJSON *item=cJSON_CreateObject();cJSON_AddNumberToObject(item,"clip_id",i);cJSON_AddStringToObject(item,"title",voice_clips[i].name);cJSON_AddStringToObject(item,"category",voice_packs[pack].name);cJSON_AddItemToArray(list,item);shown++;
        }
        cJSON_AddNumberToObject(o,"total",found);if(value+shown<found)cJSON_AddNumberToObject(o,"next_offset",value+shown);return object_content(reply,o);
    }else if(!strcmp(name,"self.audio.play_random")){
        const cJSON *q=cJSON_GetObjectItemCaseSensitive(args,"query");
        if(!keys(args,"query",NULL)||(q&&!cJSON_IsString(q)))goto invalid;
        const char *query=q?q->valuestring:"";unsigned chars=0;
        if(strlen(query)>128)goto invalid;
        for(const unsigned char *p=(const unsigned char *)query;*p;p++){if(*p<32||*p==127)goto invalid;if((*p&0xc0)!=0x80)chars++;}
        if(chars>32)goto invalid;
        if(!random_clip(query,&value))return content(reply,"No matching sound clips; try another category or an empty query",true);
        command=(badge_control_command_t){.kind=BC_PLAY_VOICE,.value=value};
        const char *error=badge_control_reserve(command,ticket);if(error)return content(reply,error,true);
        last_random_clip=(int)value;
        cJSON *o=cJSON_CreateObject();cJSON_AddBoolToObject(o,"random",true);
        cJSON_AddNumberToObject(o,"clip_id",value);cJSON_AddStringToObject(o,"title",voice_clips[value].name);
        cJSON_AddStringToObject(o,"category",voice_packs[clip_pack(value)].name);
        cJSON_AddStringToObject(o,"execution","pending");cJSON_AddStringToObject(o,"message","Random clip selected; opening soundboard and ending this conversation. Wake again after playback.");
        return object_content(reply,o);
    }else if(!strcmp(name,"self.apps.open")){
        if(!keys(args,"app",NULL))goto invalid;
        const char *app=string(args,"app");
        if(!strcmp(app,"zen-muyu"))command.kind=BC_MUYU;else if(!strcmp(app,"leo-radio"))command.kind=BC_RADIO;else if(!strcmp(app,"voice-keychain"))command.kind=BC_VOICE;else if(!strcmp(app,"cyber-yao"))command.kind=BC_YAO;else if(!strcmp(app,"muse"))command.kind=BC_MUSE;else goto invalid;
        if((command.kind==BC_RADIO||command.kind==BC_MUSE)&&!connected)return content(reply,"Wi-Fi is disconnected; connect it in badge settings first",true);
    }else if(!strcmp(name,"self.audio.play")){if(!keys(args,"clip_id",NULL)||!integer(args,"clip_id",0,VOICE_CLIP_COUNT-1,&value))goto invalid;command=(badge_control_command_t){.kind=BC_PLAY_VOICE,.value=value};}
    else if(!strcmp(name,"self.badge.show_qr")){if(!keys(args,NULL,NULL))goto invalid;command.kind=BC_QR;}
    else if(!strcmp(name,"self.badge.switch")){
        if(!keys(args,"number","name"))goto invalid;
        if(cJSON_HasObjectItem(args,"name")){
            const char *q=string(args,"name");if(!*q||strlen(q)>128||cJSON_HasObjectItem(args,"number"))goto invalid;
            badge_control_status_t state=badge_control_status();unsigned matches=0;
            for(unsigned i=0;i<5;i++)if((state.mask&(1u<<i))&&!strcmp(state.names[i],q)){matches++;value=i+1;}
            if(matches!=1)return content(reply,matches?"Duplicate badge names; ask for a badge number":"Name not found; query device status for configured names",true);
            snprintf(command.text,sizeof(command.text),"%s",q);
        }else if(!integer(args,"number",1,5,&value))goto invalid;
        command.kind=BC_BADGE;command.value=value-1;
    }
    else if(!strcmp(name,"self.xiaozhi.set_volume")){if(!keys(args,"percent",NULL)||!integer(args,"percent",0,100,&value))goto invalid;command.kind=BC_XZ_VOLUME;command.value=value;}
    else if(!strcmp(name,"self.radio.play")){if(!keys(args,"station_id",NULL)||!integer(args,"station_id",0,radio_tool_count()-1,&value))goto invalid;if(!connected)return content(reply,"Wi-Fi disconnected",true);command.kind=BC_RADIO_PRESET;command.value=value;}
    else if(!strcmp(name,"self.reminder.cancel")){if(!keys(args,"id",NULL)||!integer(args,"id",1,2147483647,&value))goto invalid;command.kind=BC_REMINDER_CANCEL;command.value=value;}
    else if(!strcmp(name,"self.reminder.set")){
        static const char *const names[]={"seconds","time","date","weekdays","text"};
        const char *text=string(args,"text");if(!allowed(args,names,5)||!*text||strlen(text)>96)goto invalid;
        unsigned chars=0;for(const unsigned char *p=(const unsigned char *)text;*p;p++){if(*p<32||*p==127)goto invalid;if((*p&0xc0)!=0x80)chars++;}if(chars>32)goto invalid;
        bool relative=cJSON_HasObjectItem(args,"seconds");
        if(relative&&(!integer(args,"seconds",1,86400,&value)||cJSON_HasObjectItem(args,"time")||cJSON_HasObjectItem(args,"date")||cJSON_HasObjectItem(args,"weekdays")))goto invalid;
        if(!relative&&!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(args,"time")))goto invalid;
        if(cJSON_HasObjectItem(args,"date")&&(!*string(args,"date")||cJSON_HasObjectItem(args,"weekdays")))goto invalid;
        const cJSON *days=cJSON_GetObjectItemCaseSensitive(args,"weekdays");unsigned mask=0;
        if(days){if(!cJSON_IsArray(days)||cJSON_GetArraySize(days)<1||cJSON_GetArraySize(days)>7)goto invalid;for(const cJSON *d=days->child;d;d=d->next){if(!cJSON_IsNumber(d)||d->valuedouble<1||d->valuedouble>7||d->valuedouble!=d->valueint||(mask&(1u<<(d->valueint-1))))goto invalid;mask|=1u<<(d->valueint-1);}}
        int64_t now=badge_alarm_service_now();if(now<BADGE_CLOCK_MIN)return content(reply,"时间尚未校准，请联网校时后再创建可重启恢复的提醒。",true);
        if(!badge_alarm_schedule(now,value,string(args,"time"),string(args,"date"),mask,&command.when,&command.minute))goto invalid;
        command.kind=BC_REMINDER_SET;command.value=value;command.repeat=mask;snprintf(command.text,sizeof(command.text),"%s",text);
    }
    else if(!strcmp(name,"self.badge.get_profile")){
        value=6;if(!keys(args,"number",NULL)||(cJSON_HasObjectItem(args,"number")&&!integer(args,"number",1,5,&value)))goto invalid;
        return object_content(reply,profile_edit_get(value-1));
    }
    else if(!strcmp(name,"self.badge.update_profile")){
        static const char *const names[]={"number","revision","field","value"};
        if(!allowed(args,names,4)||!integer(args,"number",1,5,&value)||!integer(args,"revision",0,2147483647,&command.revision))goto invalid;
        const char *field=string(args,"field"),*text=string(args,"value");
        if(!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(args,"value"))||strlen(text)>128)goto invalid;
        if(!strcmp(field,"name"))command.field=0;else if(!strcmp(field,"department"))command.field=1;else if(!strcmp(field,"title"))command.field=2;else goto invalid;
        unsigned chars=0;for(const unsigned char *p=(const unsigned char *)text;*p;p++){if(*p<32||*p==127)goto invalid;if((*p&0xc0)!=0x80)chars++;}
        if(chars>(command.field?24u:20u)||(!command.field&&!chars))goto invalid;
        command.kind=BC_PROFILE_EDIT;command.value=value-1;snprintf(command.text,sizeof(command.text),"%s",text);
    }
    else if(!strcmp(name,"self.system.action")){
        if(!keys(args,"action",NULL))goto invalid;
        const char *action=string(args,"action");
        if(!strcmp(action,"home"))command.kind=BC_HOME;else if(!strcmp(action,"shutdown"))command.kind=BC_SHUTDOWN;else goto invalid;
    }
    else if(!strcmp(name,"self.display.set_brightness")){if(!keys(args,"percent",NULL)||!integer(args,"percent",20,100,&value)||value%20)goto invalid;command=(badge_control_command_t){.kind=BC_BRIGHTNESS,.value=value};}
    else return rpc_error(reply,-32602,"Unknown tool");
    {
        const char *error=badge_control_reserve(command,ticket);if(error)return content(reply,error,true);
        return content(reply,badge_control_stays(command.kind)?"指令等待执行，尚未确认完成。":"切换指令已接收，正在打开目标页面或应用。本次小智对话即将结束，不需要再次打开或继续调用其它工具。此回复仅确认接收。",false);
    }
 invalid:return rpc_error(reply,-32602,"Invalid arguments; follow the tool schema and use integer values in range");
}
