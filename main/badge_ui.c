#include "badge_ui.h"
#include "badge_theme.h"
#include "badge_header.h"
#include "badge_footer.h"
#include "badge_profile.h"
#include "badge_layout.h"
#include "profile_format.h"
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/misc/cache/instance/lv_image_header_cache.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

LV_FONT_DECLARE(font_muyu_14);
LV_FONT_DECLARE(font_xiaozhi_14);
LV_FONT_DECLARE(font_badge_10);
LV_FONT_DECLARE(font_badge_28);
#define BLACK colors[0]
#define PANEL colors[1]
#define RED colors[2]
#define DARK_RED edge_color
#define WHITE colors[3]
#define MUTED colors[4]
static uint32_t colors[5]={0x08090B,0x181216,0xF03543,0xE8E6DF,0xA69C9F};
static uint32_t edge_color=0x56222B,on_accent=0x08090B;
static float linear(unsigned c){float v=c/255.0f;return v<=.04045f?v/12.92f:powf((v+.055f)/1.055f,2.4f);}

static bool portrait_layout,tactical_layout;
static lv_image_dsc_t tactical_panel={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=84,.h=148,.stride=168},.data_size=PROFILE_V4_PANEL_BYTES};
static lv_image_dsc_t tactical_footer={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=120,.h=40,.stride=416},.data_size=16640};
static lv_image_dsc_t tactical_signature={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=88,.h=40,.stride=416},.data_size=16640};
static lv_image_dsc_t brand_id={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=168,.h=12,.stride=336},.data_size=4032};
static unsigned badge_formats[PROFILE_BADGE_COUNT];
enum { BADGE_LIST_TOP=100, BADGE_LIST_STEP=38, BADGE_LIST_HEIGHT=36,
       BADGE_NAME_X=49, BADGE_NAME_WIDTH=128, BADGE_NAME_HEIGHT=32 };
static lv_image_dsc_t portrait_image={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=112,.h=136,.stride=224},.data_size=PROFILE_V3_AVATAR_BYTES};
static lv_image_dsc_t brand_image={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=148,.h=44,.stride=296},.data_size=13024};
static lv_image_dsc_t logo_image={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=32,.h=32,.stride=64},.data_size=2048};
/* A neutral 176px square leaves 32px side margins and 11px below the banner. */
#define QR_VIEW_SIZE 176
static uint8_t qr_bitmap[8+QR_VIEW_SIZE*QR_VIEW_SIZE/8];
static lv_image_dsc_t qr_image={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_I1,.w=QR_VIEW_SIZE,.h=QR_VIEW_SIZE,.stride=QR_VIEW_SIZE/8},.data_size=sizeof(qr_bitmap)};
static lv_obj_t *wifi_ssid,*wifi_password,*wifi_ip,*wifi_message,*wifi_action;
void badge_ui_set_custom(const uint8_t *brand,const uint8_t *logo,const uint8_t *qr,const uint32_t theme[5]) {
    lv_image_cache_drop(&brand_image);lv_image_header_cache_drop(&brand_image);
    lv_image_cache_drop(&logo_image);lv_image_header_cache_drop(&logo_image);
    lv_image_cache_drop(&qr_image);lv_image_header_cache_drop(&qr_image);
    brand_image.header.h=portrait_layout?PROFILE_V3_BRAND_H:PROFILE_BRAND_H;
    brand_image.data_size=portrait_layout?PROFILE_V3_BRAND_BYTES:PROFILE_BRAND_BYTES;
    brand_image.data=brand;logo_image.data=logo;
    badge_theme_set(theme);memcpy(colors,badge_theme_colors(),sizeof(colors));
    edge_color=((RED&0xfefefe)>>1)+((PANEL&0xfefefe)>>1);
    on_accent=(.2126f*linear((RED>>16)&255)+.7152f*linear((RED>>8)&255)+.0722f*linear(RED&255)>.179f)?0x08090B:0xFFFFFF;
    qr_image.data=NULL;
    if(qr){
        memset(qr_bitmap,0,sizeof(qr_bitmap));memset(qr_bitmap,255,4);qr_bitmap[7]=255;
        /* Nearest pixel centers keep edges binary; stored 192px QR stays intact. */
        for(unsigned y=0;y<QR_VIEW_SIZE;y++)for(unsigned x=0;x<QR_VIEW_SIZE;x++){
            unsigned sx=(x*192+QR_VIEW_SIZE/2)/QR_VIEW_SIZE,sy=(y*192+QR_VIEW_SIZE/2)/QR_VIEW_SIZE;
            unsigned src=sy*192+sx,dst=y*QR_VIEW_SIZE+x;
            if(qr[src>>3]&(128>>(src&7)))qr_bitmap[8+(dst>>3)]|=128>>(dst&7);
        }
        qr_image.data=qr_bitmap;
    }
}
static lv_obj_t *screen,*uptime_label,*health_label,*unit_label;
static lv_obj_t *return_overlay;
static badge_navigation_t navigation;
static badge_game_art_t selected_game_art;
static char ai_status[96]="你好小智";
void badge_ui_ai_status(const char *text){snprintf(ai_status,sizeof(ai_status),"%s",text);}
static int scan_progress=255;

static lv_image_dsc_t identity={
    .header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,
        .w=PROFILE_CARD_W,.h=PROFILE_CARD_H,.stride=PROFILE_CARD_W*2},
    .data_size=PROFILE_CARD_BYTES};
