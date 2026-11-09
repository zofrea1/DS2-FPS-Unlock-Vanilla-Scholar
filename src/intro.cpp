#include "intro.h"

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace {

constexpr char kInitBranchName[] = ".?AVFeSubStateTitleInitBranch@@";
constexpr int kTransitionSlot = 5;

#ifdef _WIN64
// Transition list (the call's second argument): count at +0x158, pointers from +8, room for 0x2A.
constexpr size_t kListCount = 0x158;
constexpr size_t kListItems = 8;
// Transition: +0 vtable, +8 target id, +0x18 watched int*, +0x20 value. InitBranch's choice is at +0x10.
constexpr size_t kTarget = 8;
constexpr size_t kWatched = 0x18;
constexpr size_t kValue = 0x20;
constexpr size_t kChoice = 0x10;
using SlotFn = void*(__fastcall*)(void* self, void* list, void* alloc);
#else
constexpr size_t kListCount = 0xAC;
constexpr size_t kListItems = 4;
constexpr size_t kTarget = 4;
constexpr size_t kWatched = 0x10;
constexpr size_t kValue = 0x14;
constexpr size_t kChoice = 0xC;
// thiscall (ecx = self, two stack arguments, ret 8), called as fastcall with a dummy edx.
using SlotFn = void*(__fastcall*)(void* self, void* edx, void* list, void* alloc);
#endif
constexpr int kListCapacity = 0x2A;

SlotFn g_orig = nullptr;
std::atomic<bool> g_logged{false};

template <typename T>
T& at(void* base, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

// The first-entry transition gets the target of the returning one. Anything unexpected is left alone.
void retarget(void* self, void* list, int before) {
    const int after = at<int>(list, kListCount);
    if (before < 0 || after > kListCapacity || after - before != 2) {
        if (!g_logged.exchange(true)) {
            LOG_ERROR("SkipIntro: the title's start branch added %d transitions instead of 2; start-up left as is",
                      after - before);
        }
        return;
    }
    void** items = &at<void*>(list, kListItems);
    void* first = nullptr;
    void* returning = nullptr;
    for (int i = before; i < after; ++i) {
        void* t = items[i];
        if (!t || at<int*>(t, kWatched) != &at<int>(self, kChoice)) {
            first = returning = nullptr;
            break;
        }
        if (at<int>(t, kValue) == 0) {
            first = t;
        } else if (at<int>(t, kValue) == 1) {
            returning = t;
        }
    }
    if (!first || !returning) {
        if (!g_logged.exchange(true)) {
            LOG_ERROR("SkipIntro: the title's start branch has an unexpected layout; start-up left as is");
        }
        return;
    }
    const int from = at<int>(first, kTarget);
    const int to = at<int>(returning, kTarget);
    at<int>(first, kTarget) = to;
    if (!g_logged.exchange(true)) {
        LOG_INFO("SkipIntro: the title starts at sub-state %d (title screen) instead of %d (start-up notice and logos)",
                 to, from);
    }
}

#ifdef _WIN64
void* __fastcall hk_transitions(void* self, void* list, void* alloc) {
    const int before = list ? at<int>(list, kListCount) : -1;
    void* r = g_orig(self, list, alloc);
    if (list) retarget(self, list, before);
    return r;
}
#else
void* __fastcall hk_transitions(void* self, void* edx, void* list, void* alloc) {
    const int before = list ? at<int>(list, kListCount) : -1;
    void* r = g_orig(self, edx, list, alloc);
    if (list) retarget(self, list, before);
    return r;
}
#endif

struct Range {
    uint8_t* lo;
    uint8_t* hi;
};

bool readable(const uint8_t* p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return p + n <= static_cast<uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
}

// Every committed, readable range of the module's sections.
template <typename F>
void for_each_section(uint8_t* base, F f) {
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_READ)) continue;
        uint8_t* lo = base + sec->VirtualAddress;
        uint8_t* end = lo + sec->Misc.VirtualSize;
        // Walk committed regions so an unmapped tail is never touched.
        for (uint8_t* p = lo; p < end;) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
            uint8_t* rend = static_cast<uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
            if (rend > end) rend = end;
            if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
                if (f(Range{p, rend})) return;
            }
            p = rend;
        }
    }
}

