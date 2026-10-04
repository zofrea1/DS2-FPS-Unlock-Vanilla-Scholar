#include "patches.h"

#include "frame_fixes.h"
#include "inline_hook.h"
#include "log.h"
#include "present_x86.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

// Original Dark Souls II (32-bit, DirectX 9). The Scholar hooks do not exist
// in this image. These sites were taken from the Steam build timestamped
// 1665659056.

extern "C" {
void* g_sampler_tramp = nullptr;
void* g_step_tramp = nullptr;
void* g_dur_weapon_tramp = nullptr;
void* g_dur_armor_tramp = nullptr;
void* g_dur_ring_tramp = nullptr;
void* g_world_tramp = nullptr;
float g_dur_scale = 1.0f;
float g_frame_dt = 1.0f / 60.0f;
float g_xmm_save = 0.0f;
// 1/PhysicsFPS, written into hkpWorldCinfo before the world is built.
float g_physics_dt = 1.0f / 60.0f;

// ecx is the world. [esp+4] is hkpWorldCinfo*. m_expectedMinPsiDeltaTime is +0x64
// on this 32-bit Havok (the game itself stores 1/60 there before the call).
__declspec(naked) void hk_world() {
    __asm {
        mov eax, dword ptr [esp + 4]
        test eax, eax
        je no_cinfo
        movss xmm0, dword ptr [g_physics_dt]
        movss dword ptr [eax + 0x64], xmm0
    no_cinfo:
        jmp dword ptr [g_world_tramp]
    }
}

void __cdecl publish_frame_dt(float dt) {
    if (dt > 0.0008f && dt < 1.0f) {
        g_frame_dt = dt;
    }
    frame_clock_advance(g_frame_dt);
    float scale = 1.0f;
    if (dt > 0.0008f) {
        scale = dt * 60.0f;
        if (scale < 0.05f) {
            scale = 0.05f;
        } else if (scale > 2.0f) {
            scale = 2.0f;
        }
    }
    g_dur_scale = scale;
}

__declspec(naked) void hk_sampler() {
    __asm {
        mov byte ptr [esp + 4], 0
        jmp dword ptr [g_sampler_tramp]
    }
}

__declspec(naked) void hk_step() {
    __asm {
        push ecx
        push dword ptr [esp + 8]
        call publish_frame_dt
        add esp, 4
        pop ecx
        jmp dword ptr [g_step_tramp]
    }
}

__declspec(naked) void hk_dur_weapon() {
    __asm {
        movss dword ptr [g_xmm_save], xmm1
        movss xmm1, dword ptr [esp + 8]
        mulss xmm1, dword ptr [g_dur_scale]
        movss dword ptr [esp + 8], xmm1
        movss xmm1, dword ptr [g_xmm_save]
        jmp dword ptr [g_dur_weapon_tramp]
    }
}

__declspec(naked) void hk_dur_armor() {
    __asm {
        movss dword ptr [g_xmm_save], xmm1
        movss xmm1, dword ptr [esp + 8]
        mulss xmm1, dword ptr [g_dur_scale]
        movss dword ptr [esp + 8], xmm1
        movss xmm1, dword ptr [g_xmm_save]
        jmp dword ptr [g_dur_armor_tramp]
    }
}

__declspec(naked) void hk_dur_ring() {
    __asm {
        movss dword ptr [g_xmm_save], xmm1
        movss xmm1, dword ptr [esp + 8]
        mulss xmm1, dword ptr [g_dur_scale]
        movss dword ptr [esp + 8], xmm1
        movss xmm1, dword ptr [g_xmm_save]
        jmp dword ptr [g_dur_ring_tramp]
    }
}
}