static lv_image_dsc_t profile_views[BADGE_REGION_COUNT],badge_names[PROFILE_BADGE_COUNT];
/* List-only alpha tiles remove each saved card's baked-in background. */
static lv_image_dsc_t list_names[PROFILE_BADGE_COUNT];
static void clear_list_names(void) {
    for(unsigned i=0;i<PROFILE_BADGE_COUNT;i++) {
        lv_image_cache_drop(&list_names[i]);lv_image_header_cache_drop(&list_names[i]);
        lv_free((void *)list_names[i].data);list_names[i].data=NULL;
    }
}
static unsigned name_distance(uint16_t a,uint16_t b) {
    int r=(int)((a>>11)&31)-(int)((b>>11)&31);
    int g=(int)((a>>5)&63)-(int)((b>>5)&63);
    int blue=(int)(a&31)-(int)(b&31);
    return (r<0?-r:r)*2+(g<0?-g:g)+(blue<0?-blue:blue)*2;
}
static uint16_t name_pixel(const lv_image_dsc_t *src,unsigned x,unsigned y) {
    const uint8_t *p=src->data+y*src->header.stride+x*2;
    return (uint16_t)p[0]|((uint16_t)p[1]<<8);
}
static lv_image_dsc_t *make_list_name(unsigned i) {
    const lv_image_dsc_t *src=&badge_names[i];
    unsigned w=badge_formats[i]==3?117:src->header.w;
    unsigned h=badge_formats[i]==3?18:32,stride=(w+1)/2;
    size_t size=64+stride*h;
    uint8_t *data=lv_malloc(size);if(!data)return NULL;
    memset(data,0,size);
    uint16_t background=name_pixel(src,0,0);unsigned maximum=0;
    for(unsigned y=0;y<src->header.h;y++)for(unsigned x=0;x<src->header.w;x++) {
        unsigned d=name_distance(name_pixel(src,x,y),background);
        if(d>maximum)maximum=d;
    }
    for(unsigned alpha=0;alpha<16;alpha++) {
        uint32_t color=(alpha*17u<<24)|WHITE;
        memcpy(data+alpha*4,&color,4);
    }
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
        unsigned sx=(x*src->header.w+w/2)/w,sy=(y*src->header.h+h/2)/h;
        unsigned d=name_distance(name_pixel(src,sx,sy),background);
        unsigned alpha=maximum?(d*15+maximum/2)/maximum:0;
        data[64+y*stride+x/2]|=alpha<<(x%2?0:4);
    }
    list_names[i]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,
        .cf=LV_COLOR_FORMAT_I4,.w=w,.h=h,.stride=stride},.data_size=size,.data=data};
    return &list_names[i];
}
void badge_ui_set_badges(const uint8_t *const *cards,unsigned count) {
    for(unsigned i=0;i<PROFILE_BADGE_COUNT;i++) {
        lv_image_cache_drop(&badge_names[i]);lv_image_header_cache_drop(&badge_names[i]);
        badge_names[i]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=112,.h=32,.stride=416},
            .data_size=32*416,.data=i<count&&cards[i]?cards[i]+(4*208+88)*2:NULL};
    }
}
void badge_ui_set_profile(const uint8_t *pixels) {
    lv_image_cache_drop(&brand_id);lv_image_header_cache_drop(&brand_id);brand_id.data=NULL;
    tactical_layout=false;portrait_layout=false;identity.header.h=PROFILE_CARD_H;identity.data_size=PROFILE_CARD_BYTES;
    lv_image_cache_drop(&identity);
    lv_image_header_cache_drop(&identity);
    identity.data=pixels;
    for(unsigned i=0;i<BADGE_REGION_COUNT;i++) {
        lv_image_cache_drop(&profile_views[i]);lv_image_header_cache_drop(&profile_views[i]);
        const badge_region_t *r=&BADGE_REGIONS[i];
        profile_views[i]=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,
            .cf=LV_COLOR_FORMAT_RGB565,.w=r->w,.h=r->h,.stride=PROFILE_CARD_W*2},
            .data_size=r->h*PROFILE_CARD_W*2,
            .data=pixels?pixels+(r->sy*PROFILE_CARD_W+r->sx)*2:NULL};
    }
}

void badge_ui_set_portrait(const uint8_t *pixels,const uint8_t *avatar) {
    badge_ui_set_profile(pixels);portrait_layout=true;
    identity.header.h=PROFILE_V3_CARD_H;identity.data_size=PROFILE_V3_CARD_BYTES;
    lv_image_cache_drop(&portrait_image);lv_image_header_cache_drop(&portrait_image);portrait_image.data=avatar;portrait_image.header.h=PROFILE_V3_AVATAR_H;portrait_image.data_size=PROFILE_V3_AVATAR_BYTES;
}
void badge_ui_set_tactical(const uint8_t *pixels,const uint8_t *avatar) {
    badge_ui_set_portrait(pixels,avatar);tactical_layout=true;
    portrait_image.header.h=PROFILE_V4_AVATAR_H;portrait_image.data_size=PROFILE_V4_AVATAR_BYTES;
    lv_image_cache_drop(&tactical_panel);lv_image_header_cache_drop(&tactical_panel);
    lv_image_cache_drop(&tactical_footer);lv_image_header_cache_drop(&tactical_footer);
    lv_image_cache_drop(&tactical_signature);lv_image_header_cache_drop(&tactical_signature);
    tactical_panel.data=pixels;tactical_footer.data=pixels?pixels+PROFILE_V4_PANEL_BYTES:NULL;
    tactical_signature.data=pixels?pixels+PROFILE_V4_PANEL_BYTES+240:NULL;
    bool header_id=pixels&&!memcmp(pixels+41504,"IDH1",4);
    tactical_panel.header.h=header_id?124:148;
    tactical_panel.data_size=header_id?20832:PROFILE_V4_PANEL_BYTES;
    brand_id.data=header_id?pixels+20832:NULL;
}
void badge_ui_set_badge_formats(const unsigned versions[5]) {
    for(unsigned i=0;i<PROFILE_BADGE_COUNT;i++) {
        badge_formats[i]=versions[i];
        if(versions[i]>=3&&badge_names[i].data){
            lv_image_cache_drop(&badge_names[i]);lv_image_header_cache_drop(&badge_names[i]);
            badge_names[i].data-=(4*208+88)*2;badge_names[i].header.w=208;
            if(versions[i]>=4){badge_names[i].header.w=84;badge_names[i].header.stride=168;badge_names[i].data_size=32*168;}
        }
    }
}

