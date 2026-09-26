#include "present_x86.h"

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

#include <cstdint>
#include <cstring>

#pragma comment(lib, "user32.lib")

// The vanilla EXE creates its device in DLDrawDevice (call at 0xB2E461).
// Fullscreen PresentationInterval comes back as INTERVAL_TWO whenever the
// requested refresh is 59 or 60, which scans out at half of a 120 Hz panel.
// Windowed mode already asks for INTERVAL_IMMEDIATE. The picture still stays
// at 60 because the frames are blitted into the desktop compositor. A
// flip-model swapchain was tried and crashed during boot, so windowed
// parameters are passed through unchanged. Fullscreen is rewritten to
// immediate presentation at the monitor's current refresh.

namespace {

using Create9Fn = IDirect3D9*(WINAPI*)(UINT);
using CreateDeviceFn = HRESULT(WINAPI*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*,
                                       IDirect3DDevice9**);
using ResetFn = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using PresentFn = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);

struct Slot {
    void** addr = nullptr;
    void* original = nullptr;
};

Create9Fn g_create9 = nullptr;
void** g_iat = nullptr;
Slot g_create_device;
Slot g_reset;
Slot g_present;

bool g_applied = false;

UINT g_swap = 0;
UINT g_interval = 0;
BOOL g_windowed = FALSE;

LARGE_INTEGER g_qpc_freq{};
LARGE_INTEGER g_present_t0{};
unsigned g_present_count = 0;
bool g_present_logged = false;

UINT monitor_hz(HWND hwnd) {
    wchar_t device[CCHDEVICENAME] = L"";
    const wchar_t* name = nullptr;
    if (hwnd) {
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (monitor && GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(&info))) {
            wcsncpy_s(device, info.szDevice, _TRUNCATE);
            name = device;
        }
    }
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(name, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1) {
        return mode.dmDisplayFrequency;
    }
    return 0;
}

void force_immediate(D3DPRESENT_PARAMETERS* pp) {
    if (pp->Windowed) {
        return;
    }
    pp->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    const UINT hz = monitor_hz(pp->hDeviceWindow);
    const UINT refresh = pp->FullScreen_RefreshRateInHz;
    if (hz > 60 && (refresh == 0 || refresh == 59 || refresh == 60)) {
        pp->FullScreen_RefreshRateInHz = hz;
    }
}

void restore_slot(Slot* slot) {
    if (!slot->addr || !slot->original) {
        slot->addr = nullptr;
        return;
    }
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(slot->addr), &owner)) {
        slot->addr = nullptr;
        return;
    }
    DWORD old = 0;
    if (VirtualProtect(slot->addr, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
        *slot->addr = slot->original;
        VirtualProtect(slot->addr, sizeof(void*), old, &old);
    }
    slot->addr = nullptr;
    slot->original = nullptr;
}

void patch_slot(void* object, int index, void* detour, Slot* saved) {
    auto** vtable = *reinterpret_cast<void***>(object);
    void** slot = &vtable[index];
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
        LOG_ERROR("present vtable protect failed (%lu)", GetLastError());
        return;
    }
    if (!saved->addr) {
        saved->addr = slot;
        saved->original = *slot;
    }
    *slot = detour;
    VirtualProtect(slot, sizeof(void*), old, &old);
}

void remember(const D3DPRESENT_PARAMETERS& pp) {
    g_swap = pp.SwapEffect;
    g_interval = pp.PresentationInterval;
    g_windowed = pp.Windowed;
    g_present_count = 0;
    g_present_logged = false;
}

void log_params(const char* what, const char* attempt, HRESULT hr, const D3DPRESENT_PARAMETERS& pp) {
    LOG_INFO("%s %s hr=0x%08X windowed=%ld %ux%u refresh=%u interval=0x%X swap=%u msaa=%u count=%u", what, attempt,
             static_cast<unsigned>(hr), pp.Windowed, pp.BackBufferWidth, pp.BackBufferHeight,
             pp.FullScreen_RefreshRateInHz, pp.PresentationInterval, static_cast<unsigned>(pp.SwapEffect),
             static_cast<unsigned>(pp.MultiSampleType), pp.BackBufferCount);
}

HRESULT WINAPI hook_Present(IDirect3DDevice9* self, const RECT* source, const RECT* dest, HWND window,
                            const RGNDATA* dirty) {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    auto* present = reinterpret_cast<PresentFn>(g_present.original);
    const HRESULT hr = present(self, source, dest, window, dirty);
    if (g_present_count == 0) {
        g_present_t0 = now;
    }
    ++g_present_count;
    if (!g_present_logged && g_present_count == 240 && g_qpc_freq.QuadPart != 0) {
        g_present_logged = true;
        const double seconds = static_cast<double>(now.QuadPart - g_present_t0.QuadPart) / static_cast<double>(g_qpc_freq.QuadPart);
        const double fps = seconds > 0.0 ? 239.0 / seconds : 0.0;
        LOG_INFO("Present x240 in %.2fs (%.1f fps) windowed=%ld interval=0x%X swap=%u", seconds, fps, g_windowed,
                 g_interval, g_swap);
    }
    return hr;
}

