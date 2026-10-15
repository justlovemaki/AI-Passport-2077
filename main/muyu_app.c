#include "muyu_app.h"
#include "muyu_logic.h"
#include "muyu_volume_store.h"
#include "muyu_ui.h"
#include "badge_power.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>

static const char *TAG = "muyu";
typedef struct { bsp_btn_t button; bsp_btn_ev_t event; } input_t;
typedef struct { uint32_t time; uint8_t volume; } sound_t;
static QueueHandle_t input_queue, sound_queue;
static atomic_bool audio_ready;
static atomic_bool stopping;
static SemaphoreHandle_t app_done, audio_done;
/* These flags and resource lifetimes belong to the menu lifecycle task. */
static bool app_started, audio_started, state_loaded;
static muyu_state_t state;
static nvs_handle_t storage;
static bool storage_open, storage_ok, battery_ok, buttons_ok;
static int battery = -1;
#define SAMPLE_RATE 16000
#define TONE_SAMPLES 2240
static int16_t *tone;
static const int16_t silence[1600];

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

void demo_muyu_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (!input_queue || atomic_load(&stopping) || event==BSP_BTN_RELEASE) return;
    /* Only enqueue useful events. No locks, audio, NVS, allocation or logging. */
    if ((button == BSP_BTN_OK && event != BSP_BTN_PRESS) ||
        (button != BSP_BTN_OK && event == BSP_BTN_PRESS)) return;
    const input_t input = {button, event};
    (void)xQueueSend(input_queue, &input, 0);
}

static void audio_worker(void *arg)
{
    (void)arg;
    sound_t sound;
    badge_power_enter();
    while (!atomic_load(&stopping)) {
        badge_power_audio_tick(false);
        if (xQueueReceive(sound_queue, &sound, pdMS_TO_TICKS(1000)) != pdTRUE) continue;
        if (atomic_load(&stopping)) break;
        if (!atomic_load(&audio_ready) || !sound.volume || now_ms() - sound.time > 120) continue;
        badge_power_audio_tick(true);
        bsp_audio_set_volume(sound.volume);
        esp_err_t err = bsp_audio_write(tone, TONE_SAMPLES * sizeof(*tone));
        /* Flush silence through every DMA descriptor to prevent a repeated tail. */
        if (err == ESP_OK) err = bsp_audio_write(silence, sizeof(silence));
        if (err != ESP_OK) {
            atomic_store(&audio_ready, false);
            ESP_LOGE(TAG, "Audio playback failed: %s", esp_err_to_name(err));
        }
    }
    badge_power_leave();
    xSemaphoreGive(audio_done);
    vTaskDelete(NULL);
}

static void prepare_audio(void)
{
    atomic_store(&audio_ready, false);
    if (bsp_audio_init() != ESP_OK || bsp_audio_set_format(SAMPLE_RATE, 16, 1) != ESP_OK) return;
    tone = malloc(TONE_SAMPLES * sizeof(*tone));
    if (!tone) return;
    /* Modal synthesis: inharmonic wooden resonances and a short noisy attack. */
    uint32_t noise = 0x4D595955;
    for (int i = 0; i < TONE_SAMPLES; i++) {
        float t = (float)i / SAMPLE_RATE;
        noise = noise * 1664525U + 1013904223U;
        float attack = i < 120 ? ((float)(noise >> 16) / 32768.0f - 1.0f) *
            0.18f * (1.0f - (float)i / 120) : 0;
        float body = expf(-32 * t) * (0.62f * sinf(6.2831853f * 720 * t) +
            0.26f * sinf(6.2831853f * 1170 * t) + 0.12f * sinf(6.2831853f * 1890 * t));
        float ramp = i < 16 ? (float)i / 16 : 1;
        float tail = i > TONE_SAMPLES - 160 ? (float)(TONE_SAMPLES - 1 - i) / 160 : 1;
        tone[i] = (int16_t)((body + attack) * ramp * tail * 12000);
    }
    sound_queue = xQueueCreate(1, sizeof(sound_t));
    if (!sound_queue) return;
    audio_done = xSemaphoreCreateBinary();
    if (!audio_done) {
        vQueueDelete(sound_queue);
        sound_queue = NULL;
        return;
    }
    atomic_store(&audio_ready, true);
    if (xTaskCreate(audio_worker, "muyu_audio", 3072, NULL, 6, NULL) != pdPASS) {
        atomic_store(&audio_ready, false);
        vQueueDelete(sound_queue);
        sound_queue = NULL;
        vSemaphoreDelete(audio_done);
        audio_done = NULL;
    } else {
        audio_started = true;
    }
}