static void rect(lv_layer_t *l,int x,int y,int w,int h,uint32_t color) {
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);
    d.bg_color=lv_color_hex(color);d.bg_opa=LV_OPA_COVER;
    lv_area_t a={x,y,x+w-1,y+h-1};lv_draw_rect(l,&d,&a);
}
static void line(lv_layer_t *l,int x1,int y1,int x2,int y2,int width,uint32_t color) {
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);
    d.p1=(lv_point_precise_t){x1,y1};d.p2=(lv_point_precise_t){x2,y2};
    d.width=width;d.color=lv_color_hex(color);d.opa=LV_OPA_COVER;lv_draw_line(l,&d);
}
static void circle(lv_layer_t *l,int x,int y,int radius,uint32_t color) {
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);
    d.bg_color=lv_color_hex(color);d.bg_opa=LV_OPA_COVER;d.radius=LV_RADIUS_CIRCLE;
    lv_area_t a={x-radius,y-radius,x+radius,y+radius};lv_draw_rect(l,&d,&a);
}
static void arc(lv_layer_t *l,int x,int y,int radius,int width,int from,int to,uint32_t color) {
    lv_draw_arc_dsc_t d;lv_draw_arc_dsc_init(&d);
    d.center=(lv_point_t){x,y};d.radius=radius;d.width=width;
    d.start_angle=from;d.end_angle=to;d.color=lv_color_hex(color);d.opa=LV_OPA_COVER;
    lv_draw_arc(l,&d);
}
static void cut_panel(lv_layer_t *l,int x,int y,int w,int h,uint32_t fill,uint32_t edge) {
    rect(l,x,y,w,h,fill);
    for(int i=0;i<10;i++) rect(l,x+w-10+i,y+h-1-i,10-i,1,BLACK);
    line(l,x,y,x+w-1,y,1,edge);line(l,x,y,x,y+h-1,1,edge);
    line(l,x+w-1,y,x+w-1,y+h-11,1,edge);
    line(l,x,y+h-1,x+w-11,y+h-1,1,edge);
    line(l,x+w-11,y+h-1,x+w-1,y+h-11,1,edge);
}
/* App marks use the reserved gap below the title and above the description. */
static void game_artwork(lv_layer_t *l,badge_game_art_t art) {
    if(art==BADGE_GAME_ART_MUYU) {
        line(l,89,185,102,176,2,DARK_RED);line(l,102,176,138,176,2,DARK_RED);
        line(l,138,176,151,185,2,DARK_RED);line(l,151,185,139,197,2,RED);
        line(l,139,197,102,197,2,RED);line(l,102,197,89,185,2,RED);
        circle(l,105,183,2,RED);line(l,154,174,167,195,2,RED);circle(l,152,171,3,DARK_RED);
    } else if(art==BADGE_GAME_ART_VOICE) {
        for(int i=0;i<34;i++) {
            int wave=i<5?3:((i*19)%31)*(34-i)/29+2;
            rect(l,53+i*4,185-wave/2,2,wave,i<12?RED:DARK_RED);
        }
    } else if(art==BADGE_GAME_ART_RADIO) {
        line(l,90,177,150,177,2,RED);line(l,90,177,90,200,2,RED);
        line(l,90,200,150,200,2,DARK_RED);line(l,150,177,150,200,2,DARK_RED);
        line(l,95,176,108,168,2,DARK_RED);circle(l,104,188,7,DARK_RED);
        circle(l,104,188,2,RED);line(l,119,185,141,185,2,RED);
        line(l,119,191,136,191,2,DARK_RED);line(l,154,181,159,176,2,DARK_RED);
        line(l,158,187,166,179,2,RED);
    } else if(art==BADGE_GAME_ART_YAO) {
        static const uint8_t broken=0x2a;
        for(unsigned i=0;i<6;i++) {
            int y=169+(int)i*6;
            if(broken&(1u<<i)){line(l,84,y,112,y,2,i<3?RED:DARK_RED);line(l,128,y,156,y,2,i<3?RED:DARK_RED);}
            else line(l,84,y,156,y,2,i<3?RED:DARK_RED);
        }
    } else if(art==BADGE_GAME_ART_CUPS) {
        line(l,76,174,82,192,2,DARK_RED);line(l,82,192,94,199,2,RED);
        line(l,94,199,106,192,2,RED);line(l,106,192,112,174,2,DARK_RED);
        line(l,128,174,134,192,2,DARK_RED);line(l,134,192,146,199,2,RED);
        line(l,146,199,158,192,2,RED);line(l,158,192,164,174,2,DARK_RED);
        line(l,84,174,104,174,2,RED);line(l,136,174,156,174,2,RED);
        circle(l,120,186,3,DARK_RED);
    } else if(art==BADGE_GAME_ART_MUSE) {
        line(l,120,167,120,173,2,DARK_RED);circle(l,120,166,2,RED);
        rect(l,96,173,48,28,DARK_RED);rect(l,101,177,38,19,BLACK);
        circle(l,111,186,3,RED);circle(l,129,186,3,RED);
        line(l,114,193,126,193,2,DARK_RED);
        line(l,92,180,96,180,2,RED);line(l,144,180,148,180,2,RED);
    }
}
static void artwork(lv_event_t *e) {
    lv_layer_t *l=lv_event_get_layer(e);
    if(!((navigation.page==BADGE_HOME||navigation.page==BADGE_QR)&&navigation.badge_mask&&(tactical_layout||!identity.data)))rect(l,14,291,212,1,DARK_RED);
    /* A single restrained sweep confirms page/selection changes; never flashes. */
    if(scan_progress<255) rect(l,14,28,1+scan_progress*211/255,2,WHITE);
    if(navigation.page==BADGE_HOME&&!navigation.badge_mask) {
        cut_panel(l,14,101,212,170,PANEL,DARK_RED);
    } else if((navigation.page==BADGE_HOME||navigation.page==BADGE_QR)&&(tactical_layout||!identity.data)) {
        rect(l,0,30,240,49,RED);
        for(int y=79;y<89;y++)rect(l,0,y,137-(y-79)*12/10,1,RED);
        rect(l,213,38,3,3,on_accent);rect(l,219,38,3,3,DARK_RED);rect(l,225,38,3,3,on_accent);
        line(l,0,293,25,293,1,RED);line(l,25,293,34,288,1,RED);
        line(l,34,288,129,288,1,RED);line(l,129,288,139,293,1,RED);line(l,139,293,239,293,1,RED);
        line(l,222,293,231,285,1,RED);line(l,231,285,239,285,1,RED);
        rect(l,33,287,2,2,RED);rect(l,138,292,2,2,RED);
        if(navigation.page==BADGE_HOME&&!identity.data){circle(l,63,141,24,MUTED);cut_panel(l,17,178,91,73,PANEL,PANEL);}
    } else if(navigation.page==BADGE_HOME||navigation.page==BADGE_QR) {
        /* Portrait access pass: one quiet focal image, engraved corner marks. */
        line(l,14,67,226,67,1,DARK_RED);
        line(l,14,35,42,35,1,DARK_RED);line(l,14,35,14,63,1,DARK_RED);
        line(l,42,35,42,63,1,DARK_RED);line(l,14,63,42,63,1,DARK_RED);
        if(!brand_image.data) {
            arc(l,28,49,9,1,0,360,RED);circle(l,28,49,2,RED);
            line(l,28,38,28,42,1,RED);line(l,28,56,28,60,1,RED);
        }
        if(navigation.page==BADGE_QR)return;
        rect(l,63,71,114,138,PANEL);
        line(l,61,69,73,69,2,RED);line(l,61,69,61,81,2,RED);
        line(l,179,199,179,211,2,RED);line(l,167,211,179,211,2,RED);
        line(l,61,88,61,193,1,DARK_RED);line(l,179,85,179,192,1,DARK_RED);
        if(!identity.data) {
            circle(l,120,119,24,MUTED);
            cut_panel(l,78,155,84,47,DARK_RED,DARK_RED);
        }
    } else if(navigation.page==BADGE_CARDS) {
        for(unsigned i=0;i<navigation.badge_count;i++) {
            int y=BADGE_LIST_TOP+i*BADGE_LIST_STEP;
            cut_panel(l,14,y,212,BADGE_LIST_HEIGHT,BLACK,i==navigation.badge_selected?RED:DARK_RED);
            if(i==navigation.badge_selected)rect(l,17,y+8,3,20,RED);
        }
    } else if(navigation.page==BADGE_TERMINAL) {
        for(unsigned i=0;i<5;i++) {
            bool selected=navigation.home_selected==i;
            cut_panel(l,14,104+(int)i*36,212,32,selected?RED:PANEL,selected?RED:DARK_RED);
            if(selected) {
                rect(l,19,109+(int)i*36,3,22,on_accent);
                line(l,209,114+(int)i*36,216,121+(int)i*36,2,on_accent);
                line(l,216,121+(int)i*36,209,128+(int)i*36,2,on_accent);
            }
        }
    } else if(navigation.page==BADGE_SETTINGS||navigation.page==BADGE_AI_SETTINGS||navigation.page==BADGE_SLEEP_SETTINGS) {
        unsigned count=navigation.page==BADGE_SETTINGS?4:5;
        unsigned selected=navigation.page==BADGE_SETTINGS?navigation.settings_selected:navigation.page==BADGE_SLEEP_SETTINGS?navigation.timeout_selected:navigation.ai_selected;
        unsigned step=navigation.page==BADGE_SETTINGS?34:32,height=navigation.page==BADGE_SETTINGS?28:28;
        for(unsigned i=0;i<count;i++){cut_panel(l,14,108+i*step,212,height,BLACK,i==selected?RED:DARK_RED);if(i==selected)rect(l,18,114+i*step,2,16,RED);}
    } else if(navigation.page==BADGE_GAMES) {
        cut_panel(l,14,110,212,131,PANEL,RED);
        rect(l,14,110,4,131,RED);
        game_artwork(l,selected_game_art);
        cut_panel(l,14,253,212,34,RED,RED);
    } else if(navigation.page==BADGE_WIFI||navigation.page==BADGE_PROFILE) {
        cut_panel(l,14,101,212,170,PANEL,DARK_RED);
    } else if(navigation.page==BADGE_ABOUT_SETTINGS) {
        cut_panel(l,14,108,212,151,PANEL,DARK_RED);
        line(l,27,147,213,147,1,DARK_RED);line(l,27,190,213,190,1,DARK_RED);line(l,27,232,213,232,1,DARK_RED);
    } else {
        cut_panel(l,14,110,212,91,PANEL,DARK_RED);
        for(unsigned i=0;i<5;i++) rect(l,27+i*38,178,32,9,(navigation.page==BADGE_AI_VOLUME_PAGE?i*20<navigation.ai_volume:i<=navigation.brightness)?RED:DARK_RED);
        line(l,14,229,226,229,1,DARK_RED);line(l,14,253,226,253,1,DARK_RED);
    }
}
static lv_obj_t *label(const char *text,int x,int y,int width,uint32_t color,int size) {
    lv_obj_t *o=lv_label_create(screen);
    const lv_font_t *font=size==28?&font_badge_28:size==10?&font_badge_10:((navigation.page==BADGE_AI_SETTINGS||navigation.page==BADGE_AI_VOLUME_PAGE||navigation.page==BADGE_SETTINGS||navigation.page==BADGE_SLEEP_SETTINGS||navigation.page==BADGE_ABOUT_SETTINGS||navigation.page==BADGE_WIFI)?&font_xiaozhi_14:&font_muyu_14);
    lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_CLIP);lv_obj_set_width(o,width);
    lv_obj_set_pos(o,x,y);lv_label_set_text(o,text);return o;
}
static void image_at(lv_image_dsc_t *source,int x,int y,unsigned scale) {
    lv_obj_t *o=lv_image_create(screen);lv_image_set_src(o,source);lv_obj_set_pos(o,x,y);
    if(scale!=256){lv_image_set_pivot(o,0,0);lv_image_set_scale(o,scale);}
}
static void contact_content(void) {
    if(qr_image.data)image_at(&qr_image,32,100,256);
    else {
        lv_obj_t *title=label("还没有上传二维码",20,156,200,WHITE,14);
        lv_obj_t *hint=label("请在配置页上传微信二维码",14,183,212,MUTED,14);
        lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);
    }
}
static void photo_fade(int x,int y,int w,int h,lv_grad_dir_t direction) {
    lv_obj_t *o=lv_obj_create(screen);lv_obj_remove_style_all(o);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_bg_color(o,lv_color_hex(BLACK),0);
    lv_obj_set_style_bg_grad_color(o,lv_color_hex(BLACK),0);lv_obj_set_style_bg_main_opa(o,LV_OPA_TRANSP,0);
    lv_obj_set_style_bg_grad_opa(o,LV_OPA_COVER,0);lv_obj_set_style_bg_grad_dir(o,direction,0);
}
static void scan(void *object,int32_t value) {
    scan_progress=value;lv_obj_invalidate(object);
}
void badge_ui_return_menu(const badge_return_menu_t *menu) {
    if(return_overlay){lv_obj_delete(return_overlay);return_overlay=NULL;}
    if(!menu||!menu->open)return;
    return_overlay=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(return_overlay);lv_obj_set_size(return_overlay,240,320);
    lv_obj_set_style_bg_color(return_overlay,lv_color_hex(BLACK),0);lv_obj_set_style_bg_opa(return_overlay,LV_OPA_80,0);
    lv_obj_remove_flag(return_overlay,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *panel=lv_obj_create(return_overlay);lv_obj_remove_style_all(panel);lv_obj_set_pos(panel,14,68);lv_obj_set_size(panel,212,204);
    lv_obj_set_style_bg_color(panel,lv_color_hex(PANEL),0);lv_obj_set_style_bg_opa(panel,LV_OPA_COVER,0);lv_obj_set_style_border_width(panel,1,0);lv_obj_set_style_border_color(panel,lv_color_hex(RED),0);
    const char *texts[]={"返回到哪里？","返回上一级","回到首页","继续当前页面"};
    for(unsigned i=0;i<4;i++) {
        lv_obj_t *o=lv_label_create(panel);lv_obj_set_style_text_font(o,((navigation.page==BADGE_AI_SETTINGS||navigation.page==BADGE_AI_VOLUME_PAGE||navigation.page==BADGE_SETTINGS)?&font_xiaozhi_14:&font_muyu_14),0);lv_obj_set_size(o,188,27);lv_obj_set_pos(o,12,i==0?12:45+(i-1)*40);
        lv_label_set_text(o,texts[i]);lv_obj_set_style_text_color(o,lv_color_hex(i==0?RED:WHITE),0);
        if(i>0&&i<4){lv_obj_set_style_pad_top(o,4,0);lv_obj_set_style_pad_left(o,8,0);}
        if(i>0&&i<4&&menu->selected==i-1){lv_obj_set_style_bg_color(o,lv_color_hex(RED),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_text_color(o,lv_color_hex(on_accent),0);}
    }
    lv_obj_t *actions=badge_footer_create(return_overlay,BADGE_HINT_RETURN);
    lv_obj_set_style_bg_color(actions,lv_color_hex(BLACK),0);lv_obj_set_style_bg_opa(actions,LV_OPA_COVER,0);
}
static void setup_steps(void) {
    bool network_page=navigation.page==BADGE_WIFI;
    label(network_page?"01  热点网页：资料/二维码":"01  连接下面的工牌热点",22,106,200,WHITE,14);
    wifi_ssid=label("正在开启热点…",22,129,200,RED,14);
    wifi_password=label("",22,151,200,WHITE,14);
    label(network_page?"02  网页打开设置页":"02  手机浏览器打开",22,179,200,WHITE,14);
    wifi_ip=label("http://192.168.4.1",22,201,200,RED,14);
    label(network_page?"03  目标 Wi-Fi 状态":"03  填写资料并保存",22,228,200,WHITE,14);
    wifi_message=label(network_page?"未保存 Wi-Fi":"再点 显示这张工牌",22,250,200,MUTED,14);
    label("无互联网提示仍保持连接",14,275,212,MUTED,14);
    wifi_action=badge_footer_create(screen,"");
}
void badge_ui_create(void) {
    screen=lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen,lv_color_hex(BLACK),0);lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    lv_obj_set_style_pad_all(screen,0,0);lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen,artwork,LV_EVENT_DRAW_MAIN,NULL);
    lv_screen_load(screen);
}
void badge_ui_destroy(void) {
    badge_ui_return_menu(NULL);
    if(screen) {lv_anim_delete(screen,scan);lv_obj_delete(screen);}
    clear_list_names();
    screen=uptime_label=health_label=unit_label=NULL;
    wifi_ssid=wifi_password=wifi_ip=wifi_message=wifi_action=NULL;
}
#ifdef BADGE_CONTROL_DEVICE_PROBE
static unsigned probe_homes,probe_home_badge;
unsigned badge_ui_probe_home_count(void){return probe_homes;}
unsigned badge_ui_probe_home_badge(void){return probe_home_badge;}
#endif
void badge_ui_render(const badge_navigation_t *s,const char *name,const char *description,const char *category,badge_game_art_t game_art) {
#ifdef BADGE_CONTROL_DEVICE_PROBE
    if(s->page==BADGE_HOME){probe_homes++;probe_home_badge=s->active_badge;}
#endif
    navigation=*s;selected_game_art=game_art;lv_obj_set_style_bg_color(screen,lv_color_hex(BLACK),0);lv_anim_delete(screen,scan);lv_obj_clean(screen);
    clear_list_names();
    uptime_label=health_label=unit_label=NULL;
    wifi_ssid=wifi_password=wifi_ip=wifi_message=wifi_action=NULL;
    badge_header_attach(screen);
    badge_header_badge(s->active_badge,s->badge_mask?s->badge_count:0);
    if(s->page==BADGE_HOME&&!s->badge_mask) {
        label("WELCOME",14,38,212,RED,28);label("先设置你的第一张工牌",14,78,212,WHITE,14);
        setup_steps();
    } else if(s->page==BADGE_HOME||s->page==BADGE_QR) {
        if(tactical_layout||!identity.data) {
            if(brand_image.data){image_at(&brand_image,46,43,256);image_at(&logo_image,12,43,208);}
            else{label("ARASAKA",46,44,148,on_accent,14);label("ACCESS PASS",46,65,148,on_accent,10);}
            if(brand_id.data)image_at(&brand_id,60,31,256);
            if(s->page==BADGE_QR)contact_content();
            else if(identity.data){
                image_at(&portrait_image,0,90,320);
                photo_fade(87,90,53,180,LV_GRAD_DIR_HOR);photo_fade(0,226,140,44,LV_GRAD_DIR_VER);
                image_at(&tactical_panel,145,98,256);image_at(&tactical_footer,8,250,256);image_at(&tactical_signature,128,244,256);
            }else{label("未设置",145,101,84,WHITE,14);label("请在网页配置",145,154,84,MUTED,14);}
        } else {
        if(brand_image.data) {
            lv_obj_t *b=lv_image_create(screen);lv_image_set_src(b,&brand_image);lv_obj_set_pos(b,52,34);
            if(!portrait_layout){lv_image_set_pivot(b,0,0);lv_image_set_scale(b,180);}
            lv_obj_t *icon=lv_image_create(screen);lv_image_set_src(icon,&logo_image);lv_obj_set_pos(icon,17,38);
            lv_image_set_pivot(icon,0,0);lv_image_set_scale(icon,192);
        } else {label("ARASAKA",52,35,174,WHITE,14);label("ACCESS PASS",52,53,174,RED,10);}
        if(s->page==BADGE_QR)contact_content();
        else if(identity.data&&portrait_layout) {
            lv_obj_t *photo=lv_image_create(screen);lv_image_set_src(photo,&portrait_image);lv_obj_set_pos(photo,64,72);
            lv_obj_t *details=lv_image_create(screen);lv_image_set_src(details,&identity);lv_obj_set_pos(details,14,213);
        } else if(identity.data) {
            for(unsigned i=0;i<BADGE_REGION_COUNT;i++) {
                const badge_region_t *r=&BADGE_REGIONS[i];
                lv_obj_t *v=lv_image_create(screen);lv_image_set_src(v,&profile_views[i]);
                lv_obj_set_pos(v,r->x,r->y);lv_image_set_pivot(v,0,0);
                if(r->scale!=256)lv_image_set_scale(v,r->scale);
            }
        } else {
            lv_obj_t *display_name=label(BADGE_DISPLAY_NAME,14,216,212,WHITE,14);
            lv_label_set_long_mode(display_name,LV_LABEL_LONG_WRAP);lv_obj_set_height(display_name,30);
            label("网络安全部",14,249,110,MUTED,14);
            label("CREATIVE",139,251,87,RED,10);
            unit_label=label("EMP.ID / NC------",14,269,212,MUTED,10);
        }
        }
        if(s->page==BADGE_QR){
            lv_obj_t *hint=badge_footer_create(screen,s->qr_return==BADGE_HOME?BADGE_HINT_QR_HOME:BADGE_HINT_QR_MENU);
            lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);
        }else{
        badge_footer_create(screen,BADGE_HINT_HOME);
        }
    } else if(s->page==BADGE_CARDS) {
        label("BADGES",14,40,212,WHITE,28);label("选择要显示的工牌",14,78,212,RED,14);
        for(unsigned i=0;i<s->badge_count;i++) {
            int y=BADGE_LIST_TOP+i*BADGE_LIST_STEP;
            char number[8];snprintf(number,sizeof(number),"%u",i+1);label(number,24,y+11,20,MUTED,10);
            if(badge_names[i].data) {
                /* Keep all 32 source rows inside the border, including two-line
                 * names. A parent clip also bounds older, wider profile formats. */
                lv_obj_t *clip=lv_obj_create(screen);lv_obj_remove_style_all(clip);
                lv_obj_set_pos(clip,BADGE_NAME_X,y+2);lv_obj_set_size(clip,BADGE_NAME_WIDTH,BADGE_NAME_HEIGHT);
                lv_obj_remove_flag(clip,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_OVERFLOW_VISIBLE);
                lv_image_dsc_t *tile=make_list_name(i);
                if(tile) {
                    lv_obj_t *name=lv_image_create(clip);lv_image_set_src(name,tile);
                    lv_obj_set_pos(name,0,badge_formats[i]==3?7:0);
                }else label("已设置",BADGE_NAME_X,y+9,BADGE_NAME_WIDTH,MUTED,14);
            }else label("未设置",BADGE_NAME_X,y+9,BADGE_NAME_WIDTH,MUTED,14);
            if(i==s->active_badge)label("当前",183,y+9,40,RED,14);
        }
        badge_footer_create(screen,BADGE_HINT_CARDS);
    } else {
        label(s->page==BADGE_TERMINAL?"TERMINAL":s->page==BADGE_GAMES?"SOFTWARE":s->page==BADGE_PROFILE?"PERSONNEL":s->page==BADGE_WIFI?"NETWORK":(s->page==BADGE_AI_SETTINGS||s->page==BADGE_AI_VOLUME_PAGE)?"XIAOZHI AI":s->page==BADGE_ABOUT_SETTINGS?"ABOUT":"SYSTEM",14,40,212,WHITE,28);
        label(s->page==BADGE_TERMINAL?"员工终端":s->page==BADGE_GAMES?"小程序":s->page==BADGE_PROFILE?"工牌资料":s->page==BADGE_WIFI?"手机配置":s->page==BADGE_SLEEP_SETTINGS?"自动息屏":(s->page==BADGE_AI_SETTINGS||s->page==BADGE_AI_VOLUME_PAGE)?"小智 AI":s->page==BADGE_ABOUT_SETTINGS?"设备信息":"设备设置",14,81,212,RED,14);
        if(s->page==BADGE_TERMINAL) {
            static const char *names[]={"小智 AI","小程序","我的二维码","系统设置","返回工牌"};
            static const char *codes[]={"01  XIAOZHI AI","02  MINI APPS","03  MY QR CODE","04  CONFIGURATION","05  PERSONNEL ID"};
            for(unsigned i=0;i<5;i++) {
                label(codes[i],30,104+i*36,177,s->home_selected==i?on_accent:MUTED,10);
                label(names[i],30,118+i*36,174,s->home_selected==i?on_accent:WHITE,14);
            }

            badge_footer_create(screen,BADGE_HINT_MENU);
        } else if(s->page==BADGE_GAMES) {
            char index[20];snprintf(index,sizeof(index),"%02u",(unsigned)(s->game_count?s->game_selected+1:0));
            label(index,27,122,57,RED,28);
            label(category,89,122,127,MUTED,10);
            label(name,89,143,127,WHITE,14);
            label(description,27,213,189,MUTED,14);
            char count[36];snprintf(count,sizeof(count),"%02u/%02u",(unsigned)(s->game_count?s->game_selected+1:0),(unsigned)s->game_count);
            label(count,94,263,70,on_accent,14);
            badge_footer_create(screen,BADGE_HINT_APPS);
        } else if(s->page==BADGE_WIFI) {
            setup_steps();
        } else if(s->page==BADGE_PROFILE) {
            setup_steps();
        } else if(s->page==BADGE_SETTINGS){
            static const char *items[]={"屏幕亮度","网络设置","自动息屏","关于设备"};
            for(unsigned i=0;i<4;i++)label(items[i],29,114+i*34,187,s->settings_selected==i?RED:WHITE,14);
            label("AI PASSPORT / SYSTEM",14,274,212,MUTED,10);
            badge_footer_create(screen,BADGE_HINT_SETTINGS);
        } else if(s->page==BADGE_SLEEP_SETTINGS){
            static const char *items[]={"保持常亮","30 秒","1 分钟","2 分钟","5 分钟"};
            for(unsigned i=0;i<5;i++){
                label(items[i],29,114+i*32,133,s->timeout_selected==i?RED:WHITE,14);
                if(i==s->screen_timeout)label("当前",175,114+i*32,42,MUTED,14);
            }
            health_label=label("自动息屏 · 对话时保持亮屏",14,276,212,MUTED,14);
            badge_footer_create(screen,BADGE_HINT_SELECT);
        } else if(s->page==BADGE_AI_SETTINGS){
            label("开始对话",29,114,187,s->ai_selected==0?RED:WHITE,14);
            label(s->ai_enabled?"后台唤醒  已开启":"后台唤醒  已关闭",29,146,187,s->ai_selected==1?RED:WHITE,14);
            char volume[48];snprintf(volume,sizeof(volume),"回复音量  %u%%",s->ai_volume);
            label(volume,29,178,187,s->ai_selected==2?RED:WHITE,14);
            label("唤醒词与状态",29,210,187,s->ai_selected==3?RED:WHITE,14);
            label(s->ai_style==3?"表情风格  公路之王":s->ai_style==2?"表情风格  女生头像":s->ai_style==1?"表情风格  男生头像":"表情风格  圆萌精灵",29,242,187,s->ai_selected==4?RED:WHITE,14);
            label(ai_status,14,276,212,MUTED,14);
            badge_footer_create(screen,BADGE_HINT_SELECT);
        } else if(s->page==BADGE_AI_VOLUME_PAGE){
            label("小智回复音量",27,121,186,MUTED,14);
            char v[16];snprintf(v,sizeof(v),"%u%%",s->ai_volume);label(v,27,145,180,WHITE,28);
            label("仅调整小智，其他程序独立",14,236,212,MUTED,14);
            badge_footer_create(screen,BADGE_HINT_VOLUME);
        } else if(s->page==BADGE_ABOUT_SETTINGS){
            label("固件版本",27,119,86,MUTED,14);
            label("V" BADGE_VERSION,27,137,174,WHITE,14);
            label("硬件平台",27,162,86,MUTED,14);
            label("ESP32-C3",27,180,174,WHITE,14);
            label("存储容量",27,205,86,MUTED,14);
            label("8MB FLASH",27,223,174,WHITE,14);
            label("AI PASSPORT",27,249,174,RED,14);
            badge_footer_create(screen,BADGE_HINT_SELECT);
        } else {
            label("屏幕亮度",27,122,109,MUTED,14);
            char value[20];snprintf(value,sizeof(value),"%u%%",(s->brightness+1)*20);
            label(value,135,127,83,WHITE,28);
            label("BACKLIGHT / PWM",27,153,110,MUTED,10);
            label("上/下 调整亮度",14,212,212,MUTED,14);
            uptime_label=label("UPTIME 00:00:00",14,236,212,WHITE,10);
            health_label=label("",14,263,212,RED,14);
            badge_footer_create(screen,BADGE_HINT_BRIGHTNESS);
        }
    }
    lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,screen);lv_anim_set_exec_cb(&a,scan);
    lv_anim_set_values(&a,0,255);lv_anim_set_duration(&a,160);lv_anim_start(&a);
    lv_obj_invalidate(screen);
}
void badge_ui_status(const char *unit,int battery,uint32_t seconds,bool storage,bool input) {
    badge_header_battery(battery);
    if(!screen) return;
    if(unit_label) lv_label_set_text_fmt(unit_label,"EMP.ID / NC-%s",unit);
    if(uptime_label) lv_label_set_text_fmt(uptime_label,"UPTIME %02lu:%02lu:%02lu",(unsigned long)(seconds/3600),(unsigned long)((seconds/60)%60),(unsigned long)(seconds%60));
    if(health_label) lv_label_set_text(health_label,!input?"按键不可用":storage?(navigation.page==BADGE_SLEEP_SETTINGS?"对话时保持亮屏":"设置存储正常"):"设置未保存");
}

