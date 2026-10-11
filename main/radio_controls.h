#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { RADIO_MAIN, RADIO_SETTINGS, RADIO_VOLUME, RADIO_CITY, RADIO_TIMER } radio_page_t;
typedef enum { RADIO_NOTHING, RADIO_CHANNEL_UP, RADIO_CHANNEL_DOWN, RADIO_TOGGLE, RADIO_VOLUME_CHANGED, RADIO_CITY_CHANGED, RADIO_TIMER_CHANGED } radio_action_t;
typedef enum { RADIO_PRESS, RADIO_RELEASE, RADIO_CLICK, RADIO_DOUBLE, RADIO_LONG } radio_input_event_t;
typedef struct {unsigned edges,held,long_keys;} radio_quick_input_t;
/* Each short press responds on release; later click/double events are ignored. */
bool radio_direction_click(radio_quick_input_t *input,unsigned key,radio_input_event_t event);
typedef struct {radio_page_t page;unsigned selected,city,timer;int volume;} radio_controls_t;
extern const char *const RADIO_CITIES[8];
extern const unsigned RADIO_TIMER_MINUTES[5];
void radio_controls_init(radio_controls_t *c,int volume,unsigned city);
radio_action_t radio_controls_move(radio_controls_t *c,int direction);
radio_action_t radio_controls_ok(radio_controls_t *c);
void radio_controls_settings(radio_controls_t *c);
bool radio_controls_can_back(const radio_controls_t *c);
bool radio_controls_back(radio_controls_t *c);
#ifdef __cplusplus
}
#endif
