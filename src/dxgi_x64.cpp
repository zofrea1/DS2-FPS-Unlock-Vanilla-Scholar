#include "dxgi_x64.h"

#include "display.h"
#include "iat.h"
#include "limiter.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#if defined(_M_X64)

// The game imports CreateDXGIFactory (it lists the display modes with it) and D3D11CreateDevice. Its swap chain
// comes from a factory, so the factory's CreateSwapChain is hooked through its vtable, which every factory of that
// class shares; the factory behind the device is hooked as well in case it is a different class. The swap chain's
// Present, SetFullscreenState and ResizeTarget are hooked the same way.

namespace {

using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
using CreateDeviceFn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT,
                                        UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
using CreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*,
                                                      IDXGISwapChain**);
using CreateSwapChainForHwndFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND,
                                                             const DXGI_SWAP_CHAIN_DESC1*,
                                                             const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*,
                                                             IDXGISwapChain1**);
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using SetFullscreenStateFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, BOOL, IDXGIOutput*);
using ResizeTargetFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, const DXGI_MODE_DESC*);

constexpr int kCreateSwapChain = 10;         // IDXGIFactory
constexpr int kCreateSwapChainForHwnd = 15;  // IDXGIFactory2
constexpr int kPresent = 8;                  // IDXGISwapChain
constexpr int kSetFullscreenState = 10;
constexpr int kResizeTarget = 14;

IatHook g_iat_factory;
IatHook g_iat_device;

// One patched vtable entry. Lookups go by the object's vtable, so a second class gets its own originals.
struct Slot {
    void** vtable;
    int index;
    void* original;
};
constexpr int kMaxSlots = 32;
Slot g_slots[kMaxSlots];
int g_slot_count = 0;
SRWLOCK g_slot_lock = SRWLOCK_INIT;

LARGE_INTEGER g_qpc_freq{};
LARGE_INTEGER g_present_t0{};
volatile LONG g_present_count = 0;
volatile LONG g_present_logged = 0;
volatile LONG g_refresh_logs = 0;
bool g_vsync = false;

#ifndef DXGI_PRESENT_ALLOW_TEARING
#define DXGI_PRESENT_ALLOW_TEARING 0x00000200UL
#endif

void** vtable_of(void* object) {
    return *static_cast<void***>(object);
}

void* original_for(void* object, int index) {
    void** vtable = vtable_of(object);
    AcquireSRWLockShared(&g_slot_lock);
    void* original = nullptr;
    for (int i = 0; i < g_slot_count; ++i) {
        if (g_slots[i].vtable == vtable && g_slots[i].index == index) {
            original = g_slots[i].original;
            break;
        }
    }
    ReleaseSRWLockShared(&g_slot_lock);
    return original;
}

void patch_slot(void* object, int index, void* detour, const char* name) {
    void** vtable = vtable_of(object);
    AcquireSRWLockExclusive(&g_slot_lock);
    bool known = false;
    for (int i = 0; i < g_slot_count; ++i) {
        known |= g_slots[i].vtable == vtable && g_slots[i].index == index;
    }
    DWORD old = 0;
    if (!known && g_slot_count < kMaxSlots &&
        VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &old)) {
        g_slots[g_slot_count++] = {vtable, index, vtable[index]};
        vtable[index] = detour;
        VirtualProtect(&vtable[index], sizeof(void*), old, &old);
        LOG_INFO("hk_%s: apply successful", name);
    }
    ReleaseSRWLockExclusive(&g_slot_lock);
}

// A fullscreen request below the target rate is raised to it. An unspecified rate (0) is left to DXGI, which
// takes the desktop's (DXVK does).
void raise_refresh(DXGI_RATIONAL& rate, const char* where) {
    const unsigned target = display_fullscreen_hz();
    if (!target || !rate.Numerator || !rate.Denominator) {
        return;
    }
    const double hz = static_cast<double>(rate.Numerator) / static_cast<double>(rate.Denominator);
    if (hz + 0.5 >= static_cast<double>(target)) {
        return;
    }
    if (InterlockedIncrement(&g_refresh_logs) <= 16) {
        LOG_INFO("%s: fullscreen refresh %.2f Hz -> %u Hz", where, hz, target);
    }
    rate.Numerator = target;
    rate.Denominator = 1;
}

void restart_present_stats() {
    InterlockedExchange(&g_present_count, 0);
    InterlockedExchange(&g_present_logged, 0);
}

