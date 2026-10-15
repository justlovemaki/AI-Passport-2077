/* Production LVGL modal regression; built by tools/render_return_host.py. */
#include "badge_ui.h"
#include "badge_theme.h"
#include "muyu_ui.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t frame[240*320], buffer[240*20];
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *data) {
    const uint16_t *pixels=(const uint16_t *)data;
    for(int y=area->y1;y<=area->y2;y++)for(int x=area->x1;x<=area->x2;x++) {
        assert(x>=0&&x<240&&y>=0&&y<320);
        frame[y*240+x]=*pixels++;
    }
    lv_display_flush_ready(display);
}
static void tick(void) {
    for(unsigned i=0;i<20;i++){lv_tick_inc(20);lv_timer_handler();}
}
static void inspect(const badge_return_menu_t *menu) {
    lv_obj_t *overlay=lv_obj_get_child(lv_layer_top(),-1);
    assert(overlay&&lv_obj_get_width(overlay)==240&&lv_obj_get_height(overlay)==320);
    lv_obj_t *panel=lv_obj_get_child(overlay,0);
    unsigned count=menu->stopping?2:menu->count+2;
    assert(lv_obj_get_child_count(panel)==count);
    for(unsigned i=0;i<count;i++) {
        lv_obj_t *label=lv_obj_get_child(panel,(int)i);
        lv_area_t bounds;lv_obj_get_coords(label,&bounds);
        assert(bounds.x1>=14&&bounds.x2<226&&bounds.y1>=48&&bounds.y2<288);
        if(i>=2)assert((lv_obj_get_style_bg_opa(label,0)==LV_OPA_COVER)==(menu->selected==i-2));
    }
    if(!menu->stopping) {
        const char *destination=menu->plan.destination==BADGE_HOME?"回到工牌":
                                menu->plan.destination==BADGE_PLAYING?"返回卦象":"返回小程序";
        assert(!strcmp(lv_label_get_text(lv_obj_get_child(panel,2)),destination));
        assert(!strcmp(lv_label_get_text(lv_obj_get_child(panel,(int)count-1)),menu->resume));
    }
}
static void save(const char *directory,const char *name) {
    char path[512];snprintf(path,sizeof(path),"%s/%s.rgb565",directory,name);
    FILE *file=fopen(path,"wb");assert(file);
    assert(fwrite(frame,sizeof(frame),1,file)==1);fclose(file);
}
int main(int argc,char **argv) {
    assert(argc==2);
    lv_init();badge_theme_set(NULL);
    lv_display_t *display=lv_display_create(240,320);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    /* Keep a real mini-app mounted underneath: modal memory is incremental. */
    muyu_state_t state;muyu_init(&state,108,1,MUYU_VOLUME_DEFAULT,0);
    muyu_ui_create();muyu_ui_refresh(&state,86,true,true,true);tick();
    badge_return_menu_t menu={.open=true,.count=3,.selected=0,
        .plan={.source=BADGE_PLAYING,.destination=BADGE_GAMES},
        .title="离开城市电台",.detail="退出后停止播放",.resume="继续收听"};
    badge_ui_return_menu(&menu);tick();inspect(&menu);save(argv[1],"radio-exit");
    menu.count=2;menu.selected=1;menu.plan.destination=BADGE_HOME;
    menu.title="离开Muse";menu.detail="退出将取消本地录音或等待";menu.resume="继续当前页面";
    badge_ui_return_menu(&menu);tick();inspect(&menu);save(argv[1],"muse-confirm");
    menu.count=3;menu.selected=2;menu.plan.source=BADGE_AI_CHAT;menu.plan.destination=BADGE_PLAYING;
    menu.title="离开小智对话";menu.detail="退出后结束当前对话";menu.resume="继续对话";
    badge_ui_return_menu(&menu);tick();inspect(&menu);save(argv[1],"yao-return");
    menu.stopping=true;menu.detail="正在退出，请稍候";
    badge_ui_return_menu(&menu);tick();inspect(&menu);save(argv[1],"exit-progress");
    badge_return_plan_t plan=menu.plan;
    badge_return_menu_failed(&menu,&plan);
    badge_ui_return_menu(&menu);tick();inspect(&menu);save(argv[1],"exit-retry");
    badge_ui_return_menu(NULL);tick();
    assert(lv_obj_get_child_count(lv_layer_top())==0);
    /* Warm glyph scratch allocations, then require a stable pool. */
    size_t before=0;
    for(unsigned batch=0;batch<2;batch++) {
        for(unsigned cycle=0;cycle<100;cycle++) {
            menu.count=cycle%2?2:3;menu.selected=cycle%menu.count;
            menu.plan.destination=menu.count==2?BADGE_HOME:BADGE_GAMES;
            badge_ui_return_menu(&menu);tick();inspect(&menu);
            badge_ui_return_menu(NULL);tick();
            assert(lv_obj_get_child_count(lv_layer_top())==0);
        }
        lv_mem_monitor_t memory;lv_mem_monitor(&memory);
        if(!batch)before=memory.free_size;else assert(memory.free_size>=before);
        printf("Return UI cycles: free=%zu, peak=%zu\n",memory.free_size,memory.max_used);
    }
    muyu_ui_destroy();
    puts("Return UI: PASS (production LVGL over Muyu, two/three choices, destination labels, safe defaults, progress/retry and 200 cycles)");
}
