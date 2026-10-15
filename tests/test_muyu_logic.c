#include "muyu_logic.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    muyu_state_t s;
    muyu_init(&s, 42, 1, MUYU_VOLUME_DEFAULT, 0);
    assert(!s.automatic && !s.dirty && s.session == 0 && s.total == 42);
    assert(muyu_strike(&s) && s.total == 43 && s.session == 1);
    assert(!muyu_save_due(&s, 9999) && muyu_save_due(&s, 10000));
    muyu_saved(&s, 10000, false);
    assert(s.dirty && !muyu_save_due(&s, 10001));
    assert(muyu_save_due(&s, 20000));
    muyu_saved(&s, 20000, true);
    assert(!s.dirty);
    muyu_toggle_auto(&s, 20000);
    assert(!muyu_tick(&s, 20999));
    assert(muyu_tick(&s, 21000) && s.total == 44);
    assert(muyu_tick(&s, 60000) && s.total == 45);
    assert(!muyu_tick(&s, 60000));
    muyu_toggle_auto(&s, 60001);
    assert(!muyu_tick(&s, 90000));
    for (unsigned i = 0; i < MUYU_SPEED_COUNT; i++) muyu_next_speed(&s, 90000);
    assert(s.speed == 1 && muyu_interval(&s) == 1000);
    for (unsigned i = 0; i < MUYU_VOLUME_COUNT; i++) muyu_next_volume(&s);
    assert(s.volume == 40 && muyu_volume(&s) == 40);
    muyu_init(&s, UINT32_MAX, 255, 255, UINT32_MAX - 500);
    assert(s.total == MUYU_COUNT_MAX && s.speed == 1 && s.volume == MUYU_VOLUME_DEFAULT);
    assert(!muyu_strike(&s) && s.total == MUYU_COUNT_MAX && s.session == 1);
    muyu_toggle_auto(&s, UINT32_MAX - 500);
    assert(!muyu_tick(&s, 498) && muyu_tick(&s, 499));
    muyu_next_speed(&s, 500);
    assert(!muyu_tick(&s, 1249) && muyu_tick(&s, 1250));
    s.dirty = true;
    assert(muyu_save_due(&s, 9499));
    muyu_init(&s, 123, 3, 0, 0);
    assert(s.total == 123 && !s.automatic && muyu_volume(&s) == 0);
    for(unsigned percent=0;percent<=100;percent+=5){
        muyu_init(&s,0,1,(uint8_t)percent,0);assert(muyu_volume(&s)==percent);
        for(unsigned step=0;step<MUYU_VOLUME_COUNT;step++)muyu_next_volume(&s);
        assert(muyu_volume(&s)==percent);
    }
    muyu_init(&s,0,1,95,0);muyu_next_volume(&s);assert(muyu_volume(&s)==100);
    muyu_next_volume(&s);assert(muyu_volume(&s)==0);muyu_next_volume(&s);assert(muyu_volume(&s)==5);
    uint8_t percent=255;
    for(uint8_t legacy=0;legacy<5;legacy++){assert(muyu_volume_decode(legacy,true,&percent)&&percent==legacy*20);}
    assert(!muyu_volume_decode(5,true,&percent)&&percent==80);
    assert(!muyu_volume_decode(255,true,&percent)&&percent==80);
    assert(muyu_volume_decode(100,false,&percent)&&percent==100);
    assert(!muyu_volume_decode(99,false,&percent)&&percent==100);
    assert(!muyu_volume_decode(255,false,&percent)&&percent==100);
    assert(!muyu_volume_decode(40,false,NULL));
    puts("muyu logic: PASS (count/timing/save, 21 five-percent levels, 100/mute wrap and legacy volume mapping)");
}
