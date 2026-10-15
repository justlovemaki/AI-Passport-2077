/* Adapted from LEO Radio (MIT); assets/radio/LICENSE.txt. */
#include "radio_player.h"

extern "C" {
#include "bsp_audio.h"
#include "badge_network.h"
#include "badge_power.h"
#include "bsp_battery.h"
}
#include "radio_catalog.h"
#include "radio_directory.h"
#include "radio_controls.h"
#include "radio_logic.h"
#include "radio_stream_buffer.h"
#include "radio_diagnostics.h"
#include "radio_pcm_output.h"
#include "radio_buffer_budget.h"
#include "radio_probe.h"
#include "lwip/sockets.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "radio_http.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "radio_ui.h"

#include "decoder/impl/esp_mp3_dec.h"
#include "simple_dec/esp_audio_simple_dec.h"


#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace {

constexpr char kTag[] = "radio_player";
constexpr std::size_t kInputSize = radio_buffers::kDecoderInputBytes;
// A 32 KiB reserve left only 4-8 KiB after lazy decoder/sink allocation.
// Keep network receive headroom instead of filling all remaining internal RAM.
constexpr std::size_t kReadAheadSize = radio_buffers::kCompressedBytes;
constexpr std::size_t kPrebufferSize = radio_buffers::kStartupBytes;
constexpr std::size_t kOutputSize = radio_buffers::kDecoderOutputBytes;
constexpr std::size_t kLevelCount = 18;
constexpr std::size_t kMaxStations = RadioDirectory::kLocalCapacity;
// Consecutive silent failures tolerated before moving to the next station.
constexpr uint8_t kMaxFailedAttempts = 3;

std::atomic<bool> s_network_connected{false};
std::atomic<bool> s_wanted_playing{true};
std::atomic<std::size_t> s_station_index{0};
std::atomic<std::size_t> s_station_count{0};
std::atomic<uint8_t> s_volume{55};
std::atomic<uint32_t> s_generation{1};
std::atomic<bool> s_stream_active{false};
std::atomic<bool> s_service_ready{false},s_receive_probe_running{false};
std::atomic<unsigned> s_read_bytes{radio_buffers::kDefaultReadBytes};
static portMUX_TYPE s_probe_mux=portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_probe_generation;
static std::size_t s_probe_station;
static void cancel_probe_request(){portENTER_CRITICAL(&s_probe_mux);radio_probe_cancel();portEXIT_CRITICAL(&s_probe_mux);}
static void finish_probe_request(){portENTER_CRITICAL(&s_probe_mux);radio_probe_finish();portEXIT_CRITICAL(&s_probe_mux);}
TaskHandle_t s_player_task;
SemaphoreHandle_t s_station_mutex;
SemaphoreHandle_t s_done;
std::atomic<bool> s_stopping{false},s_dirty{false},s_station_dirty{false};
std::atomic<unsigned> s_city{0};
std::atomic<uint32_t> s_catalog_request{1},s_deadline{0};
static uint32_t now_seconds(){return static_cast<uint32_t>(esp_timer_get_time()/1000000);}
static uint32_t s_last_status;
static bool s_audio_ready;
static bool s_battery_ready;
static int s_last_battery=-1;
static void refresh_battery(){
    if(!s_battery_ready)s_battery_ready=bsp_battery_init()==ESP_OK;
    const int next=s_battery_ready?bsp_battery_soc():-1;
    if(next>=0)s_last_battery=next;
    radio_ui_set_battery(s_last_battery);
}
static void flush_settings();
RadioDirectory s_directory;