HRESULT STDMETHODCALLTYPE hk_Present(IDXGISwapChain* self, UINT sync, UINT flags) {
    auto present = reinterpret_cast<PresentFn>(original_for(self, kPresent));
    BOOL fullscreen = FALSE;
    if (FAILED(self->GetFullscreenState(&fullscreen, nullptr))) {
        fullscreen = FALSE;
    }
    const UINT asked = sync;
    if (fullscreen) {
        if (g_vsync) {
            // One vblank at the refresh CreateSwapChain asked for. Tearing flags are only valid with interval 0,
            // and DO_NOT_WAIT is rejected when the interval is not 0.
            sync = 1;
            flags &= ~(DXGI_PRESENT_DO_NOT_WAIT | DXGI_PRESENT_ALLOW_TEARING);
        } else {
            sync = 0;
        }
    }
    limiter_wait_present();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const HRESULT hr = present ? present(self, sync, flags) : E_FAIL;
    const LONG count = InterlockedIncrement(&g_present_count);
    if (count == 1) {
        g_present_t0 = now;
    } else if (count == 240 && g_qpc_freq.QuadPart != 0 && !InterlockedExchange(&g_present_logged, 1)) {
        const double seconds = static_cast<double>(now.QuadPart - g_present_t0.QuadPart) /
                               static_cast<double>(g_qpc_freq.QuadPart);
        LOG_INFO("Present x240 in %.2fs (%.1f fps) fullscreen=%d sync interval %u -> %u", seconds,
                 seconds > 0.0 ? 239.0 / seconds : 0.0, fullscreen, asked, sync);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE hk_SetFullscreenState(IDXGISwapChain* self, BOOL fullscreen, IDXGIOutput* target) {
    auto set = reinterpret_cast<SetFullscreenStateFn>(original_for(self, kSetFullscreenState));
    const HRESULT hr = set ? set(self, fullscreen, target) : E_FAIL;
    LOG_INFO("SetFullscreenState(%d) hr=0x%08X", fullscreen, static_cast<unsigned>(hr));
    restart_present_stats();
    return hr;
}

HRESULT STDMETHODCALLTYPE hk_ResizeTarget(IDXGISwapChain* self, const DXGI_MODE_DESC* mode) {
    auto resize = reinterpret_cast<ResizeTargetFn>(original_for(self, kResizeTarget));
    if (!resize) {
        return E_FAIL;
    }
    if (!mode) {
        return resize(self, mode);
    }
    DXGI_MODE_DESC copy = *mode;
    raise_refresh(copy.RefreshRate, "ResizeTarget");
    const HRESULT hr = resize(self, &copy);
    LOG_INFO("ResizeTarget %ux%u @ %u/%u hr=0x%08X", copy.Width, copy.Height, copy.RefreshRate.Numerator,
             copy.RefreshRate.Denominator, static_cast<unsigned>(hr));
    restart_present_stats();
    return hr;
}

void hook_swap_chain(IDXGISwapChain* chain) {
    patch_slot(chain, kPresent, reinterpret_cast<void*>(&hk_Present), "IDXGISwapChain_Present");
    patch_slot(chain, kSetFullscreenState, reinterpret_cast<void*>(&hk_SetFullscreenState),
               "IDXGISwapChain_SetFullscreenState");
    patch_slot(chain, kResizeTarget, reinterpret_cast<void*>(&hk_ResizeTarget), "IDXGISwapChain_ResizeTarget");
    restart_present_stats();
}

HRESULT STDMETHODCALLTYPE hk_CreateSwapChain(IDXGIFactory* self, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc,
                                             IDXGISwapChain** out) {
    auto create = reinterpret_cast<CreateSwapChainFn>(original_for(self, kCreateSwapChain));
    if (!create) {
        return E_FAIL;
    }
    if (!desc) {
        return create(self, device, desc, out);
    }
    DXGI_SWAP_CHAIN_DESC copy = *desc;
    raise_refresh(copy.BufferDesc.RefreshRate, "CreateSwapChain");
    const HRESULT hr = create(self, device, &copy, out);
    LOG_INFO("CreateSwapChain hr=0x%08X windowed=%d %ux%u @ %u/%u (asked %u/%u) swap=%u buffers=%u flags=0x%X",
             static_cast<unsigned>(hr), copy.Windowed, copy.BufferDesc.Width, copy.BufferDesc.Height,
             copy.BufferDesc.RefreshRate.Numerator, copy.BufferDesc.RefreshRate.Denominator,
             desc->BufferDesc.RefreshRate.Numerator, desc->BufferDesc.RefreshRate.Denominator,
             static_cast<unsigned>(copy.SwapEffect), copy.BufferCount, copy.Flags);
    if (SUCCEEDED(hr) && out && *out) {
        hook_swap_chain(*out);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE hk_CreateSwapChainForHwnd(IDXGIFactory2* self, IUnknown* device, HWND window,
                                                    const DXGI_SWAP_CHAIN_DESC1* desc,
                                                    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,
                                                    IDXGIOutput* output, IDXGISwapChain1** out) {
    auto create = reinterpret_cast<CreateSwapChainForHwndFn>(original_for(self, kCreateSwapChainForHwnd));
    if (!create) {
        return E_FAIL;
    }
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC copy{};
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* use = fullscreen;
    if (fullscreen) {
        copy = *fullscreen;
        raise_refresh(copy.RefreshRate, "CreateSwapChainForHwnd");
        use = &copy;
    }
    const HRESULT hr = create(self, device, window, desc, use, output, out);
    LOG_INFO("CreateSwapChainForHwnd hr=0x%08X windowed=%d", static_cast<unsigned>(hr), use ? use->Windowed : 1);
    if (SUCCEEDED(hr) && out && *out) {
        hook_swap_chain(*out);
    }
    return hr;
}

void hook_factory(IUnknown* object) {
    IDXGIFactory* factory = nullptr;
    if (FAILED(object->QueryInterface(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory))) || !factory) {
        return;
    }
    patch_slot(factory, kCreateSwapChain, reinterpret_cast<void*>(&hk_CreateSwapChain), "IDXGIFactory_CreateSwapChain");
    IDXGIFactory2* factory2 = nullptr;
    if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory2))) && factory2) {
        patch_slot(factory2, kCreateSwapChainForHwnd, reinterpret_cast<void*>(&hk_CreateSwapChainForHwnd),
                   "IDXGIFactory2_CreateSwapChainForHwnd");
        factory2->Release();
    }
    factory->Release();
}

