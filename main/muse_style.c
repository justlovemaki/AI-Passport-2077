#include "muse_style.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdatomic.h>

static atomic_uint current_style;
static bool initialized;
static QueueHandle_t style_queue;
static TaskHandle_t style_task;
static atomic_bool style_stopped = true;

enum { MUSE_STYLE_STOP = 0xff };

static bool persist_style(unsigned style) {
    nvs_handle_t n;
    if (nvs_open("muse_ui", NVS_READWRITE, &n) != ESP_OK) return false;
    esp_err_t e = nvs_set_u8(n, "style", (uint8_t)style);
    if (e == ESP_OK) e = nvs_commit(n);
    nvs_close(n);
    return e == ESP_OK;
}

void muse_style_init(void) {
    if (initialized) return;
    uint8_t saved = MUSE_STYLE_CUTE;
    nvs_handle_t n;
    if (nvs_open("muse_ui", NVS_READONLY, &n) == ESP_OK) {
        if (nvs_get_u8(n, "style", &saved) != ESP_OK || saved >= MUSE_STYLE_COUNT)
            saved = MUSE_STYLE_CUTE;
        nvs_close(n);
    }
    atomic_store(&current_style, saved);
    initialized = true;
}
unsigned muse_style_get(void) {
    muse_style_init();
    return atomic_load(&current_style);
}
bool muse_style_set(unsigned style) {
    muse_style_init();
    if (style >= MUSE_STYLE_COUNT || !persist_style(style)) return false;
    atomic_store(&current_style, style);
    return true;
}

static void style_worker(void *arg) {
    (void)arg;
    unsigned style;
    while (xQueueReceive(style_queue, &style, portMAX_DELAY) == pdTRUE) {
        if (style == MUSE_STYLE_STOP) break;
        if (style < MUSE_STYLE_COUNT) (void)persist_style(style);
    }
    atomic_store(&style_stopped, true);
    /* The lifecycle owner deletes this task synchronously. Self-deletion would
     * defer its 3 KiB stack to Idle and fragment an immediate XiaoZhi launch. */
    for (;;) vTaskSuspend(NULL);
}

bool muse_style_worker_start(void) {
    muse_style_init();
    if (style_task && style_queue) return true;
    style_queue = xQueueCreate(1, sizeof(unsigned));
    if (!style_queue) return false;
    TaskHandle_t task = NULL;
    atomic_store(&style_stopped, false);
    if (xTaskCreate(style_worker, "muse_style", 3072, NULL, 2, &task) != pdPASS) {
        atomic_store(&style_stopped, true);
        vQueueDelete(style_queue); style_queue = NULL; return false;
    }
    style_task = task;
    return true;
}
void muse_style_worker_stop(void) {
    if (!style_queue) return;
    while (uxQueueMessagesWaiting(style_queue)) vTaskDelay(pdMS_TO_TICKS(10));
    unsigned stop = MUSE_STYLE_STOP;
    xQueueSend(style_queue, &stop, portMAX_DELAY);
    while (!atomic_load(&style_stopped)) vTaskDelay(pdMS_TO_TICKS(10));
    if (style_task) { vTaskDelete(style_task); style_task = NULL; }
    vQueueDelete(style_queue); style_queue = NULL;
}
unsigned muse_style_request_next(void) {
    muse_style_init();
    unsigned next = (atomic_load(&current_style) + 1) % MUSE_STYLE_COUNT;
    atomic_store(&current_style, next);
    if (style_queue) xQueueOverwrite(style_queue, &next);
    return next;
}
