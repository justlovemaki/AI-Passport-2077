#include "voice_navigation.h"
#include "voice_stream.h"
#include "voice_activity.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t data[4096];
static bool read_test(void *context,uint32_t offset,void *out,size_t size){(void)context;if(offset>sizeof(data)||size>sizeof(data)-offset)return false;memcpy(out,data+offset,size);return true;}
int main(void) {
    voice_activity_t activity;voice_activity_init(&activity);assert(voice_activity_busy(&activity));
    voice_activity_submit(&activity,1);atomic_store(&activity.ready,true);
    assert(voice_activity_busy(&activity)); /* Queued before initialization finishes. */
    voice_activity_submit(&activity,2);voice_activity_complete(&activity,1);
    assert(voice_activity_busy(&activity)); /* Old cancellation cannot release new audio. */
    voice_activity_complete(&activity,2);assert(!voice_activity_busy(&activity));
    voice_activity_submit(&activity,3);assert(voice_activity_busy(&activity));
    voice_activity_complete(&activity,3);assert(!voice_activity_busy(&activity));
    voice_navigation_t n;voice_navigation_init(&n);assert(!voice_navigation_back(&n));
    voice_navigation_move(&n,-1);assert(n.pack==VOICE_PACK_COUNT-1);voice_navigation_move(&n,1);assert(n.pack==0);
    unsigned clips=0;
    for(unsigned p=0;p<VOICE_PACK_COUNT;p++){
        n.pack=p;assert(voice_navigation_ok(&n)==-1);assert(n.view==VOICE_CLIPS&&n.clip==0);
        for(unsigned i=0;i<voice_packs[p].count;i++){
            assert(voice_navigation_ok(&n)==(int)(voice_packs[p].first+i));
            assert(voice_navigation_first(n.clip)<=n.clip&&n.clip-voice_navigation_first(n.clip)<6);
            voice_navigation_move(&n,1);clips++;
        }
        assert(n.clip==0);voice_navigation_move(&n,-1);assert(n.clip==voice_packs[p].count-1);
        voice_navigation_volume(&n);assert(n.view==VOICE_VOLUME);voice_navigation_volume(&n);assert(n.previous==VOICE_CLIPS);
        for(int i=0;i<40;i++){voice_navigation_move(&n,1);}assert(n.volume==100);
        for(int i=0;i<40;i++){voice_navigation_move(&n,-1);}assert(n.volume==0);
        assert(voice_navigation_back(&n)&&n.view==VOICE_CLIPS);assert(voice_navigation_back(&n)&&n.view==VOICE_PACKS);
    }
    assert(clips==VOICE_CLIP_COUNT && clips==709);
    uint32_t cursor=0;uint8_t packet[1500];
    data[0]=3;data[2]=1;data[3]=2;data[4]=3;
    assert(voice_stream_next(read_test,NULL,0,5,&cursor,packet)==3&&cursor==5&&packet[2]==3);
    assert(voice_stream_next(read_test,NULL,0,5,&cursor,packet)==0);
    cursor=0;assert(voice_stream_next(read_test,NULL,0,4,&cursor,packet)==-1&&cursor==0);
    data[0]=0;assert(voice_stream_next(read_test,NULL,0,5,&cursor,packet)==-1);
    data[0]=255;data[1]=255;assert(voice_stream_next(read_test,NULL,0,4096,&cursor,packet)==-1);
    assert(voice_stream_next(read_test,NULL,0,1,&cursor,packet)==-1);
    assert(voice_stream_next(read_test,NULL,VOICE_TOTAL_BYTES-1,2,&cursor,packet)==-1);
    unsigned part;uint32_t local;size_t count;
    assert(voice_stream_span(VOICE_FIRST_BYTES-1,20,&part,&local,&count)&&part==0&&local==VOICE_FIRST_BYTES-1&&count==1);
    assert(voice_stream_span(VOICE_FIRST_BYTES,19,&part,&local,&count)&&part==1&&local==0&&count==19);
    assert(!voice_stream_span(VOICE_TOTAL_BYTES-1,2,&part,&local,&count));assert(!voice_stream_span(0xffffffff,1,&part,&local,&count));
    puts("Voice navigation, all 709 selections and bounded packet reader: PASS");return 0;
}