namespace {

constexpr DWORD kVanillaStamp = 1665659056;

void __stdcall hk_pacer(void* a, void* b, void* c) {
    (void)a;
    (void)b;
    (void)c;
}

const uint8_t kSampler[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x3C, 0x53, 0x56, 0x8B, 0xF1, 0x8B, 0x46};
const uint8_t kPacer[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x57, 0x8B, 0xF9, 0x8B, 0x57, 0x24};
// call dword ptr [iat] — the address is relocated, so those four bytes are wild.
const uint8_t kStep[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0xF3};
const char kStepMask[] = "xxxxxxxxxxx????x";
// cmp byte [esi+100h], 0; je measured. The 0x74 is at +7.
const uint8_t kMeasured[] = {
    0x80, 0xBE, 0x00, 0x01, 0x00, 0x00, 0x00, 0x74, 0x2D, 0x8B, 0x86, 0xFC, 0x00, 0x00, 0x00, 0x48};
// jbe; lea eax, [ebp+8]; jmp. The displacement 0x08 is at +4. FC selects [ebp-4].
const uint8_t kClamp[] = {0x76, 0x05, 0x8D, 0x45, 0x08, 0xEB, 0x13, 0x0F};
const uint8_t kDurWeapon[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x53, 0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x4F, 0x04, 0x8B, 0x01,
    0x8B, 0x90, 0x90, 0x00, 0x00, 0x00, 0xFF, 0xD2, 0x85, 0xC0, 0x74, 0x0D, 0x8B, 0x10, 0x8B, 0xC8,
    0x8B, 0x42, 0x38, 0xFF, 0xD0, 0x8B, 0xF0, 0xEB, 0x02, 0x33, 0xF6, 0x8B, 0x4F, 0x04, 0x32, 0xDB,
    0x89, 0x4D, 0xF4, 0x89, 0x75, 0xF8, 0x85, 0xF6, 0x0F, 0x84, 0xBB, 0x00, 0x00, 0x00, 0x8B, 0x55};
const uint8_t kDurArmor[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x53, 0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x4F, 0x04, 0x8B, 0x01,
    0x8B, 0x90, 0x90, 0x00, 0x00, 0x00, 0xFF, 0xD2, 0x85, 0xC0, 0x74, 0x0D, 0x8B, 0x10, 0x8B, 0xC8,
    0x8B, 0x42, 0x38, 0xFF, 0xD0, 0x8B, 0xF0, 0xEB, 0x02, 0x33, 0xF6, 0x8B, 0x4F, 0x04, 0x32, 0xDB,
    0x89, 0x4D, 0xF4, 0x89, 0x75, 0xF8, 0x85, 0xF6, 0x0F, 0x84, 0xA4, 0x00, 0x00, 0x00, 0x8B, 0x55};
// mov eax, [abs] is relocated. The immediate is wild; the trampoline copies the live bytes.
const uint8_t kDurRing[] = {
    0x55, 0x8B, 0xEC, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x83, 0xEC, 0x0C, 0x53, 0x56, 0x57, 0x8B, 0xF9};
const char kDurRingMask[] = "xxxx????xxxxxxxx";
// hkpWorld::hkpWorld. The vtable immediate is relocated.
const uint8_t kWorld[] = {
    0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08, 0x83, 0xE4, 0xF0, 0x83, 0xC4, 0x04, 0x55, 0x8B, 0x6B, 0x04,
    0x89, 0x6C, 0x24, 0x04, 0x8B, 0xEC, 0x81, 0xEC, 0x48, 0x04, 0x00, 0x00, 0x56, 0x8B, 0xF1, 0xC7,
    0x06, 0x00, 0x00, 0x00, 0x00};
const char kWorldMask[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????";
static_assert(sizeof(kWorldMask) == sizeof(kWorld) + 1, "world mask length");
// Steady-state cloth/physics step. Callers pass the frame dt, then this loads 1/60
// instead. The 4-byte absolute is relocated. Replacement is movss xmm0, [ebp+8].
const uint8_t kCloth[] = {
    0xF3, 0x0F, 0x10, 0x05, 0x00, 0x00, 0x00, 0x00, 0x51, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xE8, 0xB5,
    0xFC, 0xFF, 0xFF};
const char kClothMask[] = "xxxx????xxxxxxxxxxx";
static_assert(sizeof(kClothMask) == sizeof(kCloth) + 1, "cloth mask length");
const uint8_t kClothReplacement[] = {0xF3, 0x0F, 0x10, 0x45, 0x08, 0x90, 0x90, 0x90};
// test al,al; je +0x22 skips the ground ray. The 0x74 is at +9. Same shape as Scholar,
// including the cmp byte [esi+0xC6] immediately after the ray.
const uint8_t kJump[] = {
    0x8B, 0xCE, 0xE8, 0xB4, 0xDC, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x22, 0x8D, 0x4D, 0xE0, 0x51, 0x8D,
    0x55, 0xD0, 0x52, 0x8B, 0xCE, 0xE8, 0x41, 0xF0, 0xFF, 0xFF};

// Ground snap call in ChrPhysicalProxy::update: "mov ecx,esi; call <gate>; test al,al; je; lea ecx,[ebp-20h];
// push ecx; lea edx,[ebp-30h]; push edx; mov ecx,esi; call <snap>". Bytes 3-6 and 22-25 are the displacements.
const uint8_t kSnapSite[] = {
    0x8B, 0xCE, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x22, 0x8D, 0x4D, 0xE0, 0x51, 0x8D,
    0x55, 0xD0, 0x52, 0x8B, 0xCE, 0xE8, 0x00, 0x00, 0x00, 0x00};
const char kSnapMask[] = "xxx????xxxxxxxxxxxxxxx????";
static_assert(sizeof(kSnapMask) == sizeof(kSnapSite) + 1, "snap mask length");
// The snap's proxy: +0x10 is the character, whose status (+0x94) holds the jump counter at +0x61C.
// ChrJumpCtrl raises it when a jump starts and lowers it on landing; the movement code applies the
// jump's velocity only while it is non-zero.
constexpr size_t kProxyChr = 0x10;
constexpr size_t kChrStatus = 0x94;
constexpr size_t kStatusJumpCount = 0x61C;
// Movement stick history used for forward + R1/R2. ecx is the attack input recognizer, then the
// frame step and the logical input on the stack. 16 entries of {x, y, magnitude, dt} start at +0x58.
// The two absolute addresses are relocated.
const uint8_t kStickHistory[] = {
    0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08, 0x83, 0xE4, 0xF0, 0x83, 0xC4, 0x04, 0x55, 0x8B, 0x6B, 0x04,
    0x89, 0x6C, 0x24, 0x04, 0x8B, 0xEC, 0x83, 0xEC, 0x38, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x33, 0xC5,
    0x89, 0x45, 0xFC, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x56, 0x8B, 0xF1, 0x8B, 0x48, 0x18, 0x57, 0xE8,
    0x4C, 0xDC, 0xFB, 0xFF, 0xF3, 0x0F, 0x10, 0x53, 0x08, 0x33, 0xD2, 0x8B, 0xF8, 0x38, 0x96, 0x59,
    0x01, 0x00, 0x00, 0x74, 0x36, 0xF3, 0x0F, 0x10};
const char kStickHistoryMask[] = "xxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
static_assert(sizeof(kStickHistoryMask) == sizeof(kStickHistory) + 1, "stick history mask length");
constexpr size_t kStickHistoryOffset = 0x58;
// Per-track TAE event dispatch (see tae_dispatch_begin in frame_fixes.h). ecx is the track, then
// the window and whether this update crossed into a new TAE frame on the stack. The pattern runs
// through the handler call: "mov [ebp-3], cl; push eax; mov ecx, esi; call edx", which is
// redirected through a stub so the start flag can be filtered first.
const uint8_t kTaeDispatch[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x48, 0x56, 0x66, 0x0F, 0xEF, 0xC0, 0x8B, 0xF1, 0x8B, 0x46, 0x04,
    0x8D, 0x4D, 0xB8, 0x51, 0x66, 0x0F, 0xD6, 0x45, 0xB8, 0x66, 0x0F, 0xD6, 0x45, 0xC0, 0x66, 0x0F,
    0xD6, 0x45, 0xC8, 0x66, 0x0F, 0xD6, 0x45, 0xD0, 0x66, 0x0F, 0xD6, 0x45, 0xD8, 0xF3, 0x0F, 0x10,
    0x45, 0x0C, 0x83, 0xEC, 0x08, 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x04, 0xF3, 0x0F, 0x10, 0x45, 0x08,
    0xF3, 0x0F, 0x11, 0x04, 0x24, 0x50, 0xE8, 0xD5, 0x76, 0x55, 0x00, 0x83, 0xC4, 0x10, 0x85, 0xC0,
    0x0F, 0x84, 0xCA, 0x00, 0x00, 0x00, 0x53, 0x8A, 0x5D, 0x10, 0x8D, 0x9B, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0x45, 0xCC, 0x8B, 0x55, 0xB8, 0x8B, 0x4D, 0xBC, 0x66, 0x0F, 0xEF, 0xC0, 0x66, 0x0F, 0xD6,
    0x45, 0xE4, 0x66, 0x0F, 0xD6, 0x45, 0xEC, 0x66, 0x0F, 0xD6, 0x45, 0xF4, 0xF3, 0x0F, 0x10, 0x45,
    0x08, 0xF3, 0x0F, 0x11, 0x45, 0xEC, 0xF3, 0x0F, 0x10, 0x45, 0x0C, 0x50, 0x89, 0x45, 0xE0, 0xC7,
    0x45, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x89, 0x55, 0xE4, 0x89, 0x4D, 0xE8, 0xF3, 0x0F, 0x11, 0x45,
    0xF0, 0xE8, 0xBA, 0x74, 0x55, 0x00, 0xD9, 0x5D, 0xF4, 0x8B, 0x55, 0xCC, 0x52, 0xE8, 0xDE, 0x74,
    0x55, 0x00, 0xD9, 0x5D, 0xF8, 0x8B, 0x56, 0x08, 0x83, 0xC4, 0x08, 0x8A, 0xC3, 0xF6, 0xD8, 0x1A,
    0xC0, 0x22, 0x45, 0xC8, 0x8A, 0xCB, 0xF6, 0xD9, 0x1A, 0xC9, 0x22, 0x4D, 0xC9, 0x3B, 0x55, 0xCC,
    0x8B, 0x16, 0x8B, 0x52, 0x08, 0x88, 0x45, 0xFC, 0x0F, 0x95, 0xC0, 0x88, 0x45, 0xFE, 0x8D, 0x45,
    0xE0, 0x88, 0x4D, 0xFD, 0x50, 0x8B, 0xCE, 0xFF, 0xD2, 0x8B, 0x45, 0xCC};
constexpr size_t kTaeHandlerCallAt = 0xE1;
constexpr size_t kTaeHandlerCallLen = 8;
constexpr size_t kTaeTrackLastEvent = 0x8;
constexpr size_t kTaeEventStart = 0x1E;

InlineHook g_sampler;
InlineHook g_pacer;
InlineHook g_step;
InlineHook g_dur_weapon;
InlineHook g_dur_armor;
InlineHook g_dur_ring;
InlineHook g_world;

struct BytePatch {
    uint8_t* site = nullptr;
    uint8_t original = 0;
};

struct SpanPatch {
    uint8_t* site = nullptr;
    uint8_t original[8]{};
    size_t n = 0;
};

BytePatch g_measured;
BytePatch g_clamp;
BytePatch g_jump;
SpanPatch g_cloth;

struct Section {
    uint8_t* start;
    size_t size;
};

std::vector<Section> code_sections() {
    std::vector<Section> out;
    auto base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            out.push_back({base + sec->VirtualAddress, sec->Misc.VirtualSize});
        }
    }
    return out;
}

