#include "muse_app.h"
#include "passport_muse.h"
#include "badge_power.h"
#include "muse_style.h"
extern "C" {
#include "badge_network.h"
#include "badge_theme.h"
#include "bsp_battery.h"
#include "xiaozhi_face_assets.h"
}
#include "xiaozhi_face_decoder.h"
#include "badge_header.h"
#include "badge_footer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <cstdio>
#include <cstring>

LV_FONT_DECLARE(font_xiaozhi_14);
LV_FONT_DECLARE(font_badge_28);

namespace {
lv_obj_t *screen, *name_label;
lv_obj_t *status_label, *detail_label, *reply_label, *footer;
lv_timer_t *timer;
uint32_t reply_revision, theme_revision, stage_at;
size_t page_at, next_page;
unsigned page_number = 1, page_count = 1;
unsigned face, eyes, mouth, face_style, face_variant;
bool page_dirty, power_owned, face_hidden;
passport_muse_stage_t stage;
passport_muse_snapshot_t ui_state;
char page_text[512];

lv_obj_t *label(const char *text, int x, int y, int w, int h,
                const lv_font_t *font, unsigned role) {
    lv_obj_t *o = lv_label_create(screen);
    lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(badge_theme_colors()[role]), 0);
    lv_label_set_long_mode(o, LV_LABEL_LONG_WRAP);
    lv_label_set_text(o, text);
    return o;
}
void visible(lv_obj_t *o, bool show) {
    if (show) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}
bool host_network(char *ssid, size_t cap, int *rssi, bool *secure) {
    badge_network_status_t state{}; badge_network_status(&state);
    if (ssid && cap) snprintf(ssid, cap, "%s", state.ssid);
    if (rssi) *rssi = -127;
    if (secure) *secure = false;
    if (!state.connected) return false;
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        if (rssi) *rssi = ap.rssi;
        if (secure) *secure = ap.authmode != WIFI_AUTH_OPEN;
    }
    return true;
}
void host_network_performance(bool active) {
    badge_network_xiaozhi_power(true, active);
}
void host_runtime_tick(bool busy, bool audio_busy) {
    badge_power_tick(busy, audio_busy);
}
const passport_muse_host_ops_t host_ops = {
    .network_status = host_network,
    .network_performance = host_network_performance,
    .runtime_tick = host_runtime_tick,
};

