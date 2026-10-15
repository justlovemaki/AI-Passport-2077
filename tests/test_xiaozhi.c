#include "xiaozhi_wire.h"
#include "xiaozhi_logic.h"
#include "xiaozhi_volume.h"
#include "xiaozhi_caption.h"
#include "xiaozhi_dispatch.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    for(unsigned state=XZ_IDLE;state<=XZ_ERROR;state++)
        assert(xz_return_confirmation((xz_state_t)state)==
               (state==XZ_CONNECTING||state==XZ_READY||state==XZ_LISTENING||
                state==XZ_THINKING||state==XZ_SPEAKING));
    assert(!xz_session_ready(false,false));assert(!xz_session_ready(true,false));
    assert(!xz_session_ready(false,true));assert(xz_session_ready(true,true));
    /* Replay observed server ordering: initialize -> hello -> list -> call. */
    assert(xz_message_allowed("mcp","initialize",false,false,false));
    assert(xz_message_allowed("mcp","tools/list",false,false,false));
    assert(xz_message_allowed("hello","",false,true,false));
    assert(xz_message_allowed("mcp","tools/call",true,true,true));
    assert(!xz_message_allowed("mcp","tools/call",false,false,false));
    assert(!xz_message_allowed("mcp","tools/call",true,true,false));
    assert(!xz_message_allowed("tts","",false,false,false));
    assert(!xz_message_allowed("tts","",true,true,false));
    assert(xz_message_allowed("tts","",true,true,true));
    assert(xz_connection_keep(1,120000000,true,49152));
    assert(!xz_connection_keep(1,120000001,true,49152));
    assert(!xz_connection_keep(1,2,false,100000));
    assert(!xz_connection_keep(1,2,true,49151));
    assert(!xz_connection_keep(0,2,true,100000));
    assert(!xz_cache_fresh(20,19,600000000));
    assert(xz_cache_fresh(1,600000000,600000000));
    assert(!xz_cache_fresh(1,600000001,600000000));
    assert(xz_caption_is_tool_progress("% self.xiaozhi.set_volume..."));
    assert(xz_caption_is_tool_progress("  % self.get_device_status…\n"));
    assert(xz_caption_is_tool_progress("% self.reminder.set..."));
    assert(xz_caption_is_tool_progress("%s"));
    assert(xz_caption_is_tool_progress(" \t%s...\n"));
    assert(xz_caption_is_tool_progress("% self.audio.play_random"));
    assert(!xz_caption_is_tool_progress("% self.audio.play_random 是一个方法"));
    assert(!xz_caption_is_tool_progress("%status"));
    assert(!xz_caption_is_tool_progress("电量还有60%"));
    assert(!xz_caption_is_tool_progress("%s 是字符串占位符"));
    assert(!xz_caption_is_tool_progress("% self.xiaozhi.set_volume...这是一段代码"));
    assert(!xz_caption_is_tool_progress("% self..invalid..."));
    assert(!xz_caption_is_tool_progress("% self."));
    assert(!xz_caption_is_tool_progress(""));
    assert(!xz_caption_is_tool_progress(NULL));
    assert(xz_ok_action(XZ_CONNECTING,false)==XZ_NO_ACTION);
    assert(xz_ok_action(XZ_READY,true)==XZ_START);
    assert(xz_ok_action(XZ_LISTENING,true)==XZ_PAUSE);
    assert(xz_ok_action(XZ_SPEAKING,true)==XZ_INTERRUPT);
    assert(xz_ok_action(XZ_THINKING,true)==XZ_INTERRUPT);
    assert(xz_ok_action(XZ_ACTIVATION,false)==XZ_CONNECT);
    assert(xz_ok_action(XZ_ERROR,false)==XZ_CONNECT);
    assert(!xz_tts_drained(0,10,900000));assert(!xz_tts_drained(100,500000,600000));
    assert(!xz_tts_drained(100,500000,400000));assert(xz_tts_drained(100,500000,900001));
    assert(xz_auto_listen(true,XZ_READY,500,500));assert(!xz_auto_listen(false,XZ_READY,500,0));
    assert(!xz_auto_listen(true,XZ_SPEAKING,500,0));assert(!xz_auto_listen(true,XZ_READY,499,500));
    assert(xz_tts_allowed(true,XZ_LISTENING,500,500));assert(xz_tts_allowed(true,XZ_THINKING,500,0));
    assert(!xz_tts_allowed(false,XZ_LISTENING,500,0));assert(!xz_tts_allowed(true,XZ_READY,500,0));
    assert(!xz_tts_allowed(true,XZ_LISTENING,499,500));
    /* Silence during an active conversation never hides or closes it. */
    assert(!xz_background_idle(XZ_LISTENING,true,1,300000001));
    assert(!xz_background_idle(XZ_READY,true,1,300000001));
    assert(!xz_background_idle(XZ_ERROR,false,1,300000001));
    assert(!xz_background_idle(XZ_READY,false,1,60000000));
    assert(xz_background_idle(XZ_READY,false,1,60000001));
    assert(xz_background_idle(XZ_IDLE,false,1,60000001));
    assert(!xz_background_idle(XZ_IDLE,false,0,60000001));
    assert(xz_retry_delay(XZ_FAULT_TRANSPORT,0)==1000000);
    assert(xz_retry_delay(XZ_FAULT_TIMEOUT,2)==4000000);
    assert(!xz_retry_delay(XZ_FAULT_TRANSPORT,3));
    assert(!xz_retry_delay(XZ_FAULT_MEMORY,0)&&!xz_retry_delay(XZ_FAULT_SERVER_END,0));
    assert(!xz_retry_delay(XZ_FAULT_QUEUE,0)&&!xz_retry_delay(XZ_FAULT_PROTOCOL,0));
    assert(xz_volume_valid(0,40)==0&&xz_volume_valid(100,40)==100);
    assert(xz_volume_valid(63,40)==63); // Existing exact voice-command percentages are not quantized.
    assert(xz_volume_valid(255,60)==60&&xz_volume_valid(255,255)==40);
    xz_volume_t v={40,40,0,0};assert(!xz_volume_due(&v,2000000,true,true));
    xz_volume_observe(&v,50,100);xz_volume_observe(&v,60,500000);
    assert(!xz_volume_due(&v,1000100,true,false)); // Repeated keys reset debounce.
    assert(!xz_volume_due(&v,1500000,false,false)); // No flash during audio.
    assert(xz_volume_due(&v,1500000,true,false));
    xz_volume_result(&v,1500000,false);assert(v.saved==40);
    assert(!xz_volume_due(&v,2000000,true,false)); // Failure stays dirty; bounded retry.
    assert(xz_volume_due(&v,4500000,true,false));
    xz_volume_result(&v,4500000,true);assert(v.saved==60);
    xz_volume_observe(&v,0,4500001);assert(xz_volume_due(&v,4500001,false,true));
    xz_volume_result(&v,4500001,true);assert(v.saved==0); // Mute survives persistence.
    xz_volume_observe(&v,10,5000000);xz_volume_observe(&v,0,5000001);
    assert(!xz_volume_due(&v,9000000,true,true)); // Returning to saved value needs no write.
    const char *caption="你A好😀";uint32_t spent=0;size_t at=0;
    assert(xz_caption_step(caption,at,89,&spent)==0&&spent==0);
    at=xz_caption_step(caption,at,90,&spent);assert(at==3&&spent==90);
    assert(xz_caption_step(caption,at,90,&spent)==at); // Stalled playback stalls text.
    at=xz_caption_step(caption,at,130,&spent);assert(at==4);
    at=xz_caption_step(caption,at,220,&spent);assert(at==7);
    at=xz_caption_step(caption,at,310,&spent);assert(at==11);
    assert(xz_caption_step(caption,at,1000,&spent)==at);
    spent=0;assert(xz_caption_step(caption,0,310,&spent)==11&&spent==310); // Delayed frame catches up once.
    spent=0;assert(xz_caption_step("你好世界",0,XZ_CAPTION_LEAD_MS,&spent)==6); // Opening text before PCM.
    assert(xz_caption_step("你好世界",6,XZ_CAPTION_LEAD_MS,&spent)==6); // Lead does not accumulate on a stall.
    assert(xz_caption_next("\xe4\xb8",0)==0);
    xz_endpoint_t endpoint;
    assert(xz_endpoint("wss://voice.example.com:8443/chat?q=1",&endpoint));
    assert(endpoint.tls&&endpoint.port==8443&&!strcmp(endpoint.host,"voice.example.com")&&!strcmp(endpoint.path,"/chat?q=1"));
    assert(xz_endpoint("ws://localhost?test=1",&endpoint)&&!strcmp(endpoint.path,"/?test=1"));
    assert(xz_endpoint("wss://voice.example.com",&endpoint)&&endpoint.port==443&&!strcmp(endpoint.path,"/"));
    const char *bad[]={"http://test","wss://","wss://user@host/","wss://host:0/","ws://host:65536","ws://host:xyz","ws://host/#x","ws://host/\r\nInjected: value","ws://host:9999999999999999999999/"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(!xz_endpoint(bad[i],&endpoint));
    uint8_t packet[1600]={0},key[16];const uint8_t *audio;size_t length;
    assert(xz_hex16("00112233445566778899aAbBcCdDeEfF",key)&&key[15]==255);
    assert(!xz_hex16("001122",key));assert(!xz_hex16("00112233445566778899aabbccddeegg",key));
    assert(!xz_ws_audio(0,packet,20,&audio,&length));
    assert(xz_ws_audio(1,packet,30,&audio,&length)&&length==30&&audio==packet);
    assert(!xz_ws_audio(1,packet,1501,&audio,&length));
    packet[1]=2;packet[15]=30;assert(xz_ws_audio(2,packet,46,&audio,&length)&&length==30&&audio==packet+16);
    assert(!xz_ws_audio(2,packet,45,&audio,&length));
    for(size_t i=0;i<17;i++)assert(!xz_ws_audio(2,packet,i,&audio,&length));
    memset(packet,0,sizeof(packet));packet[3]=12;assert(xz_ws_audio(3,packet,16,&audio,&length)&&length==12);packet[0]=1;assert(!xz_ws_audio(3,packet,16,&audio,&length));
    memset(packet,0,sizeof(packet));packet[0]=1;xz_udp_header(packet,50,900,2);uint32_t sequence;
    assert(xz_udp_audio(packet,66,1,&sequence)&&sequence==2);assert(!xz_udp_audio(packet,66,2,&sequence));assert(!xz_udp_audio(packet,65,0,&sequence));
    for(size_t i=0;i<17;i++)assert(!xz_udp_audio(packet,i,0,&sequence));
    char text[8];xz_text_copy(text,sizeof(text),"你好世界");assert(!strcmp(text,"你好"));xz_text_copy(text,4,"你a");assert(!strcmp(text,"你"));xz_text_copy(text,sizeof(text),"a\xe4\xb8");assert(!strcmp(text,"a"));
    puts("XiaoZhi URL, frame bounds, UDP replay and UTF-8 captions: PASS");return 0;
}
