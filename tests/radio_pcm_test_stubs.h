#pragma once
#include <cstddef>
#include <cstdint>
#define ESP_OK 0
#define ESP_FAIL -1
#define pdPASS 1
int bsp_audio_set_format(uint32_t rate,uint8_t bits,uint8_t channels);
void bsp_audio_set_volume(uint8_t volume);
int bsp_audio_write(const void *pcm,std::size_t bytes);
int64_t esp_timer_get_time(void);
uint32_t esp_get_free_heap_size(void);
unsigned uxTaskGetStackHighWaterMark(void *task);
int xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle);
void vTaskDelay(unsigned ticks);
void vTaskDelete(void *task);
