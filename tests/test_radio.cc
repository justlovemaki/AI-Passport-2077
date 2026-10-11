#include "radio_logic.h"
#include "radio_controls.h"
#include "radio_json_stream.h"
#include <string>
#include <cassert>
#include <cstdio>
int main(){
    assert(radio_parse_frequency("上海 FM94.7")==947);
    assert(radio_parse_frequency("FM 101")==1010);
    assert(radio_parse_frequency("交通971")==971);
    assert(radio_parse_frequency("2026新闻")==0);
    assert(radio_parse_frequency("网络音乐")==0);
    assert(radio_parse_frequency("FM 108.0")==1080);
    assert(radio_parse_frequency("FM 120.0")==0);
    assert(radio_dial_position(947,0,10)==335);
    assert(radio_dial_position(0,0,10)==0&&radio_dial_position(0,9,10)==1000);
    assert(radio_dial_position(0,0,1)==500);
    assert(radio_dial_position(870,0,10)==0);
    int16_t pcm[]={32767,32767,-32768,-32768,100,-100};
    assert(radio_downmix(pcm,sizeof(pcm),2)==6);assert(pcm[0]==32767&&pcm[1]==-32768&&pcm[2]==0);
    assert(radio_downmix(pcm,5,2)==0);assert(radio_downmix(pcm,6,3)==0);
    assert(!radio_timer_expired(0,123));assert(!radio_timer_expired(15,14));assert(radio_timer_expired(15,15));
    RadioJsonObjects objects;unsigned entries=0;
    const std::string json=R"([{"name":"a \" }","nested":{"x":1}}, {"url":"/b"}])";
    for(char ch:json)if(objects.feed(ch))++entries;
    assert(entries==2&&objects.done&&!objects.failed);
    RadioJsonObjects oversized;entries=0;
    for(char ch:std::string("[{\"name\":\"")+std::string(5000,'x')+"\"},{\"name\":\"ok\"}]")if(oversized.feed(ch))++entries;
    assert(entries==1&&oversized.done&&!oversized.failed);
    RadioJsonObjects truncated;for(char ch:std::string("[{"))truncated.feed(ch);assert(!truncated.done);
    RadioJsonObjects malformed;for(char ch:std::string("[{},]"))malformed.feed(ch);assert(malformed.failed);
    radio_controls_t c;radio_controls_init(&c,40,0);assert(!radio_controls_can_back(&c));assert(!radio_controls_back(&c));
    assert(radio_controls_move(&c,-1)==RADIO_CHANNEL_UP);assert(radio_controls_move(&c,1)==RADIO_CHANNEL_DOWN);assert(radio_controls_ok(&c)==RADIO_TOGGLE);
    radio_controls_settings(&c);radio_controls_ok(&c);assert(c.page==RADIO_VOLUME);
    for(int i=0;i<30;i++){radio_controls_move(&c,-1);}assert(c.volume==100);
    for(int i=0;i<30;i++){radio_controls_move(&c,1);}assert(c.volume==0);
    radio_controls_t before=c;
    assert(radio_controls_can_back(&c)&&c.page==before.page&&c.volume==before.volume&&c.selected==before.selected);
    assert(radio_controls_back(&c));assert(c.page==RADIO_SETTINGS);radio_controls_move(&c,1);radio_controls_ok(&c);
    assert(c.page==RADIO_CITY);radio_controls_move(&c,-1);assert(c.selected==7);assert(radio_controls_ok(&c)==RADIO_CITY_CHANGED&&c.city==7&&c.page==RADIO_MAIN);
    radio_controls_settings(&c);radio_controls_move(&c,1);radio_controls_move(&c,1);radio_controls_ok(&c);
    assert(c.page==RADIO_TIMER);radio_controls_move(&c,1);assert(radio_controls_ok(&c)==RADIO_TIMER_CHANGED&&RADIO_TIMER_MINUTES[c.timer]==15);
    radio_controls_settings(&c);radio_controls_move(&c,-1);radio_controls_ok(&c);assert(c.page==RADIO_TIMER);
    assert(radio_controls_back(&c)&&c.page==RADIO_SETTINGS);assert(radio_controls_back(&c)&&c.page==RADIO_MAIN);assert(!radio_controls_back(&c));
    radio_quick_input_t input={};
    for(unsigned key=0;key<2;key++){
        assert(!radio_direction_click(&input,key,RADIO_PRESS));
        assert(radio_direction_click(&input,key,RADIO_RELEASE));
        assert(!radio_direction_click(&input,key,RADIO_CLICK));
        assert(!radio_direction_click(&input,key,RADIO_RELEASE));
        for(int i=0;i<2;i++){assert(!radio_direction_click(&input,key,RADIO_PRESS));assert(radio_direction_click(&input,key,RADIO_RELEASE));}
        assert(!radio_direction_click(&input,key,RADIO_DOUBLE));
        assert(!radio_direction_click(&input,key,RADIO_PRESS));assert(!radio_direction_click(&input,key,RADIO_LONG));assert(!radio_direction_click(&input,key,RADIO_RELEASE));
    }
    puts("Release input, rapid taps, long-press suppression and three settings: PASS");
    puts("Radio frequency/dial, PCM downmix, timer and nested controls: PASS");
}
