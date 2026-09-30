#pragma once

struct Settings {
    bool fps_unlock = true;
    int physics_fps = 120;
    // Replace the old "never snap to the ground" jump workaround with a snap that only skips
    // while the character is rising. false restores the old workaround.
    bool ground_snap_fix = true;
};

// Reads DS2-FPS-Unlock.ini from the same directory as the DLL.
Settings settings_load(const wchar_t* dll_path);
