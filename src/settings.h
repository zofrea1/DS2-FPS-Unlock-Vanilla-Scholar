#pragma once

struct Settings {
    bool fps_unlock = true;
    int physics_fps = 120;
    // Skip the character's ground snap while a jump is in progress or the body is rising
    // (frame_fixes.h). false restores the old "never snap to the ground" jump workaround.
    bool ground_snap_fix = true;
    // Keep forward + R1/R2 (guard break, jump attack) as easy to enter as at 60 FPS.
    bool forward_attack_fix = true;
    // Let animation (TAE) events repeat only as often as they do at 60 FPS.
    bool tae_event_fix = true;
    // Step cloth with the real frame time instead of a fixed 1/60.
    bool cloth_fix = true;
    // Scale equipment durability loss by the frame time so it matches 60 FPS.
    bool durability_fix = true;
};

// Reads DS2-FPS-Unlock.ini from the same directory as the DLL.
Settings settings_load(const wchar_t* dll_path);