static void load_state(void)
{
    uint32_t total = 0;
    uint8_t speed = 1, volume = MUYU_VOLUME_DEFAULT;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_OK) err = nvs_open("zen_muyu_v1", NVS_READWRITE, &storage);
    storage_open = err == ESP_OK;
    storage_ok = storage_open;
    if (storage_open) {
        esp_err_t a = nvs_get_u32(storage, "total", &total);
        esp_err_t b = nvs_get_u8(storage, "speed", &speed);
        esp_err_t c = muyu_volume_load(storage,&volume);
        /* Wrong types/corrupt records remain visible and are never overwritten. */
        if ((a != ESP_OK && a != ESP_ERR_NVS_NOT_FOUND) ||
            (b != ESP_OK && b != ESP_ERR_NVS_NOT_FOUND) ||
            (c != ESP_OK && c != ESP_ERR_NVS_NOT_FOUND) ||
            total > MUYU_COUNT_MAX || speed >= MUYU_SPEED_COUNT) {
            nvs_close(storage);
            storage_open = storage_ok = false;
            ESP_LOGE(TAG, "Stored record invalid; preserving NVS");
        }
    } else ESP_LOGW(TAG, "NVS unavailable: %s; no erase attempted", esp_err_to_name(err));
    muyu_init(&state, total, speed, volume, now_ms());
}

static void save_state(bool explicit_save)
{
    esp_err_t err = storage_open ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK && state.dirty) {
        err = nvs_set_u32(storage, "total", state.total);
        if (err == ESP_OK) err = nvs_set_u8(storage, "speed", state.speed);
        if (err == ESP_OK) err = muyu_volume_store(storage,state.volume);
        if (err == ESP_OK) err = nvs_commit(storage);
    }
    storage_ok = err == ESP_OK;
    muyu_saved(&state, now_ms(), storage_ok);
    if (!storage_ok) ESP_LOGW(TAG, "Save failed: %s", esp_err_to_name(err));
    if (explicit_save && bsp_lvgl_lock(100)) {
        muyu_ui_refresh(&state, battery, atomic_load(&audio_ready), storage_ok, buttons_ok);
        muyu_ui_notice(storage_ok ? "已保存 可以关机" : "保存失败 请重试");
        bsp_lvgl_unlock();
    }
}

static void hit_feedback(bool increased)
{
    if (atomic_load(&audio_ready) && sound_queue) {
        const sound_t sound = {now_ms(), muyu_volume(&state)};
        /* Keep at most one pending sound; fast strikes never build a backlog. */
        xQueueOverwrite(sound_queue, &sound);
    }
    if (!badge_power_screen_off() && bsp_lvgl_lock(30)) {
        muyu_ui_hit(increased);
        bsp_lvgl_unlock();
    }
}

static void worker(void *arg)
{
    (void)arg;
    uint32_t last_ui = 0, last_battery = now_ms();
    input_t input;
    while (!atomic_load(&stopping)) {
        if (xQueueReceive(input_queue, &input, pdMS_TO_TICKS(badge_power_screen_off()&&!state.automatic?1000:20)) == pdTRUE) {
            if (atomic_load(&stopping)) break;
            uint32_t now = now_ms();
            if (input.button == BSP_BTN_OK && input.event == BSP_BTN_PRESS) {
                hit_feedback(muyu_strike(&state));
                if (state.automatic) state.next_hit_ms = now + muyu_interval(&state);
            } else if (input.button == BSP_BTN_UP && input.event == BSP_BTN_LONG) {
                /* Pause first, then save: the displayed saved count remains final. */
                state.automatic = false;
                save_state(true);
            } else if (input.button == BSP_BTN_DOWN && input.event == BSP_BTN_LONG) {
                muyu_next_volume(&state);
            } else if (input.event == BSP_BTN_CLICK || input.event == BSP_BTN_DOUBLE) {
                if (input.button == BSP_BTN_UP) muyu_toggle_auto(&state, now);
                if (input.button == BSP_BTN_DOWN) muyu_next_speed(&state, now);
            }
        }
        if (atomic_load(&stopping)) break;
        uint32_t now = now_ms();
        uint32_t before = state.total;
        if (muyu_tick(&state, now)) hit_feedback(state.total != before);
        if (muyu_save_due(&state, now)) save_state(false);
        if (!badge_power_screen_off() && (uint32_t)(now - last_battery) >= 30000) {
            if (!battery_ok) battery_ok = bsp_battery_init() == ESP_OK;
            int next_battery = battery_ok ? bsp_battery_soc() : -1;
            if (next_battery >= 0) battery = next_battery;
            last_battery = now;
        }
        if (!badge_power_screen_off() && (uint32_t)(now - last_ui) >= 80 && bsp_lvgl_lock(30)) {
            muyu_ui_refresh(&state, battery, atomic_load(&audio_ready), storage_ok, buttons_ok);
            bsp_lvgl_unlock();
            last_ui = now;
        }
    }
    xSemaphoreGive(app_done);
    vTaskDelete(NULL);
}

