#include "jump_trace.h"
#include "log.h"
#include "patches.h"
#include "settings.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cwchar>

#pragma comment(lib, "user32.lib")

namespace {

// The desktop mode of the primary display, for the FixAspect log line. The game lists every fullscreen
// mode the display offers, so the player picks the matching one in its options.
void log_display_mode() {
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) || mode.dmPelsHeight == 0) {
        return;
    }
    const float aspect = static_cast<float>(mode.dmPelsWidth) / static_cast<float>(mode.dmPelsHeight);
    const float wide = 16.0f / 9.0f;
    if (aspect > wide - 0.01f && aspect < wide + 0.01f) {
        LOG_INFO("   Display %lux%lu is 16:9", mode.dmPelsWidth, mode.dmPelsHeight);
    } else {
        LOG_INFO("   Display %lux%lu (%.3f): choose %lux%lu as the game's fullscreen resolution to fill it",
                 mode.dmPelsWidth, mode.dmPelsHeight, aspect, mode.dmPelsWidth, mode.dmPelsHeight);
    }
}

bool filename_is_game(const wchar_t* path) {
    const wchar_t* slash = wcsrchr(path, L'\\');
    const wchar_t* name = slash ? slash + 1 : path;
    return _wcsicmp(name, L"DarkSoulsII.exe") == 0;
}

void startup(HMODULE self) {
    wchar_t dll_path[MAX_PATH];
    GetModuleFileNameW(self, dll_path, MAX_PATH);
    log_init(dll_path);
    LOG_INFO("DS2-FPS-Unlock v1.2.0");

    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    char exe_narrow[MAX_PATH];
    char dll_narrow[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, exe_path, -1, exe_narrow, MAX_PATH, nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, dll_path, -1, dll_narrow, MAX_PATH, nullptr, nullptr);
    LOG_INFO("EXE module (base address: %p):", GetModuleHandleW(nullptr));
    LOG_INFO("  File path: %s", exe_narrow);
    LOG_INFO("DLL module (base address: %p):", self);
    LOG_INFO("  File path: %s", dll_narrow);

    if (!filename_is_game(exe_path)) {
        LOG_ERROR("Plugin_Init: EXE filename %s doesn't match target filename DarkSoulsII.exe, exiting", exe_narrow);
        return;
    }
#if defined(_M_IX86)
    LOG_INFO("Game detected: DS2");
#else
    LOG_INFO("Game detected: DS2 SotFS");
#endif

    const Settings settings = settings_load(dll_path);
    LOG_INFO("Settings values:");
    LOG_INFO(" - FPSUnlock: %s", settings.fps_unlock ? "true" : "false");
    LOG_INFO(" - PhysicsFPS: %d", settings.physics_fps);
    LOG_INFO(" - GroundSnapFix: %s", settings.ground_snap_fix ? "true" : "false");
    LOG_INFO(" - ForwardAttackFix: %s", settings.forward_attack_fix ? "true" : "false");
    LOG_INFO(" - TaeEventFix: %s", settings.tae_event_fix ? "true" : "false");
    LOG_INFO(" - ClothFix: %s", settings.cloth_fix ? "true" : "false");
    LOG_INFO(" - DurabilityFix: %s", settings.durability_fix ? "true" : "false");
    LOG_INFO(" - FixAspect: %s", settings.fix_aspect ? "true" : "false");
    if (settings.fix_aspect) {
        log_display_mode();
    }
    if (settings.jump_trace) {
        const bool open = jump_trace_open(dll_path, settings.fps_unlock ? L"unlocked" : L"stock");
        LOG_INFO(" - JumpTrace: %s", open ? "writing DS2-FPS-Unlock-jumptrace-*.csv" : "could not open the CSV");
    }

    if (!patches_apply(settings)) {
        LOG_ERROR("One or more patches failed. The game may be an unexpected build.");
    }
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        startup(instance);
    } else if (reason == DLL_PROCESS_DETACH) {
        patches_remove();
        jump_trace_close();
        log_close();
    }
    return TRUE;
}