void fill_attempts(const D3DPRESENT_PARAMETERS& original, D3DPRESENT_PARAMETERS* out, const char** names, int* count) {
    int n = 0;
    out[n] = original;
    force_immediate(&out[n]);
    names[n] = original.Windowed ? "windowed" : "immediate";
    ++n;
    // A rejected fullscreen mode falls back to the game's original parameters.
    if (!original.Windowed) {
        out[n] = original;
        names[n] = "original";
        ++n;
    }
    *count = n;
}

HRESULT WINAPI hook_Reset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* pp) {
    auto* reset = reinterpret_cast<ResetFn>(g_reset.original);
    if (!pp) {
        return reset(self, pp);
    }
    const D3DPRESENT_PARAMETERS original = *pp;
    D3DPRESENT_PARAMETERS tries[3]{};
    const char* names[3] = {};
    int count = 0;
    fill_attempts(original, tries, names, &count);
    HRESULT hr = E_FAIL;
    for (int i = 0; i < count; ++i) {
        *pp = tries[i];
        hr = reset(self, pp);
        log_params("Reset", names[i], hr, *pp);
        if (SUCCEEDED(hr)) {
            remember(*pp);
            return hr;
        }
    }
    return hr;
}

HRESULT WINAPI hook_CreateDevice(IDirect3D9* self, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior,
                                 D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out_device) {
    auto* create = reinterpret_cast<CreateDeviceFn>(g_create_device.original);
    if (!pp) {
        return create(self, adapter, type, focus, behavior, pp, out_device);
    }
    const D3DPRESENT_PARAMETERS original = *pp;
    LOG_INFO("CreateDevice request windowed=%ld %ux%u refresh=%u interval=0x%X swap=%u msaa=%u desktop=%u Hz",
             original.Windowed, original.BackBufferWidth, original.BackBufferHeight, original.FullScreen_RefreshRateInHz,
             original.PresentationInterval, static_cast<unsigned>(original.SwapEffect),
             static_cast<unsigned>(original.MultiSampleType), monitor_hz(focus));

    D3DPRESENT_PARAMETERS tries[3]{};
    const char* names[3] = {};
    int count = 0;
    fill_attempts(original, tries, names, &count);
    HRESULT hr = E_FAIL;
    for (int i = 0; i < count; ++i) {
        *pp = tries[i];
        hr = create(self, adapter, type, focus, behavior, pp, out_device);
        log_params("CreateDevice", names[i], hr, *pp);
        if (SUCCEEDED(hr)) {
            remember(*pp);
            if (out_device && *out_device) {
                patch_slot(*out_device, 16, reinterpret_cast<void*>(&hook_Reset), &g_reset);
                patch_slot(*out_device, 17, reinterpret_cast<void*>(&hook_Present), &g_present);
            }
            return hr;
        }
    }
    return hr;
}

IDirect3D9* WINAPI hook_Direct3DCreate9(UINT sdk) {
    IDirect3D9* d3d = g_create9(sdk);
    if (d3d) {
        patch_slot(d3d, 16, reinterpret_cast<void*>(&hook_CreateDevice), &g_create_device);
        LOG_INFO("Direct3DCreate9 sdk=%u", sdk);
    }
    return d3d;
}

bool patch_iat() {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) {
        return false;
    }
    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; desc->Name; ++desc) {
        const char* dll = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(dll, "d3d9.dll") != 0 || !desc->OriginalFirstThunk) {
            continue;
        }
        auto* hint = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        for (; hint->u1.AddressOfData; ++hint, ++iat) {
            if (IMAGE_SNAP_BY_ORDINAL(hint->u1.Ordinal)) {
                continue;
            }
            auto* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + hint->u1.AddressOfData);
            if (std::strcmp(name->Name, "Direct3DCreate9") != 0) {
                continue;
            }
            DWORD old = 0;
            if (!VirtualProtect(&iat->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
                return false;
            }
            g_create9 = reinterpret_cast<Create9Fn>(iat->u1.Function);
            g_iat = reinterpret_cast<void**>(&iat->u1.Function);
            iat->u1.Function = reinterpret_cast<ULONG_PTR>(&hook_Direct3DCreate9);
            VirtualProtect(&iat->u1.Function, sizeof(void*), old, &old);
            return g_create9 != nullptr;
        }
    }
    return false;
}

}  // namespace

bool present_apply() {
    QueryPerformanceFrequency(&g_qpc_freq);
    if (!patch_iat()) {
        LOG_ERROR("hk_Direct3DCreate9: apply failed");
        return false;
    }
    LOG_INFO("hk_Direct3DCreate9: apply successful");
    g_applied = true;
    return true;
}

void present_remove() {
    if (!g_applied) {
        return;
    }
    if (g_iat && g_create9) {
        DWORD old = 0;
        if (VirtualProtect(g_iat, sizeof(void*), PAGE_READWRITE, &old)) {
            *g_iat = reinterpret_cast<void*>(g_create9);
            VirtualProtect(g_iat, sizeof(void*), old, &old);
        }
    }
    restore_slot(&g_create_device);
    restore_slot(&g_reset);
    restore_slot(&g_present);
    g_iat = nullptr;
    g_applied = false;
}
