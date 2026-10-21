// Stand-in for a system DLL the game imports. Which one depends on this DLL's file name (proxy.h): calls to that
// DLL's exports are passed to the real library. The XInput exports are defined here; the dxgi, d3d9 and dinput8
// ones are in forward.cpp.

#include "proxy.h"

#include "log.h"

#include <cstdio>
#include <cwchar>

// Defined with the exports below. Named here so the loader check can take its address.
extern "C" DWORD WINAPI XInputGetState(DWORD index, void* state);

namespace {

using GetStateFn = DWORD(WINAPI*)(DWORD, void*);
using SetStateFn = DWORD(WINAPI*)(DWORD, void*);
using GetCapsFn = DWORD(WINAPI*)(DWORD, DWORD, void*);
using EnableFn = void(WINAPI*)(BOOL);
using GetDSoundFn = DWORD(WINAPI*)(DWORD, void*, void*);
using GetBatteryFn = DWORD(WINAPI*)(DWORD, BYTE, void*);
using GetKeystrokeFn = DWORD(WINAPI*)(DWORD, DWORD, void*);

GetStateFn pGetState = nullptr;
SetStateFn pSetState = nullptr;
GetCapsFn pGetCaps = nullptr;
EnableFn pEnable = nullptr;
GetDSoundFn pGetDSound = nullptr;
GetBatteryFn pGetBattery = nullptr;
GetKeystrokeFn pGetKeystroke = nullptr;

INIT_ONCE g_xinput_once = INIT_ONCE_STATIC_INIT;
constexpr DWORD kNotConnected = 1167;  // ERROR_DEVICE_NOT_CONNECTED

ProxyKind g_kind = ProxyKind::kOther;
HMODULE g_self = nullptr;
char g_file[MAX_PATH] = {};
wchar_t g_file_w[MAX_PATH] = {};
wchar_t g_dir[MAX_PATH] = {};
wchar_t g_chain[MAX_PATH] = {};

INIT_ONCE g_real_once = INIT_ONCE_STATIC_INIT;
HMODULE g_real = nullptr;

// Last result the game saw per XInput slot: 0 unknown, 1 connected, 2 not connected. Changes are logged.
volatile LONG g_slot_seen[4] = {};
volatile LONG g_slot_logs = 0;
constexpr LONG kMaxSlotLogs = 64;

template <typename T>
T load(HMODULE module, const char* name) {
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

void narrow_path(const wchar_t* wide, char* out, int out_len) {
    if (!wide || !WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, out_len, nullptr, nullptr)) {
        out[0] = 0;
    }
}

void module_path(HMODULE mod, char* out, int out_len) {
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(mod, path, MAX_PATH)) {
        narrow_path(path, out, out_len);
    } else {
        out[0] = 0;
    }
}

// The chain DLL's full path: as given when absolute, else next to this DLL.
bool chain_path(wchar_t* out) {
    if (!g_chain[0]) {
        return false;
    }
    const bool absolute = g_chain[0] == L'\\' || (g_chain[0] && g_chain[1] == L':');
    _snwprintf_s(out, MAX_PATH, _TRUNCATE, L"%s%s", absolute ? L"" : g_dir, g_chain);
    return true;
}

// `mod` is usable when it is a different module and its XInputGetState is not our own export. Under Wine (the
// Steam Deck) the system xinput1_3 resolves to this DLL, and binding it would recurse until the stack overflows:
// the controller goes dead while the keyboard still works.
bool bind_module(HMODULE mod, const wchar_t* label) {
    char text[MAX_PATH];
    narrow_path(label, text, MAX_PATH);
    if (!mod) {
        return false;
    }
    if (mod == g_self) {
        LOG_INFO("XInput proxy: skipped %s (that module is this DLL)", text);
        return false;
    }
    auto get_state = load<GetStateFn>(mod, "XInputGetState");
    auto set_state = load<SetStateFn>(mod, "XInputSetState");
    if (!get_state || !set_state || reinterpret_cast<void*>(get_state) == reinterpret_cast<void*>(&XInputGetState)) {
        LOG_INFO("XInput proxy: skipped %s (no XInputGetState of its own)", text);
        return false;
    }
    pGetState = get_state;
    pSetState = set_state;
    pGetCaps = load<GetCapsFn>(mod, "XInputGetCapabilities");
    pEnable = load<EnableFn>(mod, "XInputEnable");
    pGetDSound = load<GetDSoundFn>(mod, "XInputGetDSoundAudioDeviceGuids");
    pGetBattery = load<GetBatteryFn>(mod, "XInputGetBatteryInformation");
    pGetKeystroke = load<GetKeystrokeFn>(mod, "XInputGetKeystroke");
    module_path(mod, text, MAX_PATH);
    LOG_INFO("XInput proxy: forwarding to %s", text);
    return true;
}