RadioStation station_snapshot(std::size_t index) {
    RadioStation station = {};
    if (!s_station_mutex ||
        xSemaphoreTake(s_station_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return station;
    }
    const std::size_t count = std::max<std::size_t>(s_station_count.load(), 1);
    station = s_directory.at(index % count);
    xSemaphoreGive(s_station_mutex);
    return station;
}

void save_setting(const char *key, uint8_t value) {
    nvs_handle_t handle;
    if (nvs_open("badge_radio_v1", NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_u8(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
}

uint8_t load_setting(const char *key, uint8_t fallback) {
    nvs_handle_t handle;
    if (nvs_open("badge_radio_v1", NVS_READONLY, &handle) != ESP_OK) return fallback;
    uint8_t value = fallback;
    if (nvs_get_u8(handle, key, &value) != ESP_OK) value = fallback;
    nvs_close(handle);
    return value;
}

void save_station_name(const char *name) {
    nvs_handle_t handle;
    if (nvs_open("badge_radio_v1", NVS_READWRITE, &handle) != ESP_OK) return;
    if (name && name[0]) {
        nvs_set_str(handle, "station_name", name);
    } else {
        nvs_erase_key(handle, "station_name");
    }
    nvs_commit(handle);
    nvs_close(handle);
}

// Copies the station name the listener last chose. Returns false when no
// station has been remembered yet.
bool load_station_name(char *name, std::size_t capacity) {
    if (!name || capacity == 0) return false;
    name[0] = '\0';
    nvs_handle_t handle;
    if (nvs_open("badge_radio_v1", NVS_READONLY, &handle) != ESP_OK) return false;
    std::size_t length = capacity;
    const esp_err_t error = nvs_get_str(handle, "station_name", name, &length);
    nvs_close(handle);
    if (error != ESP_OK) {
        name[0] = '\0';
        return false;
    }
    return name[0] != '\0';
}

// Installs a catalog under the station mutex and reports the index that should
// stay selected. `preferred_name` keeps the previous station when the fresh
// catalog still carries it.
bool install_stations(const RadioStation *stations, std::size_t count,
                      const char *preferred_name, std::size_t *selected) {
    if ((!stations && count) || !s_station_mutex) return false;
    count = std::min(count, kMaxStations);

    // Stop the current request before replacing its backing catalog. The
    // streaming task uses a value snapshot, so the swap is atomic to users.
    s_generation.fetch_add(1, std::memory_order_acq_rel);
    if (xSemaphoreTake(s_station_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    s_directory.replace(stations, count);
    count = s_directory.count();
    std::size_t index = 0;
    if (preferred_name && preferred_name[0]) {
        for (std::size_t i = 0; i < count; ++i) {
            if (std::strcmp(s_directory.at(i).name, preferred_name) == 0) {
                index = i;
                break;
            }
        }
    }
    s_station_count.store(count, std::memory_order_release);
    s_station_index.store(index, std::memory_order_release);
    xSemaphoreGive(s_station_mutex);
    if (selected) *selected = index;
    return true;
}

bool request_still_current(uint32_t generation, std::size_t station) {
    if(s_deadline.load() && radio_timer_expired(s_deadline.load(),now_seconds())) {
        s_deadline.store(0);s_wanted_playing.store(false);s_receive_probe_running.store(false);radio_ui_set_playback(RadioPlaybackState::Stopped,"定时结束");
    }
    return !s_stopping.load() && s_network_connected.load(std::memory_order_acquire) &&
           (s_wanted_playing.load(std::memory_order_acquire)||s_receive_probe_running.load()) &&
           s_generation.load(std::memory_order_acquire) == generation &&
           s_station_index.load(std::memory_order_acquire) == station;
}

void calculate_levels(const uint8_t *pcm, std::size_t bytes, uint8_t channels) {
    if (!pcm || bytes < 64) return;
    const auto *samples = reinterpret_cast<const int16_t *>(pcm);
    const std::size_t sample_count = bytes / sizeof(int16_t);
    if (sample_count < kLevelCount) return;
    const std::size_t frames = sample_count / std::max<uint8_t>(channels, 1);
    if (frames < kLevelCount) return;

    uint8_t levels[kLevelCount] = {};
    for (std::size_t band = 0; band < kLevelCount; ++band) {
        const std::size_t begin = band * frames / kLevelCount;
        const std::size_t end = (band + 1) * frames / kLevelCount;
        int64_t energy = 0;
        std::size_t count = 0;
        for (std::size_t frame = begin; frame < end; ++frame) {
            int32_t mixed = 0;
            for (uint8_t channel = 0; channel < channels; ++channel) {
                mixed += samples[frame * channels + channel];
            }
            mixed /= std::max<uint8_t>(channels, 1);
            energy += static_cast<int64_t>(mixed) * mixed;
            ++count;
        }
        const float rms = count ? std::sqrt(static_cast<float>(energy) / count) : 0.0f;
        float db = rms > 1.0f ? 20.0f * std::log10(rms / 32768.0f) : -80.0f;
        int value = static_cast<int>((db + 58.0f) * 2.25f);

        levels[band] = static_cast<uint8_t>(std::clamp(value, 2, 100));
    }
    radio_ui_set_audio_levels(levels, kLevelCount);
}


esp_audio_simple_dec_handle_t open_mp3_decoder() {
    esp_audio_simple_dec_cfg_t config = {
        .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
        .dec_cfg = nullptr,
        .cfg_size = 0,
        .use_frame_dec = false,
    };
    esp_audio_simple_dec_handle_t decoder = nullptr;
    const esp_audio_err_t error = esp_audio_simple_dec_open(&config, &decoder);
    if (error != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(kTag, "MP3 decoder open failed: %d", error);
        return nullptr;
    }
    return decoder;
}

void capture_peer(esp_http_client_handle_t client){
    const int fd=esp_http_client_get_socket(client);if(fd<0)return;
    struct sockaddr_storage address{};socklen_t size=sizeof(address);
    if(getpeername(fd,reinterpret_cast<struct sockaddr *>(&address),&size)!=0)return;
    if(address.ss_family==AF_INET){
        radio_diagnostics_set(RADIO_DIAG_PEER_FAMILY,4);
        radio_diagnostics_set(RADIO_DIAG_PEER_IP4,ntohl(reinterpret_cast<struct sockaddr_in *>(&address)->sin_addr.s_addr));
    }else if(address.ss_family==AF_INET6)radio_diagnostics_set(RADIO_DIAG_PEER_FAMILY,6);
    radio_diagnostics_set(RADIO_DIAG_TLS,esp_http_client_get_transport_type(client)==HTTP_TRANSPORT_OVER_SSL);
}
bool socket_ready(esp_http_client_handle_t client){
    const int fd=esp_http_client_get_socket(client);if(fd<0||fd>=FD_SETSIZE)return false;
    fd_set reads;FD_ZERO(&reads);FD_SET(fd,&reads);struct timeval timeout{};
    const int ready=select(fd+1,&reads,nullptr,nullptr,&timeout);
    if(ready<0)return false;
    radio_diagnostics_add(RADIO_DIAG_SOCKET_READY_CHECKS,1);return ready>0;
}

/* Only this short-lived reader accesses HTTP while the owner decodes/enqueues.
 * Compressed read-ahead absorbs network jitter without a large PCM allocation.
 * The owner joins it before closing HTTP, freeing buffers or releasing the UI. */
struct StreamReader {
    esp_http_client_handle_t client;
    std::size_t station;
    uint32_t generation;
    unsigned read_bytes=radio_buffers::kDefaultReadBytes;
    RadioStreamBuffer<kReadAheadSize> bytes;
    std::atomic<bool> stop{false},done{false},drain_only{false};
};
void stream_reader_task(void *arg) {
    auto *reader=static_cast<StreamReader *>(arg);
    uint8_t block[radio_buffers::kMaxReadBytes];
    int empty_reads=0;
    int64_t last_data=esp_timer_get_time();
    while(!reader->stop.load()&&request_still_current(reader->generation,reader->station)) {
        if(now_seconds()!=s_last_status){
            s_last_status=now_seconds();badge_network_status_t net;badge_network_status(&net);
            radio_player_set_network(net.connected);radio_ui_set_network(net.connected);
            refresh_battery();
            radio_diagnostics_min(RADIO_DIAG_MIN_FREE_HEAP,esp_get_free_heap_size());
        }
        const int64_t read_start=esp_timer_get_time();
        const int received=esp_http_client_read(reader->client,reinterpret_cast<char *>(block),reader->read_bytes);
        if(reader->stop.load()||!request_still_current(reader->generation,reader->station))break;
        radio_diagnostics_max(RADIO_DIAG_MAX_HTTP_US,static_cast<uint32_t>(std::min<int64_t>(esp_timer_get_time()-read_start,UINT32_MAX)));
        if(received==-ESP_ERR_HTTP_EAGAIN){
            radio_diagnostics_add(RADIO_DIAG_HTTP_TIMEOUTS,1);
            if(socket_ready(reader->client))radio_diagnostics_add(RADIO_DIAG_TIMEOUT_SOCKET_READY,1);
            if(esp_timer_get_time()-last_data>8000000){radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_HTTP_IDLE_ERROR);break;}
            vTaskDelay(1);continue;
        }
        if(received<0){radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_HTTP_READ_ERROR);ESP_LOGW(kTag,"Stream read error");break;}
        if(!received){if(++empty_reads>3)break;vTaskDelay(pdMS_TO_TICKS(20));continue;}
        radio_diagnostics_add(RADIO_DIAG_HTTP_READS,1);radio_diagnostics_add(RADIO_DIAG_HTTP_BYTES,received);
        empty_reads=0;last_data=esp_timer_get_time();
        if(reader->drain_only.load()){radio_diagnostics_add(RADIO_DIAG_PROBE_DRAINED,received);continue;}
        std::size_t sent=0;int64_t wait_started=0;
        while(sent<static_cast<std::size_t>(received)&&!reader->stop.load()&&
              request_still_current(reader->generation,reader->station)){
            const std::size_t count=reader->bytes.push(block+sent,received-sent);
            sent+=count;
            if(!count){
                if(!wait_started)wait_started=esp_timer_get_time();
                radio_diagnostics_set(RADIO_DIAG_WAITING_FOR_SPACE,1);vTaskDelay(pdMS_TO_TICKS(5));
            }
        }
        radio_diagnostics_set(RADIO_DIAG_WAITING_FOR_SPACE,0);
        if(wait_started)radio_diagnostics_max(RADIO_DIAG_MAX_SPACE_WAIT_US,static_cast<uint32_t>(std::min<int64_t>(esp_timer_get_time()-wait_started,UINT32_MAX)));
    }
    /* No reader/context access after publishing completion. The owner can
     * safely release the context even before the idle task reaps this stack. */
    reader->done.store(true,std::memory_order_release);
    vTaskDelete(nullptr);
}

struct StreamIdentity {std::size_t station;uint32_t generation;};
bool stream_current(void *context){const auto *s=static_cast<StreamIdentity *>(context);return request_still_current(s->generation,s->station);}
uint8_t stream_volume(void *){return s_volume.load(std::memory_order_acquire);}
void stream_buffering(bool buffering,void *context){
    if(stream_current(context))radio_ui_set_playback(buffering?RadioPlaybackState::Buffering:RadioPlaybackState::Playing,buffering?"正在缓冲":"正在播放");
}

bool stream_station(std::size_t station,uint32_t generation,unsigned read_bytes,unsigned receive_probe_seconds=0) {
    radio_diagnostics_begin();
    radio_diagnostics_set(RADIO_DIAG_PROBE_MODE,receive_probe_seconds?1:0);
    radio_diagnostics_set(RADIO_DIAG_READ_BYTES,read_bytes);
    const RadioStation preset = station_snapshot(station);
    if (!preset.url[0]) {
        radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_HTTP_OPEN_ERROR);radio_diagnostics_set(RADIO_DIAG_ACTIVE,0);
        ESP_LOGE(kTag, "Station %u has no stream URL", static_cast<unsigned>(station));
        return false;
    }
    radio_ui_set_playback(RadioPlaybackState::Connecting, "正在连接电台");

    esp_http_client_config_t config = {};
    config.url = preset.url;
    config.timeout_ms = 2500;
    config.buffer_size = 2048;
    config.buffer_size_tx = 512;
    config.user_agent = "AI-Passport-Radio/1.1.1";
    config.keep_alive_enable = true;
    config.disable_auto_redirect = false;
    config.max_redirection_count = 4;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_MEMORY_ERROR);radio_diagnostics_set(RADIO_DIAG_ACTIVE,0);
        ESP_LOGE(kTag, "HTTP client allocation failed");
        return false;
    }
    esp_http_client_set_header(client, "Icy-MetaData", "0");
    esp_http_client_set_header(client, "Accept", "audio/mpeg,*/*");

    bool played_audio = false;
    const int64_t first_audio_deadline=esp_timer_get_time()+15000000;
    esp_audio_simple_dec_handle_t decoder = nullptr;
    uint8_t *input = nullptr;
    uint8_t *output = nullptr;
    StreamReader *reader = nullptr;
    RadioPcmOutput *pcm_output=nullptr;
    StreamIdentity identity{station,generation};
    bool drain_output=false;

    do {
        if (!radio_http_open(client, 4)) {
            radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_HTTP_OPEN_ERROR);
            ESP_LOGW(kTag, "Open failed: %s", preset.url);
            break;
        }

        const int status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_HTTP_OPEN_ERROR);
            ESP_LOGW(kTag, "HTTP status %d", status);
            break;
        }

        if(!request_still_current(generation,station))break;
        capture_peer(client);
        esp_http_client_set_timeout_ms(client,100);
        decoder = open_mp3_decoder();
        if (!decoder){radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_DECODE_ERROR);break;}
        input = static_cast<uint8_t *>(malloc(kInputSize));
        output = static_cast<uint8_t *>(malloc(kOutputSize));
        if (!input || !output) {
            radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_MEMORY_ERROR);
            ESP_LOGE(kTag, "Not enough memory for stream buffers");
            break;
        }
        reader=new (std::nothrow) StreamReader;
        if(!reader){radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_MEMORY_ERROR);ESP_LOGE(kTag,"Read-ahead buffer allocation failed");break;}
        reader->client=client;reader->station=station;reader->generation=generation;
        reader->read_bytes=radio_probe_valid_read_size(read_bytes)?read_bytes:radio_buffers::kDefaultReadBytes;
        if(xTaskCreate(stream_reader_task,"radio_http",6144,reader,4,nullptr)!=pdPASS){
            radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_MEMORY_ERROR);
            ESP_LOGE(kTag,"HTTP reader task allocation failed");
            delete reader;reader=nullptr;break;
        }

        radio_ui_set_playback(RadioPlaybackState::Buffering, "正在缓冲");
        ESP_LOGI(kTag,"Decoder buffers ready; free=%lu largest=%u",(unsigned long)esp_get_free_heap_size(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        bool format_ready = false;
        bool decode_failed = false;
        uint8_t source_channels = 1;
        int64_t last_levels=0,probe_until=0;
        bool probe_primed=false;unsigned probe_warm_frames=0;
        RadioDecoderInput pending(input,kInputSize);
        RadioPrebuffer prebuffer(kPrebufferSize,esp_timer_get_time(),radio_buffers::kStartupWaitUs);
        while (!decode_failed && request_still_current(generation, station)) {
            if(radio_pcm_output_failed(pcm_output)){decode_failed=true;break;}
            played_audio=played_audio||radio_pcm_output_played(pcm_output);
            const std::size_t queued=reader->bytes.available();
            radio_diagnostics_set(RADIO_DIAG_QUEUED_BYTES,queued);
            if(probe_primed){
                if(esp_timer_get_time()>=probe_until)break;
                const std::size_t count=reader->bytes.discard(kReadAheadSize);
                if(count)radio_diagnostics_add(RADIO_DIAG_PROBE_DRAINED,count);
                else {if(reader->done.load(std::memory_order_acquire)&&!reader->bytes.available())break;vTaskDelay(1);}
                continue;
            }
            if(!played_audio&&esp_timer_get_time()>first_audio_deadline){radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_START_TIMEOUT);break;}
            if(prebuffer.waiting(queued,reader->done.load(std::memory_order_acquire),esp_timer_get_time())){
                vTaskDelay(1);continue;
            }
            const std::size_t received=reader->bytes.pop(pending.tail(),pending.space());
            if(!received){
                if(reader->done.load(std::memory_order_acquire)){
                    /* Completion can race the pop: drain its final publication. */
                    if(reader->bytes.available())continue;
                    break;
                }
                // The independent sink drains PCM reserve and owns underrun
                // reporting. Resume decode on arrival, without a full refill.
                vTaskDelay(1);continue;
            }
            pending.append(received);
            esp_audio_simple_dec_raw_t raw = {
                .buffer = pending.data(),
                .len = static_cast<uint32_t>(pending.size()),
                .eos = false,
                .consumed = 0,
                .frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE,
            };
            while (raw.len > 0 && request_still_current(generation, station)) {
                esp_audio_simple_dec_out_t frame = {
                    .buffer = output,
                    .len = kOutputSize,
                    .needed_size = 0,
                    .decoded_size = 0,
                };
                const int64_t decode_start=esp_timer_get_time();
                const esp_audio_err_t result =
                    esp_audio_simple_dec_process(decoder, &raw, &frame);
                radio_diagnostics_max(RADIO_DIAG_MAX_DECODE_US,static_cast<uint32_t>(std::min<int64_t>(esp_timer_get_time()-decode_start,UINT32_MAX)));
                if (result == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                    radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_DECODE_ERROR);
                    ESP_LOGE(kTag, "Decoder needs %u bytes, buffer has %u",
                             static_cast<unsigned>(frame.needed_size),
                             static_cast<unsigned>(kOutputSize));
                    raw.len = 0;
                    decode_failed = true;
                    break;
                }
                if (result != ESP_AUDIO_ERR_OK) {
                    radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_DECODE_ERROR);
                    ESP_LOGW(kTag, "MP3 decode failed: %d", result);
                    raw.len = 0;
                    decode_failed = true;
                    break;
                }
                if(raw.consumed>raw.len||frame.decoded_size>kOutputSize){radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_DECODE_ERROR);decode_failed=true;break;}
                raw.buffer += raw.consumed;
                raw.len -= raw.consumed;

                if (frame.decoded_size > 0) {
                    esp_audio_simple_dec_info_t info = {};
                    if (!format_ready) {
                        if(esp_audio_simple_dec_get_info(decoder,&info)!=ESP_AUDIO_ERR_OK){radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_FORMAT_ERROR);decode_failed=true;break;}
                        source_channels = std::clamp<uint8_t>(info.channel, 1, 2);
                        if (info.channel<1 || info.channel>2 || info.bits_per_sample != 16 || info.sample_rate < 8000 ||
                            info.sample_rate > 48000) {
                            radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_FORMAT_ERROR);
                            ESP_LOGE(kTag, "Unsupported stream format: %luHz/%ubit/%uch",
                                     static_cast<unsigned long>(info.sample_rate),
                                     info.bits_per_sample, source_channels);
                            raw.len = 0;
                            decode_failed = true;
                            break;
                        }
                        radio_diagnostics_set(RADIO_DIAG_SAMPLE_RATE,info.sample_rate);radio_diagnostics_set(RADIO_DIAG_CHANNELS,source_channels);
                        pcm_output=radio_pcm_output_open({info.sample_rate,stream_current,stream_volume,stream_buffering,&identity});
                        if(!pcm_output){decode_failed=true;break;}
                        format_ready = true;
                        ESP_LOGI(kTag, "%s %s at %luHz/%uch -> mono",receive_probe_seconds?"Preparing receive probe":"Playing",preset.name,
                                 static_cast<unsigned long>(info.sample_rate), source_channels);
                    }
                    if(receive_probe_seconds&&format_ready){
                        // Prime the same lazy MP3 core, codec, PCM queue and sink
                        // stack, then retain them without further decoding/output.
                        // The control therefore does not gain a large heap reserve.
                        if(++probe_warm_frames<8)continue;
                        probe_primed=true;probe_until=esp_timer_get_time()+static_cast<int64_t>(receive_probe_seconds)*1000000;
                        radio_diagnostics_set(RADIO_DIAG_PROBE_MODE,2);reader->drain_only.store(true);
                        raw.len=0;radio_ui_set_playback(RadioPlaybackState::Connecting,"RX TEST");break;
                    }
                    if (format_ready && frame.decoded_size<=kOutputSize) {
                        const std::size_t mono_bytes =
                            radio_downmix(reinterpret_cast<int16_t *>(frame.buffer), frame.decoded_size, source_channels);
                        if(!mono_bytes){radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_OUTPUT_ERROR);decode_failed=true;break;}
                        if(!radio_pcm_output_write(pcm_output,frame.buffer,mono_bytes)){decode_failed=true;break;}
                        // The long-running stream does not return to the outer
                        // power tick. Keep real playback from being timed as idle.
                        badge_power_audio_activity();
                        played_audio=played_audio||radio_pcm_output_played(pcm_output);
                        if(esp_timer_get_time()-last_levels>=100000){calculate_levels(frame.buffer,mono_bytes,1);last_levels=esp_timer_get_time();}
                    }
                }

                /* Cached output is progress even if no new input was consumed.
                 * Only a call with neither output nor consumption needs more data. */
                if(!raw.consumed&&!frame.decoded_size)break;
            }
            if(!pending.consume(pending.size()-raw.len)||!pending.space()){
                radio_diagnostics_set(RADIO_DIAG_ERROR,RADIO_DIAG_INPUT_STALL);
                ESP_LOGW(kTag,"MP3 parser made no progress within input limit");decode_failed=true;
            }
        }
        drain_output=!decode_failed&&request_still_current(generation,station)&&reader->done.load(std::memory_order_acquire)&&!reader->bytes.available();
    } while (false);

    if(reader)reader->stop.store(true);
    played_audio=radio_pcm_output_close(pcm_output,drain_output)||played_audio;
    if(reader){
        while(!reader->done.load(std::memory_order_acquire))vTaskDelay(pdMS_TO_TICKS(5));
        delete reader;
    }
    if (decoder) esp_audio_simple_dec_close(decoder);
    radio_diagnostics_set(RADIO_DIAG_QUEUED_BYTES,0);
    radio_diag_snapshot_t stats;radio_diagnostics_get(&stats);
    ESP_LOGI(kTag,"Stream ended; rebuffer events=%lu max PCM feed gap=%lu us min heap=%lu writer stack=%lu",
             (unsigned long)stats.values[RADIO_DIAG_REBUFFERS],(unsigned long)stats.values[RADIO_DIAG_MAX_FEED_GAP_US],
             (unsigned long)stats.values[RADIO_DIAG_MIN_FREE_HEAP],(unsigned long)stats.values[RADIO_DIAG_WRITER_STACK_FREE]);
    radio_ui_set_audio_levels(nullptr,0);
    free(output);
    free(input);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    radio_diagnostics_set(RADIO_DIAG_ACTIVE,0);
    return played_audio;
}

