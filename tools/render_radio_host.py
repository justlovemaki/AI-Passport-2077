"""Render radio pages with actual LVGL and exercise 100 create/destroy cycles."""
from pathlib import Path
import subprocess,hashlib
from concurrent.futures import ThreadPoolExecutor
root=Path(__file__).resolve().parent.parent
work=root/'build/ui-render'
out=root/'build/radio-render';out.mkdir(parents=True,exist_ok=True)
lvgl=root/'managed_components/lvgl__lvgl'
source=out/'render.cc'
source.write_text(r'''#include "radio_ui.h"
extern "C" {
#include "lvgl.h"
#include "badge_theme.h"
#include "badge_header.h"
LV_FONT_DECLARE(font_badge_10);
#include "badge_ui.h"
#include "badge_navigation.h"
#include "muyu_ui.h"
#include "voice_ui.h"
#include "xiaozhi_ui.h"
}
#include <cstdio>
#include <cassert>
#include <cstring>
#include <initializer_list>
static uint16_t frame[240*320],buffer[240*10];
static unsigned flushed;
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *pixels){flushed+=(a->x2-a->x1+1)*(a->y2-a->y1+1);const uint16_t *p=(uint16_t *)pixels;for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++)frame[y*240+x]=*p++;lv_display_flush_ready(d);}
static void tick(){for(int i=0;i<20;i++){lv_tick_inc(20);lv_timer_handler();}}
static lv_obj_t *find_text(lv_obj_t *root,const char *value){
    if(lv_obj_check_type(root,&lv_label_class)&&!strcmp(lv_label_get_text(root),value))return root;
    for(unsigned i=0;i<lv_obj_get_child_count(root);i++)if(auto *found=find_text(lv_obj_get_child(root,i),value))return found;
    return nullptr;
}
static lv_obj_t *find_prefix(lv_obj_t *root,const char *full){
    if(lv_obj_check_type(root,&lv_label_class)){
        const char *t=lv_label_get_text(root);size_t n=strlen(t);
        if(n&&n<=strlen(full)&&!strncmp(t,full,n))return root;
    }
    for(unsigned i=0;i<lv_obj_get_child_count(root);i++)if(auto *found=find_prefix(lv_obj_get_child(root,i),full))return found;
    return nullptr;
}
int main(){lv_init();auto *d=lv_display_create(240,320);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(d,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
    const uint32_t themes[4][5]={{0x080808,0x1b1113,0xff334b,0xf2eeee,0x969090},{0x08151a,0x14262e,0x60e1ed,0xe4f3f5,0x8ea6ae},{0x161108,0x292114,0xffbe55,0xfff2d9,0xb4a48a},{0xe9e5dc,0xdcd5c7,0x9b292f,0x221f1c,0x70665a}};
    radio_controls_t c;radio_controls_init(&c,40,2);radio_ui_snapshot_t s;s.battery=86;s.network=true;s.count=10;s.index=2;s.state=RadioPlaybackState::Playing;strcpy(s.location,"上海");strcpy(s.station.name,"上海 FM94.7");s.station.frequency_decihz=947;for(unsigned i=0;i<18;i++)s.levels[i]=20+i*4;
    for(unsigned theme=0;theme<4;theme++)for(int bat: {-1,0,9,10,99,100})for(bool net: {false,true}){
        badge_ui_set_custom(nullptr,nullptr,nullptr,themes[theme]);badge_header_network(net);
        badge_navigation_t nav;badge_navigation_init(&nav,4);badge_navigation_badges(&nav,5,0,1);
        badge_ui_create();badge_ui_render(&nav,"敲木鱼","","",BADGE_GAME_ART_MUYU);badge_ui_status("DEMO",bat,0,true,true);tick();
        auto *badge_number=find_text(lv_screen_active(),"1/5"),*switch_hint=find_text(lv_screen_active(),"长按 OK 切换");
        assert(badge_number&&lv_obj_get_y(badge_number)==12&&lv_obj_get_x(badge_number)+lv_obj_get_width(badge_number)<=172);
        assert(switch_hint&&lv_obj_get_y(switch_hint)==300&&lv_obj_get_y(switch_hint)+lv_obj_get_height(switch_hint)<=320);
        int action_right=0;
        for(const char *action:{"上 小程序","下 亮码","OK 菜单","长按 OK 切换"}){
            auto *hint=find_text(lv_screen_active(),action);assert(hint&&lv_obj_get_y(hint)==300&&lv_obj_get_x(hint)>=action_right);
            assert(lv_obj_get_style_text_font(hint,LV_PART_MAIN)==&font_badge_10);
            assert(lv_color_eq(lv_obj_get_style_text_color(hint,LV_PART_MAIN),lv_obj_get_style_text_color(switch_hint,LV_PART_MAIN)));
            lv_point_t size;lv_text_get_size(&size,action,&font_badge_10,0,0,1000,LV_TEXT_FLAG_NONE);assert(size.x<=lv_obj_get_width(hint));
            action_right=lv_obj_get_x(hint)+lv_obj_get_width(hint);assert(action_right<=226);
        }
        uint16_t expected[240*29];memcpy(expected,frame,sizeof(expected));
        auto save=[&](const char *name){if(theme==0&&bat==100&&net){char p[100];snprintf(p,sizeof(p),"build/radio-render/header-%s.rgb565",name);FILE *f=fopen(p,"wb");assert(f);fwrite(frame,sizeof(frame),1,f);fclose(f);}};
        if(bat>=0){
            lv_font_glyph_dsc_t glyph;assert(lv_font_get_glyph_dsc(&font_badge_10,&glyph,'%',0));
            assert(glyph.adv_w>=glyph.ofs_x+glyph.box_w);
            char text[8];snprintf(text,sizeof(text),"%d%%",bat);lv_point_t extent;
            lv_text_get_size(&extent,text,&font_badge_10,0,0,200,LV_TEXT_FLAG_NONE);assert(extent.x<=32);
            lv_obj_t *reference=lv_label_create(lv_screen_active());lv_obj_set_pos(reference,60,40);lv_obj_set_size(reference,80,14);
            lv_obj_set_style_text_font(reference,&font_badge_10,0);lv_obj_set_style_text_letter_space(reference,0,0);
            lv_obj_set_style_text_color(reference,lv_color_hex(themes[theme][3]),0);
            lv_obj_set_style_bg_color(reference,lv_color_hex(themes[theme][0]),0);lv_obj_set_style_bg_opa(reference,LV_OPA_COVER,0);
            lv_label_set_text(reference,text);tick();
            for(int y=0;y<14;y++)for(int x=0;x<extent.x;x++)assert(frame[(12+y)*240+226-extent.x+x]==frame[(40+y)*240+60+x]);
            lv_obj_delete(reference);tick();
        }
        save("home");badge_ui_destroy();
        muyu_state_t m;muyu_init(&m,0,1,MUYU_VOLUME_DEFAULT,0);muyu_ui_create();muyu_ui_refresh(&m,bat,true,true,true);tick();assert(!memcmp(expected,frame,sizeof(expected)));
        auto *save_hint=find_text(lv_screen_active(),"长按上 保存  长按OK 返回");assert(save_hint&&lv_obj_get_x(save_hint)+lv_obj_get_width(save_hint)<=240&&lv_obj_get_y(save_hint)+lv_obj_get_height(save_hint)<=320);
        save("muyu");muyu_ui_destroy();
        voice_navigation_t v;voice_navigation_init(&v);voice_ui_create();voice_ui_render(&v,-1,bat,nullptr);tick();assert(!memcmp(expected,frame,sizeof(expected)));save("voice");voice_ui_destroy();
        radio_ui_create();s.battery=bat;radio_ui_render(c,s,0);tick();assert(!memcmp(expected,frame,sizeof(expected)));
        auto *radio_hint=find_text(lv_screen_active(),"上下 换台 / OK 播放暂停\n长按上 设置 / 长按OK 返回");assert(radio_hint);
        lv_point_t hint_size;lv_text_get_size(&hint_size,lv_label_get_text(radio_hint),lv_obj_get_style_text_font(radio_hint,LV_PART_MAIN),0,0,1000,LV_TEXT_FLAG_NONE);
        assert(hint_size.x<=212&&hint_size.y<=38&&lv_obj_get_y(radio_hint)+hint_size.y<=320);
        save("radio");radio_ui_destroy();
        xz_snapshot_t x{};x.state=XZ_READY;x.volume=40;strcpy(x.detail,"连接成功，按 OK 开始说话");xiaozhi_ui_create();xiaozhi_ui_render(&x);tick();assert(!memcmp(expected,frame,sizeof(expected)));save("xiaozhi");xiaozhi_ui_destroy();
    }
    {
        badge_navigation_t nav;badge_navigation_init(&nav,4);nav.page=BADGE_PROFILE;
        badge_ui_create();badge_ui_render(&nav,"","","",BADGE_GAME_ART_NONE);badge_ui_network(false,false,"","","","");tick();
        assert(find_text(lv_screen_active(),"热点未开启")&&find_text(lv_screen_active(),"OK 开启热点 / 长按OK 返回"));
        badge_ui_network(true,false,"Badge-DEMO","123456ABCDEF","","");tick();
        assert(find_text(lv_screen_active(),"Badge-DEMO")&&find_text(lv_screen_active(),"http://192.168.4.1")&&find_text(lv_screen_active(),"OK 关闭热点 / 长按OK 返回"));
        FILE *f=fopen("build/radio-render/profile-help.rgb565","wb");assert(f);fwrite(frame,sizeof(frame),1,f);fclose(f);badge_ui_destroy();
    }
    puts("Badge top index, bottom switch hint, Muyu save, radio return bounds and profile hotspot guide: PASS");
    for(unsigned theme=0;theme<4;theme++){
        badge_theme_set(themes[theme]);xiaozhi_ui_create();xz_snapshot_t x{};x.volume=40;x.state=XZ_READY;strcpy(x.detail,"说完自动发送\n上下调音量");xiaozhi_ui_render(&x);tick();
        auto *vol=find_text(lv_screen_active(),"VOL 40");assert(vol&&lv_obj_has_flag(vol,LV_OBJ_FLAG_HIDDEN));
        x.volume=50;xiaozhi_ui_render(&x);tick();assert(!lv_obj_has_flag(vol,LV_OBJ_FLAG_HIDDEN));
        for(unsigned i=0;i<8;i++)tick();assert(lv_obj_has_flag(vol,LV_OBJ_FLAG_HIDDEN));
        x.volume=40;xiaozhi_ui_render(&x);for(unsigned i=0;i<8;i++)tick();
        x.state=XZ_SPEAKING;x.level=75;strcpy(x.text,"测试字幕");xiaozhi_ui_render(&x);tick();
        static uint16_t expected[240*320];memcpy(expected,frame,sizeof(frame));
        x.state=XZ_ERROR;assert(xiaozhi_ui_post(&x));x.state=XZ_SPEAKING;assert(xiaozhi_ui_post(&x));
        memset(&x,0,sizeof(x));tick();assert(!memcmp(expected,frame,sizeof(frame)));
        x.state=XZ_SPEAKING;x.volume=40;x.level=85;
        strcpy(x.heard,"请介绍一下这个电子工牌可以做什么，也请说明如何使用小智进行连续的语音交流，不要只回答一句话。");
        strcpy(x.text,"你可以直接对我说话，说完以后我会自动回答。回答结束后，我会继续聆听下一句话。\n文字会随着声音逐字出现，超过两行时继续向上滚动。\n工牌保留了已有的主题、音量、无线网络和返回首页的操作。");
        assert(xiaozhi_ui_post(&x));tick();
        auto *opening=find_prefix(lv_screen_active(),x.text);assert(opening&&strlen(lv_label_get_text(opening))==6); // Opening is ready before PCM.
        x.played_ms=600;assert(xiaozhi_ui_post(&x));for(unsigned i=0;i<3;i++)tick();
        auto *output=find_prefix(lv_screen_active(),x.text);assert(output&&strlen(lv_label_get_text(output))==24); // Catch up to eight whole Chinese code points.
        for(unsigned i=0;i<3;i++)tick();assert(strlen(lv_label_get_text(output))==24); // No progress during an audio stall.
        x.played_ms=5000;assert(xiaozhi_ui_post(&x));for(unsigned i=0;i<7;i++)tick();
        output=find_prefix(lv_screen_active(),x.text);assert(output);
        auto *scroll=lv_obj_get_parent(output);assert(lv_obj_get_height(scroll)==36&&lv_label_get_long_mode(output)==LV_LABEL_LONG_WRAP);
        assert(lv_obj_get_scroll_y(scroll)%18==0);
        char path[100];snprintf(path,sizeof(path),"build/radio-render/xiaozhi-conversation-%u.rgb565",theme);FILE *f=fopen(path,"wb");assert(f);fwrite(frame,sizeof(frame),1,f);fclose(f);
        x.played_ms=60000;xiaozhi_ui_render(&x);for(unsigned i=0;i<60;i++)tick();
        assert(!strcmp(lv_label_get_text(output),x.text));assert(lv_obj_get_scroll_y(scroll)>0&&lv_obj_get_scroll_y(scroll)%18==0&&lv_obj_get_scroll_bottom(scroll)==0);
        x.state=XZ_THINKING;x.text[0]=0;xiaozhi_ui_render(&x);tick();
        auto *input=find_prefix(lv_screen_active(),x.heard);assert(input&&!strcmp(lv_label_get_text(input),x.heard)); // Final recognition is immediate.
        for(unsigned i=0;i<35;i++)tick();assert(!strcmp(lv_label_get_text(input),x.heard));
        x.state=XZ_ERROR;strcpy(x.detail,"连接已断开，请按 OK 重试");xiaozhi_ui_render(&x);tick();assert(lv_obj_has_flag(scroll,LV_OBJ_FLAG_HIDDEN));
        auto *error=find_text(lv_screen_active(),x.detail);assert(error&&!lv_obj_has_flag(error,LV_OBJ_FLAG_HIDDEN));
        x.state=XZ_THINKING;x.text[0]=0;strcpy(x.heard,"你好，请介绍工牌");assert(xiaozhi_ui_post(&x));
        x.state=XZ_SPEAKING;x.played_ms=x.caption_start_ms=0;strcpy(x.text,"你好，我是小智");assert(xiaozhi_ui_post(&x));
        lv_tick_inc(100);lv_timer_handler();lv_refr_now(d);
        assert(find_text(lv_screen_active(),x.heard)); // STT cannot be skipped by a coalesced TTS event.
        lv_tick_inc(50);lv_timer_handler();lv_refr_now(d);
        output=find_prefix(lv_screen_active(),x.text);assert(output&&strlen(lv_label_get_text(output))==6);
        x.heard[0]=x.text[0]=0;x.state=XZ_READY;x.played_ms=0;strcpy(x.detail,"说完自动发送\n上下调音量");xiaozhi_ui_render(&x);tick();
        uint16_t head[240*29];memcpy(head,frame,sizeof(head));
        for(unsigned state=0;state<=XZ_ERROR;state++){
            x.state=(xz_state_t)state;x.level=state==XZ_LISTENING?90:state==XZ_SPEAKING?70:0;
            strcpy(x.detail,state==XZ_ACTIVATION?"打开 xiaozhi.me\n控制台添加设备，输入下方码":"说完自动发送\n上下调音量");strcpy(x.code,state==XZ_ACTIVATION?"123456":"");
            xiaozhi_ui_render(&x);tick();assert(!memcmp(head,frame,sizeof(head)));
            snprintf(path,sizeof(path),"build/radio-render/xiaozhi-%u-%u.rgb565",theme,state);f=fopen(path,"wb");assert(f);fwrite(frame,sizeof(frame),1,f);fclose(f);
        }
        x.state=XZ_LISTENING;x.level=20;xiaozhi_ui_render(&x);tick();flushed=0;
        xiaozhi_ui_render(&x);tick();assert(flushed==0);
        x.level=80;assert(xiaozhi_ui_post(&x));lv_tick_inc(50);lv_timer_handler();lv_refr_now(d);
        assert(flushed>0&&flushed<=13*4*144);
        printf("XiaoZhi sound field update: %u pixels; active snapshot consumed within 50 ms; idle unchanged: 0 PASS\n",flushed);
        lv_mem_monitor_t before,after;xiaozhi_ui_destroy();lv_mem_monitor(&before);
        for(unsigned cycle=0;cycle<100;cycle++){xiaozhi_ui_create();x.state=XZ_READY;x.level=0;x.text[0]=x.heard[0]=x.code[0]=0;strcpy(x.detail,"说完自动发送\n上下调音量");xiaozhi_ui_render(&x);tick();xiaozhi_ui_destroy();}
        lv_mem_monitor(&after);printf("XiaoZhi theme=%u before=%zu after=%zu\n",theme,before.free_size,after.free_size);assert(after.free_size>=before.free_size&&after.free_size>4096);xiaozhi_ui_destroy();
    }
    puts("XiaoZhi immediate STT, coalesced STT/TTS order, pre-audio opening, catch-up/stall, two-line captions, four themes and 100 lifecycle cycles: PASS");
    puts("Home/all mini-app headers pixel-identical across four themes, Wi-Fi and all battery percentages: PASS");
    s.battery=86;badge_header_network(true);badge_header_battery(86);
    for(unsigned theme=0;theme<4;theme++){badge_theme_set(themes[theme]);radio_ui_create();c.page=RADIO_MAIN;radio_ui_render(c,s,900);tick();assert(frame[30*240+230]==lv_color_to_u16(lv_color_hex(themes[theme][0])));char path[80];snprintf(path,sizeof(path),"build/radio-render/theme-%u.rgb565",theme);FILE *f=fopen(path,"wb");assert(f);fwrite(frame,sizeof(frame),1,f);fclose(f);radio_ui_destroy();}
    badge_theme_set(themes[0]);radio_ui_create();radio_ui_render(c,s,0);tick();lv_mem_monitor_t m;lv_mem_monitor(&m);const size_t before=m.free_size;
    flushed=0;radio_ui_render(c,s,0);tick();assert(flushed==0);
    s.levels[0]=90;flushed=0;radio_ui_render(c,s,0);tick();assert(flushed>0&&flushed<240*40);
    printf("Radio unchanged frame: 0 pixels; meter update: %u / 76800 pixels\n",flushed);
    uint16_t header[240*29];memcpy(header,frame,sizeof(header));
    for(int page=0;page<=RADIO_TIMER;page++){c.page=(radio_page_t)page;radio_ui_render(c,s,0);tick();assert(memcmp(header,frame,sizeof(header))==0);}
    c.page=RADIO_SETTINGS;c.selected=0;radio_ui_render(c,s,0);tick();flushed=0;
    c.selected=1;radio_ui_render(c,s,0);tick();assert(flushed>0&&flushed<240*70);
    printf("Radio setting selection update: %u / 76800 pixels\n",flushed);
    c.page=RADIO_MAIN;radio_ui_render(c,s,0);tick();
    /* Compare empty screens: allocated-screen free bytes depend on allocator
     * splitting/alignment, even when identical labels have no retained leak. */
    radio_ui_destroy();lv_mem_monitor(&m);const size_t empty_before=m.free_size;
    for(int round=0;round<100;round++){radio_ui_create();for(int page=0;page<=RADIO_TIMER;page++){c.page=(radio_page_t)page;c.selected=page==RADIO_CITY?7:0;radio_ui_render(c,s,0);tick();}radio_ui_destroy();}
    lv_mem_monitor(&m);assert(m.free_size>=empty_before);assert(m.free_size>4096);printf("Radio LVGL 100 lifecycle cycles: empty_free=%zu before=%zu peak=%zu (32 KiB pool)\n",m.free_size,empty_before,m.max_used);
    radio_ui_create();c.page=RADIO_MAIN;radio_ui_render(c,s,0);tick();
    for(unsigned page=RADIO_SETTINGS;page<=RADIO_TIMER;page++){c.page=(radio_page_t)page;c.selected=page==RADIO_CITY?7:0;radio_ui_render(c,s,0);tick();char path[80];snprintf(path,sizeof(path),"build/radio-render/page-%u.rgb565",page);FILE *f=fopen(path,"wb");assert(f);fwrite(frame,sizeof(frame),1,f);fclose(f);}radio_ui_destroy();
}
''')
sources=list((lvgl/'src').rglob('*.c'))+[root/'main'/n for n in ['xiaozhi_ui.c','font_xiaozhi_14.c','radio_ui.cc','radio_logic.cc','radio_controls.c','badge_theme.c','badge_header.c','font_radio_14.c','font_badge_10.c','font_badge_28.c','badge_ui.c','badge_navigation.c','muyu_ui.c','muyu_logic.c','voice_ui.c','voice_navigation.c','font_muyu_14.c','font_muyu_22.c','font_voice_14.c']]+[root/'build/voice_catalog.c',source]
flags=['-O0','-w','-DLV_CONF_INCLUDE_SIMPLE',f'-I{work}',f'-I{lvgl}',f'-I{root}/main',f'-I{root}/build']
def compile(path):
    obj=work/(hashlib.sha256(str(path).encode()).hexdigest()[:16]+'.o')
    if not obj.exists() or obj.stat().st_mtime<max(path.stat().st_mtime,(work/'lv_conf.h').stat().st_mtime):subprocess.run(['g++' if path.suffix=='.cc' else 'gcc',*flags,'-c',str(path),'-o',str(obj)],check=True)
    return str(obj)
with ThreadPoolExecutor(max_workers=8) as pool:objects=list(pool.map(compile,sources))
subprocess.run(['g++',*objects,'-lm','-o',str(out/'render')],check=True)
subprocess.run([str(out/'render')],cwd=root,check=True,timeout=60)
