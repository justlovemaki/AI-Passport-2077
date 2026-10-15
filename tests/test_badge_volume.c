#include "badge_volume.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
int main(void){
    assert(BADGE_VOLUME_STEP==5&&BADGE_VOLUME_MAX==100);
    for(int value=0;value<=100;value++){
        assert(badge_volume_adjust(value,1)==(value>95?100:value+5));
        assert(badge_volume_adjust(value,-1)==(value<5?0:value-5));
        assert(badge_volume_adjust(value,0)==value);
    }
    assert(badge_volume_adjust(INT_MAX,1)==100&&badge_volume_adjust(INT_MIN,-1)==0);
    assert(badge_volume_adjust(INT_MAX,-1)==95&&badge_volume_adjust(INT_MIN,1)==5);
    assert(badge_volume_adjust(63,1)==68&&badge_volume_adjust(63,-1)==58);
    for(unsigned value=0;value<=100;value++)for(unsigned segment=0;segment<20;segment++)
        assert(badge_volume_segment_filled(value,segment)==((segment+1)*5<=value));
    assert(!badge_volume_segment_filled(100,20)&&!badge_volume_segment_filled(UINT_MAX,UINT_MAX));
    int value=0;
    for(int i=0;i<20;i++)value=badge_volume_adjust(value,1);
    assert(value==100);
    for(int i=0;i<20;i++)value=badge_volume_adjust(value,-1);
    assert(value==0);
    puts("Volume keys: five-point steps, mute/100 bounds, exact saved values and overflow guards PASS");
}
