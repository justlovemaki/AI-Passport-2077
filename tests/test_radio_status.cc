#include "radio_ui.h"
#include "radio_diagnostics.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
int main(){
    radio_ui_reset_state();
    assert(radio_diagnostics_playback_state()==static_cast<unsigned>(RadioPlaybackState::Stopped));
    for(auto next:{RadioPlaybackState::Connecting,RadioPlaybackState::Buffering,RadioPlaybackState::Playing,RadioPlaybackState::Buffering,RadioPlaybackState::Playing,RadioPlaybackState::Stopped,RadioPlaybackState::Error}){
        radio_ui_set_playback(next,"diagnostic state");
        const auto snapshot=radio_ui_snapshot();
        assert(snapshot.state==next&&!std::strcmp(snapshot.detail,"diagnostic state"));
        assert(radio_diagnostics_playback_state()==static_cast<unsigned>(snapshot.state));
        radio_ui_set_volume(25);radio_ui_set_network(true);
        assert(radio_diagnostics_playback_state()==static_cast<unsigned>(next));
    }
    puts("Radio status: C diagnostic view follows real buffering/playing/pause/error state without LVGL PASS");
}
