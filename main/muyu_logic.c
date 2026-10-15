#include "muyu_logic.h"
static const uint32_t intervals[] = {2000, 1000, 750, 500};
bool muyu_volume_decode(uint8_t stored,bool legacy,uint8_t *percent)
{
    if(!percent)return false;
    if(legacy){if(stored>=5)return false;*percent=(uint8_t)(stored*20);return true;}
    if(stored>BADGE_VOLUME_MAX||stored%BADGE_VOLUME_STEP)return false;
    *percent=stored;return true;
}
void muyu_init(muyu_state_t *s, uint32_t total, uint8_t speed, uint8_t volume, uint32_t now)
{
    *s = (muyu_state_t){.total = total <= MUYU_COUNT_MAX ? total : MUYU_COUNT_MAX,
        .speed = speed < MUYU_SPEED_COUNT ? speed : 1,
        .volume = volume <= BADGE_VOLUME_MAX && volume % BADGE_VOLUME_STEP == 0 ? volume : MUYU_VOLUME_DEFAULT, .last_save_ms = now};
}
bool muyu_strike(muyu_state_t *s)
{
    if (s->session < MUYU_COUNT_MAX) s->session++;
    if (s->total == MUYU_COUNT_MAX) return false;
    s->total++;
    s->dirty = true;
    return true;
}
uint32_t muyu_interval(const muyu_state_t *s) { return intervals[s->speed]; }
uint8_t muyu_volume(const muyu_state_t *s) { return s->volume; }
void muyu_toggle_auto(muyu_state_t *s, uint32_t now)
{
    s->automatic = !s->automatic;
    s->next_hit_ms = now + muyu_interval(s);
}
void muyu_next_speed(muyu_state_t *s, uint32_t now)
{
    s->speed = (s->speed + 1) % MUYU_SPEED_COUNT;
    s->next_hit_ms = now + muyu_interval(s);
    s->dirty = true;
}
void muyu_next_volume(muyu_state_t *s)
{
    s->volume = s->volume == BADGE_VOLUME_MAX ? 0 : (uint8_t)badge_volume_adjust(s->volume,1);
    s->dirty = true;
}
bool muyu_tick(muyu_state_t *s, uint32_t now)
{
    if (!s->automatic || (int32_t)(now - s->next_hit_ms) < 0) return false;
    /* A delayed worker emits one hit, never a catch-up burst. */
    s->next_hit_ms = now + muyu_interval(s);
    muyu_strike(s);
    return true;
}
bool muyu_save_due(const muyu_state_t *s, uint32_t now)
{
    return s->dirty && (uint32_t)(now - s->last_save_ms) >= MUYU_SAVE_MS;
}
void muyu_saved(muyu_state_t *s, uint32_t now, bool success)
{
    s->last_save_ms = now;
    if (success) {
        s->dirty = false;
        /* Session is the unsaved strike batch shown on screen. Once the batch
         * is committed, clear it so the page no longer looks like there are
         * pending records. The lifetime total remains in s->total. */
        s->session = 0;
    }
}