bool try_file(const wchar_t* file) {
    wchar_t sys[MAX_PATH];
    if (GetSystemDirectoryW(sys, MAX_PATH)) {
        wchar_t path[MAX_PATH];
        _snwprintf_s(path, _TRUNCATE, L"%s\\%s", sys, file);
        if (bind_module(LoadLibraryW(path), path)) {
            return true;
        }
    }
    // By name, so Wine's builtin (which is not a file in system32) can still load.
    return bind_module(LoadLibraryW(file), file);
}

// The system xinput1_3 under another file name, so the loader cannot hand back this DLL.
bool try_renamed_system() {
    wchar_t sys[MAX_PATH];
    if (!GetSystemDirectoryW(sys, MAX_PATH)) {
        return false;
    }
    wchar_t src[MAX_PATH];
    _snwprintf_s(src, _TRUNCATE, L"%s\\xinput1_3.dll", sys);
    wchar_t dir[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, dir)) {
        return false;
    }
    wchar_t dst[MAX_PATH];
#if defined(_M_X64)
    _snwprintf_s(dst, _TRUNCATE, L"%sds2-xinput-host64.dll", dir);
#else
    _snwprintf_s(dst, _TRUNCATE, L"%sds2-xinput-host32.dll", dir);
#endif
    if (!CopyFileW(src, dst, FALSE)) {
        return false;
    }
    return bind_module(LoadLibraryW(dst), dst);
}

// Loading from inside DllMain would run under the loader lock, so the real library is chosen on the first
// XInput call. Under another file name this DLL is not xinput1_3, so the game's own xinput1_3 is the one to read.
// As xinput1_3, a ChainDll comes first, then xinput1_4: it is the Windows 8+ library and Wine's real controller
// backend, and a native override of xinput1_3 does not apply to it. xinput9_1_0 (Windows 7) covers GetState and
// SetState, which is all the game imports. The system xinput1_3 under another name is the last resort.
BOOL CALLBACK xinput_init_once(PINIT_ONCE, PVOID, PVOID*) {
    if (g_kind != ProxyKind::kXInput) {
        HMODULE game = GetModuleHandleW(L"xinput1_3.dll");
        if (bind_module(game ? game : LoadLibraryW(L"xinput1_3.dll"), L"xinput1_3.dll") ||
            try_file(L"xinput1_4.dll")) {
            return TRUE;
        }
    } else {
        wchar_t chain[MAX_PATH];
        if (chain_path(chain) && bind_module(LoadLibraryW(chain), chain)) {
            return TRUE;
        }
        if (try_file(L"xinput1_4.dll") || try_file(L"xinput9_1_0.dll") || try_renamed_system()) {
            return TRUE;
        }
    }
    LOG_ERROR("XInput proxy: no real XInput library could be loaded; controllers will be disconnected");
    return TRUE;
}

void ensure_xinput() {
    InitOnceExecuteOnce(&g_xinput_once, xinput_init_once, nullptr, nullptr);
}

// Logs a slot the first time it answers and whenever it connects or drops, so a Deck log shows whether the game
// ever saw the pad.
void note_slot(DWORD index, DWORD result) {
    if (index >= 4) {
        return;
    }
    const LONG now = result == 0 ? 1 : 2;
    if (InterlockedExchange(&g_slot_seen[index], now) == now || InterlockedIncrement(&g_slot_logs) > kMaxSlotLogs) {
        return;
    }
    if (now == 1) {
        unsigned char caps[20] = {};
        const DWORD caps_result = pGetCaps ? pGetCaps(index, 0, caps) : kNotConnected;
        if (caps_result == 0) {
            LOG_INFO("XInput: slot %lu connected (type %u, subtype %u)", index, caps[0], caps[1]);
        } else {
            LOG_INFO("XInput: slot %lu connected", index);
        }
    } else {
        LOG_INFO("XInput: slot %lu not connected (result %lu)", index, result);
    }
}

DWORD get_state(DWORD index, void* state) {
    ensure_xinput();
    const DWORD result = pGetState ? pGetState(index, state) : kNotConnected;
    note_slot(index, result);
    return result;
}

