#include "inline_hook.h"

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

namespace {

constexpr size_t kPage = 0x1000;
uint8_t* g_page = nullptr;
size_t g_used = 0;

void* arena_alloc(size_t bytes) {
    if (!g_page || g_used + bytes > kPage) {
        return nullptr;
    }
    void* p = g_page + g_used;
    g_used += (bytes + 15) & ~size_t{15};
    return p;
}

bool rel32_fits(const void* from, const void* to) {
    const intptr_t rel = reinterpret_cast<intptr_t>(to) - (reinterpret_cast<intptr_t>(from) + 5);
    return rel >= static_cast<intptr_t>(INT32_MIN) && rel <= static_cast<intptr_t>(INT32_MAX);
}

void write_abs_jmp(uint8_t* at, const void* dest) {
#if defined(_M_X64)
    // jmp qword ptr [rip+0]; dq dest
    at[0] = 0xFF;
    at[1] = 0x25;
    at[2] = 0x00;
    at[3] = 0x00;
    at[4] = 0x00;
    at[5] = 0x00;
    const uint64_t addr = reinterpret_cast<uint64_t>(dest);
    std::memcpy(at + 6, &addr, sizeof(addr));
#else
    // jmp dword ptr [absolute slot]; dd dest
    uint8_t* slot = at + 6;
    at[0] = 0xFF;
    at[1] = 0x25;
    const uint32_t slot_addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(slot));
    const uint32_t dest_addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dest));
    std::memcpy(at + 2, &slot_addr, sizeof(slot_addr));
    std::memcpy(slot, &dest_addr, sizeof(dest_addr));
#endif
}

void write_rel_jmp(uint8_t* at, const void* dest) {
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<intptr_t>(dest) - (reinterpret_cast<intptr_t>(at) + 5));
    at[0] = 0xE9;
    std::memcpy(at + 1, &rel, sizeof(rel));
}

}  // namespace

bool near_arena_init(void* near_addr) {
    if (g_page) {
        return true;
    }
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
    const uintptr_t origin = reinterpret_cast<uintptr_t>(near_addr) & ~(gran - 1);

    for (uintptr_t dist = gran; dist < 0x40000000; dist += gran) {
        const uintptr_t low = dist < origin ? origin - dist : 0;
        const uintptr_t candidates[2] = {low, origin + dist};
        for (uintptr_t cand : candidates) {
            if (cand < 0x10000) {
                continue;
            }
            void* p = VirtualAlloc(reinterpret_cast<void*>(cand), kPage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (!p) {
                continue;
            }
            // The site will jump to the start of this page. Confirm that fits.
            if (!rel32_fits(near_addr, p)) {
                VirtualFree(p, 0, MEM_RELEASE);
                continue;
            }
            g_page = static_cast<uint8_t*>(p);
            g_used = 0;
            return true;
        }
    }
    LOG_ERROR("near_arena_init: could not allocate a hook trampoline within 2GB of the game");
    return false;
}

uint8_t* near_arena_alloc(size_t bytes) {
    if (!g_page) {
        return nullptr;
    }
    return static_cast<uint8_t*>(arena_alloc(bytes));
}

bool InlineHook::install(void* target_site, void* detour, size_t stolen_bytes) {
    if (stolen_bytes < 5 || stolen_bytes > sizeof(original)) {
        return false;
    }
    if (!g_page && !near_arena_init(target_site)) {
        return false;
    }
    // gateway (14) + trampoline (stolen + 14), plus alignment slack
    uint8_t* block = static_cast<uint8_t*>(arena_alloc(stolen_bytes + 48));
    if (!block) {
        LOG_ERROR("InlineHook: trampoline arena exhausted");
        return false;
    }
    uint8_t* gateway = block;
    uint8_t* tramp = block + 16;
    write_abs_jmp(gateway, detour);
    std::memcpy(tramp, target_site, stolen_bytes);
    write_abs_jmp(tramp + stolen_bytes, static_cast<uint8_t*>(target_site) + stolen_bytes);

    if (!rel32_fits(target_site, gateway)) {
        LOG_ERROR("InlineHook: relative jump from %p to gateway does not fit", target_site);
        return false;
    }

    DWORD old = 0;
    if (!VirtualProtect(target_site, stolen_bytes, PAGE_EXECUTE_READWRITE, &old)) {
        LOG_ERROR("InlineHook: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }
    std::memcpy(original, target_site, stolen_bytes);
    write_rel_jmp(static_cast<uint8_t*>(target_site), gateway);
    for (size_t i = 5; i < stolen_bytes; ++i) {
        static_cast<uint8_t*>(target_site)[i] = 0x90;
    }
    VirtualProtect(target_site, stolen_bytes, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target_site, stolen_bytes);
    FlushInstructionCache(GetCurrentProcess(), block, stolen_bytes + 32);

    target = target_site;
    trampoline = tramp;
    stolen = stolen_bytes;
    installed = true;
    return true;
}

void InlineHook::remove() {
    if (!installed || !target) {
        return;
    }
    DWORD old = 0;
    if (VirtualProtect(target, stolen, PAGE_EXECUTE_READWRITE, &old)) {
        std::memcpy(target, original, stolen);
        VirtualProtect(target, stolen, old, &old);
        FlushInstructionCache(GetCurrentProcess(), target, stolen);
    }
    installed = false;
}