bool bytes_match(const uint8_t* have, const uint8_t* pat, const char* mask, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (mask && mask[i] == '?') {
            continue;
        }
        if (have[i] != pat[i]) {
            return false;
        }
    }
    return true;
}

uint8_t* find_unique(const uint8_t* pat, size_t n, const char* name, const char* mask = nullptr) {
    uint8_t* found = nullptr;
    int hits = 0;
    for (const Section& s : code_sections()) {
        if (s.size < n) {
            continue;
        }
        for (size_t i = 0; i + n <= s.size; ++i) {
            if (bytes_match(s.start + i, pat, mask, n)) {
                ++hits;
                found = s.start + i;
            }
        }
    }
    if (hits != 1) {
        LOG_ERROR("%s: pattern matched %d times", name, hits);
        return nullptr;
    }
    return found;
}

DWORD exe_timestamp() {
    auto base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return nt->FileHeader.TimeDateStamp;
}

bool hook_named(InlineHook& hook, void** tramp, const char* name, const uint8_t* pat, size_t n, size_t stolen,
                void* detour, const char* mask = nullptr) {
    uint8_t* site = find_unique(pat, n, name, mask);
    if (!site || !hook.install(site, detour, stolen)) {
        LOG_ERROR("%s: apply failed", name);
        return false;
    }
    if (tramp) {
        *tramp = hook.trampoline;
    }
    LOG_INFO("%s: apply successful", name);
    return true;
}

