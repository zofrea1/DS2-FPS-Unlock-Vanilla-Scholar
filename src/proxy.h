#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// The system DLL this DLL stands in for, chosen by its own file name. The game imports all of them, so any of
// these names loads the mod at start-up. Another name (a mod loader that loads this DLL itself) gets no forwarding.
enum class ProxyKind {
    kOther,
    kXInput,   // xinput1_3.dll
    kDxgi,     // dxgi.dll (Scholar only)
    kD3d9,     // d3d9.dll (original game only)
    kDinput8,  // dinput8.dll
};

// Records this DLL's file name. Loads nothing, so it is safe from DllMain.
ProxyKind proxy_init(HMODULE self);
ProxyKind proxy_kind();
// This DLL's file name as loaded, e.g. "dxgi.dll".
const char* proxy_file_name();
// The names this build answers to, for the log.
const char* proxy_supported_names();
// ChainDll from the INI: a DLL in the game folder (or a full path) to forward to instead of the system DLL, so
// another mod that needs the same file name can stay installed under a different one. Empty = the system DLL.
void proxy_set_chain(const char* utf8_path);

// The real DLL's export, loaded on first use (never from DllMain). Null if it cannot be loaded or lacks `name`.
FARPROC proxy_real_proc(const char* name);

// XInput as the game sees it: the library this DLL forwards to when it is xinput1_3.dll, otherwise the game's
// own xinput1_3.dll. Used by the controller fix. Same results as XInputGetState / XInputGetCapabilities.
DWORD xinput_get_state(DWORD index, void* state);
DWORD xinput_get_caps(DWORD index, void* caps);
