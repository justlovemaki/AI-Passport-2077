#pragma once
#include <stdbool.h>
#include <stdint.h>

/* A modal/page transition consumes the tail of the initiating gesture. Delayed
 * clicks stay blocked until a fresh press, not merely until release. Key IDs
 * follow the caller's three-button ordering; no ESP-IDF dependency. */
typedef struct { uint8_t held, blocked, app_held; } badge_return_input_t;
static inline bool badge_return_input_event(badge_return_input_t *s,unsigned key,
                                            bool press,bool release) {
    if(key>=3)return false;
    uint8_t bit=(uint8_t)(1u<<key);
    if(press){s->blocked&=(uint8_t)~bit;s->held|=bit;}
    bool consumed=(s->blocked&bit)!=0;
    if(release)s->held&=(uint8_t)~bit;
    return consumed;
}
/* Register only presses actually dispatched to an app, never modal presses. */
static inline void badge_return_input_app_press(badge_return_input_t *s,unsigned key) {
    if(key<3)s->app_held|=(uint8_t)(1u<<key);
}
static inline bool badge_return_input_app_release(badge_return_input_t *s,unsigned key) {
    if(key>=3)return false;
    uint8_t bit=(uint8_t)(1u<<key);
    bool owned=(s->app_held&bit)!=0;
    s->app_held&=(uint8_t)~bit;
    return owned;
}
static inline void badge_return_input_barrier(badge_return_input_t *s,unsigned key) {
    s->blocked|=s->held;
    if(key<3)s->blocked|=(uint8_t)(1u<<key);
}