void player_task(void *) {
    badge_power_enter();
    std::size_t last_attempted_station = SIZE_MAX;
    uint8_t failed_attempts = 0;
    uint32_t catalog_seen=0;
    while (!s_stopping.load()) {
        badge_power_audio_tick(false);
        badge_network_status_t net;badge_network_status(&net);radio_player_set_network(net.connected);
        radio_ui_set_network(net.connected);if(!badge_power_screen_off())refresh_battery();
        if(s_dirty.load())flush_settings();
        if(s_deadline.load() && radio_timer_expired(s_deadline.load(),now_seconds())){s_deadline.store(0);s_wanted_playing.store(false);cancel_probe_request();}
        const uint32_t requested=s_catalog_request.load();
        if(net.connected && catalog_seen!=requested){
            catalog_seen=requested;radio_ui_set_playback(RadioPlaybackState::Connecting,"正在查找城市电台");
            const unsigned city=s_city.load();RadioStation stations[kMaxStations]={};RadioLocation location={};
            radio_catalog_reset_cancel();
            if(s_stopping.load())break;
            const size_t count=city?radio_catalog_discover_city(RADIO_CITIES[city],stations,kMaxStations,&location):radio_catalog_discover(stations,kMaxStations,&location);
            if(s_stopping.load())break;
            if(requested!=s_catalog_request.load())continue;
            if(count){radio_player_replace_stations_resuming(stations,count);radio_ui_set_location("本地 / 精选电台");}
            else {radio_player_replace_stations_resuming(nullptr,0);radio_ui_set_location("精选电台");radio_ui_set_playback(RadioPlaybackState::Connecting,"使用精选电台");}
        }
        if (!s_network_connected.load(std::memory_order_acquire)) {
            cancel_probe_request();
            radio_ui_set_playback(RadioPlaybackState::Stopped, "等待网络");
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        radio_probe_request_t probe{};std::size_t selected=0;uint32_t probe_generation=0;
        portENTER_CRITICAL(&s_probe_mux);
        const bool probe_pending=radio_probe_take(&probe);
        if(probe_pending){selected=s_probe_station;probe_generation=s_probe_generation;}
        portEXIT_CRITICAL(&s_probe_mux);
        if(probe_pending){
            const uint32_t generation=probe_generation;
            if(!s_wanted_playing.load()&&!s_stopping.load()&&generation==s_generation.load()&&selected==s_station_index.load()){
                s_receive_probe_running.store(true);
                if(generation==s_generation.load()&&!s_wanted_playing.load()){
                    s_stream_active.store(true);badge_power_audio_tick(true);badge_network_xiaozhi_power(true,true);
                    (void)stream_station(selected,generation,probe.read_bytes,probe.seconds);
                    badge_network_xiaozhi_power(false,false);s_stream_active.store(false);
                }
                s_receive_probe_running.store(false);
            }
            finish_probe_request();
            if(!s_stopping.load()&&!s_wanted_playing.load())radio_ui_set_playback(RadioPlaybackState::Stopped,"已暂停");
            continue;
        }
        if (!s_wanted_playing.load(std::memory_order_acquire)) {
            radio_ui_set_playback(RadioPlaybackState::Stopped, "已暂停");
            vTaskDelay(pdMS_TO_TICKS(150));
            continue;
        }

        const std::size_t station = s_station_index.load(std::memory_order_acquire);
        const uint32_t generation = s_generation.load(std::memory_order_acquire);
        if (station != last_attempted_station) {
            last_attempted_station = station;
            failed_attempts = 0;
        }
        s_stream_active.store(true, std::memory_order_release);
        badge_power_audio_tick(true);
        // Modem sleep's DTIM batching can exceed the high-rate PCM reserve.
        badge_network_xiaozhi_power(true,true);
        const bool played = stream_station(station,generation,s_read_bytes.load());
        badge_network_xiaozhi_power(false,false);
        s_stream_active.store(false, std::memory_order_release);
        if (!request_still_current(generation, station)) continue;

        if (played) {
            // The stream worked at least briefly, so this station is alive and
            // simply dropped. Keep it and reconnect.
            failed_attempts = 0;
            radio_ui_set_playback(RadioPlaybackState::Reconnecting,
                                  "信号中断，正在重连");
            for(int i=0;i<12&&!s_stopping.load()&&request_still_current(generation,station);i++)vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        ++failed_attempts;
        const std::size_t count = radio_player_station_count();
        // Public directories keep dead entries around. After a few silent
        // failures, move on instead of retrying the same address forever.
        if (failed_attempts >= kMaxFailedAttempts && count > 1) {
            ESP_LOGW(kTag, "Station %u unreachable, skipping to the next one",
                     static_cast<unsigned>(station));
            radio_ui_set_playback(RadioPlaybackState::Error,
                                  "电台无法播放 已换台");
            for(int i=0;i<12&&!s_stopping.load()&&request_still_current(generation,station);i++)vTaskDelay(pdMS_TO_TICKS(100));
            failed_attempts = 0;
            radio_player_select_relative(1);
            continue;
        }

        radio_ui_set_playback(RadioPlaybackState::Error, "连接失败，正在重试");
        for(int i=0;i<25&&!s_stopping.load()&&request_still_current(generation,station);i++)vTaskDelay(pdMS_TO_TICKS(100));
    }
    flush_settings();s_stream_active.store(false);s_receive_probe_running.store(false);cancel_probe_request();
    badge_power_leave();
    ESP_LOGI(kTag,"Stopped; free heap=%lu stack remaining=%u",(unsigned long)esp_get_free_heap_size(),(unsigned)uxTaskGetStackHighWaterMark(nullptr));
    xSemaphoreGive(s_done);vTaskDelete(nullptr);
}

void flush_settings() {
    // Stream playback never calls this: cache-disabled NVS commits interrupt
    // Flash-resident I2S/decoder code even when HTTP has been decoupled.
    if(!s_dirty.exchange(false))return;
    save_setting("volume",s_volume.load());save_setting("city",s_city.load());
    save_setting("station",static_cast<uint8_t>(s_station_index.load()));
    if(s_station_dirty.exchange(false)){const RadioStation chosen=station_snapshot(s_station_index.load());save_station_name(chosen.name);}
}
}  // namespace

bool radio_player_init(int preset) {
    if(s_player_task)return true;
    s_service_ready.store(false);s_receive_probe_running.store(false);s_read_bytes.store(radio_buffers::kDefaultReadBytes);cancel_probe_request();
    s_station_mutex=xSemaphoreCreateMutex();s_done=xSemaphoreCreateBinary();
    if(!s_station_mutex||!s_done)goto fail;
    s_directory.replace(nullptr,0);
    s_station_count.store(s_directory.count());
    s_station_index.store(load_setting("station",0)%s_station_count.load());
    s_volume.store(std::min<uint8_t>(load_setting("volume",40),100));
    s_city.store(load_setting("city",0)%8);s_stopping.store(false);s_wanted_playing.store(true);
    s_network_connected.store(false);s_deadline.store(0);s_catalog_request.store(1);s_dirty.store(false);s_station_dirty.store(false);
    if(preset>=0&&static_cast<size_t>(preset)<RADIO_PRESET_COUNT){
        for(size_t i=0;i<s_directory.count();i++)if(!strcmp(s_directory.at(i).url,RADIO_PRESETS[preset].url)){s_station_index.store(i);break;}
        s_catalog_request.store(0); // No geolocation delay before a specifically requested preset.
        s_dirty.store(true);s_station_dirty.store(true);
    }
    s_audio_ready=bsp_audio_init()==ESP_OK;
    if(!s_audio_ready||esp_mp3_dec_register()!=ESP_AUDIO_ERR_OK)goto fail;
    radio_ui_set_station(s_station_index.load(),s_station_count.load(),station_snapshot(s_station_index.load()));
    radio_ui_set_volume(s_volume.load());
    // Decode outranks LVGL (4); the independent bounded PCM sink runs at 6.
    if(xTaskCreate(player_task,"badge_radio",10240,nullptr,5,&s_player_task)==pdPASS){s_service_ready.store(true);return true;}
fail:
    (void)bsp_audio_sleep();
    esp_audio_dec_unregister(ESP_AUDIO_TYPE_MP3);
    if(s_station_mutex)vSemaphoreDelete(s_station_mutex);
    if(s_done)vSemaphoreDelete(s_done);
    s_station_mutex=nullptr;s_done=nullptr;s_player_task=nullptr;
    radio_ui_set_playback(RadioPlaybackState::Error,"电台启动失败");return false;
}

bool radio_player_shutdown(uint32_t timeout_ms) {
    s_service_ready.store(false);s_read_bytes.store(radio_buffers::kDefaultReadBytes);cancel_probe_request();
    if(!s_player_task)return true;
    s_stopping.store(true);s_wanted_playing.store(false);s_generation.fetch_add(1);radio_catalog_cancel();
    radio_ui_set_playback(RadioPlaybackState::Stopped,"正在停止");
    if(xSemaphoreTake(s_done,pdMS_TO_TICKS(timeout_ms))!=pdTRUE)return false;
    s_player_task=nullptr;esp_audio_dec_unregister(ESP_AUDIO_TYPE_MP3);
    vSemaphoreDelete(s_station_mutex);s_station_mutex=nullptr;vSemaphoreDelete(s_done);s_done=nullptr;
    s_deadline.store(0);return true;
}
extern "C" bool radio_player_probe_available(void){
    return s_service_ready.load()&&s_network_connected.load()&&!s_stopping.load()&&!s_wanted_playing.load()&&!s_stream_active.load()&&!radio_probe_busy();
}
extern "C" bool radio_player_receive_probe(unsigned seconds,unsigned read_bytes){
    portENTER_CRITICAL(&s_probe_mux);
    const bool accepted=radio_probe_reserve(s_service_ready.load()&&s_network_connected.load()&&!s_stopping.load(),!s_wanted_playing.load(),s_stream_active.load(),seconds,read_bytes);
    if(accepted){s_probe_generation=s_generation.load();s_probe_station=s_station_index.load();}
    portEXIT_CRITICAL(&s_probe_mux);return accepted;
}
extern "C" bool radio_player_set_read_size(unsigned read_bytes){
    if(!radio_player_probe_available()||!radio_probe_valid_read_size(read_bytes))return false;
    s_read_bytes.store(read_bytes);return true;
}
extern "C" unsigned radio_player_next_read_size(void){return s_read_bytes.load();}
void radio_player_set_city(unsigned city){s_city.store(city%8);s_dirty.store(true);s_catalog_request.fetch_add(1);s_generation.fetch_add(1);radio_catalog_cancel();}
unsigned radio_player_city(){return s_city.load();}
void radio_player_set_timer(unsigned minutes){s_deadline.store(minutes?now_seconds()+minutes*60:0);}
unsigned radio_player_timer_seconds(){const uint32_t d=s_deadline.load(),now=now_seconds();return d>now?d-now:0;}

void radio_player_set_network(bool connected) {
    if(s_network_connected.exchange(connected)!=connected)s_generation.fetch_add(1);
}

void radio_player_toggle() {
    radio_player_set_playing(!radio_player_is_playing());
}

void radio_player_set_playing(bool playing) {
    s_wanted_playing.store(playing, std::memory_order_release);
    s_generation.fetch_add(1, std::memory_order_acq_rel);
    if (!playing) radio_ui_set_playback(RadioPlaybackState::Stopped, "已暂停");
}

bool radio_player_is_playing() {
    return s_wanted_playing.load(std::memory_order_acquire);
}

bool radio_player_wait_idle(uint32_t timeout_ms) {
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (s_stream_active.load(std::memory_order_acquire)) {
        if (static_cast<int32_t>(deadline - xTaskGetTickCount()) <= 0) return false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
}

void radio_player_select_relative(int delta) {
    const int count = static_cast<int>(radio_player_station_count());
    const int current = static_cast<int>(s_station_index.load());
    const std::size_t next = static_cast<std::size_t>((current + delta + count) % count);
    radio_player_set_station(next);
}

void radio_player_set_station(std::size_t index) {
    index %= radio_player_station_count();
    s_station_index.store(index, std::memory_order_release);
    s_generation.fetch_add(1, std::memory_order_acq_rel);
    s_dirty.store(true);
    s_station_dirty.store(true);
    radio_ui_set_station(index, radio_player_station_count(), radio_player_station(index));
    if (radio_player_is_playing()) {
        radio_ui_set_playback(RadioPlaybackState::Connecting, "正在切换电台");
    }
}

std::size_t radio_player_station_index() {
    return s_station_index.load(std::memory_order_acquire);
}

std::size_t radio_player_station_count() {
    return std::max<std::size_t>(s_station_count.load(std::memory_order_acquire), 1);
}

RadioStation radio_player_station(std::size_t index) {
    return station_snapshot(index);
}

bool radio_player_replace_stations(const RadioStation *stations, std::size_t count) {
    std::size_t selected = 0;
    if (!install_stations(stations, count, nullptr, &selected)) return false;

    s_dirty.store(true);
    const RadioStation chosen = radio_player_station(selected);
    s_station_dirty.store(true);
    radio_ui_set_station(selected, radio_player_station_count(), chosen);
    ESP_LOGI(kTag, "Installed combined catalog with %u stations",
             static_cast<unsigned>(radio_player_station_count()));
    return true;
}

bool radio_player_replace_stations_resuming(const RadioStation *stations,
                                           std::size_t count) {
    char remembered[sizeof(RadioStation::name)] = {};
    const bool has_remembered = load_station_name(remembered, sizeof(remembered));

    std::size_t selected = 0;
    if (!install_stations(stations, count, has_remembered ? remembered : nullptr,
                          &selected)) {
        return false;
    }

    s_dirty.store(true);
    const RadioStation chosen = radio_player_station(selected);
    s_station_dirty.store(true);
    radio_ui_set_station(selected, radio_player_station_count(), chosen);
    if (has_remembered && selected > 0) {
        ESP_LOGI(kTag, "Resumed remembered station '%s' at index %u",
                 chosen.name, static_cast<unsigned>(selected));
    } else {
        ESP_LOGI(kTag, "Installed combined catalog with %u stations",
                 static_cast<unsigned>(radio_player_station_count()));
    }
    return true;
}

void radio_player_set_volume(uint8_t volume) {
    volume = std::min<uint8_t>(volume, 100);
    s_volume.store(volume, std::memory_order_release);
    s_dirty.store(true);
    radio_ui_set_volume(volume);
}

uint8_t radio_player_volume() {
    return s_volume.load(std::memory_order_acquire);
}
