// Exports for the other names this DLL can be installed under (proxy.h). Each passes the call to the real DLL's
// export of the same name, loaded on first use. Under a name they do not belong to they are never called.

#include "proxy.h"

#include <cstdint>

namespace {

constexpr HRESULT kFail = static_cast<HRESULT>(0x80004005L);  // E_FAIL

}  // namespace

#if defined(_M_X64)

namespace {

template <typename Fn>
Fn real(const char* name) {
    return reinterpret_cast<Fn>(proxy_real_proc(name));
}

}  // namespace

extern "C" {

// dxgi.dll. The game imports CreateDXGIFactory; d3d11.dll imports CreateDXGIFactory1 (DXVK) or
// CreateDXGIFactory2 (Windows) from whichever dxgi.dll is loaded, which is this one.
HRESULT WINAPI fwd_CreateDXGIFactory(const GUID& riid, void** factory) {
    static const auto fn = real<HRESULT(WINAPI*)(const GUID&, void**)>("CreateDXGIFactory");
    return fn ? fn(riid, factory) : kFail;
}
HRESULT WINAPI fwd_CreateDXGIFactory1(const GUID& riid, void** factory) {
    static const auto fn = real<HRESULT(WINAPI*)(const GUID&, void**)>("CreateDXGIFactory1");
    return fn ? fn(riid, factory) : kFail;
}
HRESULT WINAPI fwd_CreateDXGIFactory2(UINT flags, const GUID& riid, void** factory) {
    static const auto fn = real<HRESULT(WINAPI*)(UINT, const GUID&, void**)>("CreateDXGIFactory2");
    return fn ? fn(flags, riid, factory) : kFail;
}
HRESULT WINAPI fwd_DXGIGetDebugInterface1(UINT flags, const GUID& riid, void** debug) {
    static const auto fn = real<HRESULT(WINAPI*)(UINT, const GUID&, void**)>("DXGIGetDebugInterface1");
    return fn ? fn(flags, riid, debug) : kFail;
}
HRESULT WINAPI fwd_DXGIDeclareAdapterRemovalSupport() {
    static const auto fn = real<HRESULT(WINAPI*)()>("DXGIDeclareAdapterRemovalSupport");
    return fn ? fn() : kFail;
}

// dinput8.dll.
HRESULT WINAPI fwd_DirectInput8Create(HINSTANCE instance, DWORD version, const GUID& riid, void** out, void* outer) {
    static const auto fn = real<HRESULT(WINAPI*)(HINSTANCE, DWORD, const GUID&, void**, void*)>("DirectInput8Create");
    return fn ? fn(instance, version, riid, out, outer) : kFail;
}
HRESULT WINAPI fwd_DllCanUnloadNow() {
    static const auto fn = real<HRESULT(WINAPI*)()>("DllCanUnloadNow");
    return fn ? fn() : S_FALSE;
}
HRESULT WINAPI fwd_DllGetClassObject(const GUID& clsid, const GUID& riid, void** out) {
    static const auto fn = real<HRESULT(WINAPI*)(const GUID&, const GUID&, void**)>("DllGetClassObject");
    return fn ? fn(clsid, riid, out) : kFail;
}
HRESULT WINAPI fwd_DllRegisterServer() {
    static const auto fn = real<HRESULT(WINAPI*)()>("DllRegisterServer");
    return fn ? fn() : kFail;
}
HRESULT WINAPI fwd_DllUnregisterServer() {
    static const auto fn = real<HRESULT(WINAPI*)()>("DllUnregisterServer");
    return fn ? fn() : kFail;
}
const void* WINAPI fwd_GetdfDIJoystick() {
    static const auto fn = real<const void*(WINAPI*)()>("GetdfDIJoystick");
    return fn ? fn() : nullptr;
}

}  // extern "C"

#else  // 32-bit: one jump per export, so no signatures are needed (several are undocumented)

extern "C" {

// Taken in place of a missing export: E_FAIL (0x80004005), removing that export's stdcall arguments.
__declspec(naked) void fwd_fail0() { __asm mov eax, 0x80004005 __asm ret }
__declspec(naked) void fwd_fail4() { __asm mov eax, 0x80004005 __asm ret 4 }
__declspec(naked) void fwd_fail8() { __asm mov eax, 0x80004005 __asm ret 8 }
__declspec(naked) void fwd_fail12() { __asm mov eax, 0x80004005 __asm ret 12 }
__declspec(naked) void fwd_fail16() { __asm mov eax, 0x80004005 __asm ret 16 }
__declspec(naked) void fwd_fail20() { __asm mov eax, 0x80004005 __asm ret 20 }

}  // extern "C"