bool patch_byte(BytePatch& patch, const uint8_t* pat, size_t n, size_t index, uint8_t expect, uint8_t value,
                const char* name) {
    uint8_t* hit = find_unique(pat, n, name);
    if (!hit) {
        LOG_ERROR("%s: apply failed", name);
        return false;
    }
    uint8_t* site = hit + index;
    if (*site != expect) {
        LOG_ERROR("%s: expected %02X, found %02X", name, expect, *site);
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(site, 1, PAGE_EXECUTE_READWRITE, &old)) {
        LOG_ERROR("%s: VirtualProtect failed (%lu)", name, GetLastError());
        return false;
    }
    patch.original = *site;
    *site = value;
    VirtualProtect(site, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 1);
    patch.site = site;
    LOG_INFO("%s: apply successful", name);
    return true;
}

void restore_byte(BytePatch& patch) {
    if (!patch.site) {
        return;
    }
    DWORD old = 0;
    if (VirtualProtect(patch.site, 1, PAGE_EXECUTE_READWRITE, &old)) {
        *patch.site = patch.original;
        VirtualProtect(patch.site, 1, old, &old);
        FlushInstructionCache(GetCurrentProcess(), patch.site, 1);
    }
    patch.site = nullptr;
}

bool patch_span(SpanPatch& patch, const uint8_t* pat, size_t n, const char* mask, const uint8_t* replacement,
                size_t replace_n, const char* name) {
    uint8_t* site = find_unique(pat, n, name, mask);
    if (!site || replace_n > sizeof(patch.original)) {
        LOG_ERROR("%s: apply failed", name);
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(site, replace_n, PAGE_EXECUTE_READWRITE, &old)) {
        LOG_ERROR("%s: VirtualProtect failed (%lu)", name, GetLastError());
        return false;
    }
    std::memcpy(patch.original, site, replace_n);
    std::memcpy(site, replacement, replace_n);
    VirtualProtect(site, replace_n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, replace_n);
    patch.site = site;
    patch.n = replace_n;
    LOG_INFO("%s: apply successful", name);
    return true;
}

