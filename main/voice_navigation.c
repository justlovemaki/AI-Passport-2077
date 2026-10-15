#include "voice_navigation.h"
#include "badge_volume.h"
void voice_navigation_init(voice_navigation_t *n) {*n=(voice_navigation_t){.volume=40};}
bool voice_navigation_select(voice_navigation_t *n,unsigned clip) {
    if(clip>=VOICE_CLIP_COUNT)return false;
    for(unsigned p=0;p<VOICE_PACK_COUNT;p++)if(clip>=voice_packs[p].first&&clip<voice_packs[p].first+voice_packs[p].count){
        n->pack=p;n->clip=clip-voice_packs[p].first;n->view=VOICE_CLIPS;return true;
    }
    return false;
}
unsigned voice_navigation_first(unsigned selected) {return selected<6?0:selected-5;}
void voice_navigation_move(voice_navigation_t *n,int direction) {
    if(n->view==VOICE_VOLUME) {n->volume=badge_volume_adjust(n->volume,direction);return;}
    unsigned *selected=n->view==VOICE_PACKS?&n->pack:&n->clip;
    unsigned count=n->view==VOICE_PACKS?VOICE_PACK_COUNT:voice_packs[n->pack].count;
    if(direction<0) {*selected=*selected?*selected-1:count-1;}
    else {*selected=(*selected+1)%count;}
}
int voice_navigation_ok(voice_navigation_t *n) {
    if(n->view==VOICE_VOLUME){n->view=n->previous;return -1;}
    if(n->view==VOICE_PACKS){n->view=VOICE_CLIPS;n->clip=0;return -1;}
    return (int)(voice_packs[n->pack].first+n->clip);
}
bool voice_navigation_can_back(const voice_navigation_t *n) {
    return n->view!=VOICE_PACKS;
}
bool voice_navigation_back(voice_navigation_t *n) {
    if(n->view==VOICE_VOLUME){n->view=n->previous;return true;}
    if(n->view==VOICE_CLIPS){n->view=VOICE_PACKS;return true;}
    return false;
}
void voice_navigation_volume(voice_navigation_t *n) {
    if(n->view!=VOICE_VOLUME){n->previous=n->view;n->view=VOICE_VOLUME;}
}
