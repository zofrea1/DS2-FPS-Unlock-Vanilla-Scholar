#pragma once

// Original Dark Souls II only. Rewrites the Direct3D 9 swapchain so fullscreen
// Present is not held at 60 Hz. Windowed mode stays on the game's blit path.
bool present_apply();
void present_remove();