HRESULT WINAPI hk_CreateDXGIFactory(REFIID riid, void** out) {
    const HRESULT hr = reinterpret_cast<CreateFactoryFn>(g_iat_factory.original)(riid, out);
    if (SUCCEEDED(hr) && out && *out) {
        hook_factory(static_cast<IUnknown*>(*out));
    }
    return hr;
}

HRESULT WINAPI hk_D3D11CreateDevice(IDXGIAdapter* adapter, D3D_DRIVER_TYPE type, HMODULE software, UINT flags,
                                    const D3D_FEATURE_LEVEL* levels, UINT level_count, UINT sdk,
                                    ID3D11Device** device, D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** context) {
    const HRESULT hr = reinterpret_cast<CreateDeviceFn>(g_iat_device.original)(
        adapter, type, software, flags, levels, level_count, sdk, device, level, context);
    if (FAILED(hr) || !device || !*device) {
        return hr;
    }
    IDXGIDevice* dxgi_device = nullptr;
    if (SUCCEEDED((*device)->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device)))) {
        IDXGIAdapter* device_adapter = nullptr;
        if (SUCCEEDED(dxgi_device->GetAdapter(&device_adapter))) {
            IDXGIFactory* factory = nullptr;
            if (SUCCEEDED(device_adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory)))) {
                hook_factory(factory);
                factory->Release();
            }
            device_adapter->Release();
        }
        dxgi_device->Release();
    }
    return hr;
}

// Puts a vtable entry back only while the module that owns the vtable is still loaded.
void restore_slot(const Slot& slot) {
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(slot.vtable), &owner)) {
        return;
    }
    DWORD old = 0;
    if (VirtualProtect(&slot.vtable[slot.index], sizeof(void*), PAGE_READWRITE, &old)) {
        slot.vtable[slot.index] = slot.original;
        VirtualProtect(&slot.vtable[slot.index], sizeof(void*), old, &old);
    }
}

}  // namespace

bool dxgi_apply(bool vsync, int fps_limit) {
    g_vsync = vsync;
    limiter_start(fps_limit);
    LOG_INFO("Fullscreen vsync: %s", vsync ? "on (one vblank at the fullscreen refresh)" : "off (immediate)");
    QueryPerformanceFrequency(&g_qpc_freq);
    bool ok = true;
    if (g_iat_factory.install("dxgi.dll", "CreateDXGIFactory", reinterpret_cast<void*>(&hk_CreateDXGIFactory))) {
        LOG_INFO("hk_CreateDXGIFactory: apply successful");
    } else {
        LOG_ERROR("hk_CreateDXGIFactory: apply failed");
        ok = false;
    }
    if (g_iat_device.install("d3d11.dll", "D3D11CreateDevice", reinterpret_cast<void*>(&hk_D3D11CreateDevice))) {
        LOG_INFO("hk_D3D11CreateDevice: apply successful");
    } else {
        LOG_ERROR("hk_D3D11CreateDevice: apply failed");
        ok = false;
    }
    return ok;
}

void dxgi_remove() {
    limiter_stop();
    g_iat_factory.remove();
    g_iat_device.remove();
    AcquireSRWLockExclusive(&g_slot_lock);
    for (int i = 0; i < g_slot_count; ++i) {
        restore_slot(g_slots[i]);
    }
    g_slot_count = 0;
    ReleaseSRWLockExclusive(&g_slot_lock);
}

#endif  // _M_X64
