#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum {XZ_IDLE,XZ_CONNECTING,XZ_ACTIVATION,XZ_READY,XZ_LISTENING,XZ_THINKING,XZ_SPEAKING,XZ_ERROR} xz_state_t;
typedef enum {XZ_NO_ACTION,XZ_CONNECT,XZ_START,XZ_PAUSE,XZ_INTERRUPT} xz_action_t;
typedef enum {XZ_FAULT_NONE,XZ_FAULT_TRANSPORT,XZ_FAULT_TIMEOUT,XZ_FAULT_MEMORY,
    XZ_FAULT_QUEUE,XZ_FAULT_PROTOCOL,XZ_FAULT_AUDIO,XZ_FAULT_SERVER_END} xz_fault_t;
/* READY can auto-resume capture; conservatively protect an active session. */
static inline bool xz_return_confirmation(xz_state_t state){
    return state==XZ_CONNECTING||state==XZ_READY||state==XZ_LISTENING||
           state==XZ_THINKING||state==XZ_SPEAKING;
}
/* Start capture only after discovery. The shared codec arena is reserved
 * before the handshake and retained until this audio session is parked. */
static inline bool xz_session_ready(bool hello,bool tools_advertised){return hello&&tools_advertised;}
/* RAM-only service lease. Never extend it merely by checking its age. */
static inline bool xz_cache_fresh(int64_t since,int64_t now,int64_t ttl){
    return since>0&&now>=since&&now-since<ttl;
}
static inline bool xz_connection_keep(int64_t since,int64_t now,bool connected,unsigned free_heap){
    return connected&&free_heap>=49152&&xz_cache_fresh(since,now,120000000);
}
/* An active listening turn must never be treated as idle from PCM amplitude.
 * Keep errors visible; only a paused/ended session may return to local wake. */
static inline bool xz_background_idle(xz_state_t state,bool conversation,int64_t since,int64_t now){
    return !conversation&&(state==XZ_IDLE||state==XZ_READY)&&since>0&&now>=since&&now-since>=60000000;
}
static inline int64_t xz_retry_delay(xz_fault_t reason,unsigned attempts){
    return (reason==XZ_FAULT_TRANSPORT||reason==XZ_FAULT_TIMEOUT)&&attempts<3?(1000000LL<<attempts):0;
}
static inline xz_action_t xz_ok_action(xz_state_t state,bool connected){
    if(state==XZ_CONNECTING)return XZ_NO_ACTION;
    if(!connected)return XZ_CONNECT;
    if(state==XZ_LISTENING)return XZ_PAUSE;
    if(state==XZ_SPEAKING||state==XZ_THINKING)return XZ_INTERRUPT;
    return XZ_START;
}
static inline bool xz_auto_listen(bool active,xz_state_t state,int64_t now,int64_t resume_at){
    return active&&state==XZ_READY&&now>=resume_at;
}
static inline bool xz_tts_allowed(bool active,xz_state_t state,int64_t now,int64_t ignore_until){
    return active&&now>=ignore_until&&(state==XZ_LISTENING||state==XZ_THINKING||state==XZ_SPEAKING);
}
static inline bool xz_tts_drained(int64_t stopped,int64_t last_audio,int64_t now){
    int64_t latest=stopped>last_audio?stopped:last_audio;
    return stopped>0&&now>=latest&&now-latest>400000;
}
