#include "proxy.h"

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>

namespace {

HMODULE g_real = nullptr;

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

constexpr DWORD kNotConnected = 1167;

template <typename T>
T load(const char* name) {
    return reinterpret_cast<T>(GetProcAddress(g_real, name));
}

}  // namespace

bool proxy_init() {
    wchar_t sys[MAX_PATH];
    if (!GetSystemDirectoryW(sys, MAX_PATH)) {
        return false;
    }
    wchar_t path[MAX_PATH];
    _snwprintf_s(path, _TRUNCATE, L"%s\\xinput1_3.dll", sys);
    g_real = LoadLibraryW(path);
    if (!g_real) {
        LOG_ERROR("proxy: failed to load system xinput1_3.dll (%lu)", GetLastError());
        return false;
    }
    pGetState = load<GetStateFn>("XInputGetState");
    pSetState = load<SetStateFn>("XInputSetState");
    pGetCaps = load<GetCapsFn>("XInputGetCapabilities");
    pEnable = load<EnableFn>("XInputEnable");
    pGetDSound = load<GetDSoundFn>("XInputGetDSoundAudioDeviceGuids");
    pGetBattery = load<GetBatteryFn>("XInputGetBatteryInformation");
    pGetKeystroke = load<GetKeystrokeFn>("XInputGetKeystroke");
    return pGetState && pSetState;
}

extern "C" {

DWORD WINAPI XInputGetState(DWORD index, void* state) {
    return pGetState ? pGetState(index, state) : kNotConnected;
}
DWORD WINAPI XInputSetState(DWORD index, void* vibration) {
    return pSetState ? pSetState(index, vibration) : kNotConnected;
}
DWORD WINAPI XInputGetCapabilities(DWORD index, DWORD flags, void* caps) {
    return pGetCaps ? pGetCaps(index, flags, caps) : kNotConnected;
}
void WINAPI XInputEnable(BOOL enable) {
    if (pEnable) {
        pEnable(enable);
    }
}
DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD index, void* render, void* capture) {
    return pGetDSound ? pGetDSound(index, render, capture) : kNotConnected;
}
DWORD WINAPI XInputGetBatteryInformation(DWORD index, BYTE devType, void* battery) {
    return pGetBattery ? pGetBattery(index, devType, battery) : kNotConnected;
}
DWORD WINAPI XInputGetKeystroke(DWORD index, DWORD reserved, void* keystroke) {
    return pGetKeystroke ? pGetKeystroke(index, reserved, keystroke) : kNotConnected;
}

}  // extern "C"
