#pragma once

// Original Dark Souls II only. Rewrites the Direct3D 9 swapchain so fullscreen
// Present is not held at 60 Hz. `vsync` waits for one vblank at the fullscreen
// refresh; otherwise presentation is immediate. `fps_limit` arms the cap (0 leaves
// it uncapped); the wait itself is in the frame-time sampler, before the pad is
// read. Windowed mode stays on the game's blit path.
bool present_apply(bool vsync, int fps_limit);
void present_remove();
