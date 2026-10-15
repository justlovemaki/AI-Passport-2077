#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {unsigned seconds,read_bytes;} radio_probe_request_t;
/* One bounded diagnostic request. The lifecycle owner alone takes/finishes or
 * cancels it; a running request stays reserved until finish. No allocation. */
bool radio_probe_reserve(bool available,bool paused,bool stream_busy,unsigned seconds,unsigned read_bytes);
bool radio_probe_take(radio_probe_request_t *out);
bool radio_probe_busy(void);
void radio_probe_finish(void);
void radio_probe_cancel(void);
bool radio_probe_valid_read_size(unsigned bytes);
/* Player adapters: only an initialized, connected, paused, fully joined radio service may
 * accept these USB diagnostics. Read-size tuning is volatile and resets on exit. */
bool radio_player_probe_available(void);
bool radio_player_receive_probe(unsigned seconds,unsigned read_bytes);
bool radio_player_set_read_size(unsigned read_bytes);
unsigned radio_player_next_read_size(void);
#ifdef __cplusplus
}
#endif