// Finds `value` (n bytes) aligned to `align` in the module; calls test(address) on each hit until it says yes.
template <typename Test>
uint8_t* find_aligned(uint8_t* base, const void* value, size_t n, size_t align, Test test) {
    uint8_t* found = nullptr;
    for_each_section(base, [&](Range r) {
        uint8_t* p = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(r.lo) + align - 1) & ~(align - 1));
        for (; p + n <= r.hi; p += align) {
            if (std::memcmp(p, value, n) == 0 && test(p)) {
                found = p;
                return true;
            }
        }
        return false;
    });
    return found;
}

// The vtable of the class with RTTI name `name` (complete object, offset 0).
void** find_vtable(uint8_t* base, const char* name) {
    const size_t len = std::strlen(name) + 1;
    uint8_t* str = find_aligned(base, name, len, 1, [](uint8_t*) { return true; });
    if (!str) return nullptr;
    uint8_t* td = str - 2 * sizeof(void*);  // TypeDescriptor: vfptr, spare, name
#ifdef _WIN64
    // CompleteObjectLocator: signature 1, offset, cdOffset, TypeDescriptor RVA, ClassHierarchy RVA, self RVA.
    const uint32_t td_rva = static_cast<uint32_t>(td - base);
    uint8_t* col_td = find_aligned(base, &td_rva, 4, 4, [&](uint8_t* p) {
        if (!readable(p - 12, 24)) return false;
        const auto* d = reinterpret_cast<const uint32_t*>(p - 12);
        return d[0] == 1 && d[1] == 0 && d[5] == static_cast<uint32_t>(p - 12 - base);
    });
    if (!col_td) return nullptr;
    const uint64_t col = reinterpret_cast<uint64_t>(col_td - 12);
#else
    // CompleteObjectLocator: signature 0, offset, cdOffset, TypeDescriptor*, ClassHierarchy*.
    const uint32_t td_va = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(td));
    uint8_t* col_td = find_aligned(base, &td_va, 4, 4, [&](uint8_t* p) {
        if (!readable(p - 12, 16)) return false;
        const auto* d = reinterpret_cast<const uint32_t*>(p - 12);
        return d[0] == 0 && d[1] == 0;
    });
    if (!col_td) return nullptr;
    const uint32_t col = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(col_td - 12));
#endif
    uint8_t* meta = find_aligned(base, &col, sizeof(col), sizeof(void*), [](uint8_t*) { return true; });
    return meta ? reinterpret_cast<void**>(meta + sizeof(void*)) : nullptr;
}

bool in_module_code(uint8_t* base, void* p) {
    bool hit = false;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        uint8_t* lo = base + sec->VirtualAddress;
        if ((sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) && p >= lo && p < lo + sec->Misc.VirtualSize) hit = true;
    }
    return hit;
}

}  // namespace

#ifdef INTRO_TEST
// Test harness entry points (scratchpad): the lookup on a mapped image, and the retarget on fake objects.
extern "C" __declspec(dllexport) void** intro_test_vtable(uint8_t* base) { return find_vtable(base, kInitBranchName); }
extern "C" __declspec(dllexport) void intro_test_retarget(void* self, void* list, int before) {
    retarget(self, list, before);
}
#endif

bool intro_install() {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    void** vtable = find_vtable(base, kInitBranchName);
    if (!vtable || !readable(reinterpret_cast<uint8_t*>(vtable), (kTransitionSlot + 1) * sizeof(void*))) {
        LOG_ERROR("SkipIntro: the title's start branch (FeSubStateTitleInitBranch) was not found; start-up left as is");
        return false;
    }
    void* slot = vtable[kTransitionSlot];
    if (!in_module_code(base, slot)) {
        LOG_ERROR("SkipIntro: the start branch's transitions point outside the game (%p, hooked by another mod?); "
                  "start-up left as is", slot);
        return false;
    }
    g_orig = reinterpret_cast<SlotFn>(slot);
    DWORD old = 0;
    if (!VirtualProtect(&vtable[kTransitionSlot], sizeof(void*), PAGE_READWRITE, &old)) {
        LOG_ERROR("SkipIntro: could not hook the title's start branch (Win32=%lu)", GetLastError());
        return false;
    }
    vtable[kTransitionSlot] = reinterpret_cast<void*>(&hk_transitions);
    VirtualProtect(&vtable[kTransitionSlot], sizeof(void*), old, &old);
    LOG_INFO("SkipIntro: apply successful (start branch vtable RVA 0x%X, transitions at RVA 0x%X)",
             static_cast<unsigned>(reinterpret_cast<uint8_t*>(vtable) - base),
             static_cast<unsigned>(static_cast<uint8_t*>(slot) - base));
    return true;
}