void restore_span(SpanPatch& patch) {
    if (!patch.site || patch.n == 0) {
        return;
    }
    DWORD old = 0;
    if (VirtualProtect(patch.site, patch.n, PAGE_EXECUTE_READWRITE, &old)) {
        std::memcpy(patch.site, patch.original, patch.n);
        VirtualProtect(patch.site, patch.n, old, &old);
        FlushInstructionCache(GetCurrentProcess(), patch.site, patch.n);
    }
    patch.site = nullptr;
    patch.n = 0;
}

// The character's ground snap: see ground_snap_skip in frame_fixes.h. When the snap is skipped the
// position passes through unchanged, as the old "never snap" workaround did for every frame.
using SnapFn = float*(__fastcall*)(void* self, void* edx, float* out, const float* in);
SnapFn g_snap_orig = nullptr;
uint8_t* g_snap_site = nullptr;
int32_t g_snap_rel_orig = 0;

bool jump_in_progress(void* proxy) {
    auto* chr = *reinterpret_cast<uint8_t* const*>(static_cast<uint8_t*>(proxy) + kProxyChr);
    if (!chr) {
        return false;
    }
    auto* status = *reinterpret_cast<uint8_t* const*>(chr + kChrStatus);
    return status && *reinterpret_cast<const int32_t*>(status + kStatusJumpCount) != 0;
}

