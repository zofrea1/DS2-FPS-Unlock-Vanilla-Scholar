#include "log.h"
#include "patches.h"
#include "proxy.h"
#include "settings.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cwchar>

namespace {

bool filename_is_game(const wchar_t* path) {
    const wchar_t* slash = wcsrchr(path, L'\\');
    const wchar_t* name = slash ? slash + 1 : path;
    return _wcsicmp(name, L"DarkSoulsII.exe") == 0;
}

void startup(HMODULE self) {
    wchar_t dll_path[MAX_PATH];
    GetModuleFileNameW(self, dll_path, MAX_PATH);
    log_init(dll_path);
    LOG_INFO("DS2-FPS-Unlock v1.0.0");

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

    if (!proxy_init()) {
        LOG_ERROR("XInput proxy failed; controller input will not work");
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
        log_close();
    }
    return TRUE;
}
