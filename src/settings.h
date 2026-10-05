#pragma once

struct Settings {
    bool fps_unlock = true;
    int physics_fps = 120;
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
};

// Reads DS2-FPS-Unlock.ini from the same directory as the DLL.
Settings settings_load(const wchar_t* dll_path);