float* __fastcall hk_snap(void* self, void* edx, float* out, const float* in) {
    if (ground_snap_skip(self, in[1], g_frame_dt, jump_in_progress(self))) {
        std::memcpy(out, in, 16);
        ground_snap_record(self, in[1]);
        return out;
    }
    float* result = g_snap_orig(self, edx, out, in);
    ground_snap_record(self, result[1]);
    return result;
}

bool patch_snap() {
    constexpr const char* kName = "hk_ChrPhysicalProxy_GroundSnap";
    uint8_t* hit = find_unique(kSnapSite, sizeof(kSnapSite), kName, kSnapMask);
    if (!hit) {
        LOG_ERROR("%s: apply failed", kName);
        return false;
    }
    uint8_t* call = hit + 21;
    int32_t rel = 0;
    std::memcpy(&rel, call + 1, 4);
    uint8_t* target = call + 5 + rel;
    const int32_t value = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&hk_snap) - reinterpret_cast<uintptr_t>(call + 5));
    DWORD old = 0;
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old)) {
        LOG_ERROR("%s: VirtualProtect failed (%lu)", kName, GetLastError());
        return false;
    }
    g_snap_orig = reinterpret_cast<SnapFn>(target);
    g_snap_rel_orig = rel;
    std::memcpy(call + 1, &value, 4);
    VirtualProtect(call, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    g_snap_site = call;
    LOG_INFO("%s: apply successful (snap at %p)", kName, static_cast<void*>(target));
    return true;
}

void unpatch_snap() {
    if (!g_snap_site) {
        return;
    }
    DWORD old = 0;
    if (VirtualProtect(g_snap_site, 5, PAGE_EXECUTE_READWRITE, &old)) {
        std::memcpy(g_snap_site + 1, &g_snap_rel_orig, 4);
        VirtualProtect(g_snap_site, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), g_snap_site, 5);
    }
    g_snap_site = nullptr;
}

// Guard break and jump attack: see stick_history_before/after in frame_fixes.h.
using StickHistoryFn = void(__fastcall*)(void* recognizer, void* edx, float dt, void* input);
InlineHook g_stick_history;

void __fastcall hk_stick_history(void* recognizer, void* edx, float dt, void* input) {
    auto* history = reinterpret_cast<float*>(static_cast<uint8_t*>(recognizer) + kStickHistoryOffset);
    StickHistorySave save;
    stick_history_before(history, save);
    reinterpret_cast<StickHistoryFn>(g_stick_history.trampoline)(recognizer, edx, dt, input);
    stick_history_after(history, save, dt);
}

// TAE events: see tae_dispatch_begin in frame_fixes.h. The fourth argument is a byte pushed as a
// dword, so only its low byte is meaningful.
using TaeDispatchFn = void(__fastcall*)(void* track, void* edx, float t0, float t1, int advanced);
using TaeHandlerFn = void(__fastcall*)(void* track, void* edx, void* event);
InlineHook g_tae_dispatch;
uint8_t* g_tae_call_site = nullptr;
uint8_t g_tae_call_orig[kTaeHandlerCallLen]{};

