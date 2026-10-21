#pragma once

#include <string>

struct Settings {
    bool fps_unlock = true;
    int physics_fps = 120;
    // Cap in frames per second after the game's own 60 FPS wait is removed. 0 leaves the framerate uncapped.
    int fps_limit = 0;
    // Fullscreen presents immediately when fps_unlock is on. true waits for one vblank at fullscreen_refresh.
    bool vsync = false;
    // Release the body from the ground snap on the frames where 60 FPS would (ground_release in
    // frame_fixes.h). false restores the old "never snap to the ground" jump workaround.
    bool ground_snap_fix = true;
    // Keep forward + R1/R2 (guard break, jump attack) as easy to enter as at 60 FPS.
    bool forward_attack_fix = true;
    // Let animation (TAE) events repeat only as often as they do at 60 FPS.
    bool tae_event_fix = true;
    // Step cloth with the real frame time instead of a fixed 1/60.
    bool cloth_fix = true;
    // Scale equipment durability loss by the frame time so it matches 60 FPS.
    bool durability_fix = true;
    // Diagnostic: write the player's per-frame jump physics to DS2-FPS-Unlock-jumptrace-*.csv.
    bool jump_trace = false;
    // Fill the screen in exclusive fullscreen at any aspect ratio instead of a 16:9 picture between black bars.
    // Independent of fps_unlock.
    bool fix_aspect = true;
    // Refresh rate exclusive fullscreen asks for (with fps_unlock). 0 = the desktop's own rate.
    int fullscreen_refresh = 0;
    // A DLL to forward to instead of the system DLL this one stands in for (proxy.h), UTF-8. Empty = none.
    std::string chain_dll;
};

// Reads DS2-FPS-Unlock.ini from the same directory as the DLL.
Settings settings_load(const wchar_t* dll_path);
