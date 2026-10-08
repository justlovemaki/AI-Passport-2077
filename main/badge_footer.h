#pragma once
#include "lvgl.h"
#include "badge_theme.h"
#ifdef __cplusplus
extern "C" {
#endif
LV_FONT_DECLARE(font_badge_10);
#define BADGE_HINT_REMINDER "OK返回工牌"
#define BADGE_HINT_HOME "上小程序 下亮码 OK菜单 长OK切换 长上小智"
/* Long-press keys are prefixed with 长; all hints fit one fixed bottom row. */
#define BADGE_HINT_MUYU "OK敲 上自动 下节奏 长上存 长下音量 长OK返回"
#define BADGE_HINT_VOICE_PACKS "上下选 OK进入 长上音量 长下停止 长OK返回"
#define BADGE_HINT_VOICE_CLIPS "上下选 OK播放 长上音量 长下停止 长OK返回"
#define BADGE_HINT_VOLUME "上下音量 OK返回 长OK返回"
#define BADGE_HINT_RADIO "上下换台 OK播放/暂停 长上设置 长OK返回"
#define BADGE_HINT_SELECT "上下选择 OK确认 长OK返回"
#define BADGE_HINT_CARDS "上下选择 OK切换 长OK返回"
#define BADGE_HINT_MENU "上下选择 OK进入 长OK返回"
#define BADGE_HINT_APPS "上下选择 OK启动 长OK返回"
#define BADGE_HINT_YAO_CAST "OK投掷 长下重起 长OK返回"
#define BADGE_HINT_YAO_RESULT "上下阅读 OK小智解读 长下重起 长OK返回"
#define BADGE_HINT_SHENGBEI "OK再掷 长OK返回"
#define BADGE_HINT_SETTINGS "上下选择 OK进入 长OK返回"
#define BADGE_HINT_BRIGHTNESS "上下亮度 OK确认 长OK返回"
#define BADGE_HINT_AP_ON "长OK返回"
#define BADGE_HINT_AP_OFF "长OK返回"
#define BADGE_HINT_SETUP_ON "下菜单 上小程序"
#define BADGE_HINT_SETUP_OFF "OK开启热点 下菜单 上小程序"
#define BADGE_HINT_QR_HOME "下/OK返回工牌 长OK返回"
#define BADGE_HINT_QR_MENU "下/OK返回终端 长OK返回"
#define BADGE_HINT_RETURN "上下选择 OK确认 长OK取消"
#define BADGE_HINT_XZ_START "上下音量 OK开始 长上表情 长下设置 长OK返回"
#define BADGE_HINT_XZ_PAUSE "上下音量 OK暂停 长上表情 长下设置 长OK返回"
#define BADGE_HINT_XZ_RETRY "上下音量 OK重试 长上表情 长下设置 长OK返回"
#define BADGE_HINT_XZ_WAIT "上下音量 长上表情 长下设置 长OK返回"
#define BADGE_HINT_XZ_INTERRUPT "上下音量 OK打断 长上表情 长下设置 长OK返回"
#define BADGE_HINT_MUSE_SETUP "手机页设置 长下形象 长OK返回"
#define BADGE_HINT_MUSE_PAIR "OK确认 长下重配 长OK返回"
#define BADGE_HINT_MUSE_WAIT "请等待 长下形象 长OK返回"
#define BADGE_HINT_MUSE_READY "按住上键说话 松开发送 长下形象 长OK返回"
#define BADGE_HINT_MUSE_LISTEN "松开上键发送 下键取消 长OK返回"
#define BADGE_HINT_MUSE_WORK "下键取消等待 长OK返回"
#define BADGE_HINT_MUSE_REPLY "按住上键继续说话 长下形象 长OK返回"
#define BADGE_HINT_MUSE_PAGES "OK翻页 按住上键继续说话 长下形象 长OK返回"
#define BADGE_HINT_MUSE_ERROR "OK重试 长下重新配对 长OK返回"
static inline lv_obj_t *badge_footer_create(lv_obj_t *parent,const char *text) {
    lv_obj_t *o=lv_label_create(parent);
    lv_obj_set_pos(o,8,300);lv_obj_set_size(o,224,14);
    lv_obj_set_style_text_font(o,&font_badge_10,0);
    lv_obj_set_style_text_letter_space(o,0,0);
    lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(o,lv_color_hex(badge_theme_colors()[BADGE_MUTED]),0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_CLIP);lv_label_set_text(o,text);
    return o;
}
#ifdef __cplusplus
}
#endif
