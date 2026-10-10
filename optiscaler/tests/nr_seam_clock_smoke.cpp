// Headless sequence regression for PR #11; no game or NVIDIA runtime is loaded.
#include "../OptiScaler/shaders/dlssnr/DlssNr_SeamClock.h"
#include <cassert>
#include <cstdio>

int main()
{
    DlssNrSeamClock clock;
    assert(clock.AtSeam(true, 10) == 10);
    // Present ticks between the paired calls: keep identity, then advance once next frame.
    assert(clock.AtSeam(false, 11) == 10);
    assert(clock.AtSeam(true, 11) == 11);
    assert(clock.AtSeam(false, 11) == 11);
    // A stalled Present counter must not look like a duplicate native evaluate.
    assert(clock.AtSeam(true, 11) == 12);
    assert(clock.AtSeam(false, 12) == 12);
    assert(clock.AtSeam(true, 12) == 13);
    // A genuine gap and a swapchain counter reset remain monotonic.
    assert(clock.AtSeam(true, 20) == 20);
    assert(clock.AtSeam(false, 21) == 20);
    assert(clock.AtSeam(true, 0) == 21);
    std::puts("PASS: deferred seam pairing, stalls, gaps and resets");
}
