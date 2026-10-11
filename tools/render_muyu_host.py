from pathlib import Path
import re, subprocess
import hashlib
from concurrent.futures import ThreadPoolExecutor

root = Path(__file__).resolve().parent.parent
repo = root
lvgl = repo/'managed_components/lvgl__lvgl'
work = repo/'build/ui-render'
work.mkdir(parents=True, exist_ok=True)
pins = (repo/'components/bsp/include/bsp_pins.h').read_text()
(work/'bsp_pins.h').write_text('\n'.join(re.findall(r'^#define BSP_LCD_[WH]\s+\d+', pins, re.M))+'\n')
(work/'lv_conf.h').write_text('''#pragma once
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_MEM_SIZE (32 * 1024)
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_USE_LOG 1
#define LV_LOG_PRINTF 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_MEM_INTEGRITY 1
''')
(work/'menu.c').write_text('''#include "badge_ui.h"
#include "voice_catalog.h"
static badge_navigation_t nav;
static void show(void) {
    badge_ui_render(&nav,"敲木鱼","轻敲一下，放空片刻","ZEN / RELAX",BADGE_GAME_ART_MUYU);
    badge_ui_status("30BDA8",86,3661,true,true);
}
void render_main_menu(void) {
    badge_navigation_init(&nav,4);badge_navigation_badges(&nav,5,0,1); badge_ui_set_profile(NULL);badge_ui_set_custom(NULL,NULL,NULL,NULL);badge_ui_create(); show();
}
void render_cards(const uint8_t *p) {
    const uint8_t *cards[]={p,p,NULL,NULL,p};
    badge_ui_set_badges(cards,5);badge_navigation_badges(&nav,5,0,19);
    nav.page=BADGE_CARDS;nav.badge_selected=1;show();
}
void render_terminal(void) {nav.page=BADGE_TERMINAL;show();}
void render_library(void) {nav.page=BADGE_GAMES;show();}
void render_game_art(badge_game_art_t art) {nav.page=BADGE_GAMES;badge_ui_render(&nav,"示例小程序","说明文字不应被图案遮挡","CATEGORY",art);}
void render_voice_entry(void) {nav.game_selected=1;badge_ui_render(&nav,"音效钥匙扣",VOICE_SUMMARY,"VOICE / SOUNDBOARD",BADGE_GAME_ART_VOICE);nav.game_selected=0;}
void render_settings(void) {nav.page=BADGE_SETTINGS;show();}
void render_qr(void) {nav.page=BADGE_QR;show();}
void render_wifi(void) {nav.page=BADGE_WIFI;show();badge_ui_network(true,false,"Badge-DEMO","123456ABCDEF","","Setup hotspot ready");}
void render_profile_help(void) {nav.page=BADGE_PROFILE;show();}
void render_first_setup(void) {badge_navigation_badges(&nav,5,0,0);nav.page=BADGE_HOME;show();badge_ui_network(true,false,"Badge-DEMO","123456ABCDEF","","Setup hotspot ready");}
void finish_first_setup(void) {badge_navigation_badges(&nav,5,0,1);nav.page=BADGE_HOME;show();}
void render_custom_profile(const uint8_t *p) {badge_ui_set_profile(p);nav.page=BADGE_HOME;show();}
void destroy_main_menu(void) {badge_ui_destroy();}
''',encoding='utf-8')
(work/'render.c').write_text('''#include "lvgl.h"
#include "muyu_ui.h"
#include "badge_ui.h"
#include "badge_theme.h"
#include "voice_ui.h"
#include "profile_format.h"
#include "badge_layout.h"
#include "bsp_pins.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
static uint16_t frame[BSP_LCD_W*BSP_LCD_H];
static uint16_t buffer[BSP_LCD_W*20];
void render_main_menu(void);
void destroy_main_menu(void);
void render_library(void);
void render_game_art(badge_game_art_t art);
void render_voice_entry(void);
void render_terminal(void);
void render_cards(const uint8_t *p);
void render_settings(void);
void render_profile_help(void);
void render_first_setup(void);
void finish_first_setup(void);
void render_qr(void);
void render_wifi(void);
void render_custom_profile(const uint8_t *p);
static uint16_t profile_pixels[208*148];
static uint8_t extra[PROFILE_V2_BYTES-PROFILE_PIXELS_BYTES];
static void flush(lv_display_t *d, const lv_area_t *area, uint8_t *p) {
    uint16_t *pixels=(uint16_t*)p;
    for(int y=area->y1;y<=area->y2;y++)
        for(int x=area->x1;x<=area->x2;x++) frame[y*BSP_LCD_W+x]=*pixels++;
    lv_display_flush_ready(d);
}
static void tick(unsigned n) {for(unsigned i=0;i<n;i++){lv_tick_inc(20);lv_timer_handler();}}
int main(int argc,char **argv) {
    (void)argc;
    setvbuf(stdout,NULL,_IONBF,0);
    lv_init();
    lv_display_t *d=lv_display_create(BSP_LCD_W,BSP_LCD_H);
    lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d,flush);
    render_main_menu(); tick(20);
    FILE *menu=fopen(argv[2],"wb");assert(menu);fwrite(frame,sizeof(frame),1,menu);fclose(menu);
    /* Render real connection transitions; every changed pixel must stay in
       the reserved header slot, preserving the title, battery and body. */
    uint16_t before_wifi[BSP_LCD_W*BSP_LCD_H];memcpy(before_wifi,frame,sizeof(frame));
    badge_ui_network(true,false,"","","","");tick(20);
    assert(memcmp(before_wifi,frame,sizeof(frame))==0); /* AP alone is not STA. */
    badge_ui_network(false,true,"","","","");tick(20);
    unsigned changed=0;
    for(int y=0;y<BSP_LCD_H;y++)for(int x=0;x<BSP_LCD_W;x++)
        if(frame[y*BSP_LCD_W+x]!=before_wifi[y*BSP_LCD_W+x]) {
            assert(x>=179&&x<=193&&y>=14&&y<=23);changed++;
        }
    assert(changed>10);
    FILE *wi=fopen("build/ui-render/wifi-connected.rgb565","wb");assert(wi);fwrite(frame,sizeof(frame),1,wi);fclose(wi);
    badge_ui_network(false,false,"","","","");tick(20);
    assert(memcmp(before_wifi,frame,sizeof(frame))==0);
    puts("Wi-Fi connected/disconnected/AP-only rendering and header bounds: PASS");
    render_first_setup();tick(20);
    FILE *setup=fopen("build/ui-render/first-setup.rgb565","wb");assert(setup);fwrite(frame,sizeof(frame),1,setup);fclose(setup);
    badge_ui_network(false,false,"Badge-DEMO","","","");tick(20);
    setup=fopen("build/ui-render/first-setup-off.rgb565","wb");assert(setup);fwrite(frame,sizeof(frame),1,setup);fclose(setup);
    finish_first_setup();
    render_terminal(); tick(20);
    FILE *terminal=fopen(argv[5],"wb");assert(terminal);fwrite(frame,sizeof(frame),1,terminal);fclose(terminal);
    badge_return_menu_t rm={.open=true,.selected=1,.count=3,.plan={.source=BADGE_PLAYING,.destination=BADGE_GAMES}};badge_ui_return_menu(&rm);tick(20);
    FILE *return_view=fopen("build/ui-render/return-menu.rgb565","wb");assert(return_view);fwrite(frame,sizeof(frame),1,return_view);fclose(return_view);badge_ui_return_menu(NULL);tick(2);
    render_library(); tick(20);
    FILE *games=fopen(argv[3],"wb");assert(games);fwrite(frame,sizeof(frame),1,games);fclose(games);
    const badge_game_art_t game_art[]={BADGE_GAME_ART_NONE,BADGE_GAME_ART_MUYU,BADGE_GAME_ART_VOICE,BADGE_GAME_ART_RADIO,BADGE_GAME_ART_YAO,BADGE_GAME_ART_CUPS};
    const char *game_art_name[]={"none","muyu","voice","radio","yao","cups"};uint16_t plain[240*320];
    for(unsigned art=0;art<sizeof(game_art)/sizeof(*game_art);art++){
        render_game_art(game_art[art]);tick(20);char path[80];snprintf(path,sizeof(path),"build/ui-render/game-%s.rgb565",game_art_name[art]);
        FILE *view=fopen(path,"wb");assert(view);fwrite(frame,sizeof(frame),1,view);fclose(view);
        if(!art)memcpy(plain,frame,sizeof(plain));else{
            unsigned changed=0;for(int y=0;y<320;y++)for(int x=0;x<240;x++)if(frame[y*240+x]!=plain[y*240+x]){
                assert(y>=166&&y<=202);changed++;
            }assert(changed>20);
        }
    }
    render_voice_entry();tick(20);FILE *ve=fopen("build/ui-render/voice-entry.rgb565","wb");assert(ve);fwrite(frame,sizeof(frame),1,ve);fclose(ve);
    render_settings(); tick(20);
    FILE *settings=fopen(argv[4],"wb");assert(settings);fwrite(frame,sizeof(frame),1,settings);fclose(settings);
    FILE *sample=fopen("assets/images/cyber-badge/demo-card.rgb565","rb");assert(sample);assert(fread(profile_pixels,1,sizeof(profile_pixels),sample)==sizeof(profile_pixels));fclose(sample);
    sample=fopen("assets/images/cyber-badge/demo-brand.rgb565","rb");assert(sample);assert(fread(extra,1,PROFILE_BRAND_BYTES,sample)==PROFILE_BRAND_BYTES);fclose(sample);
    sample=fopen("assets/images/cyber-badge/demo-logo.rgb565","rb");assert(sample);assert(fread(extra+PROFILE_BRAND_BYTES,1,PROFILE_LOGO_BYTES,sample)==PROFILE_LOGO_BYTES);fclose(sample);
    FILE *input=fopen("assets/images/cyber-badge/test-qr.bin","rb");assert(input);assert(fread(extra+PROFILE_BRAND_BYTES+PROFILE_LOGO_BYTES,1,PROFILE_QR_BYTES,input)==PROFILE_QR_BYTES);fclose(input);
    badge_ui_set_custom(extra,extra+PROFILE_BRAND_BYTES,extra+PROFILE_BRAND_BYTES+PROFILE_LOGO_BYTES,NULL);
    render_custom_profile((const uint8_t *)profile_pixels);badge_ui_network(false,true,"","","","");tick(20);
    for(unsigned i=0;i<BADGE_REGION_COUNT;i++) {
        const badge_region_t *r=&BADGE_REGIONS[i];
        assert(r->sx+r->w<=208&&r->sy+r->h<=148);
        assert(r->x+(r->w*r->scale+255)/256<=240);
        assert(r->y+(r->h*r->scale+255)/256<291);
        if(r->scale==256)for(unsigned y=0;y<r->h;y++)for(unsigned x=0;x<r->w;x++)
            assert(frame[(r->y+y)*240+r->x+x]==profile_pixels[(r->sy+y)*208+r->sx+x]);
    }
    puts("Rearranged profile fields: exact pixel preservation and bounds PASS");
    FILE *custom=fopen(argv[6],"wb");assert(custom);fwrite(frame,sizeof(frame),1,custom);fclose(custom);
    render_cards((const uint8_t *)profile_pixels);tick(20);
    FILE *cards=fopen("build/ui-render/cards.rgb565","wb");assert(cards);fwrite(frame,sizeof(frame),1,cards);fclose(cards);
    render_qr();tick(20);FILE *q=fopen("build/ui-render/qr.rgb565","wb");assert(q);fwrite(frame,sizeof(frame),1,q);fclose(q);
    render_wifi();tick(20);FILE *w=fopen("build/ui-render/wifi.rgb565","wb");assert(w);fwrite(frame,sizeof(frame),1,w);fclose(w);
    render_profile_help();tick(20);
    destroy_main_menu();
    muyu_ui_create();
    muyu_state_t s; muyu_init(&s,108,1,2,0);
    muyu_ui_refresh(&s,86,true,true,true);
    tick(20);
    FILE *f=fopen(argv[1],"wb");assert(f);fwrite(frame,sizeof(frame),1,f);fclose(f);
    /* Change the shared theme while the game is mounted, as profile reload does. */
    const uint32_t themes[][5]={{0x071116,0x10252c,0x39d9e3,0xedf5f5,0xa0b7bf},{0x120e09,0x241b11,0xf5b84b,0xf0e4cf,0xbcb09a},{0xe9edf1,0xd7dfe7,0x7e294a,0x14202a,0x425560}};
    for(unsigned theme=0;theme<3;theme++) {
        badge_ui_set_custom(NULL,NULL,NULL,themes[theme]);muyu_ui_refresh(&s,86,true,true,true);tick(20);
        assert(frame[0]==lv_color_to_u16(lv_color_hex(themes[theme][0])));
        assert(frame[319*240+239]==lv_color_to_u16(lv_color_hex(themes[theme][1])));
        unsigned text_pixels=0,accent_pixels=0;
        for(int y=88;y<114;y++)for(int x=14;x<120;x++)if(frame[y*240+x]==lv_color_to_u16(lv_color_hex(themes[theme][3])))text_pixels++;
        for(int y=168;y<245;y++)for(int x=53;x<191;x++)if(frame[y*240+x]==lv_color_to_u16(lv_color_hex(themes[theme][2])))accent_pixels++;
        assert(text_pixels>10&&accent_pixels>20);assert(s.total==108&&s.session==0);
        char path[80];snprintf(path,sizeof(path),"build/ui-render/muyu-theme-%u.rgb565",theme);FILE *t=fopen(path,"wb");assert(t);fwrite(frame,sizeof(frame),1,t);fclose(t);
    }
    badge_ui_set_custom(NULL,NULL,NULL,NULL);muyu_ui_refresh(&s,86,true,true,true);tick(20);
    puts("Live game themes: background, panel, text, percussion accent and unchanged count PASS");
    for(int i=0;i<1000;i++){muyu_strike(&s);muyu_ui_hit(true);muyu_ui_refresh(&s,-1,true,true,true);tick(2);}
    tick(50);
    lv_mem_monitor_t m;lv_mem_monitor(&m);
    printf("LVGL 32 KB pool, after 1000 strikes: free=%zu, max_used=%zu, frag=%u%%\\n",m.free_size,m.max_used,m.frag_pct);
    assert(m.free_size>2048);
    /* Compare identical freshly-created pages, after warming allocation caches.
       The hit test above used a different battery string and growing labels. */
    muyu_ui_destroy(); render_main_menu(); tick(2); render_custom_profile((const uint8_t *)profile_pixels); tick(2); badge_ui_set_custom(extra,extra+PROFILE_BRAND_BYTES,extra+PROFILE_BRAND_BYTES+PROFILE_LOGO_BYTES,NULL);render_qr();tick(2);render_wifi();tick(2);render_profile_help(); tick(2); render_cards((const uint8_t *)profile_pixels);tick(2); render_terminal(); tick(2); render_library(); tick(2); render_settings(); tick(2); destroy_main_menu();
    muyu_ui_create(); muyu_ui_refresh(&s,86,true,true,true); muyu_ui_hit(true); tick(50);
    lv_mem_monitor(&m); size_t before=m.free_size;
    for(int i=0;i<100;i++) {
        muyu_ui_destroy(); render_main_menu(); tick(2); render_custom_profile((const uint8_t *)profile_pixels); tick(2); badge_ui_set_custom(extra,extra+PROFILE_BRAND_BYTES,extra+PROFILE_BRAND_BYTES+PROFILE_LOGO_BYTES,NULL);render_qr();tick(2);render_wifi();tick(2);render_profile_help(); tick(2); render_cards((const uint8_t *)profile_pixels);tick(2); render_terminal(); tick(2); render_library(); tick(2); render_settings(); tick(2); destroy_main_menu();
        muyu_ui_create();badge_ui_set_custom(NULL,NULL,NULL,themes[i%3]); muyu_ui_refresh(&s,86,true,true,true); muyu_ui_hit(true); tick(2);
    }
    tick(50);lv_mem_monitor(&m);
    printf("100 menu/wooden-fish round trips: free=%zu, before=%zu\\n",m.free_size,before);
    assert(m.free_size>=before);
    muyu_ui_destroy();
    voice_navigation_t vn;voice_navigation_init(&vn);badge_theme_set(NULL);
    voice_ui_create();voice_ui_render(&vn,-1,86,NULL);tick(20);
    lv_mem_monitor(&m);size_t voice_before=m.free_size;
    for(unsigned theme=0;theme<4;theme++) {
        badge_theme_set(theme?themes[theme-1]:NULL);
        for(unsigned view=0;view<3;view++) {
            vn.view=(voice_view_t)view;vn.pack=0;vn.clip=0;
            voice_ui_render(&vn,view==1?0:-1,86,NULL);tick(20);
            char path[100];snprintf(path,sizeof(path),"build/ui-render/voice-%u-%u.rgb565",theme,view);
            FILE *vf=fopen(path,"wb");assert(vf);fwrite(frame,sizeof(frame),1,vf);fclose(vf);
        }
    }
    /* First sweep warms LVGL glyph scratch buffers; the second must plateau. */
    for(unsigned batch=0;batch<2;batch++){
    for(unsigned cycle=0;cycle<100;cycle++) {
        voice_ui_destroy();muyu_ui_create();muyu_ui_refresh(&s,86,true,true,true);tick(2);muyu_ui_destroy();
        voice_ui_create();vn.pack=cycle%VOICE_PACK_COUNT;vn.clip=voice_packs[vn.pack].count-1;vn.view=VOICE_CLIPS;
        voice_ui_render(&vn,-1,86,NULL);tick(2);
        rm=(badge_return_menu_t){.open=true,.selected=cycle%3,.count=3,.plan={.source=BADGE_PLAYING,.destination=BADGE_GAMES}};badge_ui_return_menu(&rm);tick(2);badge_ui_return_menu(NULL);tick(2);
    }
    /* Compare freshly-created, identical themed pages, not a page that retains extra list styles. */
    voice_ui_destroy();badge_theme_set(NULL);voice_navigation_init(&vn);voice_ui_create();voice_ui_render(&vn,-1,86,NULL);tick(20);lv_mem_monitor(&m);
    printf("100 voice/wooden-fish round trips: free=%zu, before=%zu, peak=%zu\\n",m.free_size,voice_before,m.max_used);
    if(batch==0)voice_before=m.free_size;else assert(m.free_size>=voice_before);
    }
    return 0;
}
''')
sources = list((lvgl/'src').rglob('*.c'))
sources += [repo/'main'/p for p in ['muyu_ui.c','muyu_logic.c','font_muyu_22.c','font_muyu_14.c','badge_ui.c','badge_theme.c','badge_header.c','badge_navigation.c','font_badge_10.c','font_badge_28.c','voice_ui.c','voice_navigation.c','font_voice_14.c']]
sources += [work/'render.c', work/'menu.c', repo/'build/voice_catalog.c']
flags = ['gcc','-O0','-w','-DLV_CONF_INCLUDE_SIMPLE',f'-I{work}',f'-I{lvgl}',f'-I{repo}/main',f'-I{repo}/build']
def compile_source(path):
    obj = work/(hashlib.sha256(str(path).encode()).hexdigest()[:16]+'.o')
    import sys
    if '--incremental' not in sys.argv or not obj.exists() or obj.stat().st_mtime < path.stat().st_mtime:
        subprocess.run(flags + ['-c',str(path),'-o',str(obj)],check=True)
    return str(obj)
with ThreadPoolExecutor(max_workers=8) as pool:
    objects = list(pool.map(compile_source, sources))
subprocess.run(['gcc',*objects,'-lm','-o',str(work/'render')],check=True)
subprocess.run([str(work/'render'),str(work/'screen.rgb565'),str(work/'menu.rgb565'),str(work/'games.rgb565'),str(work/'settings.rgb565'),str(work/'terminal.rgb565'),str(work/'custom.rgb565')],check=True,timeout=60)

