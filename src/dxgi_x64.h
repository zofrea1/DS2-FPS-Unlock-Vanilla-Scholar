#pragma once

// Scholar of the First Sin only. In exclusive fullscreen the game asks DXGI for a 60 Hz display mode and presents
// with vsync. On Windows that runs the screen at 60 Hz; under DXVK (the Steam Deck), whose mode changes are
// emulated, it engages a frame limiter at the selected mode's rate, so the game stayed at 60 FPS on a 90 Hz Deck
// while a window was not capped. Fullscreen now asks for display_fullscreen_hz() and presents without waiting for
// vsync, as the original game's fullscreen does (present_x86.h). Windowed presentation is unchanged.
bool dxgi_apply();
void dxgi_remove();
