#pragma once

struct Settings {
    bool fps_unlock = true;
    int physics_fps = 120;
};

// Reads DS2-FPS-Unlock.ini from the same directory as the DLL.
Settings settings_load(const wchar_t* dll_path);