void demo_muyu_enter(void)
{
    muyu_ui_create();
    muyu_ui_notice("正在启动 请稍候");
}

void demo_muyu_exit(void)
{
    /* The menu calls stop successfully before acquiring the lock and deleting UI. */
    muyu_ui_destroy();
}

esp_err_t demo_muyu_start(void)
{
    if (app_started || audio_started) return ESP_ERR_INVALID_STATE;
    atomic_store(&stopping, false);
    if (!state_loaded) {
        load_state();
        state_loaded = true;
    }
    state.automatic = false;
    state.last_save_ms = now_ms();
    battery_ok = bsp_battery_init() == ESP_OK;
    battery = battery_ok ? bsp_battery_soc() : -1;
    buttons_ok = true; /* The menu enables this entry only when its input works. */
    input_queue = xQueueCreate(16, sizeof(input_t));
    app_done = xSemaphoreCreateBinary();
    if (!input_queue || !app_done) goto fail;
    prepare_audio();
    if (xTaskCreate(worker, "muyu_app", 4096, NULL, 4, NULL) != pdPASS) {
        goto fail;
    }
    app_started = true;
    ESP_LOGI(TAG, "Page ready: audio=%d storage=%d heap=%lu", atomic_load(&audio_ready),
        storage_ok, (unsigned long)esp_get_free_heap_size());
    return ESP_OK;
fail:
    {
        ESP_LOGE(TAG, "Application worker allocation failed");
        esp_err_t cleanup = demo_muyu_stop();
        if (bsp_lvgl_lock(100)) {
            muyu_ui_notice("启动失败 请重启");
            bsp_lvgl_unlock();
        }
        return cleanup == ESP_OK ? ESP_ERR_NO_MEM : cleanup;
    }
}

esp_err_t demo_muyu_stop(void)
{
    /* A timeout leaves all resources and UI intact. Another long press retries. */
    atomic_store(&stopping, true);
    if (app_started) {
        if (xSemaphoreTake(app_done, pdMS_TO_TICKS(2000)) != pdTRUE) return ESP_ERR_TIMEOUT;
        app_started = false;
    }
    if (audio_started) {
        if (xSemaphoreTake(audio_done, pdMS_TO_TICKS(2000)) != pdTRUE) return ESP_ERR_TIMEOUT;
        audio_started = false;
    }
    /* Also handles partial startup before the audio worker acquired ownership. */
    (void)bsp_audio_sleep();
    atomic_store(&audio_ready, false);
    state.automatic = false;
    free(tone);
    tone = NULL;
    /* NVS writes now happen after PCM has drained, without holding the LVGL lock. */
    if (state_loaded && state.dirty) save_state(false);
    /* Retain a failed save in RAM across visits; keep the one NVS handle until reboot. */
    if (input_queue) { vQueueDelete(input_queue); input_queue = NULL; }
    if (sound_queue) { vQueueDelete(sound_queue); sound_queue = NULL; }
    if (app_done) { vSemaphoreDelete(app_done); app_done = NULL; }
    if (audio_done) { vSemaphoreDelete(audio_done); audio_done = NULL; }
    ESP_LOGI(TAG, "Page stopped; saved=%d heap=%lu", !state.dirty,
        (unsigned long)esp_get_free_heap_size());
    return ESP_OK;
}
