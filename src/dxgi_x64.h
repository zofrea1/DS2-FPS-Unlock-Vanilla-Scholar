#pragma once

// Scholar of the First Sin only. In exclusive fullscreen the game asks DXGI for a 60 Hz display mode and presents
// with vsync. On Windows that runs the screen at 60 Hz; under DXVK (the Steam Deck), whose mode changes are
// emulated, it engages a frame limiter at the selected mode's rate, so the game stayed at 60 FPS on a 90 Hz Deck
// while a window was not capped. Fullscreen now asks for display_fullscreen_hz(). With vsync off it presents
// without waiting; with vsync on it waits for one vblank at that rate. `fps_limit` arms the cap (0 off); it
// waits where FPSLimitPoint says (limiter.h): before Present, or in the frame-time sampler before the pad is read.
// Windowed presentation is unchanged.
bool dxgi_apply(bool vsync, int fps_limit);
void dxgi_remove();
