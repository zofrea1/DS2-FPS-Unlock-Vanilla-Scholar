#pragma once

// FPSLimit. 0 disables the wait. limiter_set_early chooses which call actually waits. limiter_wait() is the
// frame-time sampler, before the pad is read. limiter_wait_present() is just before Present, after the pad
// has been read. The other call returns immediately. C linkage so the original game's sampler stub can call
// limiter_wait.
extern "C" {
void limiter_start(int fps);
void limiter_set_early(bool early);
void limiter_wait();
void limiter_wait_present();
void limiter_stop();
}