HMODULE load_real_module() {
    wchar_t path[MAX_PATH];
    if (chain_path(path)) {
        HMODULE mod = LoadLibraryW(path);
        char text[MAX_PATH];
        narrow_path(path, text, MAX_PATH);
        if (mod && mod != g_self) {
            LOG_INFO("Proxy: forwarding %s to ChainDll %s", g_file, text);
            return mod;
        }
        LOG_ERROR("Proxy: ChainDll %s could not be loaded (%lu); using the system %s", text, GetLastError(), g_file);
    }
    wchar_t sys[MAX_PATH];
    if (!GetSystemDirectoryW(sys, MAX_PATH)) {
        return nullptr;
    }
    _snwprintf_s(path, _TRUNCATE, L"%s\\%s", sys, g_file_w);
    HMODULE mod = LoadLibraryW(path);
    if (!mod || mod == g_self) {
        // Under Wine a DLL that is one of Wine's own (dinput8, or d3d9/dxgi without DXVK) loads as this DLL again
        // when this one has its name. DXVK's d3d9 and dxgi are files of their own, so those names work on Proton.
        LOG_ERROR("Proxy: the system %s could not be loaded (%lu%s)", g_file, GetLastError(),
                  mod == g_self ? ", it resolved to this DLL; under Wine/Proton use xinput1_3.dll, or d3d9.dll / "
                                  "dxgi.dll with DXVK" : "");
        return nullptr;
    }
    char text[MAX_PATH];
    module_path(mod, text, MAX_PATH);
    LOG_INFO("Proxy: forwarding %s to %s", g_file, text);
    return mod;
}

BOOL CALLBACK real_init_once(PINIT_ONCE, PVOID, PVOID*) {
    g_real = load_real_module();
    return TRUE;
}

}  // namespace

ProxyKind proxy_init(HMODULE self) {
    g_self = self;
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(self, path, MAX_PATH)) {
        path[0] = 0;
    }
    wchar_t* slash = wcsrchr(path, L'\\');
    const wchar_t* name = slash ? slash + 1 : path;
    lstrcpynW(g_file_w, name, MAX_PATH);
    narrow_path(g_file_w, g_file, MAX_PATH);
    lstrcpynW(g_dir, path, MAX_PATH);
    if (wchar_t* dir_end = wcsrchr(g_dir, L'\\')) {
        dir_end[1] = 0;
    } else {
        g_dir[0] = 0;
    }
    if (_wcsicmp(name, L"xinput1_3.dll") == 0) {
        g_kind = ProxyKind::kXInput;
#if defined(_M_X64)
    } else if (_wcsicmp(name, L"dxgi.dll") == 0) {
        g_kind = ProxyKind::kDxgi;
#else
    } else if (_wcsicmp(name, L"d3d9.dll") == 0) {
        g_kind = ProxyKind::kD3d9;
#endif
    } else if (_wcsicmp(name, L"dinput8.dll") == 0) {
        g_kind = ProxyKind::kDinput8;
    } else {
        g_kind = ProxyKind::kOther;
    }
    return g_kind;
}

ProxyKind proxy_kind() {
    return g_kind;
}

const char* proxy_file_name() {
    return g_file;
}

const char* proxy_supported_names() {
#if defined(_M_X64)
    return "xinput1_3.dll, dxgi.dll, dinput8.dll";
#else
    return "xinput1_3.dll, d3d9.dll, dinput8.dll";
#endif
}

void proxy_set_chain(const char* utf8_path) {
    g_chain[0] = 0;
    if (utf8_path && utf8_path[0]) {
        MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, g_chain, MAX_PATH);
    }
}

FARPROC proxy_real_proc(const char* name) {
    InitOnceExecuteOnce(&g_real_once, real_init_once, nullptr, nullptr);
    if (!g_real) {
        return nullptr;
    }
    FARPROC proc = GetProcAddress(g_real, name);
    if (!proc) {
        LOG_ERROR("Proxy: the real %s has no %s", g_file, name);
    }
    return proc;
}

DWORD xinput_get_state(DWORD index, void* state) {
    return get_state(index, state);
}

DWORD xinput_get_caps(DWORD index, void* caps) {
    ensure_xinput();
    return pGetCaps ? pGetCaps(index, 0, caps) : kNotConnected;
}

extern "C" {

DWORD WINAPI XInputGetState(DWORD index, void* state) {
    return get_state(index, state);
}
DWORD WINAPI XInputSetState(DWORD index, void* vibration) {
    ensure_xinput();
    return pSetState ? pSetState(index, vibration) : kNotConnected;
}
DWORD WINAPI XInputGetCapabilities(DWORD index, DWORD flags, void* caps) {
    ensure_xinput();
    return pGetCaps ? pGetCaps(index, flags, caps) : kNotConnected;
}
void WINAPI XInputEnable(BOOL enable) {
    ensure_xinput();
    if (pEnable) {
        pEnable(enable);
    }
}
DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD index, void* render, void* capture) {
    ensure_xinput();
    return pGetDSound ? pGetDSound(index, render, capture) : kNotConnected;
}
DWORD WINAPI XInputGetBatteryInformation(DWORD index, BYTE devType, void* battery) {
    ensure_xinput();
    return pGetBattery ? pGetBattery(index, devType, battery) : kNotConnected;
}
DWORD WINAPI XInputGetKeystroke(DWORD index, DWORD reserved, void* keystroke) {
    ensure_xinput();
    return pGetKeystroke ? pGetKeystroke(index, reserved, keystroke) : kNotConnected;
}

}  // extern "C"
