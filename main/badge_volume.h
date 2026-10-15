#pragma once
/* Shared local-key policy. Exact percentages from voice/API commands remain
 * valid; key presses move by five points and clamp without rounding them. */
#define BADGE_VOLUME_STEP 5
#define BADGE_VOLUME_MAX 100
#define BADGE_VOLUME_DEFAULT 40
static inline int badge_volume_adjust(int percent,int direction){
    if(percent<0)percent=0;
    if(percent>BADGE_VOLUME_MAX)percent=BADGE_VOLUME_MAX;
    if(direction>0)return percent>BADGE_VOLUME_MAX-BADGE_VOLUME_STEP?BADGE_VOLUME_MAX:percent+BADGE_VOLUME_STEP;
    if(direction<0)return percent<BADGE_VOLUME_STEP?0:percent-BADGE_VOLUME_STEP;
    return percent;
}
static inline int badge_volume_segment_filled(unsigned percent,unsigned segment){
    return segment<20 && (segment+1)*BADGE_VOLUME_STEP<=percent;
}
