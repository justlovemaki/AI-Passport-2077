#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { BC_MUYU, BC_RADIO, BC_VOICE, BC_PLAY_VOICE, BC_QR, BC_BADGE, BC_BRIGHTNESS, BC_XZ_VOLUME, BC_RADIO_PRESET, BC_REMINDER_SET, BC_REMINDER_CANCEL, BC_YAO, BC_MUSE, BC_PROFILE_EDIT, BC_HOME, BC_SHUTDOWN } badge_control_kind_t;
typedef struct {badge_control_kind_t kind;unsigned value;char text[129];int64_t when;unsigned minute,repeat,revision,field;} badge_control_command_t;
typedef struct {unsigned brightness,mask,qr_mask,count,active;bool enabled;int last_result;int battery;char names[5][129];unsigned reminder_seconds;bool reminder_active;char reminder_text[97];} badge_control_status_t;
/* Main task publishes navigation; the voice task never reads live LVGL state. */
void badge_control_publish(unsigned brightness,unsigned mask,unsigned qr_mask,unsigned count,unsigned active);
badge_control_status_t badge_control_status(void);
void badge_control_enable(bool enabled);
void badge_control_identity(unsigned slot,const char *name);
void badge_control_runtime(int battery,bool reminder_active,unsigned remaining,const char *text);
void badge_control_cancel(void);
/* Reserve then commit. In-place actions await execution before replying;
 * navigation actions commit only after their acceptance response is sent. */
const char *badge_control_reserve(badge_control_command_t command,uint32_t *ticket);
void badge_control_commit(uint32_t ticket,bool sent);
bool badge_control_take(badge_control_command_t *command);
void badge_control_finish(bool success);
/* Wake the navigation owner after committing a command, outside the lock. */
void badge_control_set_wakeup(void (*wake)(void));
bool badge_control_peek(uint32_t ticket,badge_control_command_t *command);
/* 0 pending, 1 completed, -1 failed, -2 cancelled/replaced. */
int badge_control_result(uint32_t ticket);
bool badge_control_stays(badge_control_kind_t kind);
void badge_control_set_created_id(uint32_t id);
uint32_t badge_control_created_id(void);
#ifdef __cplusplus
}
#endif
