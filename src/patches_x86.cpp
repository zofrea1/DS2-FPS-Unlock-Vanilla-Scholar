#include "patches.h"

#include "inline_hook.h"
#include "log.h"
#include "present_x86.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <limits>
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

// The character's ground snap: after the physics step moves the body, this casts 0.1 units
// straight down and pulls the body onto any floor it finds. The game runs it once per frame with
// that fixed reach. At a high frame rate a jump rises only a few hundredths of a unit per frame,
// so every frame the snap dragged the body back down and the jump lost height. The old fix skipped
// the snap altogether, which left the character floating on slopes, so runs downhill kept losing
// the ground and rolls after a landing ended early. Here the snap stays on and is skipped only
// while the body is rising. Rising is judged against where the body ended up on the previous call.
using SnapFn = float*(__fastcall*)(void* self, void* edx, float* out, const float* in);
SnapFn g_snap_orig = nullptr;
uint8_t* g_snap_site = nullptr;
int32_t g_snap_rel_orig = 0;

struct SnapTrack {
    void* self = nullptr;
    float y = 0.0f;
};
SnapTrack g_snap_track[64];

SnapTrack* snap_track_for(void* self) {
    const size_t start = (reinterpret_cast<uintptr_t>(self) >> 4) & 63;
    for (size_t i = 0; i < 64; ++i) {
        SnapTrack& t = g_snap_track[(start + i) & 63];
        if (t.self == self) {
            return &t;
        }
        if (!t.self) {
            t.self = self;
            t.y = std::numeric_limits<float>::quiet_NaN();
            return &t;
        }
    }
    return nullptr;
}

float* __fastcall hk_snap(void* self, void* edx, float* out, const float* in) {
    SnapTrack* track = snap_track_for(self);
    const float dt = g_frame_dt;
    if (track && track->y == track->y) {
        const float rise = in[1] - track->y;
        // Above 0.4 units per second of climb, and not a teleport.
        if (rise > 0.4f * dt && rise < 2.0f) {
            std::memcpy(out, in, 16);
            track->y = in[1];
            return out;
        }
    }
    float* result = g_snap_orig(self, edx, out, in);
    if (track) {
        track->y = result[1];
    }
    return result;
}

bool patch_snap() {
    uint8_t* hit = find_unique(kSnapSite, sizeof(kSnapSite), "hk_ChrPhysicalProxy_GroundSnap", kSnapMask);
    if (!hit) {
        LOG_ERROR("hk_ChrPhysicalProxy_GroundSnap: apply failed");
        return false;
    }
    uint8_t* call = hit + 21;
    int32_t rel = 0;
    std::memcpy(&rel, call + 1, 4);
    uint8_t* target = call + 5 + rel;
    const int32_t value = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&hk_snap) - reinterpret_cast<uintptr_t>(call + 5));
    DWORD old = 0;
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old)) {
        LOG_ERROR("hk_ChrPhysicalProxy_GroundSnap: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }
    g_snap_orig = reinterpret_cast<SnapFn>(target);
    g_snap_rel_orig = rel;
    std::memcpy(call + 1, &value, 4);
    VirtualProtect(call, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    g_snap_site = call;
    LOG_INFO("hk_ChrPhysicalProxy_GroundSnap: apply successful (snap at %p)", static_cast<void*>(target));
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
    ok &= hook_named(g_dur_weapon, &g_dur_weapon_tramp, "hk_Durability_Weapon", kDurWeapon, sizeof(kDurWeapon), 6,
                     reinterpret_cast<void*>(&hk_dur_weapon));
    ok &= hook_named(g_dur_armor, &g_dur_armor_tramp, "hk_Durability_Armor", kDurArmor, sizeof(kDurArmor), 6,
                     reinterpret_cast<void*>(&hk_dur_armor));
    ok &= hook_named(g_dur_ring, &g_dur_ring_tramp, "hk_Durability_RingOnHit", kDurRing, sizeof(kDurRing), 8,
                     reinterpret_cast<void*>(&hk_dur_ring), kDurRingMask);
    if (settings.ground_snap_fix) {
        ok &= patch_snap();
    } else {
        ok &= patch_byte(g_jump, kJump, sizeof(kJump), 9, 0x74, 0xEB, "pch_ChrPhysicalProxy_JumpHeightFix");
    }
    ok &= patch_span(g_cloth, kCloth, sizeof(kCloth), kClothMask, kClothReplacement,
                     sizeof(kClothReplacement), "hk_PXClothWorld_FrametimeUpdate");
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
    restore_byte(g_measured);
    restore_byte(g_clamp);
    restore_byte(g_jump);
    unpatch_snap();
    restore_span(g_cloth);
}
