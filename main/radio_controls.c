#include "radio_controls.h"
#include "badge_volume.h"
bool radio_direction_click(radio_quick_input_t *input,unsigned key,radio_input_event_t event){
    if(key>1)return false;
    unsigned bit=1u<<key;
    if(event==RADIO_PRESS){input->edges|=bit;input->held|=bit;input->long_keys&=~bit;return false;}
    if(event==RADIO_LONG){input->long_keys|=bit;return false;}
    if(event==RADIO_RELEASE){bool clicked=(input->held&bit)&&!(input->long_keys&bit);input->held&=~bit;return clicked;}
    return !(input->edges&bit); /* Synthetic click-only callers remain supported. */
}
const char *const RADIO_CITIES[8]={"自动定位","北京","上海","广州","深圳","长沙","杭州","成都"};
const unsigned RADIO_TIMER_MINUTES[5]={0,15,30,60,90};
static unsigned cycle(unsigned value,int delta,unsigned count){return (value+count+(delta>0?1:-1))%count;}
void radio_controls_init(radio_controls_t *c,int volume,unsigned city){*c=(radio_controls_t){.page=RADIO_MAIN,.volume=volume,.city=city<8?city:0};}
void radio_controls_settings(radio_controls_t *c){c->page=RADIO_SETTINGS;c->selected=0;}
radio_action_t radio_controls_move(radio_controls_t *c,int direction){
    if(c->page==RADIO_MAIN)return direction<0?RADIO_CHANNEL_UP:RADIO_CHANNEL_DOWN;
    if(c->page==RADIO_VOLUME){c->volume=badge_volume_adjust(c->volume,direction<0?1:-1);return RADIO_VOLUME_CHANGED;}
    if(c->page==RADIO_SETTINGS)c->selected=cycle(c->selected,direction,3);
    if(c->page==RADIO_CITY)c->selected=cycle(c->selected,direction,8);
    if(c->page==RADIO_TIMER)c->selected=cycle(c->selected,direction,5);
    return RADIO_NOTHING;
}
radio_action_t radio_controls_ok(radio_controls_t *c){
    if(c->page==RADIO_MAIN)return RADIO_TOGGLE;
    if(c->page==RADIO_SETTINGS){static const radio_page_t pages[]={RADIO_VOLUME,RADIO_CITY,RADIO_TIMER};c->page=pages[c->selected%3];c->selected=c->page==RADIO_CITY?c->city:c->page==RADIO_TIMER?c->timer:0;return RADIO_NOTHING;}
    if(c->page==RADIO_CITY){c->city=c->selected;c->page=RADIO_MAIN;return RADIO_CITY_CHANGED;}
    if(c->page==RADIO_TIMER){c->timer=c->selected;c->page=RADIO_MAIN;return RADIO_TIMER_CHANGED;}
    c->page=RADIO_SETTINGS;c->selected=0;return RADIO_NOTHING;
}
bool radio_controls_can_back(const radio_controls_t *c){return c->page!=RADIO_MAIN;}
bool radio_controls_back(radio_controls_t *c){
    if(c->page==RADIO_MAIN)return false;
    c->page=c->page==RADIO_SETTINGS?RADIO_MAIN:RADIO_SETTINGS;c->selected=0;return true;
}
