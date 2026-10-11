#pragma once
#include <stddef.h>
#include <stdbool.h>
typedef enum { BADGE_HOME, BADGE_TERMINAL, BADGE_GAMES, BADGE_SETTINGS, BADGE_PROFILE, BADGE_QR, BADGE_WIFI, BADGE_PLAYING, BADGE_CARDS, BADGE_AI_SETTINGS, BADGE_AI_CHAT, BADGE_DISPLAY_SETTINGS, BADGE_AI_VOLUME_PAGE, BADGE_SLEEP_SETTINGS, BADGE_ABOUT_SETTINGS } badge_page_t;
typedef enum { BADGE_NONE, BADGE_UP, BADGE_DOWN, BADGE_OK, BADGE_BACK, BADGE_SWITCH, BADGE_GO_HOME, BADGE_AI, BADGE_AI_OPEN_SETTINGS, BADGE_AI_CYCLE_STYLE } badge_input_t;
typedef enum { BADGE_IDLE, BADGE_REDRAW, BADGE_LAUNCH, BADGE_STOP, BADGE_BRIGHTNESS, BADGE_WIFI_TOGGLE, BADGE_SELECT, BADGE_WIFI_START, BADGE_AI_START, BADGE_AI_STOP, BADGE_AI_TOGGLE, BADGE_AI_VOLUME, BADGE_AI_STYLE, BADGE_SLEEP_SAVE } badge_action_t;
typedef struct {
    badge_page_t page;
    badge_page_t qr_return; /* Return to the page that opened quick contact. */
    badge_page_t settings_return;
    badge_page_t wifi_return;
    badge_page_t games_return;
    badge_page_t playing_return;
    size_t home_selected, game_selected, game_count;
    unsigned badge_count,active_badge,badge_selected,badge_mask;
    unsigned settings_selected,ai_selected,ai_volume,ai_style;
    bool ai_enabled;
    badge_page_t ai_return;
    badge_page_t ai_settings_return;
    unsigned brightness; /* 0..4 maps to 20..100 percent; never black out controls. */
    unsigned screen_timeout,timeout_selected;
} badge_navigation_t;
/* Read-only app query. A failed query must not be interpreted as an exit.
 * Presentation strings must remain valid until the chooser closes (literals). */
typedef struct {
    bool internal, confirm;
    const char *detail, *resume;
} badge_return_context_t;
typedef struct {
    badge_page_t source, destination;
    size_t game_selected;
    bool internal, confirm;
} badge_return_plan_t;
typedef struct {
    bool open, stopping;
    unsigned selected, count;
    badge_return_plan_t plan;
    const char *title, *detail, *resume;
} badge_return_menu_t;
badge_input_t badge_ai_long_gesture(badge_page_t page,bool up,bool down,bool long_press);
/* Planning is side-effect free; internal app navigation precedes shell exit. */
badge_page_t badge_navigation_parent(const badge_navigation_t *state);
badge_return_plan_t badge_return_plan(const badge_navigation_t *state,
                                      const badge_return_context_t *context);
/* Only app/session boundaries with distinct destinations or a real interruption
 * need a chooser. A stale plan cancels without executing a navigation action. */
badge_input_t badge_return_menu_handle(badge_return_menu_t *menu,
                                      const badge_return_plan_t *plan,badge_input_t input);
/* Reopen a failed exit with Continue selected; never claim playback resumed. */
void badge_return_menu_failed(badge_return_menu_t *menu,const badge_return_plan_t *plan);
/* Long OK switches badges on Home; elsewhere it requests a contextual return. */
badge_input_t badge_ok_gesture(badge_page_t page,bool long_press,bool click);
badge_input_t badge_up_gesture(badge_page_t page,bool configured,bool long_press,bool click);
/* The caller resolves the app ID; invalid/missing apps never change selection. */
badge_action_t badge_navigation_quick_launch(badge_navigation_t *state,size_t index);
void badge_navigation_init(badge_navigation_t *state, size_t games);
badge_action_t badge_navigation_handle(badge_navigation_t *state, badge_input_t input);

void badge_navigation_badges(badge_navigation_t *state,unsigned count,unsigned active,unsigned mask);
