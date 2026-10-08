#include "game_registry.h"
#include "muyu_app.h"
#include "voice_app.h"
#include "radio_app.h"
#include "xiaozhi_app.h"
#include "voice_catalog.h"
#include "yao_app.h"
#include "shengbei_app.h"
#include "muse_app.h"
const badge_game_t BADGE_GAMES_REGISTRY[] = {
    {.id="zen-muyu", .description="轻敲一下，放空片刻", .category="ZEN / RELAX", .artwork=BADGE_GAME_ART_MUYU,
     .lifecycle={.name="敲木鱼", .enter=demo_muyu_enter, .exit=demo_muyu_exit,
        .key=demo_muyu_key, .start=demo_muyu_start, .stop=demo_muyu_stop}},
    {.id="voice-keychain", .description=VOICE_SUMMARY, .category="VOICE / SOUNDBOARD", .artwork=BADGE_GAME_ART_VOICE, .back=demo_voice_back,
     .lifecycle={.name="音效钥匙扣", .enter=demo_voice_enter, .exit=demo_voice_exit,
        .key=demo_voice_key, .start=demo_voice_start, .stop=demo_voice_stop}},
    {.id="leo-radio", .description="城市电台 / 在线收听", .category="LEO / INTERNET RADIO", .artwork=BADGE_GAME_ART_RADIO, .back=demo_radio_back,
     .lifecycle={.name="城市电台", .enter=demo_radio_enter, .exit=demo_radio_exit,
        .key=demo_radio_key, .start=demo_radio_start, .stop=demo_radio_stop}},
    {.id="cyber-yao", .description="六爻起卦 / 小智解读", .category="CYBER YAO", .artwork=BADGE_GAME_ART_YAO,
     .lifecycle={.name="赛博摇卦", .enter=demo_yao_enter, .exit=demo_yao_exit,
        .key=demo_yao_key, .start=demo_yao_start, .stop=demo_yao_stop}},
    {.id="holy-cup", .description="默念问题 / 掷杯决策", .category="ORACLE / DECISION", .artwork=BADGE_GAME_ART_CUPS,
     .lifecycle={.name="圣杯决策", .enter=demo_shengbei_enter, .exit=demo_shengbei_exit,
        .key=demo_shengbei_key, .start=demo_shengbei_start, .stop=demo_shengbei_stop}},
    {.id="muse", .description="语音输入 / 文字回复", .category="MUSE / VOICE AGENT", .artwork=BADGE_GAME_ART_MUSE,
     .lifecycle={.name="Muse", .enter=demo_muse_enter, .exit=demo_muse_exit,
        .key=demo_muse_key, .start=demo_muse_start, .stop=demo_muse_stop}},

};
const size_t BADGE_GAME_COUNT=sizeof(BADGE_GAMES_REGISTRY)/sizeof(BADGE_GAMES_REGISTRY[0]);
