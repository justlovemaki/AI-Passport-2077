#include "badge_return_input.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    badge_return_input_t s={0};
    assert(!badge_return_input_event(&s,2,true,false)); /* Hold OK. */
    badge_return_input_barrier(&s,2); /* Open chooser or return one level. */
    assert(badge_return_input_event(&s,2,false,true));
    assert(!s.held);
    assert(badge_return_input_event(&s,2,false,false)); /* Delayed click/double/long. */
    assert(!badge_return_input_event(&s,2,true,false)); /* New intentional press. */
    assert(!badge_return_input_event(&s,2,false,true));
    assert(!badge_return_input_event(&s,2,false,false));
    badge_return_input_barrier(&s,2); /* Confirm chooser after release/click. */
    assert(badge_return_input_event(&s,2,false,false));
    assert(!badge_return_input_event(&s,0,true,false)); /* Other key works. */
    assert(!badge_return_input_event(&s,0,false,true));
    assert(!badge_return_input_event(&s,0,false,false));
    assert(!badge_return_input_event(&s,0,true,false)); /* App-owned recording press. */
    badge_return_input_app_press(&s,0);
    assert(!badge_return_input_event(&s,2,true,false));
    badge_return_input_barrier(&s,2);
    assert(s.held==5&&s.blocked==5);
    /* Caller delivers this old app-owned release before consuming its tail. */
    assert(badge_return_input_event(&s,0,false,true));
    assert(badge_return_input_app_release(&s,0)); /* Forward to Muse exactly once. */
    assert(!badge_return_input_app_release(&s,0));
    assert(s.held==4&&badge_return_input_event(&s,0,false,false));
    assert(badge_return_input_event(&s,2,false,true));
    assert(!badge_return_input_event(&s,0,true,false)); /* Modal-owned press. */
    badge_return_input_barrier(&s,3); /* External page replacement. */
    assert(badge_return_input_event(&s,0,false,true));
    assert(!badge_return_input_app_release(&s,0)); /* Modal never owned capture. */
    assert(!badge_return_input_app_release(&s,255));
    assert(!badge_return_input_event(&s,255,true,false));
    puts("Return input: PASS (held-key tails, delayed clicks, fresh presses and modal barriers)");
}
