/* Exercise the real power owner with deterministic BSP/time substitutes. */
#include "badge_power.h"
#include "esp_pm.h"
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
static uint32_t clock_ms;
static int locks[4],created,display_calls,audio_calls,live_pm,fail_create_at,fail_configure;
static bool standby;
static bool panel_dark,audio_dark,fail_resume,fail_sleep,fail_wake,fail_suspend;
static int transition_lock;
int *xSemaphoreCreateMutex(void){return &transition_lock;}
int xSemaphoreTake(int *lock,unsigned wait){(void)wait;assert(!*lock);*lock=1;return 1;}
void xSemaphoreGive(int *lock){assert(*lock);*lock=0;}
bool bsp_audio_needs_wake(void){return audio_dark||fail_wake;}
bool bsp_display_is_suspended(void){return panel_dark;}

int64_t esp_timer_get_time(void){return (int64_t)clock_ms*1000;}
esp_err_t esp_pm_lock_create(int type,int arg,const char *name,esp_pm_lock_handle_t *lock){(void)type;(void)arg;(void)name;assert(created<4);if(created+1==fail_create_at)return ESP_FAIL;*lock=&locks[created++];live_pm++;return ESP_OK;}
void esp_pm_lock_delete(esp_pm_lock_handle_t lock){assert(lock&&!*lock&&live_pm>0);live_pm--;}
void esp_pm_lock_acquire(esp_pm_lock_handle_t lock){assert(*lock==0);++*lock;}
void esp_pm_lock_release(esp_pm_lock_handle_t lock){assert(*lock==1);--*lock;}
esp_err_t esp_pm_configure(const esp_pm_config_t *c){assert(c->min_freq_mhz==40&&c->max_freq_mhz==160);return fail_configure?ESP_FAIL:ESP_OK;}
esp_err_t bsp_display_suspend(void){display_calls++;panel_dark=true;return fail_suspend?ESP_FAIL:ESP_OK;}
esp_err_t bsp_display_resume(void){display_calls++;if(fail_resume)return ESP_FAIL;panel_dark=false;return ESP_OK;}
uint8_t bsp_display_brightness(void){return panel_dark?0:80;}
bool bsp_audio_is_sleeping(void){return audio_dark;}
esp_err_t bsp_audio_sleep(void){audio_calls++;if(fail_sleep)return ESP_FAIL;audio_dark=true;return ESP_OK;}
esp_err_t bsp_audio_wake(void){audio_calls++;if(fail_wake)return ESP_FAIL;audio_dark=false;return ESP_OK;}
void badge_network_xiaozhi_power(bool active,bool busy){(void)active;(void)busy;}
void badge_network_standby(bool asleep){standby=asleep;}
esp_err_t badge_network_wake_probe(void){return ESP_OK;}
static void step(uint32_t ms,bool awake,bool cpu){clock_ms=ms;badge_power_display_tick(awake,cpu);}
int main(void){
    for(int n=1;n<=4;n++){
        fail_create_at=n;created=0;
        assert(badge_power_init()!=ESP_OK&&live_pm==0);
    }
    fail_create_at=0;fail_configure=1;created=0;
    assert(badge_power_init()!=ESP_OK&&live_pm==0);
    for(int n=0;n<4;n++)assert(!locks[n]);
    fail_configure=0;created=0;

    assert(badge_power_init()==ESP_OK);
    badge_power_set_timeout(2);step(59999,false,false);assert(!panel_dark);
    step(60000,false,false);assert(panel_dark&&locks[0]==0); /* No AI owner required. */
    badge_power_activity();step(60001,false,false);assert(!panel_dark&&locks[0]==1);
    badge_power_set_timeout(0);step(1000000,false,false);assert(!panel_dark);
    badge_power_set_timeout(1);badge_power_enter();
    clock_ms=1029000;badge_power_tick(true,true);step(1050000,false,false);assert(!panel_dark);
    badge_power_tick(false,false);step(1079999,false,false);assert(!panel_dark);
    step(1080000,false,false);assert(panel_dark);
    int before=display_calls;badge_power_tick(false,false);assert(display_calls==before&&audio_dark);
    badge_power_activity();step(1080001,false,false);assert(!panel_dark);
    badge_power_tick(false,false);assert(!audio_dark); /* Mic restore on same worker. */
    badge_power_leave();step(1110000,false,true);assert(panel_dark&&locks[0]==1); /* Radio may keep playing. */
    badge_power_activity();step(1110001,false,false);assert(!panel_dark);
    step(1140000,false,false);assert(panel_dark);
    before=display_calls;badge_power_enter();badge_power_set_local_listening(true);
    badge_power_tick(false,true);assert(display_calls==before&&!audio_dark);
    step(1140001,false,false);assert(panel_dark&&locks[0]==1); /* Wake detector doesn't light screen. */
    before=display_calls;badge_power_set_local_listening(false);badge_power_leave();
    assert(display_calls==before);step(1140002,false,false);assert(panel_dark&&locks[0]==0);
    fail_resume=true;badge_power_activity();step(1140003,false,false);assert(panel_dark);
    fail_resume=false;step(1140004,false,false);assert(!panel_dark); /* Failed resume retries. */
    step(1170003,true,true);assert(!panel_dark); /* Reminder keeps screen awake. */
    step(1200003,false,false);assert(panel_dark);
    badge_power_set_timeout(0);step(1200004,false,false);assert(!panel_dark);
    /* A mini-app can be open with both display and audio asleep. Starting
     * playback acquires its own leases before the shell runs another tick. */
    badge_power_set_timeout(1);badge_power_enter();
    clock_ms=1200005;badge_power_audio_tick(true);assert(!audio_dark&&locks[2]&&locks[3]);
    clock_ms=1215004;badge_power_audio_tick(false);assert(!audio_dark);
    clock_ms=1215005;badge_power_audio_tick(false);assert(audio_dark&&!locks[2]&&!locks[3]);
    step(1230004,false,false);assert(panel_dark&&!locks[0]&&!locks[1]&&standby);
    badge_power_audio_tick(true);assert(!audio_dark&&locks[2]&&locks[3]&&!standby);
    step(1230005,false,false);assert(panel_dark&&!standby); /* Playback stays dark. */
    badge_power_leave();assert(audio_dark&&!locks[2]&&!locks[3]);
    step(1230006,false,false);assert(standby);
    badge_power_status_t status=badge_power_status();
    assert(status.screen_off&&status.audio_sleeping&&!status.audio_owner&&!status.audio_performance&&!status.display_performance);
    /* Failed exit hands off retry without blocking a worker join. */
    badge_power_enter();badge_power_audio_tick(true);fail_sleep=true;
    assert(badge_power_leave()==ESP_FAIL);
    status=badge_power_status();assert(!status.audio_owner&&status.audio_performance&&!status.audio_sleeping);
    before=audio_calls;step(1230506,false,false);assert(audio_calls==before);
    step(1231006,false,false);assert(audio_calls==before+1);
    /* A new owner prevents the shell from suspending its live stream. */
    badge_power_enter();before=audio_calls;step(1233006,false,false);assert(audio_calls==before);
    fail_sleep=false;assert(badge_power_leave()==ESP_OK);
    badge_power_enter();badge_power_audio_tick(true);fail_sleep=true;
    assert(badge_power_leave()==ESP_FAIL);fail_sleep=false;
    step(1234006,false,false);assert(bsp_audio_is_sleeping()&&!badge_power_status().audio_performance);
    /* Wake failures retain audio leases until a later successful restore. */
    badge_power_enter();fail_wake=true;badge_power_audio_tick(true);
    assert(audio_dark&&badge_power_status().audio_performance);
    fail_wake=false;badge_power_audio_tick(true);assert(!audio_dark);badge_power_leave();
    /* Panel rollback failure is represented as dark, then explicitly retried. */
    badge_power_activity();step(1234007,false,false);fail_suspend=true;
    step(1264007,false,false);assert(panel_dark&&badge_power_screen_off());
    fail_suspend=false;step(1264008,false,false);assert(!panel_dark&&!badge_power_screen_off());
    /* Radio can spend a minute inside its stream loop without outer ticks.
     * Enqueued media must refresh activity so a channel change doesn't suspend
     * a codec that was playing immediately before the transition. */
    badge_power_enter();badge_power_audio_tick(true);
    for(clock_ms=1265008;clock_ms<=1324008;clock_ms+=1000)badge_power_audio_activity();
    before=audio_calls;badge_power_audio_tick(false);
    assert(!audio_dark&&audio_calls==before&&locks[2]&&locks[3]);
    clock_ms=1339007;badge_power_audio_tick(false);assert(!audio_dark);
    clock_ms=1339008;badge_power_audio_tick(false);assert(audio_dark);
    badge_power_leave();
    assert(audio_calls>0);
    puts("Unified display owner: deadlines, stream activity, AI busy, audio restore, retry and PM locks PASS");
    return 0;
}