void face_part(lv_layer_t *layer, const xz_face_asset_t *asset, int x, int y,
               uint32_t color, lv_opa_t opacity = LV_OPA_COVER) {
    const lv_image_dsc_t *image = &asset->image; x += asset->x; y += asset->y;
    lv_area_t a = {x, y, x + image->header.w - 1, y + image->header.h - 1};
    if (layer->_clip_area.x2 < a.x1 || layer->_clip_area.x1 > a.x2 ||
        layer->_clip_area.y2 < a.y1 || layer->_clip_area.y1 > a.y2) return;
    lv_draw_image_dsc_t d; lv_draw_image_dsc_init(&d); d.src = image;
    d.recolor = lv_color_hex(color); d.recolor_opa = LV_OPA_COVER; d.opa = opacity;
    lv_draw_image(layer, &d, &a);
}
void face_artwork(lv_event_t *event) {
    if (face_hidden) return;
    lv_layer_t *layer = lv_event_get_layer(event); const uint32_t *c = badge_theme_colors();
    if (face_style == MUSE_STYLE_ROUND) {
        const xz_face_pose_t *p = xz_roadking_pose((xz_face_t)face, face_variant);
        lv_color_t bg = lv_color_hex(c[BADGE_BACKGROUND]), text = lv_color_hex(c[BADGE_TEXT]);
        lv_color_t accent = lv_color_hex(c[BADGE_ACCENT]);
        lv_color_t skin = lv_color_mix(lv_color_mix(text, accent, 205), bg, 218);
        int light = lv_color_brightness(skin), bg_light = lv_color_brightness(bg);
        int text_light = lv_color_brightness(text);
        int bg_delta = light > bg_light ? light - bg_light : bg_light - light;
        int text_delta = light > text_light ? light - text_light : text_light - light;
        lv_color_t ink = bg_delta > text_delta ? bg : text;
        uint32_t line_color = lv_color_to_u32(ink);
        face_part(layer, &xz_round_outline, 40, 62, line_color);
        face_part(layer, &xz_round_hair, 40, 62, lv_color_to_u32(lv_color_mix(ink, skin, 224)));
        face_part(layer, &xz_round_skin, 40, 62, lv_color_to_u32(skin));
        face_part(layer, &xz_round_nose, 40, 62, lv_color_to_u32(lv_color_mix(ink, skin, 150)));
        face_part(layer, &xz_round_shirt, 40, 62, lv_color_to_u32(lv_color_mix(accent, ink, 65)));
        face_part(layer, &xz_round_eye_left_frames[eyes], 64 + p->eye_x, 120, line_color);
        face_part(layer, &xz_round_eye_right_frames[eyes], 64 + p->eye_x, 120, line_color);
        face_part(layer, &xz_round_mouth_frames[mouth], 93 + p->mouth_x, 163,
                  lv_color_to_u32(lv_color_mix(ink, skin, 205)));
        return;
    }
    bool female = face_style == MUSE_STYLE_FEMALE;
    bool portrait = face_style == MUSE_STYLE_ABSTRACT || female;
    lv_color_t body = lv_color_mix(lv_color_hex(c[BADGE_TEXT]), lv_color_hex(c[BADGE_ACCENT]), female ? 242 : portrait ? 205 : 40);
    uint32_t ink = c[BADGE_BACKGROUND];
    const xz_face_pose_t *p = portrait ? xz_portrait_pose((xz_face_t)face, face_variant) : xz_face_pose((xz_face_t)face, face_variant);
    face_part(layer, female ? &xz_female_body : portrait ? &xz_abstract_body : &xz_face_body, 48, 65, lv_color_to_u32(body));
    if (portrait) face_part(layer, female ? &xz_female_detail : &xz_abstract_detail, 48, 65, ink);
    if (portrait && !female) face_part(layer, &xz_bald_gloss, 48, 65, c[BADGE_TEXT]);
    if (female) face_part(layer, &xz_female_accent, 48, 65, c[BADGE_ACCENT]);
    face_part(layer, portrait ? &xz_abstract_eye_frames[eyes] : &xz_face_eye_frames[eyes], 80 + p->eye_x + (portrait ? 4 : 0), 113, ink);
    face_part(layer, portrait ? &xz_abstract_mouth_frames[mouth] : &xz_face_mouth_frames[mouth], 96 + p->mouth_x + (portrait ? 4 : 0), 152, ink);
}
void update_face(const passport_muse_snapshot_t &state, uint32_t now, bool showing_reply) {
    bool hidden_changed = face_hidden != showing_reply;
    face_hidden = showing_reply;
    xz_face_t next = XZ_FACE_NEUTRAL;
    if (state.stage == PASSPORT_MUSE_ERROR) next = XZ_FACE_SAD;
    else if (state.stage == PASSPORT_MUSE_WORKING || state.stage == PASSPORT_MUSE_CONNECTING || state.stage == PASSPORT_MUSE_PHONE_SETUP) next = XZ_FACE_THINKING;
    else if (state.stage == PASSPORT_MUSE_REPLY) next = XZ_FACE_HAPPY;
    unsigned style = muse_style_get();
    unsigned variant = ((unsigned)state.stage + 1) % XZ_FACE_VARIANTS;
    const xz_face_pose_t *p = style == MUSE_STYLE_ROUND ? xz_roadking_pose(next, variant) :
        style == MUSE_STYLE_CUTE ? xz_face_pose(next, variant) : xz_portrait_pose(next, variant);
    bool blink = next == XZ_FACE_NEUTRAL && state.stage != PASSPORT_MUSE_LISTENING && now % 4400 < 120;
    unsigned next_eyes = style == MUSE_STYLE_ROUND ? xz_roadking_eyes(next, variant, blink) : blink ? (unsigned)XZ_FACE_BLINK : p->eyes;
    unsigned phase = state.stage == PASSPORT_MUSE_LISTENING ?
        (state.level < 900 ? 0 : state.level < 4000 ? 1 : state.level < 9000 ? 2 : 3) : 0;
    unsigned next_mouth = phase ? XZ_FACE_COUNT + phase - 1 : p->mouth;
    if (hidden_changed || face != (unsigned)next || eyes != next_eyes || mouth != next_mouth ||
        face_style != style || face_variant != variant) {
        face = next; eyes = next_eyes; mouth = next_mouth; face_style = style; face_variant = variant;
        lv_area_t area = {38, 58, 202, 206}; lv_obj_invalidate_area(screen, &area);
    }
}
void apply_theme(void) {
    uint32_t revision = badge_theme_revision();
    if (revision == theme_revision) return;
    const uint32_t *c = badge_theme_colors();
    lv_obj_set_style_bg_color(screen, lv_color_hex(c[BADGE_BACKGROUND]), 0);
    lv_obj_set_style_text_color(name_label, lv_color_hex(c[BADGE_ACCENT]), 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(c[BADGE_TEXT]), 0);
    lv_obj_set_style_text_color(detail_label, lv_color_hex(c[BADGE_MUTED]), 0);
    lv_obj_set_style_text_color(reply_label, lv_color_hex(c[BADGE_TEXT]), 0);
    lv_obj_set_style_text_color(footer, lv_color_hex(c[BADGE_MUTED]), 0);
    theme_revision = revision;
}
void refresh(lv_timer_t *) {
    if (!screen || badge_power_screen_off()) return;
    uint32_t now = lv_tick_get();
    passport_muse_snapshot(&ui_state); const passport_muse_snapshot_t &state = ui_state;
    bool changed = state.stage != stage;
    if (changed) { stage = state.stage; stage_at = now; }

    const char *status = "连接你的 Muse", *detail = state.detail, *action = BADGE_HINT_MUSE_SETUP;
    switch (state.stage) {
    case PASSPORT_MUSE_NEEDS_TOKEN: break;
    case PASSPORT_MUSE_PAIRING: status = "等待配对"; action = BADGE_HINT_MUSE_PAIR; break;
    case PASSPORT_MUSE_CONFIRM: status = "是你的手机吗"; action = BADGE_HINT_MUSE_PAIR; break;
    case PASSPORT_MUSE_PHONE_SETUP: status = "就快好了"; action = BADGE_HINT_MUSE_WAIT; break;
    case PASSPORT_MUSE_CONNECTING: status = "正在连接"; action = BADGE_HINT_MUSE_WAIT; break;
    case PASSPORT_MUSE_READY: status = "我准备好了"; action = BADGE_HINT_MUSE_READY; break;
    case PASSPORT_MUSE_LISTENING: status = "我在听"; action = BADGE_HINT_MUSE_LISTEN; break;
    case PASSPORT_MUSE_WORKING: status = "正在思考"; action = BADGE_HINT_MUSE_WORK; break;
    case PASSPORT_MUSE_REPLY: status = "收到回复"; action = BADGE_HINT_MUSE_REPLY; break;
    case PASSPORT_MUSE_ERROR: status = "连接遇到问题"; action = BADGE_HINT_MUSE_ERROR; break;
    }

    bool showing_reply = state.reply_is_answer && state.reply[0];
    if (showing_reply && (state.reply_revision != reply_revision || page_dirty)) {
        if (state.reply_revision != reply_revision) {
            reply_revision = state.reply_revision;
            page_at = 0; page_number = 1; page_count = 0;
            size_t at = 0, next;
            do {
                next = muse_hatch_reply_page(state.reply, at, page_text, sizeof(page_text));
                page_count++; if (next <= at) break; at = next;
            } while (state.reply[at]);
        }
        next_page = muse_hatch_reply_page(state.reply, page_at, page_text, sizeof(page_text));
        lv_label_set_text(reply_label, page_text); page_dirty = false;
    }
    char heading[64];
    if (showing_reply) {
        snprintf(heading, sizeof(heading), "%s  %u/%u",
                 state.stage == PASSPORT_MUSE_ERROR ? "同步中断" : "收到回复",
                 page_number, page_count);
        status = heading;
        action = state.stage == PASSPORT_MUSE_ERROR ? BADGE_HINT_MUSE_ERROR :
                 page_count > 1 ? BADGE_HINT_MUSE_PAGES : BADGE_HINT_MUSE_REPLY;
    }
    if (state.stage == PASSPORT_MUSE_PAIRING) {
        static char pairing[96]; const char *tail = strrchr(state.device_name, '-');
        snprintf(pairing, sizeof(pairing), "手机 Muse / 设置 / 设备\n选择尾号 %.16s",
                 tail ? tail + 1 : state.device_name); detail = pairing;
    }
    if (!showing_reply && (state.stage == PASSPORT_MUSE_LISTENING || state.stage == PASSPORT_MUSE_WORKING)) {
        static char elapsed[48]; unsigned seconds = (now - stage_at) / 1000;
        snprintf(elapsed, sizeof(elapsed), "%s %02u:%02u",
                 state.stage == PASSPORT_MUSE_LISTENING ? "录音" : "等待",
                 seconds / 60, seconds % 60); detail = elapsed;
    }
    lv_obj_set_y(status_label, showing_reply ? 66 : 217);
    if (!showing_reply) lv_obj_set_y(detail_label, 239);
    lv_label_set_text(status_label, status);
    lv_label_set_text(detail_label, detail ? detail : "");
    lv_label_set_text(footer, action);
    visible(reply_label, showing_reply); visible(detail_label, !showing_reply);
    lv_obj_set_style_text_color(status_label,
        lv_color_hex(badge_theme_colors()[state.stage == PASSPORT_MUSE_ERROR ? BADGE_ACCENT : BADGE_TEXT]), 0);

    update_face(state, now, showing_reply);
    int battery = bsp_battery_soc();
    int rssi = -127; bool online = host_network(nullptr, 0, &rssi, nullptr);
    badge_header_battery(battery);
    badge_header_network(online);
    badge_header_refresh();
    apply_theme();
}
}