void __fastcall hk_tae_dispatch(void* track, void* edx, float t0, float t1, int advanced) {
    const void* last = *reinterpret_cast<void* const*>(static_cast<uint8_t*>(track) + kTaeTrackLastEvent);
    void* outer = tae_dispatch_begin(track, last, (advanced & 0xFF) != 0);
    reinterpret_cast<TaeDispatchFn>(g_tae_dispatch.trampoline)(track, edx, t0, t1, advanced);
    tae_dispatch_end(outer);
}

// Reached from the stub at the handler call with the track, the event and the handler.
void __stdcall hk_tae_handler(void* track, uint8_t* event, TaeHandlerFn handler) {
    tae_filter_event(track, *reinterpret_cast<void* const*>(event), event + kTaeEventStart);
    handler(track, nullptr, event);
}

bool patch_tae() {
    constexpr const char* kName = "hk_MorphemeTimeActTrack_EventDispatch";
    uint8_t* site = find_unique(kTaeDispatch, sizeof(kTaeDispatch), kName);
    if (!site) {
        LOG_ERROR("%s: apply failed", kName);
        return false;
    }
    uint8_t* call = site + kTaeHandlerCallAt;
    // mov [ebp-3], cl; push edx (handler); push eax (event); push esi (track); call hk_tae_handler; ret
    uint8_t* stub = near_arena_alloc(16);
    if (!stub) {
        LOG_ERROR("%s: trampoline arena exhausted", kName);
        return false;
    }
    const uint8_t head[] = {0x88, 0x4D, 0xFD, 0x52, 0x50, 0x56, 0xE8};
    std::memcpy(stub, head, sizeof(head));
    const int32_t to_hook = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&hk_tae_handler) -
                                                 reinterpret_cast<uintptr_t>(stub + sizeof(head) + 4));
    std::memcpy(stub + sizeof(head), &to_hook, sizeof(to_hook));
    stub[sizeof(head) + 4] = 0xC3;
    if (!g_tae_dispatch.install(site, reinterpret_cast<void*>(&hk_tae_dispatch), 6)) {
        LOG_ERROR("%s: apply failed", kName);
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(call, kTaeHandlerCallLen, PAGE_EXECUTE_READWRITE, &old)) {
        LOG_ERROR("%s: VirtualProtect failed (%lu)", kName, GetLastError());
        g_tae_dispatch.remove();
        return false;
    }
    std::memcpy(g_tae_call_orig, call, kTaeHandlerCallLen);
    // 3-byte nop, then call the stub so the handler returns to the instruction after the original call.
    call[0] = 0x0F;
    call[1] = 0x1F;
    call[2] = 0x00;
    call[3] = 0xE8;
    const int32_t to_stub = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stub) -
                                                 reinterpret_cast<uintptr_t>(call + kTaeHandlerCallLen));
    std::memcpy(call + 4, &to_stub, sizeof(to_stub));
    VirtualProtect(call, kTaeHandlerCallLen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), call, kTaeHandlerCallLen);
    FlushInstructionCache(GetCurrentProcess(), stub, 16);
    g_tae_call_site = call;
    LOG_INFO("%s: apply successful", kName);
    return true;
}

void unpatch_tae() {
    if (g_tae_call_site) {
        DWORD old = 0;
        if (VirtualProtect(g_tae_call_site, kTaeHandlerCallLen, PAGE_EXECUTE_READWRITE, &old)) {
            std::memcpy(g_tae_call_site, g_tae_call_orig, kTaeHandlerCallLen);
            VirtualProtect(g_tae_call_site, kTaeHandlerCallLen, old, &old);
            FlushInstructionCache(GetCurrentProcess(), g_tae_call_site, kTaeHandlerCallLen);
        }
        g_tae_call_site = nullptr;
    }
    g_tae_dispatch.remove();
}

}  // namespace

