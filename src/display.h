#pragma once

// The refresh rate exclusive fullscreen asks for with FPSUnlock on. The games ask for 60 Hz, and a 60 Hz mode
// with vsync caps the picture at 60 (DXVK on the Steam Deck caps it to the selected mode's rate even though the
// screen keeps running at 90). `configured_hz` is FullscreenRefreshRate: 0 means the desktop's own rate, read
// once here so a fullscreen mode switch later cannot lower it. Also logs the display mode for FixAspect.
void display_init(int configured_hz, bool log_aspect);
// 0 when unknown.
unsigned display_fullscreen_hz();
