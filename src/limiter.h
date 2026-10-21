#pragma once

// FPSLimit. 0 disables the wait. Call limiter_wait() once per frame at the start of the frame-time sampler,
// before that function reads the clock and before the frame loop reads the pad. The sample then includes the
// wait, and the gameplay fixes step at the capped rate. C linkage so the original game's sampler stub can call it.
extern "C" {
void limiter_start(int fps);
void limiter_wait();
void limiter_stop();
}