bool patches_apply(const Settings& settings) {
    if (!settings.fps_unlock) {
        LOG_INFO("FPSUnlock is false; the game is left unchanged");
        return true;
    }

    const DWORD stamp = exe_timestamp();
    LOG_INFO("  Header timestamp: %lu", stamp);
    if (stamp != kVanillaStamp) {
        LOG_INFO("EXE timestamp %lu is not the vanilla build this was developed against (%lu); continuing if patterns match",
                 stamp, kVanillaStamp);
    }

    auto* game = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    if (!near_arena_init(game + 0x1000)) {
        return false;
    }

    bool ok = true;
    ok &= hook_named(g_sampler, &g_sampler_tramp, "hk_MainAppFrameTimeInfo_UnlockDeltatime", kSampler,
                     sizeof(kSampler), 6, reinterpret_cast<void*>(&hk_sampler));
    ok &= hook_named(g_pacer, nullptr, "hk_FramePacer_Disable", kPacer, sizeof(kPacer), 6,
                     reinterpret_cast<void*>(&hk_pacer));
    ok &= hook_named(g_step, &g_step_tramp, "hk_KatanaMainApp_UpdateDT", kStep, sizeof(kStep), 6,
                     reinterpret_cast<void*>(&hk_step), kStepMask);
    ok &= patch_byte(g_measured, kMeasured, sizeof(kMeasured), 7, 0x74, 0xEB, "pch_UseMeasuredDeltatime");
    ok &= patch_byte(g_clamp, kClamp, sizeof(kClamp), 4, 0x08, 0xFC, "pch_FrameDeltaMinClamp");
    ok &= present_apply();
    if (settings.durability_fix) {
        ok &= hook_named(g_dur_weapon, &g_dur_weapon_tramp, "hk_Durability_Weapon", kDurWeapon, sizeof(kDurWeapon), 6,
                         reinterpret_cast<void*>(&hk_dur_weapon));
        ok &= hook_named(g_dur_armor, &g_dur_armor_tramp, "hk_Durability_Armor", kDurArmor, sizeof(kDurArmor), 6,
                         reinterpret_cast<void*>(&hk_dur_armor));
        ok &= hook_named(g_dur_ring, &g_dur_ring_tramp, "hk_Durability_RingOnHit", kDurRing, sizeof(kDurRing), 8,
                         reinterpret_cast<void*>(&hk_dur_ring), kDurRingMask);
    }
    if (settings.ground_snap_fix) {
        ok &= patch_snap();
    } else {
        ok &= patch_byte(g_jump, kJump, sizeof(kJump), 9, 0x74, 0xEB, "pch_ChrPhysicalProxy_JumpHeightFix");
    }
    if (settings.forward_attack_fix) {
        ok &= hook_named(g_stick_history, nullptr, "hk_ChrPadAttackInput_StickHistory", kStickHistory,
                         sizeof(kStickHistory), 6, reinterpret_cast<void*>(&hk_stick_history), kStickHistoryMask);
    }
    if (settings.tae_event_fix) {
        ok &= patch_tae();
    }
    if (settings.cloth_fix) {
        ok &= patch_span(g_cloth, kCloth, sizeof(kCloth), kClothMask, kClothReplacement,
                         sizeof(kClothReplacement), "hk_PXClothWorld_FrametimeUpdate");
    }
    if (settings.physics_fps > 0) {
        g_physics_dt = 1.0f / static_cast<float>(settings.physics_fps);
        ok &= hook_named(g_world, &g_world_tramp, "hk_hkpWorld_UpdateExpectedDeltaTime", kWorld, sizeof(kWorld), 6,
                         reinterpret_cast<void*>(&hk_world), kWorldMask);
    }
    return ok;
}

void patches_remove() {
    present_remove();
    g_sampler.remove();
    g_pacer.remove();
    g_step.remove();
    g_dur_weapon.remove();
    g_dur_armor.remove();
    g_dur_ring.remove();
    g_world.remove();
    g_stick_history.remove();
    unpatch_tae();
    restore_byte(g_measured);
    restore_byte(g_clamp);
    restore_byte(g_jump);
    unpatch_snap();
    restore_span(g_cloth);
}