void badge_ui_network(bool active,bool connected,const char *sta_ssid,const char *ap_ssid,const char *password,const char *ip,const char *message) {
    (void)ip;
    badge_header_network(connected);
    if(!wifi_ssid)return;
    lv_label_set_text(wifi_ssid,active?ap_ssid:"热点未开启");
    if(active)lv_label_set_text_fmt(wifi_password,"密码：%s",password);else lv_label_set_text(wifi_password,"热点已关闭");
    lv_label_set_text(wifi_ip,"http://192.168.4.1");
    if(navigation.page==BADGE_WIFI) {
        if(sta_ssid&&sta_ssid[0]) {
            if(connected) {
                lv_label_set_text_fmt(wifi_message,"已连接：%s",sta_ssid);
            } else if(message&&message[0]&&strcmp(message,"已连接")!=0&&strcmp(message,"Connected")!=0) {
                lv_label_set_text_fmt(wifi_message,"%s",message);
            } else {
                lv_label_set_text_fmt(wifi_message,"正在连接：%s",sta_ssid);
            }
        }
        else lv_label_set_text(wifi_message,"未保存 Wi-Fi");
    } else {
        lv_label_set_text(wifi_message,(message&&message[0])?message:"再点 显示这张工牌");
    }
    if(navigation.page==BADGE_HOME)lv_label_set_text(wifi_action,active?BADGE_HINT_SETUP_ON:BADGE_HINT_SETUP_OFF);
    else lv_label_set_text(wifi_action,active?BADGE_HINT_AP_ON:BADGE_HINT_AP_OFF);
}
