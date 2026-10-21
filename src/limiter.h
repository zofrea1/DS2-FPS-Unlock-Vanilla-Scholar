#pragma once

// FPSLimit. 0 disables the wait. Call limiter_wait() once per presented frame, before the real Present, so the
// next frame-time sample includes the wait and the gameplay fixes step at the capped rate.
void limiter_start(int fps);
void limiter_wait();
void limiter_stop();
