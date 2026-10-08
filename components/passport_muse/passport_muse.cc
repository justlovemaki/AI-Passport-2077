// Passport OS adapter for Meta's community Muse Gadget SDK. No account secrets
// are compiled in. All chat-turn calls belong to this module's single worker.
#include "passport_muse.h"
extern "C" {
#include "config_store.h"
#include "ble_server.h"
#include "link_pairing.h"
#include "vm_api.h"
#include "muse_chat.h"
#include "muse_chat_priv.h"
#include "muse_link.h"
#include "muse_wifi.h"
#include "http_proxy.h"
}
#include "noise_control.h"
extern "C" {
#include "bsp_audio.h"
}
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
passport_muse_snapshot_t s_view{};
std::atomic<bool> s_active{false}, s_exit{false}, s_pressed{false}, s_cancel{false};
std::atomic<bool> s_confirm{false}, s_pair_done{false}, s_refresh{false};
std::atomic<bool> s_repair{false};
std::atomic<bool> s_paired{false};
std::atomic<uint32_t> s_pairing_prompt_generation{0};
std::atomic<unsigned> s_callbacks{0};
std::atomic<bool> s_worker_running{false}, s_worker_exited{true};
std::atomic<TaskHandle_t> s_worker_task{nullptr};
char s_sdk_token[64], s_node[64], s_mac[18], s_device_id[64], s_ble_name[32];
bool s_initialized;
passport_muse_host_ops_t s_host{};

bool network_status(char *ssid = nullptr, size_t cap = 0, int *rssi = nullptr, bool *secure = nullptr) {
    if (ssid && cap) ssid[0] = 0;
    if (rssi) *rssi = -127;
    if (secure) *secure = false;
    return s_host.network_status && s_host.network_status(ssid, cap, rssi, secure);
}
void network_performance(bool active) {
    if (s_host.network_performance) s_host.network_performance(active);
}
bool valid_sdk_token(const char *token) {
    if (!token || strncmp(token, "mgst_", 5) || strlen(token) < 12 || strlen(token) >= sizeof(s_sdk_token)) return false;
    for (const char *p = token; *p; ++p) if (*p <= 32 || *p >= 127) return false;
    return true;
}
void log_nvs_stats(const char *where) {
    nvs_stats_t stats{};
    esp_err_t result = nvs_get_stats(nullptr, &stats);
    ESP_LOGI("muse_pair", "nvs %s result=%s used=%u free=%u namespaces=%u",
             where, esp_err_to_name(result), (unsigned)stats.used_entries,
             (unsigned)stats.free_entries, (unsigned)stats.namespace_count);
}

