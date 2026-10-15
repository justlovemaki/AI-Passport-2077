#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "badge_volume.h"
#define MUYU_COUNT_MAX 999999999U
#define MUYU_SAVE_MS 10000U
#define MUYU_SPEED_COUNT 4U
#define MUYU_VOLUME_COUNT (BADGE_VOLUME_MAX / BADGE_VOLUME_STEP + 1U)
#define MUYU_VOLUME_DEFAULT BADGE_VOLUME_DEFAULT
typedef struct {
    uint32_t total, session, next_hit_ms, last_save_ms;
    uint8_t speed, volume; /* volume is a percentage, not the legacy NVS index. */
    bool automatic, dirty;
} muyu_state_t;
void muyu_init(muyu_state_t *s, uint32_t total, uint8_t speed, uint8_t volume, uint32_t now);
bool muyu_strike(muyu_state_t *s);
void muyu_toggle_auto(muyu_state_t *s, uint32_t now);
void muyu_next_speed(muyu_state_t *s, uint32_t now);
void muyu_next_volume(muyu_state_t *s);
bool muyu_tick(muyu_state_t *s, uint32_t now);
bool muyu_save_due(const muyu_state_t *s, uint32_t now);
void muyu_saved(muyu_state_t *s, uint32_t now, bool success);
uint32_t muyu_interval(const muyu_state_t *s);
uint8_t muyu_volume(const muyu_state_t *s);
/* Legacy indices 0..4 map to 0/20/40/60/80. New values are 0..100 in 5% steps.
 * Invalid data leaves the output untouched so callers can preserve storage. */
bool muyu_volume_decode(uint8_t stored,bool legacy,uint8_t *percent);
