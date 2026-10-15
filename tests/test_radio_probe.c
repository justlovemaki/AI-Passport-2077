#include "radio_probe.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    radio_probe_request_t request={0};radio_probe_cancel();
    assert(!radio_probe_busy()&&!radio_probe_take(&request));
    assert(!radio_probe_reserve(false,true,false,20,1024));
    assert(!radio_probe_reserve(true,false,false,20,1024));
    assert(!radio_probe_reserve(true,true,true,20,1024));
    assert(!radio_probe_reserve(true,true,false,4,1024));
    assert(!radio_probe_reserve(true,true,false,21,1024));
    assert(!radio_probe_reserve(true,true,false,20,0));
    assert(!radio_probe_reserve(true,true,false,20,2048));
    assert(!radio_probe_busy());
    for(unsigned bytes=256;bytes<=1024;bytes*=4){
        assert(radio_probe_valid_read_size(bytes));
        assert(radio_probe_reserve(true,true,false,20,bytes));
        assert(radio_probe_busy()&&!radio_probe_take(NULL));
        assert(!radio_probe_reserve(true,true,false,5,bytes));
        assert(radio_probe_take(&request)&&request.seconds==20&&request.read_bytes==bytes);
        assert(radio_probe_busy()&&!radio_probe_take(&request));
        assert(!radio_probe_reserve(true,true,false,5,bytes));
        radio_probe_finish();assert(!radio_probe_busy());
    }
    assert(radio_probe_reserve(true,true,false,5,256));radio_probe_cancel();
    assert(!radio_probe_busy()&&!radio_probe_take(&request));
    assert(radio_probe_reserve(true,true,false,5,1024));assert(radio_probe_take(&request));
    radio_probe_cancel();radio_probe_finish();assert(!radio_probe_busy());
    puts("Radio receive probe: paused/available admission, duration/read-size limits, single reservation, take/finish and cancellation PASS");
}