void view(passport_muse_stage_t stage, const char *detail) {
    portENTER_CRITICAL(&s_lock);
    s_view.stage = stage;
    strlcpy(s_view.detail, detail, sizeof(s_view.detail));
    portEXIT_CRITICAL(&s_lock);
}
const char *voice_error_detail(const char *reason) {
    if (strstr(reason, "OUT OF MEMORY")) return "录音内存不足，松开上键后重试";
    if (strstr(reason, "CAN'T KEEP UP")) return "语音上传中断，检查网络后重试";
    if (strstr(reason, "SYNC INTERRUPTED")) return "回复同步中断，按确定键重连";
    if (strstr(reason, "NO REPLY")) return "回复超时，可在手机 Muse 查看";
    if (strstr(reason, "REPLY TOO LONG") || strstr(reason, "BUFFER LIMIT")) return "回复较长，请在手机 Muse 查看";
    if (strstr(reason, "AUTH") || strstr(reason, "DENIED")) return "Muse 授权异常，按确定键重连";
    return "Muse 连接中断，检查网络后重试";
}
void pairing_view(passport_muse_stage_t stage, const char *detail, uint32_t generation) {
    portENTER_CRITICAL(&s_lock);
    s_pairing_prompt_generation = generation;
    s_view.stage = stage;
    strlcpy(s_view.detail, detail, sizeof(s_view.detail));
    portEXIT_CRITICAL(&s_lock);
}
void recover_pairing_prompt(uint32_t generation) {
    portENTER_CRITICAL(&s_lock);
    if (s_pairing_prompt_generation == generation &&
        (s_view.stage == PASSPORT_MUSE_CONFIRM || s_view.stage == PASSPORT_MUSE_PHONE_SETUP)) {
        s_pairing_prompt_generation = 0;
        s_view.stage = PASSPORT_MUSE_PAIRING;
        strlcpy(s_view.detail, "手机 Muse / 设置 / 设备", sizeof(s_view.detail));
    }
    portEXIT_CRITICAL(&s_lock);
}
void answer(const char *text) {
    portENTER_CRITICAL(&s_lock);
    if (text[0] && s_view.reply_is_answer) { portEXIT_CRITICAL(&s_lock); return; }
    // SDK emits valid UTF-8 captions; do not clip halfway through a character.
    muse_hatch_tail_words(text, s_view.reply, sizeof(s_view.reply));
    s_view.reply_revision++;
    s_view.reply_is_answer = false;
    portEXIT_CRITICAL(&s_lock);
}
void reply_answer() {
    portENTER_CRITICAL(&s_lock);
    if (muse_hatch_turn_reply(s_view.reply, sizeof(s_view.reply))) {
        s_view.reply_revision++; s_view.reply_is_answer = true;
    }
    portEXIT_CRITICAL(&s_lock);
}
struct view_brief_t {
    passport_muse_stage_t stage;
    bool reply_is_answer, sdk_token_set;
    uint32_t reply_revision;
    size_t reply_bytes;
};
view_brief_t view_brief() {
    view_brief_t out{};
    portENTER_CRITICAL(&s_lock);
    out.stage = s_view.stage;
    out.reply_is_answer = s_view.reply_is_answer;
    out.sdk_token_set = s_view.sdk_token_set;
    out.reply_revision = s_view.reply_revision;
    out.reply_bytes = strnlen(s_view.reply, sizeof(s_view.reply));
    portEXIT_CRITICAL(&s_lock);
    return out;
}
void status(const char *state) {
    if (s_exit) return;
    if (!strcmp(state, "ws_connected")) {
        if (view_brief().reply_is_answer) view(PASSPORT_MUSE_REPLY, "继续同步后续回复");
        else view(PASSPORT_MUSE_READY, "按住上键说话");
    }
    else if (!strcmp(state, "ws_auth_failed") || !strcmp(state, "ws_refresh_needed")) s_refresh = true;
    else if (!strcmp(state, "ws_disconnected")) view(PASSPORT_MUSE_CONNECTING, "正在重新连接");
    else if (!strcmp(state, "ws_unpaired")) {
        config_clear_pairing();
        s_paired = false;
        s_exit = true;
        view(PASSPORT_MUSE_ERROR, "请在 Muse 中重新配对");
    }
}
void agent_name(const char *name) {
    portENTER_CRITICAL(&s_lock);
    muse_hatch_tail_words(name, s_view.name, sizeof(s_view.name));
    portEXIT_CRITICAL(&s_lock);
}
cJSON *command(const char *, cJSON *, const char *, noise_ctrl_session_generation_t) {
    cJSON *out = cJSON_CreateObject();
    if (out) cJSON_AddStringToObject(out, "error", "Passport OS does not execute remote device commands");
    return out;
}
void send_json(cJSON *obj, uint32_t generation) {
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (json) { ble_server_send_encrypted_json(json, generation); free(json); }
}
void scan() {
    // Reuse the shell's Wi-Fi owner. Muse pairing may select only the network
    // that is already active; it never starts a second radio manager.
    cJSON *obj = cJSON_CreateObject(), *arr = cJSON_CreateArray();
    if (!obj || !arr) { cJSON_Delete(obj); cJSON_Delete(arr); return; }
    cJSON_AddStringToObject(obj, "type", "wifi_scan_result");
    char ssid[33]; int rssi = -127; bool secure = false;
    if (network_status(ssid, sizeof(ssid), &rssi, &secure)) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", ssid);
        cJSON_AddNumberToObject(item, "rssi", rssi);
        cJSON_AddBoolToObject(item, "secure", secure);
        cJSON_AddItemToArray(arr, item);
    }
    cJSON_AddItemToObject(obj, "networks", arr);
    send_json(obj, link_pairing_session_generation());
}
void device_info() {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return;
    cJSON_AddStringToObject(obj, "type", "device_info");
    cJSON_AddStringToObject(obj, "node_id", s_node);
    cJSON_AddStringToObject(obj, "version", esp_app_get_description()->version);
    link_pairing_add_device_info(obj);
    // Muse asks for these public protocol capabilities BEFORE client_hello.
    // There is no encrypted session yet: using send_json() drops generation 0
    // and makes the phone time out without ever reaching physical confirmation.
    // Match the upstream discovery response; secrets/provisioning stay encrypted.
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (json) { ble_server_send_chunked(json); free(json); }
}
void finished(uint32_t generation) {
    if (s_exit || !link_pairing_session_is_current(generation)) return;
    if (ble_server_send_pairing_status("confirm_required", generation) &&
        link_pairing_arm_confirmation(generation)) pairing_view(PASSPORT_MUSE_CONFIRM, "按确定键允许配对", generation);
    else ble_server_disconnect_pairing_session(generation);
}
void provision(const char *ssid, const char *, const char *access, const char *refresh,
               const char *username, const char *, bool, const char *,
               const char *api, const char *host, uint32_t generation) {
    s_callbacks++;
    if (!s_exit && link_pairing_provisioning_session_valid(generation)) {
        pairing_view(PASSPORT_MUSE_PHONE_SETUP, "正在保存手机配置", generation);
        char current_ssid[33];
        bool connected = network_status(current_ssid, sizeof(current_ssid));
        bool ssid_match = connected && ssid && !strcmp(current_ssid, ssid);
        ESP_LOGI("muse_pair", "provision generation=%lu connected=%d ssid=%d access=%d refresh=%d",
                 (unsigned long)generation, connected, ssid_match,
                 access && *access, refresh && *refresh);
        if (!access || !*access || !refresh || !*refresh) {
            ESP_LOGE("muse_pair", "provision missing account credentials");
            ble_server_send_pairing_status("error_missing_credentials", generation);
            view(PASSPORT_MUSE_ERROR, "配对资料不完整，请在手机重试");
        } else if (!connected) {
            ESP_LOGE("muse_pair", "provision rejected: shared Wi-Fi is offline");
            ble_server_send_pairing_status("wifi_failed", generation);
            view(PASSPORT_MUSE_ERROR, "工牌 Wi-Fi 已断开，请重新连接");
        } else {
            /* The shell owns Wi-Fi. The phone can only select the one network
             * returned by scan(); retain that live connection even if a phone
             * normalizes the SSID differently. */
            ble_server_send_pairing_status("wifi_connected", generation);
            log_nvs_stats("before");
            bool ok = config_clear_setup_complete();
            if (ok) ok = config_set_str("access_token", access);
            if (ok) ok = config_set_str("refresh_token", refresh);
            if (ok) ok = config_set_str("username", username ? username : "");
            if (ok) ok = config_set_str("api_url_v2", api ? api : "");
            if (ok) ok = config_set_str("noise_host", host ? host : "");
            log_nvs_stats(ok ? "stored" : "store_failed");
            // Pairing records are encrypted and physically confirmed. Commit
            // only if this exact session is still valid after the NVS writes.
            ok = ok && !s_exit && link_pairing_commit_provisioning(generation, config_mark_setup_complete);
            if (ok) {
                if (ble_server_send_pairing_status("auth_ok", generation)) {
                    ESP_LOGI("muse_pair", "provision committed generation=%lu", (unsigned long)generation);
                    s_paired = true;
                    s_pair_done = true;
                } else {
                    // Keep discovery alive when the final result was not sent.
                    config_clear_setup_complete();
                    view(PASSPORT_MUSE_ERROR, "配对结果未送达，请重新添加");
                }
            } else {
                ESP_LOGE("muse_pair", "provision storage/session commit failed");
                config_clear_pairing();
                log_nvs_stats("cleared_after_failure");
                ble_server_send_pairing_status("error_storage", generation);
                view(PASSPORT_MUSE_ERROR, "配对未保存，请重试");
            }
        }
    }
    s_callbacks--;
}
void unpair() {
    // Defer storage/connection mutation to the owner task.
    s_cancel = true;
    config_clear_pairing();
    s_paired = false;
    s_exit = true;
    view(PASSPORT_MUSE_ERROR, "已解除配对，重新进入可配对");
}
void setup_ble() {
    const ble_callbacks_t callbacks = {
        .on_provision = provision, .on_wifi_scan = scan, .on_ota = nullptr,
        .on_unpair = unpair, .on_get_device_info = device_info,
        .on_client_connected = nullptr, .on_client_disconnected = nullptr,
        .on_pairing_client_finished = finished,
    };
    link_pairing_init(s_node, s_device_id, s_mac, esp_app_get_description()->version, s_sdk_token);
    ble_server_start(s_ble_name, &callbacks);
    if (ble_server_is_started()) {
        ble_server_begin_advertising();
        view(PASSPORT_MUSE_PAIRING, "手机 Muse / 设置 / 设备");
    } else view(PASSPORT_MUSE_ERROR, "蓝牙启动失败，请返回重试");
}
void log_network() {
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    view_brief_t current = view_brief();
    ESP_LOGI("muse_net", "stage=%d reply_bytes=%u revision=%u cloud=%d", (int)current.stage,
             (unsigned)current.reply_bytes, (unsigned)current.reply_revision, noise_ctrl_is_connected());
    ESP_LOGI("muse_net", "wifi=%d paired=%d worker=%d ble=%d heap=%u largest=%u",
        muse_wifi_connected(), s_paired.load(), s_worker_running.load(), ble_server_is_started(),
        (unsigned)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    for (int i = ESP_NETIF_DNS_MAIN; netif && i <= ESP_NETIF_DNS_FALLBACK; ++i) {
        esp_netif_dns_info_t dns{};
        if (esp_netif_get_dns_info(netif, static_cast<esp_netif_dns_type_t>(i), &dns) == ESP_OK
            && dns.ip.type == ESP_IPADDR_TYPE_V4) {
            char address[16]; esp_ip4addr_ntoa(&dns.ip.u_addr.ip4, address, sizeof(address));
            // DNS addresses only; never log Wi-Fi passwords or account tokens.
            ESP_LOGI("muse_net", "dns%d=%s", i, address);
        }
    }
}
bool resolve_api(const char *endpoint) {
    const char *url = endpoint && *endpoint ? endpoint : VM_API_DEFAULT_BASE_URL;
    const char *begin = strstr(url, "://");
    if (!begin) { view(PASSPORT_MUSE_ERROR, "服务器配置无效，请重新配对"); return false; }
    begin += 3;
    size_t length = strcspn(begin, ":/?#");
    char hostname[128];
    if (!length || length >= sizeof(hostname)) {
        view(PASSPORT_MUSE_ERROR, "服务器配置无效，请重新配对"); return false;
    }
    memcpy(hostname, begin, length); hostname[length] = 0;
    log_network();
    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(hostname, nullptr, &hints, &result);
    if (result) freeaddrinfo(result);
    ESP_LOGI("muse_net", "api_dns_result=%d", rc);
    if (rc) view(PASSPORT_MUSE_ERROR, "服务器域名解析失败\n检查 Wi-Fi 网络后确定重试");
    return rc == 0;
}
bool connect_account(bool refresh_first) {
    if (ble_server_is_started()) {
        view(PASSPORT_MUSE_ERROR, "蓝牙未释放，请重启后连接");
        return false;
    }
    view(PASSPORT_MUSE_CONNECTING, "正在连接你的 Muse");
    if (!muse_wifi_connected()) { view(PASSPORT_MUSE_ERROR, "Wi-Fi 已断开，请先连接网络"); return false; }
    char endpoint[256], host[128];
    config_get_str("api_url_v2", endpoint, sizeof(endpoint));
    config_get_str("noise_host", host, sizeof(host));
    muse_proxy_load_config();
    // Fail at the actual DNS boundary before allocating credential/TLS buffers.
    if ((!muse_proxy_enabled() && !resolve_api(endpoint)) || s_exit) return false;
    if (muse_proxy_enabled()) log_network();
    char *access = static_cast<char *>(calloc(1, 4096));
    char *refresh = static_cast<char *>(calloc(1, 4096));
    if (!access || !refresh) {
        free(access); free(refresh);
        view(PASSPORT_MUSE_ERROR, "内存不足，请返回重试"); return false;
    }
    config_get_str("access_token", access, 4096);
    config_get_str("refresh_token", refresh, 4096);
    vm_api_set_base_url(endpoint);
    vm_api_set_sdk_token(s_sdk_token);
    noise_ctrl_set_host(host);
    vm_info_t vms[VM_API_MAX_VMS] = {};
    bool refresh_rejected = false, storage_failed = false;
    int count = refresh_first ? VM_API_ERR_AUTH : vm_api_fetch_vms(access, vms, VM_API_MAX_VMS);
    if (!s_exit && count == VM_API_ERR_AUTH) {
        vm_device_tokens_t tokens{};
        // The SDK's token endpoint calls this field device_id, but upstream
        // supplies the advertised node_id, not the pairing transcript device_id.
        int refreshed = vm_api_refresh_device_token(nullptr, refresh, s_node, &tokens, nullptr);
        refresh_rejected = refreshed == VM_API_ERR_AUTH;
        if (refreshed == 0) {
            if (config_set_str("access_token", tokens.access_token) && config_set_str("refresh_token", tokens.refresh_token)) {
                strlcpy(access, tokens.access_token, 4096);
                count = s_exit ? 0 : vm_api_fetch_vms(access, vms, VM_API_MAX_VMS);
            } else { storage_failed = true; count = VM_API_ERR_FAILED; }
        } else count = refreshed;
        vm_device_tokens_free(&tokens);
    }
    free(access); free(refresh);
    const vm_info_t *vm = count > 0 ? vm_find_default(vms, count) : nullptr;
    char ssid[33]; bool connected = network_status(ssid, sizeof(ssid));
    bool ok = !s_exit && connected && vm && noise_ctrl_connect(vm->vm_id, vm->vm_auth_token, ssid);
    vm_list_free(vms, count > 0 ? count : 0);
    if (!ok && !s_exit) view(PASSPORT_MUSE_ERROR, storage_failed ? "授权未保存，请确定重试" :
        refresh_rejected ? "账号授权失效，请重新配对" :
        count == VM_API_ERR_AUTH ? "云端暂未接受授权\n确定重试" :
        count == 0 ? "账号没有可用的 Muse，请查看手机" : "云端未连通\n请检查 Wi-Fi 的网络通道");
    return ok;
}
bool prepare_audio_resources() {
    esp_err_t e = bsp_audio_init();
    if (e == ESP_OK) e = bsp_audio_set_format(16000, 16, 1);
    esp_err_t sleep_result = bsp_audio_sleep();
    ESP_LOGI("muse_ptt", "audio reserved result=%s sleep=%s heap=%u largest=%u",
             esp_err_to_name(e), esp_err_to_name(sleep_result),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    if (e != ESP_OK || sleep_result != ESP_OK) {
        view(PASSPORT_MUSE_ERROR, "麦克风资源不足，请返回后重试");
        return false;
    }
    return true;
}
void worker(void *) {
    bool recording = false, previous_press = false, capture_wifi = false;
    int64_t began = 0;
    if (!s_paired) setup_ble();
    else if (prepare_audio_resources()) connect_account(false);
    while (!s_exit) {
        if (!s_paired && !s_callbacks && ble_server_is_started()) {
            passport_muse_stage_t current_stage = view_brief().stage;
            if (current_stage == PASSPORT_MUSE_CONFIRM || current_stage == PASSPORT_MUSE_PHONE_SETUP) {
                uint32_t generation = s_pairing_prompt_generation.load();
                if (!link_pairing_confirmation_required() && !link_pairing_session_confirmed()) {
                    // Keep the prompt's token: expiry can make the public
                    // active-session getter return zero. A new hello has its
                    // own generation and must never be closed by this prompt.
                    if (link_pairing_session_is_current(generation))
                        ble_server_disconnect_pairing_session(generation);
                    recover_pairing_prompt(generation);
                }
            }
            /* Preserve a concrete pairing error after disconnect. Returning to
             * generic "waiting" hid the failing stage from both user and logs. */
        }
        if (s_repair.exchange(false)) {
            s_pressed = false; previous_press = false;
            muse_hatch_turn_cancel();
            if (recording) { recording = false; (void)bsp_audio_sleep(); }
            noise_ctrl_disconnect();
            while (s_callbacks && !s_exit) vTaskDelay(pdMS_TO_TICKS(10));
            ble_server_full_shutdown(); link_pairing_reset();
            if (!s_exit && config_clear_pairing()) {
                s_paired = false; s_pair_done = false; s_refresh = false; s_confirm = false;
                portENTER_CRITICAL(&s_lock); s_view.paired = false; portEXIT_CRITICAL(&s_lock);
                setup_ble();
            } else if (!s_exit) view(PASSPORT_MUSE_ERROR, "配对记录未清除，请返回重试");
        }
        if (s_confirm.exchange(false)) {
            passport_muse_stage_t current_stage = view_brief().stage;
            uint32_t gen = link_pairing_confirm_active_session();
            if (gen) {
                if (ble_server_send_pairing_status("pairing_confirmed", gen))
                    pairing_view(PASSPORT_MUSE_PHONE_SETUP, "请在手机选择当前 Wi-Fi", gen);
                else {
                    ble_server_disconnect_pairing_session(gen);
                    view(PASSPORT_MUSE_ERROR, "确认未送达，请在手机重试");
                }
            }
            else if (s_paired && !recording && current_stage == PASSPORT_MUSE_ERROR) {
                muse_hatch_turn_cancel();
                noise_ctrl_disconnect();
                connect_account(false);
            }
        }
        if (s_pair_done.exchange(false)) {
            while (s_callbacks && !s_exit) vTaskDelay(pdMS_TO_TICKS(10));
            // Upstream keeps the GATT connection alive for 2 seconds after
            // auth_ok. Immediate teardown loses the phone's final setup result.
            int64_t deadline = esp_timer_get_time() + 2000000;
            while (!s_exit && esp_timer_get_time() < deadline) vTaskDelay(pdMS_TO_TICKS(20));
            ble_server_full_shutdown();
            portENTER_CRITICAL(&s_lock); s_view.paired = true; portEXIT_CRITICAL(&s_lock);
            if (!s_exit && prepare_audio_resources()) connect_account(false);
        }
        // Refresh only after a real server refusal/request. The SDK's device
        // access tokens are long-lived; timer rotation can replace a valid pair.
        if (!recording && s_paired && s_refresh.exchange(false)) {
            muse_hatch_turn_cancel();
            noise_ctrl_disconnect();
            if (!s_exit) connect_account(true);
        }
        bool pressed = s_pressed;
        if (s_cancel.exchange(false)) {
            pressed = false; s_pressed = false;
            muse_hatch_turn_cancel();
            if (recording) { recording = false; (void)bsp_audio_sleep(); }
            view(noise_ctrl_is_connected() ? PASSPORT_MUSE_READY : PASSPORT_MUSE_CONNECTING, "已停止等待，已发送任务请在手机查看");
        }
        if (pressed && !previous_press) {
            if (muse_hatch_ready()) {
                network_performance(true);
                capture_wifi = true;
            }
            if (muse_hatch_ready() && bsp_audio_init() == ESP_OK && bsp_audio_set_format(16000, 16, 1) == ESP_OK) {
                muse_hatch_turn_begin();
                recording = muse_hatch_turn_recording();
                if (recording) {
                    began = esp_timer_get_time();
                    answer(""); view(PASSPORT_MUSE_LISTENING, "松开上键发送");
                    ESP_LOGI("muse_ptt", "recording started heap=%u largest=%u",
                             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
                } else (void)bsp_audio_sleep();
            } else { (void)bsp_audio_sleep(); view(PASSPORT_MUSE_ERROR, "连接或麦克风未就绪"); }
        }
        if (recording && (!pressed || esp_timer_get_time() - began >= 30000000)) {
            recording = false; s_pressed = false; pressed = false;
            ESP_LOGI("muse_ptt", "recording stopped duration_ms=%lld", (long long)((esp_timer_get_time() - began) / 1000));
            (void)bsp_audio_sleep(); muse_hatch_turn_end();
            view(PASSPORT_MUSE_WORKING, "等待 Muse 回复");
        }
        previous_press = pressed;
        if (recording) {
            int16_t pcm[256];
            if (bsp_audio_read(pcm, sizeof(pcm)) != ESP_OK) {
                muse_hatch_turn_cancel(); (void)bsp_audio_sleep(); recording = false;
                view(PASSPORT_MUSE_ERROR, "录音失败，请重试");
            } else {
                unsigned peak = 0;
                for (int16_t sample : pcm) { unsigned n = sample < 0 ? -int(sample) : sample; if (n > peak) peak = n; }
                portENTER_CRITICAL(&s_lock); s_view.level = peak; portEXIT_CRITICAL(&s_lock);
                muse_hatch_turn_audio(pcm, 256);
            }
        }
        char text[160];
        muse_hatch_ev_t ev = muse_hatch_turn_event(text, sizeof(text));
        if (ev == MUSE_HATCH_EV_ERROR) {
            if (recording) (void)bsp_audio_sleep();
            recording = false; s_pressed = false; previous_press = false;
            view(PASSPORT_MUSE_ERROR, voice_error_detail(text));
        } else if (ev == MUSE_HATCH_EV_HEARD) {
            answer(text);
        } else if (ev == MUSE_HATCH_EV_REPLY) {
            reply_answer(); view(PASSPORT_MUSE_REPLY, "继续同步后续回复");
        } else if (ev == MUSE_HATCH_EV_SYNCING) {
            view(PASSPORT_MUSE_WORKING, "正在恢复回复同步");
        } else if (ev == MUSE_HATCH_EV_RESUMED || ev == MUSE_HATCH_EV_DONE) {
            view(view_brief().reply_is_answer ? PASSPORT_MUSE_REPLY : PASSPORT_MUSE_WORKING, "继续同步后续回复");
        } else if (ev == MUSE_HATCH_EV_SENT) {
            view(PASSPORT_MUSE_WORKING, "Muse 已收到语音");
        }
        if (!recording && capture_wifi) {
            network_performance(false);
            capture_wifi = false;
        }
        if (s_host.runtime_tick) {
            passport_muse_stage_t current_stage = view_brief().stage;
            bool busy = recording || current_stage == PASSPORT_MUSE_PAIRING ||
                current_stage == PASSPORT_MUSE_CONFIRM || current_stage == PASSPORT_MUSE_PHONE_SETUP ||
                current_stage == PASSPORT_MUSE_CONNECTING || current_stage == PASSPORT_MUSE_WORKING;
            s_host.runtime_tick(busy, recording);
        }
        if (!recording) vTaskDelay(pdMS_TO_TICKS(30));
    }
    muse_hatch_turn_cancel();
    if (recording) (void)bsp_audio_sleep();
    if (capture_wifi) network_performance(false);
    noise_ctrl_disconnect();
    while (s_callbacks) vTaskDelay(pdMS_TO_TICKS(10));
    ble_server_full_shutdown();
    link_pairing_reset();
    s_worker_running = false;
    s_worker_exited = true;
    /* passport_muse_stop() owns deletion so the 12 KiB stack is returned
     * before the launcher can allocate XiaoZhi's larger voice task. */
    for (;;) vTaskSuspend(nullptr);
}
} // namespace

extern "C" bool muse_wifi_connected(void) { return network_status(); }
extern "C" bool muse_link_hatch_linked(void) { return s_paired; }
extern "C" bool muse_link_req_ready(void) { return noise_ctrl_is_connected(); }
extern "C" int64_t muse_link_req_open(const char *v, const char *p, const char *const *h, bool e, muse_link_req_cb cb, void *ctx) {
    return noise_ctrl_req_open(v, p, h, e, cb, ctx);
}
extern "C" bool muse_link_req_send(int64_t id, const void *p, size_t n, bool end, int ms) { return noise_ctrl_req_send(id, p, n, end, ms); }
extern "C" void muse_link_req_cancel(int64_t id) { noise_ctrl_req_cancel(id); }

extern "C" esp_err_t passport_muse_set_host_ops(const passport_muse_host_ops_t *ops) {
    if (!ops || !ops->network_status) return ESP_ERR_INVALID_ARG;
    if (s_active.load()) return ESP_ERR_INVALID_STATE;
    s_host = *ops;
    return ESP_OK;
}
extern "C" esp_err_t passport_muse_save_config(const char *token, const char *host, uint16_t port) {
    if (!token || !host || (token[0] && !valid_sdk_token(token)) || !muse_proxy_valid_config(host, port))
        return ESP_ERR_INVALID_ARG;
    if (s_active.exchange(true)) return ESP_ERR_INVALID_STATE;
    struct ReleaseConfigGate { ~ReleaseConfigGate() { s_active = false; } } release;
    if (token[0]) {
        char old[64]; config_get_str("sdk_token", old, sizeof(old));
        if (strcmp(old, token) && !config_clear_pairing()) return ESP_FAIL;
        if (!config_set_str("sdk_token", token)) return ESP_FAIL;
    }
    return muse_proxy_save_config(host, port) ? ESP_OK : ESP_FAIL;
}
extern "C" esp_err_t passport_muse_set_sdk_token(const char *token) {
    if (!valid_sdk_token(token)) return ESP_ERR_INVALID_ARG;
    char host[16]; uint16_t port = 0; muse_proxy_get_config(host, sizeof(host), &port);
    return passport_muse_save_config(token, host, port);
}
extern "C" void passport_muse_config_status(bool *token, bool *paired) {
    char value[64];
    if (token) *token = config_get_str("sdk_token", value, sizeof(value)) && value[0];
    if (paired) *paired = config_is_provisioned();
}
extern "C" esp_err_t passport_muse_set_proxy(const char *host, uint16_t port) {
    if (s_active.exchange(true)) return ESP_ERR_INVALID_STATE;
    struct ReleaseConfigGate { ~ReleaseConfigGate() { s_active = false; } } release;
    if (!muse_proxy_valid_config(host, port)) return ESP_ERR_INVALID_ARG;
    return muse_proxy_save_config(host, port) ? ESP_OK : ESP_FAIL;
}
extern "C" bool passport_muse_proxy_valid(const char *host, uint16_t port) { return muse_proxy_valid_config(host, port); }
extern "C" void passport_muse_proxy_config(char *host, size_t cap, uint16_t *port) {
    muse_proxy_get_config(host, cap, port);
}
extern "C" esp_err_t passport_muse_start(void) {
    if (!s_host.network_status) return ESP_ERR_INVALID_STATE;
    if (s_active.exchange(true)) return ESP_ERR_INVALID_STATE;
    s_exit = false; s_pressed = false; s_cancel = false; s_confirm = false; s_pair_done = false; s_refresh = false; s_repair = false;
    s_pairing_prompt_generation = 0;
    bool token, paired; passport_muse_config_status(&token, &paired);
    s_paired = paired;
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_mac, sizeof(s_mac), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    snprintf(s_node, sizeof(s_node), "homelink-%02x%02x%02x", mac[3],mac[4],mac[5]);
    snprintf(s_device_id, sizeof(s_device_id), "hatch-link:%s", s_mac);
    snprintf(s_ble_name, sizeof(s_ble_name), "MuseGadget-Passport-%02X%02X%02X", mac[3],mac[4],mac[5]);
    portENTER_CRITICAL(&s_lock);
    s_view = {}; s_view.sdk_token_set = token; s_view.paired = paired;
    strlcpy(s_view.name, "Muse", sizeof(s_view.name)); strlcpy(s_view.device_name, s_ble_name, sizeof(s_view.device_name));
    portEXIT_CRITICAL(&s_lock);
    if (!token) { view(PASSPORT_MUSE_NEEDS_TOKEN, "手机设置页填写 SDK Token"); return ESP_OK; }
    if (!muse_wifi_connected()) { view(PASSPORT_MUSE_ERROR, "请先连接 Wi-Fi"); return ESP_OK; }
    config_get_str("sdk_token", s_sdk_token, sizeof(s_sdk_token));
    if (!noise_ctrl_initialized()) {
        noise_ctrl_init(s_node, "Passport OS", status);
        noise_ctrl_set_agent_name_cb(agent_name); noise_ctrl_set_command_cb(command);
    }
    // Muse's large bounded parser buffers belong to this app session instead
    // of permanently shrinking the heap available to Radio and Xiaozhi.
    muse_hatch_start();
    s_initialized = noise_ctrl_initialized() && muse_hatch_initialized();
    if (!s_initialized) { view(PASSPORT_MUSE_ERROR, "内存不足，请返回重试"); return ESP_ERR_NO_MEM; }
    TaskHandle_t task = nullptr;
    view(PASSPORT_MUSE_CONNECTING, "正在准备连接");
    s_worker_running = true;
    s_worker_exited = false;
    // Proxy TLS plus the account-response parser exceeds 8 KiB on ESP-IDF
    // 5.5.3 (observed stack-protection fault just below the task boundary).
    // Keep this extra budget local to Muse instead of enlarging other apps.
    if (xTaskCreate(worker, "passport_muse", 12288, nullptr, 4, &task) != pdPASS) {
        s_worker_running = false;
        s_worker_exited = true;
        view(PASSPORT_MUSE_ERROR, "内存不足，请返回重试"); return ESP_ERR_NO_MEM;
    }
    s_worker_task = task;
    return ESP_OK;
}
extern "C" bool passport_muse_stop(void) {
    ESP_LOGI("muse_lifecycle", "stop begin heap=%u largest=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    s_exit = true;
    muse_proxy_cancel();
    // The owner exits cooperatively; never kill a task holding codec/TLS locks.
    while (s_worker_running.load()) vTaskDelay(pdMS_TO_TICKS(10));
    while (!s_worker_exited.load()) vTaskDelay(pdMS_TO_TICKS(1));
    TaskHandle_t worker_task = s_worker_task.exchange(nullptr);
    if (worker_task) vTaskDelete(worker_task);
    // worker() has disconnected and joined Home Link, so callbacks can no
    // longer reference the per-session chat scratch area. Its task stack has
    // also been reclaimed synchronously for the next no-PSRAM application.
    muse_hatch_stop();
    s_initialized = false;
    s_active = false;
    ESP_LOGI("muse_lifecycle", "stop complete heap=%u largest=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return !ble_server_is_started();
}
extern "C" void passport_muse_key(bool pressed, bool cancel) { s_pressed = pressed; if (cancel) s_cancel = true; }
extern "C" void passport_muse_confirm(void) {
    if (s_active && !s_worker_running) { passport_muse_stop(); passport_muse_start(); }
    else s_confirm = true;
}
extern "C" void passport_muse_repair(void) {
    passport_muse_stage_t stage = view_brief().stage;
    if (!s_active || (stage != PASSPORT_MUSE_ERROR && stage != PASSPORT_MUSE_PAIRING)) return;
    if (s_worker_running) s_repair = true;
    else {
        passport_muse_stop();
        if (config_clear_pairing()) passport_muse_start();
        else view(PASSPORT_MUSE_ERROR, "配对记录未清除，请返回重试");
    }
}
extern "C" void passport_muse_log_diagnostics(void) { log_network(); }
extern "C" void passport_muse_snapshot(passport_muse_snapshot_t *out) {
    if (!out) return;
    portENTER_CRITICAL(&s_lock); *out = s_view; portEXIT_CRITICAL(&s_lock);
}
extern "C" bool passport_muse_reply_state(size_t at, uint32_t *revision,
                                            passport_muse_stage_t *stage) {
    bool available;
    portENTER_CRITICAL(&s_lock);
    if (revision) *revision = s_view.reply_revision;
    if (stage) *stage = s_view.stage;
    available = s_view.reply_is_answer && at < sizeof(s_view.reply) && s_view.reply[at];
    portEXIT_CRITICAL(&s_lock);
    return available;
}
extern "C" bool passport_muse_diagnostic_ble_cycle(void) {
    view_brief_t state = view_brief();
    if (!s_active || s_worker_running || state.sdk_token_set || state.stage != PASSPORT_MUSE_NEEDS_TOKEN) return false;
    unsigned before = esp_get_free_heap_size();
    link_pairing_init(s_node, s_device_id, s_mac, esp_app_get_description()->version, nullptr);
    ble_server_start(s_ble_name, nullptr);
    vTaskDelay(pdMS_TO_TICKS(300));
    bool started = ble_server_is_started();
    unsigned during = esp_get_free_heap_size();
    ble_server_full_shutdown(); link_pairing_reset();
    vTaskDelay(pdMS_TO_TICKS(100));
    bool stopped = !ble_server_is_started();
    ESP_LOGI("muse_probe", "BLE cycle: started=%d stopped=%d before=%u during=%u after=%u", started, stopped, before, during, (unsigned)esp_get_free_heap_size());
    return started && stopped;
}