extern "C" void demo_muse_enter(void) {
    const uint32_t *c = badge_theme_colors();
    muse_style_init(); face_decoder_start();
    screen = lv_obj_create(nullptr); lv_obj_remove_style_all(screen); lv_obj_set_size(screen, 240, 320);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(screen, lv_color_hex(c[BADGE_BACKGROUND]), 0);
    lv_obj_add_event_cb(screen, face_artwork, LV_EVENT_DRAW_MAIN, nullptr);
    badge_header_attach(screen);
    name_label = label("MUSE", 14, 34, 212, 32, &font_badge_28, BADGE_ACCENT);
    status_label = label("正在准备", 12, 217, 216, 28, &font_xiaozhi_14, BADGE_TEXT);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
    detail_label = label("", 12, 239, 216, 48, &font_xiaozhi_14, BADGE_MUTED);
    lv_obj_set_style_text_align(detail_label, LV_TEXT_ALIGN_CENTER, 0);
    reply_label = label("", 14, 96, 212, 190, &font_xiaozhi_14, BADGE_TEXT);
    lv_obj_set_style_text_line_space(reply_label, 3, 0); visible(reply_label, false);
    footer = badge_footer_create(screen, "正在准备 Muse");
    reply_revision = theme_revision = 0; page_at = next_page = 0; page_number = page_count = 1;
    page_dirty = true; face = eyes = mouth = face_style = face_variant = 0;
    face_hidden = false; stage = (passport_muse_stage_t)-1; stage_at = lv_tick_get();
    lv_screen_load(screen); timer = lv_timer_create(refresh, 80, nullptr); refresh(nullptr);
}
extern "C" void demo_muse_exit(void) {
    if (timer) { lv_timer_delete(timer); timer = nullptr; }
    if (screen) { lv_obj_delete(screen); screen = nullptr; }
    face_decoder_stop();
    name_label = status_label = detail_label = reply_label = footer = nullptr;
}
extern "C" esp_err_t demo_muse_start(void) {
    if (!muse_style_worker_start()) return ESP_ERR_NO_MEM;
    esp_err_t e = passport_muse_set_host_ops(&host_ops);
    if (e != ESP_OK) { muse_style_worker_stop(); return e; }
    (void)badge_network_close_ap();
    for (unsigned i = 0; i < 100; i++) {
        badge_network_status_t network{}; badge_network_status(&network);
        if (!network.active) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    badge_power_enter(); power_owned = true;
    badge_network_xiaozhi_power(true, false);
    e = passport_muse_start();
    if (e != ESP_OK) {
        (void)passport_muse_stop();
        muse_style_worker_stop();
        (void)badge_power_leave(); power_owned = false;
    }
    return e;
}
extern "C" esp_err_t demo_muse_stop(void) {
    passport_muse_key(false, true);
    bool stopped = passport_muse_stop();
    muse_style_worker_stop();
    esp_err_t power = power_owned ? badge_power_leave() : ESP_OK; power_owned = false;
    if (!stopped) return ESP_ERR_TIMEOUT;
    return power;
}
extern "C" void demo_muse_key(bsp_btn_t button, bsp_btn_ev_t event) {
    if (button == BSP_BTN_UP) {
        if (event == BSP_BTN_PRESS) passport_muse_key(true, false);
        else if (event == BSP_BTN_RELEASE) passport_muse_key(false, false);
    } else if (button == BSP_BTN_DOWN) {
        if (event == BSP_BTN_CLICK || event == BSP_BTN_DOUBLE) passport_muse_key(false, true);
        else if (event == BSP_BTN_LONG) {
            passport_muse_stage_t current_stage;
            (void)passport_muse_reply_state(0, nullptr, &current_stage);
            if (current_stage == PASSPORT_MUSE_ERROR || current_stage == PASSPORT_MUSE_PAIRING)
                passport_muse_repair();
            else if (current_stage == PASSPORT_MUSE_LISTENING || current_stage == PASSPORT_MUSE_WORKING)
                passport_muse_key(false, true);
            else
                (void)muse_style_request_next();
        }
    } else if (button == BSP_BTN_OK && (event == BSP_BTN_CLICK || event == BSP_BTN_DOUBLE)) {
        uint32_t revision = 0; passport_muse_stage_t current_stage;
        if (passport_muse_reply_state(0, &revision, &current_stage) && current_stage != PASSPORT_MUSE_ERROR) {
            if (revision != reply_revision) { page_at = 0; page_number = 1; }
            else if (passport_muse_reply_state(next_page, nullptr, nullptr)) { page_at = next_page; page_number++; }
            else { page_at = 0; page_number = 1; }
            page_dirty = true;
        } else passport_muse_confirm();
    }
}
