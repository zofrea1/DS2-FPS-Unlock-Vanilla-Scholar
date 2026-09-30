#include "patches.h"

#include "inline_hook.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <vector>

#if defined(_M_X64)

namespace {

// Scholar of the First Sin. The timestamp is logged; the byte patterns decide
// whether the sites are safe. Builds used while writing this: 1663233821 and 1665452630.
constexpr DWORD kKnownStampA = 1663233821;
constexpr DWORD kKnownStampB = 1665452630;

std::atomic<float> g_frame_dt{1.0f / 60.0f};
int g_physics_fps = 120;
// Written by the frame sampler and read by the ring-rate stub in the near page.
float* g_scale_slot = nullptr;

// Prologue of MainApp's frame-time sampler. Three running slots at +0/+4/+8,
// last QPC timestamp at +0x18, QPC frequency at +0x20. dl != 0 enables the
// internal sleep that caps the game at ~60 FPS.
const uint8_t kFrameTime[] = {
    0x48, 0x8B, 0xC4, 0x56, 0x57, 0x41, 0x56, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x0F, 0x29,
    0x70, 0xC8, 0x48, 0x89, 0x58, 0xE0, 0x48, 0x8B};
// KatanaMainApp::update(float dt_seconds). xmm1 is the step passed to gameplay.
const uint8_t kUpdateDt[] = {
    0x48, 0x89, 0x5C, 0x24, 0x20, 0xF3, 0x0F, 0x11, 0x4C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40,
    0x48, 0x8B, 0xD9, 0xFF, 0x15, 0x13, 0xEA, 0xFB};
// hkpWorld::hkpWorld(hkpWorldCinfo&, uint). rdx is the cinfo.
const uint8_t kWorldCtor[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x8D, 0xA8, 0xA8, 0xFB, 0xFF};
// PXClothWorld step. xmm1 is the cloth timestep; vanilla callers pass 1/60.
const uint8_t kCloth[] = {
    0x40, 0x55, 0x48, 0x83, 0xEC, 0x40, 0x0F, 0x29, 0x74, 0x24, 0x30, 0x0F, 0x57, 0xC0, 0x0F, 0x28,
    0xF1, 0x48, 0x8B, 0xE9, 0x0F, 0x2F, 0xF0, 0x0F};
// Unique bytes around the jump-ray je. The 0x74 sits 8 bytes in.
const uint8_t kJump[] = {
    0xCE, 0xE8, 0x31, 0x1B, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x13, 0x4C, 0x8D, 0x45, 0x80, 0x48, 0x8D};
// ChrPhysicalProxy::update, from "mov rcx,rsi; call <snap gate>" through the ground snap call.
// Bytes 4-7 and 24-27 are the two call displacements. The je at +10 is the legacy jump hack's target.
const uint8_t kSnapSite[] = {
    0x48, 0x8B, 0xCE, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x13, 0x4C, 0x8D, 0x45, 0x80,
    0x48, 0x8D, 0x55, 0x90, 0x48, 0x8B, 0xCE, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x28, 0x38};
const char kSnapMask[] = "xxxx????xxxxxxxxxxxxxxxx????xxx";
static_assert(sizeof(kSnapMask) == sizeof(kSnapSite) + 1, "snap mask length");
// ApplyDurability for weapons. xmm2 is a signed delta. PlayerEquipBrokenActionCtrl.
const uint8_t kDurWeapon[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
    0xEC, 0x70};
// Armor sibling. Same ABI. The first 0x60 bytes match the ring sibling, so the
// pattern includes the branch displacement that distinguishes them.
const uint8_t kDurArmor[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
    0x48, 0x83, 0xEC, 0x70, 0x48, 0x8B, 0xF1, 0x48, 0x8B, 0x49, 0x08, 0x44, 0x0F, 0x29, 0x44, 0x24,
    0x40, 0x48, 0x8B, 0x01, 0x41, 0x0F, 0xB6, 0xE9, 0x44, 0x0F, 0x28, 0xC2, 0x8B, 0xFA, 0xFF, 0x90,
    0x20, 0x01, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x0F, 0x4C, 0x8B, 0x00, 0x48, 0x8B, 0xC8, 0x41,
    0xFF, 0x50, 0x70, 0x48, 0x8B, 0xD8, 0xEB, 0x02, 0x33, 0xDB, 0x48, 0x8B, 0x46, 0x08, 0x40, 0x32,
    0xF6, 0x48, 0x89, 0x5C, 0x24, 0x30, 0x48, 0x89, 0x44, 0x24, 0x28, 0x48, 0x85, 0xDB, 0x0F, 0x84,
    0xB2, 0x00, 0x00, 0x00, 0x44, 0x0F, 0xB6, 0xC5};
// Ring sibling used by the integer durability channel. xmm2 is a signed delta.
const uint8_t kDurRing[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
    0x48, 0x83, 0xEC, 0x70, 0x48, 0x8B, 0xF1, 0x48, 0x8B, 0x49, 0x08, 0x44, 0x0F, 0x29, 0x44, 0x24,
    0x40, 0x48, 0x8B, 0x01, 0x41, 0x0F, 0xB6, 0xE9, 0x44, 0x0F, 0x28, 0xC2, 0x8B, 0xFA, 0xFF, 0x90,
    0x20, 0x01, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x0F, 0x4C, 0x8B, 0x00, 0x48, 0x8B, 0xC8, 0x41,
    0xFF, 0x50, 0x70, 0x48, 0x8B, 0xD8, 0xEB, 0x02, 0x33, 0xDB, 0x48, 0x8B, 0x46, 0x08, 0x40, 0x32,
    0xF6, 0x48, 0x89, 0x5C, 0x24, 0x30, 0x48, 0x89, 0x44, 0x24, 0x28, 0x48, 0x85, 0xDB, 0x0F, 0x84,
    0xB3, 0x00, 0x00, 0x00, 0x8B, 0xD7, 0x48, 0x8B};
// On-hit ring loss. xmm2 is the hit factor multiplied into the durability loss,
// not the final delta. Scaling it scales the loss once.
const uint8_t kDurRingHit[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0x05, 0xFD,
    0xBE, 0x41, 0x01, 0x48, 0x8B, 0xF1, 0x0F, 0x29};
// Integer ring channel inside the equip update. movd xmm1, r12d / movss / cvtdq2ps
// / addss xmm1, xmm6. The integer is the signed delta added to current durability.
const uint8_t kDurRingRate[] = {
    0x66, 0x41, 0x0F, 0x6E, 0xCC, 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x2C, 0x0F, 0x5B, 0xC9, 0xF3, 0x0F,
    0x58, 0xCE};

InlineHook g_frame_hook;
InlineHook g_update_hook;
InlineHook g_world_hook;
InlineHook g_cloth_hook;
InlineHook g_dur_weapon;
InlineHook g_dur_armor;
InlineHook g_dur_ring;
InlineHook g_dur_ring_hit;

uint8_t* g_jump_site = nullptr;
uint8_t g_jump_orig = 0x74;

uint8_t* g_ring_rate_site = nullptr;
uint8_t g_ring_rate_orig[14]{};
bool g_ring_rate_installed = false;

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

using FrameTimeFn = float(__fastcall*)(void* self, unsigned char limit);
using UpdateFn = void(__fastcall*)(void* self, float dt);
using WorldFn = void*(__fastcall*)(void* self, void* cinfo, unsigned int version);
using ClothFn = void*(__fastcall*)(void* self, float dt);
using DurabilityFn = bool(__fastcall*)(void* self, int slot, float damage, unsigned char flag);

float durability_scale() {
    float scale = 1.0f;
    const float dt = g_frame_dt.load(std::memory_order_relaxed);
    if (dt > 0.0008f) {
        scale = dt * 60.0f;
        if (scale < 0.05f) {
            scale = 0.05f;
        } else if (scale > 2.0f) {
            scale = 2.0f;
        }
    }
    if (g_scale_slot) {
        *g_scale_slot = scale;
    }
    return scale;
}

// Sum of the three smoothed slots, clamped to 1 second, is the real frame dt.
// Calling through with limit=0 skips the Sleep that enforces 60 FPS.
float __fastcall hk_frame_time(void* self, unsigned char /*limit*/) {
    auto* slots = static_cast<float*>(self);
    float sum = slots[0] + slots[1] + slots[2];
    if (sum > 1.0f) {
        sum = 1.0f;
    }
    g_frame_dt.store(sum, std::memory_order_relaxed);
    durability_scale();
    auto orig = reinterpret_cast<FrameTimeFn>(g_frame_hook.trampoline);
    orig(self, 0);
    return sum;
}

void __fastcall hk_update(void* self, float /*dt*/) {
    auto orig = reinterpret_cast<UpdateFn>(g_update_hook.trampoline);
    orig(self, g_frame_dt.load(std::memory_order_relaxed));
}

void* __fastcall hk_world(void* self, void* cinfo, unsigned int version) {
    if (cinfo && g_physics_fps > 0) {
        // hkpWorldCinfo::m_expectedMinPsiDeltaTime on this SotFS build.
        const float dt = 1.0f / static_cast<float>(g_physics_fps);
        *reinterpret_cast<float*>(static_cast<uint8_t*>(cinfo) + 0x70) = dt;
    }
    auto orig = reinterpret_cast<WorldFn>(g_world_hook.trampoline);
    return orig(self, cinfo, version);
}

void* __fastcall hk_cloth(void* self, float /*dt*/) {
    auto orig = reinterpret_cast<ClothFn>(g_cloth_hook.trampoline);
    return orig(self, g_frame_dt.load(std::memory_order_relaxed));
}

// Contact damage is sampled again on later frames of the same swing or hit.
// Scale the signed delta (or the hit factor, for the ring on-hit entry) by
// frameDt * 60 so a 60 FPS frame is unchanged.
bool call_scaled(InlineHook& hook, void* self, int slot, float damage, unsigned char flag) {
    auto orig = reinterpret_cast<DurabilityFn>(hook.trampoline);
    return orig(self, slot, damage * durability_scale(), flag);
}

bool __fastcall hk_dur_weapon(void* self, int slot, float damage, unsigned char flag) {
    return call_scaled(g_dur_weapon, self, slot, damage, flag);
}
bool __fastcall hk_dur_armor(void* self, int slot, float damage, unsigned char flag) {
    return call_scaled(g_dur_armor, self, slot, damage, flag);
}
bool __fastcall hk_dur_ring(void* self, int slot, float damage, unsigned char flag) {
    return call_scaled(g_dur_ring, self, slot, damage, flag);
}
bool __fastcall hk_dur_ring_hit(void* self, int slot, float damage, unsigned char flag) {
    return call_scaled(g_dur_ring_hit, self, slot, damage, flag);
}

bool hook_named(InlineHook& hook, const char* name, const uint8_t* pat, size_t pat_len, size_t stolen, void* detour) {
    uint8_t* site = find_unique(pat, pat_len, name);
    if (!site) {
        LOG_ERROR("%s: apply failed", name);
        return false;
    }
    if (!hook.install(site, detour, stolen)) {
        LOG_ERROR("%s: apply failed", name);
        return false;
    }
    LOG_INFO("%s: apply successful", name);
    return true;
}

// The character's ground snap: after the physics step moves the body, this casts 0.1 units
// straight down and pulls the body onto any floor it finds. The game runs it once per frame with
// that fixed reach. At a high frame rate a jump rises only a few hundredths of a unit per frame,
// so every frame the snap dragged the body back down and the jump lost height. The old fix skipped
// the snap altogether, which left the character floating on slopes, so runs downhill kept losing
// the ground and rolls after a landing ended early. Here the snap stays on and is skipped only
// while the body is rising. Rising is judged against where the body ended up on the previous call.
using SnapFn = void*(__fastcall*)(void* self, float* out, const float* in);
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

void* __fastcall hk_snap(void* self, float* out, const float* in) {
    SnapTrack* track = snap_track_for(self);
    const float dt = g_frame_dt.load(std::memory_order_relaxed);
    if (track && track->y == track->y) {
        const float rise = in[1] - track->y;
        // Above 0.4 units per second of climb, and not a teleport.
        if (rise > 0.4f * dt && rise < 2.0f) {
            std::memcpy(out, in, 16);
            track->y = in[1];
            return out;
        }
    }
    void* result = g_snap_orig(self, out, in);
    if (track) {
        track->y = static_cast<const float*>(result)[1];
    }
    return result;
}

bool patch_snap() {
    uint8_t* hit = find_unique(kSnapSite, sizeof(kSnapSite), "hk_ChrPhysicalProxy_GroundSnap", kSnapMask);
    if (!hit) {
        LOG_ERROR("hk_ChrPhysicalProxy_GroundSnap: apply failed");
        return false;
    }
    uint8_t* call = hit + 23;
    int32_t rel = 0;
    std::memcpy(&rel, call + 1, 4);
    uint8_t* target = call + 5 + rel;
    uint8_t* stub = near_arena_alloc(16);
    if (!stub) {
        LOG_ERROR("hk_ChrPhysicalProxy_GroundSnap: trampoline arena exhausted");
        return false;
    }
    stub[0] = 0xFF;
    stub[1] = 0x25;
    std::memset(stub + 2, 0, 4);
    const uint64_t addr = reinterpret_cast<uint64_t>(&hk_snap);
    std::memcpy(stub + 6, &addr, 8);
    const intptr_t new_rel = reinterpret_cast<intptr_t>(stub) - reinterpret_cast<intptr_t>(call + 5);
    if (new_rel < static_cast<intptr_t>(INT32_MIN) || new_rel > static_cast<intptr_t>(INT32_MAX)) {
        LOG_ERROR("hk_ChrPhysicalProxy_GroundSnap: relative call does not fit");
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old)) {
        LOG_ERROR("hk_ChrPhysicalProxy_GroundSnap: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }
    g_snap_orig = reinterpret_cast<SnapFn>(target);
    g_snap_rel_orig = rel;
    const int32_t value = static_cast<int32_t>(new_rel);
    std::memcpy(call + 1, &value, 4);
    VirtualProtect(call, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    FlushInstructionCache(GetCurrentProcess(), stub, 16);
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

bool patch_jump() {
    uint8_t* hit = find_unique(kJump, sizeof(kJump), "pch_ChrPhysicalProxy_JumpHeightFix");
    if (!hit) {
        LOG_ERROR("pch_ChrPhysicalProxy_JumpHeightFix: apply failed");
        return false;
    }
    uint8_t* site = hit + 8;
    if (*site != 0x74 && *site != 0xEB) {
        LOG_ERROR("pch_ChrPhysicalProxy_JumpHeightFix: expected je/jmp, found %02X", *site);
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(site, 1, PAGE_EXECUTE_READWRITE, &old)) {
        LOG_ERROR("pch_ChrPhysicalProxy_JumpHeightFix: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }
    g_jump_orig = *site;
    *site = 0xEB;
    VirtualProtect(site, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 1);
    g_jump_site = site;
    LOG_INFO("pch_ChrPhysicalProxy_JumpHeightFix: apply successful");
    return true;
}

void unpatch_jump() {
    if (!g_jump_site) {
        return;
    }
    DWORD old = 0;
    if (VirtualProtect(g_jump_site, 1, PAGE_EXECUTE_READWRITE, &old)) {
        *g_jump_site = g_jump_orig;
        VirtualProtect(g_jump_site, 1, old, &old);
        FlushInstructionCache(GetCurrentProcess(), g_jump_site, 1);
    }
    g_jump_site = nullptr;
}

// The other ring channel adds an integer delta in place instead of calling the
// ring apply function. Replay those three instructions, scale xmm1, then rejoin
// at the add onto current durability. No extra stack frame: this is mid-function.
bool install_ring_rate(uint8_t* site) {
    constexpr size_t kSpan = 14;
    uint8_t* stub = near_arena_alloc(64);
    if (!stub) {
        LOG_ERROR("hk_Durability_RingRate: trampoline arena exhausted");
        return false;
    }
    size_t n = 0;
    auto emit = [&](std::initializer_list<uint8_t> bytes) {
        for (uint8_t b : bytes) {
            stub[n++] = b;
        }
    };
    emit({0x66, 0x41, 0x0F, 0x6E, 0xCC});             // movd xmm1, r12d
    emit({0xF3, 0x0F, 0x11, 0x44, 0x24, 0x2C});       // movss [rsp+2Ch], xmm0
    emit({0x0F, 0x5B, 0xC9});                         // cvtdq2ps xmm1, xmm1
    const size_t mulss_at = n;
    emit({0xF3, 0x0F, 0x59, 0x0D, 0x00, 0x00, 0x00, 0x00});  // mulss xmm1, [rip+disp]
    const size_t jmp_at = n;
    emit({0xE9, 0x00, 0x00, 0x00, 0x00});
    while (n % 4) {
        stub[n++] = 0x90;
    }
    g_scale_slot = reinterpret_cast<float*>(stub + n);
    *g_scale_slot = durability_scale();
    n += 4;

    const int32_t disp = static_cast<int32_t>(reinterpret_cast<uint8_t*>(g_scale_slot) - (stub + mulss_at + 8));
    std::memcpy(stub + mulss_at + 4, &disp, 4);
    uint8_t* cont = site + kSpan;
    const int32_t rel = static_cast<int32_t>(cont - (stub + jmp_at + 5));
    std::memcpy(stub + jmp_at + 1, &rel, 4);

    const intptr_t site_rel = reinterpret_cast<intptr_t>(stub) - (reinterpret_cast<intptr_t>(site) + 5);
    if (site_rel < static_cast<intptr_t>(INT32_MIN) || site_rel > static_cast<intptr_t>(INT32_MAX)) {
        LOG_ERROR("hk_Durability_RingRate: relative jump does not fit");
        g_scale_slot = nullptr;
        return false;
    }

    DWORD old = 0;
    if (!VirtualProtect(site, kSpan, PAGE_EXECUTE_READWRITE, &old)) {
        LOG_ERROR("hk_Durability_RingRate: VirtualProtect failed (%lu)", GetLastError());
        g_scale_slot = nullptr;
        return false;
    }
    std::memcpy(g_ring_rate_orig, site, kSpan);
    site[0] = 0xE9;
    const int32_t jmp = static_cast<int32_t>(site_rel);
    std::memcpy(site + 1, &jmp, 4);
    for (size_t i = 5; i < kSpan; ++i) {
        site[i] = 0x90;
    }
    VirtualProtect(site, kSpan, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, kSpan);
    FlushInstructionCache(GetCurrentProcess(), stub, n);
    g_ring_rate_site = site;
    g_ring_rate_installed = true;
    LOG_INFO("hk_Durability_RingRate: apply successful");
    return true;
}

void remove_ring_rate() {
    if (!g_ring_rate_installed || !g_ring_rate_site) {
        return;
    }
    DWORD old = 0;
    if (VirtualProtect(g_ring_rate_site, sizeof(g_ring_rate_orig), PAGE_EXECUTE_READWRITE, &old)) {
        std::memcpy(g_ring_rate_site, g_ring_rate_orig, sizeof(g_ring_rate_orig));
        VirtualProtect(g_ring_rate_site, sizeof(g_ring_rate_orig), old, &old);
        FlushInstructionCache(GetCurrentProcess(), g_ring_rate_site, sizeof(g_ring_rate_orig));
    }
    g_ring_rate_installed = false;
    g_ring_rate_site = nullptr;
    g_scale_slot = nullptr;
}

}  // namespace

bool patches_apply(const Settings& settings) {
    if (!settings.fps_unlock) {
        LOG_INFO("FPSUnlock is false; the game is left unchanged");
        return true;
    }

    g_physics_fps = settings.physics_fps;
    const DWORD stamp = exe_timestamp();
    LOG_INFO("  Header timestamp: %lu", stamp);
    if (stamp != kKnownStampA && stamp != kKnownStampB) {
        LOG_INFO("EXE timestamp %lu is not one of the two builds this was developed against (%lu / %lu); continuing if patterns match",
                 stamp, kKnownStampA, kKnownStampB);
    }

    auto* game = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    if (!near_arena_init(game + 0x1000)) {
        return false;
    }

    bool ok = true;
    ok &= hook_named(g_frame_hook, "hk_MainAppFrameTimeInfo_UnlockDeltatime", kFrameTime, sizeof(kFrameTime), 5,
                     reinterpret_cast<void*>(&hk_frame_time));
    ok &= hook_named(g_update_hook, "hk_KatanaMainApp_UpdateDT", kUpdateDt, sizeof(kUpdateDt), 5,
                     reinterpret_cast<void*>(&hk_update));
    ok &= settings.ground_snap_fix ? patch_snap() : patch_jump();
    ok &= hook_named(g_dur_weapon, "hk_Durability_Weapon", kDurWeapon, sizeof(kDurWeapon), 5,
                     reinterpret_cast<void*>(&hk_dur_weapon));
    ok &= hook_named(g_dur_armor, "hk_Durability_Armor", kDurArmor, sizeof(kDurArmor), 5,
                     reinterpret_cast<void*>(&hk_dur_armor));
    ok &= hook_named(g_dur_ring, "hk_Durability_Ring", kDurRing, sizeof(kDurRing), 5,
                     reinterpret_cast<void*>(&hk_dur_ring));
    ok &= hook_named(g_dur_ring_hit, "hk_Durability_RingOnHit", kDurRingHit, sizeof(kDurRingHit), 5,
                     reinterpret_cast<void*>(&hk_dur_ring_hit));
    uint8_t* rate = find_unique(kDurRingRate, sizeof(kDurRingRate), "hk_Durability_RingRate");
    if (!rate) {
        LOG_ERROR("hk_Durability_RingRate: apply failed");
        ok = false;
    } else {
        ok &= install_ring_rate(rate);
    }
    if (settings.physics_fps > 0) {
        ok &= hook_named(g_world_hook, "hk_hkpWorld_UpdateExpectedDeltaTime", kWorldCtor, sizeof(kWorldCtor), 7,
                         reinterpret_cast<void*>(&hk_world));
    }
    ok &= hook_named(g_cloth_hook, "hk_PXClothWorld_FrametimeUpdate", kCloth, sizeof(kCloth), 6,
                     reinterpret_cast<void*>(&hk_cloth));
    return ok;
}

void patches_remove() {
    g_frame_hook.remove();
    g_update_hook.remove();
    g_world_hook.remove();
    g_cloth_hook.remove();
    g_dur_weapon.remove();
    g_dur_armor.remove();
    g_dur_ring.remove();
    g_dur_ring_hit.remove();
    remove_ring_rate();
    unpatch_jump();
    unpatch_snap();
}

#endif  // _M_X64
