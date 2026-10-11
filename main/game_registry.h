#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "demo.h"
#include "badge_game_art.h"
#include "badge_navigation.h"
/* Append games here through game_registry.c. Callbacks follow demo.h locking
 * and stop-before-destroy contract. Games consume badge_theme.h under the LVGL
 * lock and apply palette revisions during UI refresh. No UI or navigation edits are required. */
typedef struct {
    const char *id;
    const char *description;
    const char *category;
    badge_game_art_t artwork;
    /* Optional nested-page back, paired with return_context reporting internal.
     * Called without LVGL lock from the lifecycle task. A failed internal back
     * never falls through to an app exit; absent queries describe a root page. */
    bool (*back)(void);
    /* Side-effect-free snapshot before showing a chooser. False defers the
     * request (e.g. LVGL lock contention); never exits on an unknown state. */
    bool (*return_context)(badge_return_context_t *context);
    demo_entry_t lifecycle;
} badge_game_t;
extern const badge_game_t BADGE_GAMES_REGISTRY[];
extern const size_t BADGE_GAME_COUNT;
