#include "badge_navigation.h"
#include <assert.h>
#include <stdio.h>
static void test_return_policy(void) {
    badge_navigation_t s;
    badge_navigation_init(&s,6);
    badge_return_context_t context={0};
    for(unsigned page=BADGE_HOME;page<=BADGE_ABOUT_SETTINGS;page++) {
        s.page=(badge_page_t)page;
        badge_navigation_t before=s;
        badge_return_plan_t plan=badge_return_plan(&s,&context);
        assert(s.page==before.page&&s.game_selected==before.game_selected);
        badge_return_menu_t menu={0};
        if(page!=BADGE_PLAYING&&page!=BADGE_AI_CHAT) {
            assert(badge_return_menu_handle(&menu,&plan,BADGE_BACK)==BADGE_BACK&&!menu.open);
            badge_navigation_handle(&s,BADGE_BACK);
            assert(s.page==plan.destination);
        }
    }
    s.page=BADGE_PLAYING;s.playing_return=BADGE_GAMES;s.game_selected=2;
    context.internal=true;
    badge_return_plan_t plan=badge_return_plan(&s,&context);
    badge_return_menu_t menu={0};
    assert(badge_return_menu_handle(&menu,&plan,BADGE_BACK)==BADGE_BACK&&!menu.open);
    context.internal=false;plan=badge_return_plan(&s,&context);
    assert(badge_return_menu_handle(&menu,&plan,BADGE_BACK)==BADGE_NONE&&menu.open);
    assert(menu.count==3&&menu.selected==0&&s.page==BADGE_PLAYING);
    assert(badge_return_menu_handle(&menu,&plan,BADGE_OK)==BADGE_BACK&&!menu.open);
    badge_return_menu_handle(&menu,&plan,BADGE_BACK);
    badge_return_menu_handle(&menu,&plan,BADGE_DOWN);
    assert(badge_return_menu_handle(&menu,&plan,BADGE_OK)==BADGE_GO_HOME&&!menu.open);
    badge_return_menu_handle(&menu,&plan,BADGE_BACK);
    badge_return_menu_handle(&menu,&plan,BADGE_UP);
    assert(menu.selected==2);
    assert(badge_return_menu_handle(&menu,&plan,BADGE_OK)==BADGE_NONE&&!menu.open);
    badge_return_menu_handle(&menu,&plan,BADGE_BACK);
    assert(badge_return_menu_handle(&menu,&plan,BADGE_BACK)==BADGE_NONE&&!menu.open);
    /* A quick launch already returns Home: no duplicate destinations. */
    s.playing_return=BADGE_HOME;plan=badge_return_plan(&s,&context);
    assert(badge_return_menu_handle(&menu,&plan,BADGE_BACK)==BADGE_BACK&&!menu.open);
    context.confirm=true;plan=badge_return_plan(&s,&context);
    assert(badge_return_menu_handle(&menu,&plan,BADGE_BACK)==BADGE_NONE&&menu.open);
    assert(menu.count==2&&menu.selected==1); /* Safe default for cancellation. */
    badge_return_menu_handle(&menu,&plan,BADGE_UP);
    assert(badge_return_menu_handle(&menu,&plan,BADGE_OK)==BADGE_BACK);
    /* Cross-app interpretation preserves its return to the hexagram. */
    s.page=BADGE_AI_CHAT;s.ai_return=BADGE_PLAYING;
    plan=badge_return_plan(&s,&context);
    badge_return_menu_handle(&menu,&plan,BADGE_BACK);
    assert(menu.count==3&&menu.selected==2&&menu.plan.destination==BADGE_PLAYING);
    context.confirm=false;
    badge_return_plan_t completed=badge_return_plan(&s,&context);
    badge_return_menu_handle(&menu,&completed,BADGE_UP);
    badge_return_menu_handle(&menu,&completed,BADGE_UP);
    assert(badge_return_menu_handle(&menu,&completed,BADGE_OK)==BADGE_BACK);
    /* Changing the origin, app or internal layer invalidates an old chooser. */
    s.page=BADGE_PLAYING;s.playing_return=BADGE_GAMES;plan=badge_return_plan(&s,&context);
    for(unsigned change=0;change<4;change++) {
        badge_return_menu_handle(&menu,&plan,BADGE_BACK);
        badge_return_plan_t stale=plan;
        if(change==0)stale.source=BADGE_SETTINGS;
        if(change==1)stale.destination=BADGE_HOME;
        if(change==2)stale.game_selected++;
        if(change==3)stale.internal=true;
        assert(badge_return_menu_handle(&menu,&stale,BADGE_OK)==BADGE_NONE&&!menu.open);
    }
    badge_return_menu_failed(&menu,&plan);
    assert(menu.open&&!menu.stopping&&menu.count==3&&menu.selected==2);
    assert(badge_return_menu_handle(&menu,&plan,BADGE_OK)==BADGE_NONE&&!menu.open);
    badge_return_menu_failed(&menu,&plan);
    menu.stopping=true;
    assert(badge_return_menu_handle(&menu,&plan,BADGE_OK)==BADGE_NONE&&menu.open);
}
int main(void) {
    test_return_policy();
    badge_navigation_t s;
    badge_navigation_init(&s,1);
    assert(s.screen_timeout==2);
    s.page=BADGE_SETTINGS;s.settings_selected=2;
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_REDRAW&&s.page==BADGE_SLEEP_SETTINGS);
    assert(s.timeout_selected==2);
    badge_navigation_handle(&s,BADGE_UP);
    badge_navigation_handle(&s,BADGE_BACK);
    assert(s.page==BADGE_SETTINGS&&s.screen_timeout==2); /* Cancel preserves saved choice. */
    badge_navigation_handle(&s,BADGE_OK);
    badge_navigation_handle(&s,BADGE_DOWN);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_SLEEP_SAVE&&s.screen_timeout==3);
    badge_navigation_handle(&s,BADGE_OK);
    for(int i=0;i<2;i++)badge_navigation_handle(&s,BADGE_DOWN);
    assert(s.timeout_selected==0);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_SLEEP_SAVE&&s.screen_timeout==0);
    badge_navigation_handle(&s,BADGE_DOWN);assert(s.settings_selected==3);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_REDRAW&&s.page==BADGE_ABOUT_SETTINGS);
    badge_navigation_handle(&s,BADGE_OK);assert(s.page==BADGE_SETTINGS);
    badge_navigation_handle(&s,BADGE_DOWN);assert(s.settings_selected==0);
    badge_navigation_handle(&s,BADGE_UP);assert(s.settings_selected==3);
    badge_navigation_handle(&s,BADGE_UP);assert(s.settings_selected==2);
    badge_navigation_init(&s,1);
    assert(s.page==BADGE_HOME);
    assert(badge_ok_gesture(BADGE_HOME,false,false)==BADGE_NONE); // Press/release do not open Menu.
    assert(badge_ok_gesture(BADGE_HOME,false,true)==BADGE_OK); // Short/double click is Menu, not Switch.
    assert(badge_ok_gesture(BADGE_HOME,true,false)==BADGE_SWITCH);
    assert(badge_ok_gesture(BADGE_CARDS,true,false)==BADGE_BACK);
    assert(badge_ok_gesture(BADGE_PLAYING,true,false)==BADGE_BACK);
    assert(badge_navigation_handle(&s,BADGE_NONE)==BADGE_IDLE);
    badge_navigation_handle(&s,BADGE_OK);assert(s.page==BADGE_TERMINAL);
    badge_navigation_handle(&s,BADGE_UP);assert(s.home_selected==4);
    badge_navigation_handle(&s,BADGE_OK);assert(s.page==BADGE_HOME);
    /* Home shortcut keeps terminal selection and never launches a game. */
    size_t selected=s.home_selected;
    const badge_input_t exits[]={BADGE_DOWN,BADGE_OK,BADGE_BACK};
    for(unsigned i=0;i<3;i++) {
        assert(badge_navigation_handle(&s,BADGE_DOWN)==BADGE_REDRAW);
        assert(s.page==BADGE_QR && s.qr_return==BADGE_HOME);
        assert(badge_navigation_handle(&s,BADGE_UP)==BADGE_IDLE);
        assert(badge_navigation_handle(&s,exits[i])==BADGE_REDRAW);
        assert(s.page==BADGE_HOME && s.home_selected==selected);
    }
    badge_navigation_handle(&s,BADGE_OK);s.home_selected=1;
    badge_navigation_handle(&s,BADGE_OK);assert(s.page==BADGE_GAMES);
    badge_navigation_handle(&s,BADGE_DOWN);assert(s.game_selected==0);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_LAUNCH);
    assert(badge_navigation_handle(&s,BADGE_UP)==BADGE_IDLE);
    assert(badge_navigation_handle(&s,BADGE_BACK)==BADGE_STOP);
    assert(s.page==BADGE_GAMES);
    badge_navigation_handle(&s,BADGE_BACK);assert(s.page==BADGE_TERMINAL);
    badge_navigation_handle(&s,BADGE_BACK);assert(s.page==BADGE_HOME);
    assert(badge_navigation_handle(&s,BADGE_UP)==BADGE_REDRAW&&s.page==BADGE_GAMES);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_LAUNCH);
    assert(badge_navigation_handle(&s,BADGE_BACK)==BADGE_STOP&&s.page==BADGE_GAMES);
    badge_navigation_handle(&s,BADGE_BACK);assert(s.page==BADGE_HOME);
    badge_navigation_handle(&s,BADGE_OK);s.home_selected=3;
    badge_navigation_handle(&s,BADGE_OK);assert(s.page==BADGE_SETTINGS);
    badge_navigation_handle(&s,BADGE_OK);assert(s.page==BADGE_DISPLAY_SETTINGS);
    for(int i=0;i<20;i++) badge_navigation_handle(&s,BADGE_DOWN);
    assert(s.brightness==0);
    for(int i=0;i<20;i++) badge_navigation_handle(&s,BADGE_UP);
    assert(s.brightness==4);
    badge_navigation_handle(&s,BADGE_OK);assert(s.page==BADGE_SETTINGS);
    badge_navigation_handle(&s,BADGE_DOWN);
    badge_navigation_handle(&s,BADGE_OK);assert(s.page==BADGE_WIFI);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_IDLE);
    assert(badge_navigation_handle(&s,BADGE_DOWN)==BADGE_IDLE);
    badge_navigation_handle(&s,BADGE_BACK);assert(s.page==BADGE_SETTINGS);
    badge_navigation_handle(&s,BADGE_BACK);assert(s.page==BADGE_TERMINAL);
    assert(s.home_selected==3);
    badge_navigation_handle(&s,BADGE_DOWN);assert(s.home_selected==4);
    badge_navigation_handle(&s,BADGE_DOWN);assert(s.home_selected==0);
    badge_navigation_handle(&s,BADGE_DOWN);assert(s.home_selected==1);
    badge_navigation_handle(&s,BADGE_DOWN);assert(s.home_selected==2);
    badge_navigation_handle(&s,BADGE_OK);assert(s.page==BADGE_QR && s.qr_return==BADGE_TERMINAL);
    badge_navigation_handle(&s,BADGE_DOWN);assert(s.page==BADGE_TERMINAL && s.home_selected==2);
    badge_navigation_handle(&s,BADGE_OK);badge_navigation_handle(&s,BADGE_BACK);assert(s.page==BADGE_TERMINAL);
    badge_navigation_handle(&s,BADGE_OK);badge_navigation_handle(&s,BADGE_OK);assert(s.page==BADGE_TERMINAL);
    badge_navigation_handle(&s,BADGE_DOWN);assert(s.home_selected==3);
    badge_navigation_handle(&s,BADGE_OK);assert(s.page==BADGE_SETTINGS);
    badge_navigation_handle(&s,BADGE_BACK);assert(s.page==BADGE_TERMINAL);
    badge_navigation_init(&s,7);badge_navigation_handle(&s,BADGE_OK);s.home_selected=1;badge_navigation_handle(&s,BADGE_OK);
    badge_navigation_handle(&s,BADGE_UP);assert(s.game_selected==6);
    badge_navigation_handle(&s,BADGE_DOWN);assert(s.game_selected==0);
    for(int i=0;i<3;i++) badge_navigation_handle(&s,BADGE_DOWN);
    assert(s.game_selected==3);
    badge_navigation_handle(&s,BADGE_OK);badge_navigation_handle(&s,BADGE_BACK);
    assert(s.game_selected==3);
    badge_navigation_init(&s,0);badge_navigation_handle(&s,BADGE_OK);s.home_selected=1;badge_navigation_handle(&s,BADGE_OK);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_IDLE);
    assert(badge_navigation_handle(&s,BADGE_UP)==BADGE_IDLE);
    assert(s.page==BADGE_GAMES);
    badge_navigation_init(&s,1);badge_navigation_badges(&s,5,2,0x15);
    assert(badge_navigation_handle(&s,badge_ok_gesture(s.page,true,false))==BADGE_REDRAW&&s.page==BADGE_CARDS&&s.badge_selected==2);
    badge_navigation_handle(&s,BADGE_DOWN);assert(s.badge_selected==3);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_IDLE&&s.page==BADGE_CARDS);
    badge_navigation_handle(&s,BADGE_DOWN);assert(s.badge_selected==4);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_SELECT&&s.page==BADGE_HOME);
    badge_navigation_badges(&s,5,4,0x15);badge_navigation_handle(&s,BADGE_SWITCH);
    badge_navigation_handle(&s,BADGE_DOWN);assert(s.badge_selected==0);
    badge_navigation_handle(&s,BADGE_UP);assert(s.badge_selected==4);
    badge_navigation_handle(&s,BADGE_BACK);assert(s.page==BADGE_HOME);
    badge_navigation_handle(&s,BADGE_DOWN);assert(s.page==BADGE_QR);
    badge_navigation_handle(&s,BADGE_OK);assert(s.page==BADGE_HOME);
    /* Empty first boot shows setup; a configured inactive badge is still a badge. */
    badge_navigation_init(&s,2);badge_navigation_badges(&s,5,0,0);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_WIFI_START&&s.page==BADGE_HOME);
    assert(badge_navigation_handle(&s,BADGE_SWITCH)==BADGE_WIFI_START);
    badge_navigation_handle(&s,BADGE_DOWN);assert(s.page==BADGE_TERMINAL);
    badge_navigation_handle(&s,BADGE_GO_HOME);assert(s.page==BADGE_HOME);
    for(unsigned i=0;i<5;i++){badge_navigation_badges(&s,5,0,1u<<i);assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_REDRAW&&s.page==BADGE_TERMINAL);badge_navigation_handle(&s,BADGE_GO_HOME);}
    s.page=BADGE_PROFILE;assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_WIFI_TOGGLE&&s.page==BADGE_PROFILE);
    badge_navigation_handle(&s,BADGE_BACK);assert(s.page==BADGE_TERMINAL);
    /* Long Up launches a resolved app only on a configured Home screen. */
    assert(badge_up_gesture(BADGE_HOME,true,true,false)==BADGE_AI);
    assert(badge_up_gesture(BADGE_HOME,true,false,true)==BADGE_UP);
    assert(badge_up_gesture(BADGE_HOME,true,false,false)==BADGE_NONE);
    assert(badge_up_gesture(BADGE_HOME,false,true,false)==BADGE_NONE);
    assert(badge_up_gesture(BADGE_PLAYING,true,true,false)==BADGE_NONE);
    badge_navigation_init(&s,4);s.game_selected=1;
    assert(badge_navigation_quick_launch(&s,4)==BADGE_IDLE&&s.page==BADGE_HOME&&s.game_selected==1);
    s.badge_mask=0;assert(badge_navigation_quick_launch(&s,3)==BADGE_IDLE);
    s.badge_mask=1;s.page=BADGE_GAMES;assert(badge_navigation_quick_launch(&s,3)==BADGE_IDLE);
    s.page=BADGE_HOME;assert(badge_navigation_quick_launch(&s,3)==BADGE_LAUNCH);
    assert(s.page==BADGE_PLAYING&&s.game_selected==3&&s.playing_return==BADGE_HOME);
    assert(badge_navigation_quick_launch(&s,3)==BADGE_IDLE);
    assert(badge_navigation_handle(&s,BADGE_BACK)==BADGE_STOP&&s.page==BADGE_HOME);
    badge_navigation_handle(&s,BADGE_UP);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_LAUNCH&&s.playing_return==BADGE_GAMES);
    assert(badge_navigation_handle(&s,BADGE_BACK)==BADGE_STOP&&s.page==BADGE_GAMES);
    s.page=BADGE_HOME;badge_navigation_quick_launch(&s,3);
    assert(badge_navigation_handle(&s,BADGE_GO_HOME)==BADGE_STOP&&s.page==BADGE_HOME);
    /* AI is a system destination, independent of the mini-app registry. */
    badge_navigation_init(&s,3);s.page=BADGE_TERMINAL;s.home_selected=0;
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_REDRAW&&s.page==BADGE_AI_SETTINGS);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_AI_START&&s.page==BADGE_AI_CHAT);
    assert(badge_navigation_handle(&s,BADGE_BACK)==BADGE_AI_STOP&&s.page==BADGE_AI_SETTINGS);
    badge_navigation_handle(&s,BADGE_DOWN);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_AI_TOGGLE);
    badge_navigation_handle(&s,BADGE_DOWN);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_REDRAW&&s.page==BADGE_AI_VOLUME_PAGE);
    s.ai_volume=100;assert(badge_navigation_handle(&s,BADGE_UP)==BADGE_AI_VOLUME&&s.ai_volume==100);
    s.ai_volume=0;assert(badge_navigation_handle(&s,BADGE_DOWN)==BADGE_AI_VOLUME&&s.ai_volume==0);
    assert(badge_navigation_handle(&s,BADGE_UP)==BADGE_AI_VOLUME&&s.ai_volume==10);
    badge_navigation_handle(&s,BADGE_BACK);assert(s.page==BADGE_AI_SETTINGS);
    s.ai_selected=0;badge_navigation_handle(&s,BADGE_UP);assert(s.ai_selected==4);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_AI_STYLE&&s.ai_style==1);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_AI_STYLE&&s.ai_style==2);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_AI_STYLE&&s.ai_style==3);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_AI_STYLE&&s.ai_style==0);
    badge_navigation_handle(&s,BADGE_DOWN);assert(s.ai_selected==0);
    for(unsigned i=0;i<5;i++)badge_navigation_handle(&s,BADGE_DOWN);
    assert(s.ai_selected==0);
    badge_navigation_handle(&s,BADGE_BACK);assert(s.page==BADGE_TERMINAL);
    s.page=BADGE_HOME;
    assert(badge_navigation_handle(&s,BADGE_AI)==BADGE_AI_START&&s.page==BADGE_AI_CHAT);
    assert(badge_navigation_handle(&s,BADGE_GO_HOME)==BADGE_AI_STOP&&s.page==BADGE_HOME);
    /* Conversation shortcuts do not change volume or route through home. */
    assert(badge_ai_long_gesture(BADGE_AI_CHAT,true,false,true)==BADGE_AI_CYCLE_STYLE);
    assert(badge_ai_long_gesture(BADGE_AI_CHAT,false,true,true)==BADGE_AI_OPEN_SETTINGS);
    assert(badge_ai_long_gesture(BADGE_AI_CHAT,true,false,false)==BADGE_NONE);
    assert(badge_ai_long_gesture(BADGE_HOME,true,false,true)==BADGE_NONE);
    s.page=BADGE_AI_CHAT;s.ai_return=BADGE_HOME;s.ai_volume=70;s.ai_style=0;
    for(unsigned i=0;i<4;i++){
        assert(badge_navigation_handle(&s,BADGE_AI_CYCLE_STYLE)==BADGE_AI_STYLE);
        assert(s.page==BADGE_AI_CHAT&&s.ai_style==(i+1)%4&&s.ai_volume==70);
    }
    assert(badge_navigation_handle(&s,BADGE_AI_OPEN_SETTINGS)==BADGE_AI_STOP);
    assert(s.page==BADGE_AI_SETTINGS&&s.ai_settings_return==BADGE_AI_CHAT&&s.ai_return==BADGE_HOME);
    s.ai_selected=4;assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_AI_STYLE);
    assert(badge_navigation_handle(&s,BADGE_BACK)==BADGE_AI_START&&s.page==BADGE_AI_CHAT);
    assert(badge_navigation_handle(&s,BADGE_BACK)==BADGE_AI_STOP&&s.page==BADGE_HOME);
    s.page=BADGE_AI_CHAT;s.ai_return=BADGE_HOME;
    badge_navigation_handle(&s,BADGE_AI_OPEN_SETTINGS);
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_AI_START&&s.ai_return==BADGE_HOME);
    badge_navigation_handle(&s,BADGE_AI_OPEN_SETTINGS);badge_navigation_handle(&s,BADGE_GO_HOME);
    s.page=BADGE_TERMINAL;s.home_selected=0;badge_navigation_handle(&s,BADGE_OK);
    assert(badge_navigation_handle(&s,BADGE_BACK)==BADGE_REDRAW&&s.page==BADGE_TERMINAL);
    badge_navigation_init(&s,1);badge_navigation_badges(&s,5,0,0x1f);
    badge_navigation_handle(&s,BADGE_OK);s.home_selected=0;
    assert(badge_navigation_handle(&s,BADGE_OK)==BADGE_REDRAW&&s.page==BADGE_AI_SETTINGS);
    assert(s.ai_settings_return==BADGE_TERMINAL);
    assert(badge_navigation_handle(&s,BADGE_BACK)==BADGE_REDRAW&&s.page==BADGE_TERMINAL);
    puts("Badge navigation: PASS (first setup, actual parents, contextual return plans, safe game stop, conversation shortcuts and return paths)");
}