namespace {

struct Forward {
    const char* name;
    void (*fail)();
};

// The index of each entry is the number its thunk pushes (FWD below).
const Forward kForwards[] = {
    // d3d9.dll
    {"Direct3DCreate9", fwd_fail4},
    {"Direct3DCreate9Ex", fwd_fail8},
    {"D3DPERF_BeginEvent", fwd_fail8},
    {"D3DPERF_EndEvent", fwd_fail0},
    {"D3DPERF_GetStatus", fwd_fail0},
    {"D3DPERF_QueryRepeatFrame", fwd_fail0},
    {"D3DPERF_SetMarker", fwd_fail8},
    {"D3DPERF_SetOptions", fwd_fail4},
    {"D3DPERF_SetRegion", fwd_fail8},
    {"DebugSetLevel", fwd_fail0},
    {"DebugSetMute", fwd_fail0},
    {"Direct3D9EnableMaximizedWindowedModeShim", fwd_fail4},
    {"Direct3DShaderValidatorCreate9", fwd_fail0},
    {"PSGPError", fwd_fail12},
    {"PSGPSampleTexture", fwd_fail20},
    {"Direct3DCreate9On12", fwd_fail12},
    {"Direct3DCreate9On12Ex", fwd_fail16},
    // dinput8.dll
    {"DirectInput8Create", fwd_fail20},
    {"DllCanUnloadNow", fwd_fail0},
    {"DllGetClassObject", fwd_fail12},
    {"DllRegisterServer", fwd_fail0},
    {"DllUnregisterServer", fwd_fail0},
    {"GetdfDIJoystick", fwd_fail0},
};
constexpr int kForwardCount = static_cast<int>(sizeof(kForwards) / sizeof(kForwards[0]));

}  // namespace

extern "C" {

void* g_fwd_target[kForwardCount] = {};

void* __stdcall fwd_resolve(int index) {
    void* target = reinterpret_cast<void*>(proxy_real_proc(kForwards[index].name));
    if (!target) {
        target = reinterpret_cast<void*>(kForwards[index].fail);
    }
    InterlockedExchangePointer(&g_fwd_target[index], target);
    return target;
}

// [esp] is the export's index, pushed by its thunk; [esp+4] the caller's return address. The stack is left as the
// caller made it before jumping on, so the real export sees its own arguments.
__declspec(naked) void fwd_dispatch() {
    __asm {
        mov eax, dword ptr [esp]
        mov eax, g_fwd_target[eax * 4]
        test eax, eax
        jz resolve
        add esp, 4
        jmp eax
    resolve:
        call fwd_resolve
        jmp eax
    }
}

#define FWD(index, name) \
    __declspec(naked) void fwd_##name() { __asm push index __asm jmp fwd_dispatch }

FWD(0, Direct3DCreate9)
FWD(1, Direct3DCreate9Ex)
FWD(2, D3DPERF_BeginEvent)
FWD(3, D3DPERF_EndEvent)
FWD(4, D3DPERF_GetStatus)
FWD(5, D3DPERF_QueryRepeatFrame)
FWD(6, D3DPERF_SetMarker)
FWD(7, D3DPERF_SetOptions)
FWD(8, D3DPERF_SetRegion)
FWD(9, DebugSetLevel)
FWD(10, DebugSetMute)
FWD(11, Direct3D9EnableMaximizedWindowedModeShim)
FWD(12, Direct3DShaderValidatorCreate9)
FWD(13, PSGPError)
FWD(14, PSGPSampleTexture)
FWD(15, Direct3DCreate9On12)
FWD(16, Direct3DCreate9On12Ex)
FWD(17, DirectInput8Create)
FWD(18, DllCanUnloadNow)
FWD(19, DllGetClassObject)
FWD(20, DllRegisterServer)
FWD(21, DllUnregisterServer)
FWD(22, GetdfDIJoystick)

#undef FWD

}  // extern "C"

#endif
