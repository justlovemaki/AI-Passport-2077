#pragma once
#include <stdbool.h>
#include "voice_catalog.h"
typedef enum {VOICE_PACKS, VOICE_CLIPS, VOICE_VOLUME} voice_view_t;
typedef struct {voice_view_t view, previous; unsigned pack, clip; int volume;} voice_navigation_t;
void voice_navigation_init(voice_navigation_t *n);
bool voice_navigation_select(voice_navigation_t *n,unsigned clip);
void voice_navigation_move(voice_navigation_t *n, int direction);
/* Returns a clip index to play, or -1 for navigation only. */
int voice_navigation_ok(voice_navigation_t *n);
bool voice_navigation_can_back(const voice_navigation_t *n);
bool voice_navigation_back(voice_navigation_t *n);
void voice_navigation_volume(voice_navigation_t *n);
unsigned voice_navigation_first(unsigned selected);
