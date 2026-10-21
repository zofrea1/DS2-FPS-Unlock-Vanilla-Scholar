#include "display.h"
#include "jump_trace.h"
#include "log.h"
#include "patches.h"
#include "proxy.h"
#include "settings.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cwchar>

namespace {

// Wine and Proton (the Steam Deck) export wine_get_version from ntdll.
void log_wine() {
    using VersionFn = const char*(__cdecl*)();
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto version = ntdll ? reinterpret_cast<VersionFn>(GetProcAddress(ntdll, "wine_get_version")) : nullptr;
    if (version) {
        LOG_INFO("Running under Wine/Proton %s", version());
    }
}

// The DLL's name decides which system DLL it stands in for (proxy.h). Wine only loads a DLL from the game folder
// in place of its own when the launch options name it, e.g. WINEDLLOVERRIDES="xinput1_3=n,b" %command%.
void log_proxy(ProxyKind kind) {
    const char* name = proxy_file_name();
    switch (kind) {
    case ProxyKind::kXInput:
        LOG_INFO("Loaded as %s: controller calls are forwarded to the system's XInput", name);
        break;
    case ProxyKind::kDxgi:
    case ProxyKind::kD3d9:
    case ProxyKind::kDinput8:
        LOG_INFO("Loaded as %s: its calls are forwarded to the real %s; the game's xinput1_3.dll is its own", name,
                 name);
        break;
    case ProxyKind::kOther:
        LOG_INFO("Loaded as %s, which is not a name the game imports (%s). Fine when another mod loads this DLL; "
                 "otherwise rename it to one of those",
                 name, proxy_supported_names());
        break;
    }
}

// Two copies of the mod under two names (an old xinput1_3.dll left next to a new dxgi.dll) would patch the game
// twice. The first one loaded applies the mod; the other only passes its DLL's calls on. The mutex handle is kept
// for the life of the process.
bool first_copy() {
    wchar_t name[64];
    _snwprintf_s(name, _TRUNCATE, L"Local\\DS2-FPS-Unlock.%lu", GetCurrentProcessId());
    HANDLE mutex = CreateMutexW(nullptr, FALSE, name);
    return mutex && GetLastError() != ERROR_ALREADY_EXISTS;
}

bool filename_is_game(const wchar_t* path) {
    const wchar_t* slash = wcsrchr(path, L'\\');
    const wchar_t* name = slash ? slash + 1 : path;
    return _wcsicmp(name, L"DarkSoulsII.exe") == 0;
}

void startup(HMODULE self) {
    wchar_t dll_path[MAX_PATH];
    GetModuleFileNameW(self, dll_path, MAX_PATH);
    const ProxyKind kind = proxy_init(self);
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    // Another program in the game folder (a mod launcher) can load this DLL too. It only forwards there, and leaves
    // the game's log alone.
    if (!filename_is_game(exe_path)) {
        return;
    }
    if (!first_copy()) {
        log_init(dll_path, true);
        LOG_ERROR("Another copy of DS2-FPS-Unlock is already loaded. This one (%s) only passes its DLL's calls "
                  "on; delete one of the two DLLs.", proxy_file_name());
        return;
    }
    log_init(dll_path);
#if defined(_M_IX86)
    const char* build = "32-bit, original game";
#else
    const char* build = "64-bit, Scholar of the First Sin";
#endif
    LOG_INFO("DS2-FPS-Unlock v1.3.0 (%s; built %s %s)", build, __DATE__, __TIME__);

    char exe_narrow[MAX_PATH];
    char dll_narrow[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, exe_path, -1, exe_narrow, MAX_PATH, nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, dll_path, -1, dll_narrow, MAX_PATH, nullptr, nullptr);
    LOG_INFO("EXE module (base address: %p):", GetModuleHandleW(nullptr));
    LOG_INFO("  File path: %s", exe_narrow);
    LOG_INFO("DLL module (base address: %p):", self);
    LOG_INFO("  File path: %s", dll_narrow);
    log_proxy(kind);
    log_wine();
#if defined(_M_IX86)
    LOG_INFO("Game detected: DS2");
#else
    LOG_INFO("Game detected: DS2 SotFS");
#endif

    const Settings settings = settings_load(dll_path);
    LOG_INFO("Settings values:");
    LOG_INFO(" - FPSUnlock: %s", settings.fps_unlock ? "true" : "false");
    LOG_INFO(" - FPSLimit: %d%s", settings.fps_limit, settings.fps_limit > 0 ? "" : " (uncapped)");
    LOG_INFO(" - VSync: %s", settings.vsync ? "true" : "false");
    LOG_INFO(" - PhysicsFPS: %d", settings.physics_fps);
    LOG_INFO(" - GroundSnapFix: %s", settings.ground_snap_fix ? "true" : "false");
    LOG_INFO(" - ForwardAttackFix: %s", settings.forward_attack_fix ? "true" : "false");
    LOG_INFO(" - TaeEventFix: %s", settings.tae_event_fix ? "true" : "false");
    LOG_INFO(" - ClothFix: %s", settings.cloth_fix ? "true" : "false");
    LOG_INFO(" - DurabilityFix: %s", settings.durability_fix ? "true" : "false");
    LOG_INFO(" - FixAspect: %s", settings.fix_aspect ? "true" : "false");
    LOG_INFO(" - FullscreenRefreshRate: %d%s", settings.fullscreen_refresh,
             settings.fullscreen_refresh > 0 ? "" : " (the desktop's)");
    LOG_INFO(" - ChainDll: %s", settings.chain_dll.empty() ? "(none)" : settings.chain_dll.c_str());
    proxy_set_chain(settings.chain_dll.c_str());
    display_init(settings.fullscreen_refresh, settings.fix_aspect);
    if (!settings.fps_unlock && (settings.fps_limit > 0 || settings.vsync)) {
        LOG_INFO("FPSLimit and VSync take effect with FPSUnlock; the game keeps its own pacing");
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
