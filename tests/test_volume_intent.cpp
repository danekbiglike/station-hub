#include "../windows/native/volume_intent.h"
#include <cassert>
int main() {
    VolumeIntent intent;
    auto now = VolumeIntent::Clock::now();
    assert(intent.select(1,2,now)==2);              // one user action -> one command
    assert(intent.desired()==2);
    assert(!intent.observe(true,1,now));             // old state must not flash
    assert(intent.desired()==2);
    assert(!intent.select(1,1,now));                // quick reversal is queued
    assert(intent.desired()==1);
    assert(intent.observe(true,2,now)==1);           // send reversal only after confirmation
    assert(intent.desired()==1);
    assert(!intent.observe(true,1,now));
    assert(intent.desired()==-1);
    assert(intent.select(1,10,now)==2);              // no large jump
    assert(!intent.observe(true,1,now+std::chrono::seconds(9)));
    assert(intent.desired()==-1);                    // timeout returns to measured value
    assert(!intent.select(-1,2,now));                // unknown level cannot issue a command
    assert(intent.select(2,1,now)==1);
    assert(!intent.observe(false,-1,now));
    assert(intent.desired()==-1);                    // disconnect clears optimistic state
}
