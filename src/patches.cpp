#include "patches.h"

#include "frame_fixes.h"
#include "inline_hook.h"
#include "jump_trace.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
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
// The snap's proxy: +0x08 character, +0x10 PXCharacterRigidBody, +0x64 commanded upward speed,
// +0xC4 grounded, +0xC5 ground probe result. Character: +0xB8 status, whose +0x4C8 bit 12 makes the
// probe cast its ground ray instead of taking Havok's flag (and +0x61C is the airborne counter).
// Body: +0x120 Havok support state, +0x1A4 accumulated fall speed, +0x1C1 walkable support.
constexpr size_t kProxyChr = 0x08;
constexpr size_t kProxyBody = 0x10;
constexpr size_t kProxyRise = 0x64;
constexpr size_t kProxyGrounded = 0xC4;
constexpr size_t kProxyProbe = 0xC5;
constexpr size_t kChrStatus = 0xB8;
constexpr size_t kStatusFlags = 0x4C8;
constexpr uint32_t kStatusForceRay = 0x1000;
constexpr size_t kStatusJumpCount = 0x61C;
constexpr size_t kBodySupport = 0x120;
constexpr size_t kBodyFall = 0x1A4;
constexpr size_t kBodyWalkable = 0x1C1;
// ChrPhysicalProxy::update(proxy, step), run after the physics step (JumpTrace only). The security
// cookie load is masked.
const uint8_t kProxyUpdate[] = {
    0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x56, 0x41, 0x57, 0x49, 0x8D, 0x6B, 0xC8, 0x48, 0x81, 0xEC, 0x18,
    0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45,
    0xE0, 0x48, 0x8B, 0xF1, 0x48, 0x8B, 0x49, 0x10, 0x41, 0x0F, 0x29, 0x73, 0xC8, 0x41, 0x0F, 0x29,
    0x7B, 0xB8, 0x48, 0x8B, 0xDA, 0xE8};
const char kProxyUpdateMask[] = "xxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxx";
static_assert(sizeof(kProxyUpdateMask) == sizeof(kProxyUpdate) + 1, "proxy update mask length");
// Movement stick history used for forward + R1/R2. rcx is the attack input recognizer, xmm1 the
// frame step, r8 the logical input. 16 entries of {x, y, magnitude, dt} start at +0x74.
const uint8_t kStickHistory[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x70, 0x18, 0x57, 0x48, 0x81, 0xEC, 0x90,
    0x00, 0x00, 0x00, 0x0F, 0x29, 0x70, 0xE8, 0x0F, 0x29, 0x78, 0xD8, 0x44, 0x0F, 0x29, 0x40, 0xC8,
    0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x50, 0x48,
    0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD9, 0x49, 0x8B, 0xF0};
const char kStickHistoryMask[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxx????xxxxxx";
static_assert(sizeof(kStickHistoryMask) == sizeof(kStickHistory) + 1, "stick history mask length");
constexpr size_t kStickHistoryOffset = 0x74;
// Per-track TAE event dispatch (see tae_dispatch_begin in frame_fixes.h). rcx is the track,
// xmm1/xmm2 the window, r9b whether this update crossed into a new TAE frame. The pattern runs
// through the handler call: "mov [rbp-10h], cl; mov rcx, rbx; setne [rbp-0Fh]; call [rax+10h]",
// which is redirected through a stub so the start flag can be filtered first.
const uint8_t kTaeDispatch[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x78, 0x10, 0x55, 0x48, 0x8D, 0x68, 0xA1,
    0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00, 0x0F, 0x29, 0x70, 0xE8, 0x0F, 0x29, 0x78, 0xD8, 0x33,
    0xC0, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x49, 0x08, 0x41, 0x0F, 0xB6, 0xF9, 0x4C, 0x8D, 0x4D, 0xF7,
    0x48, 0x89, 0x45, 0xF7, 0x48, 0x89, 0x45, 0xFF, 0x0F, 0x28, 0xF2, 0x0F, 0x28, 0xF9, 0x48, 0x89,
    0x45, 0x07, 0x48, 0x89, 0x45, 0x0F, 0x48, 0x89, 0x45, 0x17, 0x48, 0x89, 0x45, 0x1F, 0x48, 0x89,
    0x45, 0x27, 0x48, 0x89, 0x45, 0x2F, 0xE8, 0xC5, 0xA1, 0x65, 0x00, 0x48, 0x85, 0xC0, 0x0F, 0x84,
    0xB5, 0x00, 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x4D, 0x17, 0x33, 0xC0, 0x48, 0x89, 0x45, 0xDF, 0x48, 0x89, 0x45, 0xCF, 0x48, 0x89,
    0x45, 0xD7, 0xF3, 0x0F, 0x11, 0x7D, 0xDF, 0xF3, 0x0F, 0x11, 0x75, 0xE3, 0x48, 0x89, 0x45, 0xE7,
    0x48, 0x89, 0x45, 0xEF, 0x8B, 0x45, 0xF7, 0x89, 0x45, 0xCF, 0x48, 0x8B, 0x45, 0xFF, 0x48, 0x89,
    0x4D, 0xC7, 0x48, 0x89, 0x45, 0xD7, 0xE8, 0xB5, 0xA0, 0x65, 0x00, 0x48, 0x8B, 0x4D, 0x17, 0xF3,
    0x0F, 0x11, 0x45, 0xE7, 0xE8, 0xB7, 0xA0, 0x65, 0x00, 0x0F, 0xB6, 0x45, 0x0F, 0x33, 0xC9, 0x40,
    0x84, 0xFF, 0x48, 0x8D, 0x55, 0xC7, 0xF3, 0x0F, 0x11, 0x45, 0xEB, 0x0F, 0x45, 0xC8, 0x0F, 0xB6,
    0x45, 0x10, 0x88, 0x4D, 0xEF, 0x33, 0xC9, 0x40, 0x84, 0xFF, 0x0F, 0x45, 0xC8, 0x48, 0x8B, 0x45,
    0x17, 0x48, 0x39, 0x43, 0x10, 0x48, 0x8B, 0x03, 0x88, 0x4D, 0xF0, 0x48, 0x8B, 0xCB, 0x0F, 0x95,
    0x45, 0xF1, 0xFF, 0x50, 0x10};
constexpr size_t kTaeHandlerCallAt = 0xE8;
constexpr size_t kTaeHandlerCallLen = 13;
constexpr size_t kTaeTrackLastEvent = 0x10;
constexpr size_t kTaeEventStart = 0x2A;
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
    const float dt = g_frame_dt.load(std::memory_order_relaxed);
    frame_clock_advance(dt);
    orig(self, dt);
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

// Guard break and jump attack: see stick_history_before/after in frame_fixes.h.
using StickHistoryFn = void(__fastcall*)(void* recognizer, float dt, void* input);
InlineHook g_stick_history;

void __fastcall hk_stick_history(void* recognizer, float dt, void* input) {
    auto* history = reinterpret_cast<float*>(static_cast<uint8_t*>(recognizer) + kStickHistoryOffset);
    StickHistorySave save;
    stick_history_before(history, save);
    reinterpret_cast<StickHistoryFn>(g_stick_history.trampoline)(recognizer, dt, input);
    stick_history_after(history, save, dt);
}

// TAE events: see tae_dispatch_begin in frame_fixes.h.
using TaeDispatchFn = void(__fastcall*)(void* track, float t0, float t1, char advanced);
using TaeHandlerFn = void(__fastcall*)(void* track, void* event);
InlineHook g_tae_dispatch;
uint8_t* g_tae_call_site = nullptr;
uint8_t g_tae_call_orig[kTaeHandlerCallLen]{};

void __fastcall hk_tae_dispatch(void* track, float t0, float t1, char advanced) {
    const void* last = *reinterpret_cast<void* const*>(static_cast<uint8_t*>(track) + kTaeTrackLastEvent);
    void* outer = tae_dispatch_begin(track, last, advanced != 0);
    reinterpret_cast<TaeDispatchFn>(g_tae_dispatch.trampoline)(track, t0, t1, advanced);
    tae_dispatch_end(outer);
}

// Reached from the stub at the handler call with the track, the event and the handler.
void __fastcall hk_tae_handler(void* track, uint8_t* event, TaeHandlerFn handler) {
    tae_filter_event(track, *reinterpret_cast<void* const*>(event), event + kTaeEventStart);
    handler(track, event);
}

bool patch_tae() {
    constexpr const char* kName = "hk_MorphemeTimeActTrack_EventDispatch";
    uint8_t* site = find_unique(kTaeDispatch, sizeof(kTaeDispatch), kName);
    if (!site) {
        LOG_ERROR("%s: apply failed", kName);
        return false;
    }
    uint8_t* call = site + kTaeHandlerCallAt;
    // mov [rbp-10h], cl; mov rcx, rbx; setne [rbp-0Fh]; mov r8, [rax+10h]; jmp [rip]; dq hk_tae_handler
    uint8_t* stub = near_arena_alloc(32);
    if (!stub) {
        LOG_ERROR("%s: trampoline arena exhausted", kName);
        return false;
    }
    const uint8_t head[] = {0x88, 0x4D, 0xF0, 0x48, 0x8B, 0xCB, 0x0F, 0x95, 0x45, 0xF1,
                            0x4C, 0x8B, 0x40, 0x10, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00};
    std::memcpy(stub, head, sizeof(head));
    const uint64_t target = reinterpret_cast<uint64_t>(&hk_tae_handler);
    std::memcpy(stub + sizeof(head), &target, sizeof(target));
    const intptr_t rel = reinterpret_cast<intptr_t>(stub) - reinterpret_cast<intptr_t>(call + kTaeHandlerCallLen);
    if (rel < static_cast<intptr_t>(INT32_MIN) || rel > static_cast<intptr_t>(INT32_MAX)) {
        LOG_ERROR("%s: relative call does not fit", kName);
        return false;
    }
    if (!g_tae_dispatch.install(site, reinterpret_cast<void*>(&hk_tae_dispatch), 7)) {
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
    // 8-byte nop, then call the stub so the handler returns to the instruction after the original call.
    const uint8_t nop8[] = {0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::memcpy(call, nop8, sizeof(nop8));
    call[8] = 0xE8;
    const int32_t rel32 = static_cast<int32_t>(rel);
    std::memcpy(call + 9, &rel32, sizeof(rel32));
    VirtualProtect(call, kTaeHandlerCallLen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), call, kTaeHandlerCallLen);
    FlushInstructionCache(GetCurrentProcess(), stub, 32);
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

bool hook_named(InlineHook& hook, const char* name, const uint8_t* pat, size_t pat_len, size_t stolen, void* detour,
                const char* mask = nullptr) {
    uint8_t* site = find_unique(pat, pat_len, name, mask);
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

// Set when the fixes are off and the snap hook only reports to the jump trace.
bool g_snap_observe_only = false;
using SnapFn = void*(__fastcall*)(void* self, float* out, const float* in);
SnapFn g_snap_orig = nullptr;
uint8_t* g_snap_site = nullptr;
int32_t g_snap_rel_orig = 0;

// The character's ground snap: see ground_release in frame_fixes.h. A released frame leaves the
// position as it is and clears the grounded and support flags, as the game sees them at 60 FPS.
template <typename T>
T field(const void* base, size_t offset) {
    T v;
    std::memcpy(&v, static_cast<const uint8_t*>(base) + offset, sizeof(T));
    return v;
}

template <typename T>
void set_field(void* base, size_t offset, T v) {
    std::memcpy(static_cast<uint8_t*>(base) + offset, &v, sizeof(T));
}

bool snap_released(void* proxy) {
    auto* chr = field<uint8_t*>(proxy, kProxyChr);
    auto* body = field<uint8_t*>(proxy, kProxyBody);
    if (!chr || !body || !field<uint8_t>(proxy, kProxyProbe)) {
        return false;
    }
    auto* status = field<uint8_t*>(chr, kChrStatus);
    const bool ray = status && (field<uint32_t>(status, kStatusFlags) & kStatusForceRay) != 0;
    const bool walkable = !ray && field<uint8_t>(body, kBodyWalkable) != 0;
    if (!ground_release(field<float>(proxy, kProxyRise), field<float>(body, kBodyFall), walkable)) {
        return false;
    }
    set_field<uint8_t>(proxy, kProxyGrounded, 0);
    set_field<uint8_t>(proxy, kProxyProbe, 0);
    set_field<uint8_t>(body, kBodyWalkable, 0);
    set_field<int32_t>(body, kBodySupport, 0);
    return true;
}

void* __fastcall hk_snap(void* self, float* out, const float* in) {
    const float in_y = in[1];
    if (!g_snap_observe_only && snap_released(self)) {
        std::memcpy(out, in, 16);
        jump_trace_snap(self, true, in_y, in_y);
        return out;
    }
    void* result = g_snap_orig(self, out, in);
    jump_trace_snap(self, false, in_y, static_cast<const float*>(result)[1]);
    return result;
}

// JumpTrace: the player's body before and after ChrPhysicalProxy::update. Offsets: proxy +0x08
// character, +0x10 PXCharacterRigidBody, +0x60 desired velocity, +0xC4 grounded, +0xC5 probe;
// character +0x90 final position, +0xB8 status, +0xC0 control flags; body +0x70 position, +0x1A0
// fall velocity, +0x1B4 state, +0x120 support, +0x130 surface normal, +0x1C1 walkable contact,
// +0x1D0 Havok character (+0x20 rigid body, whose +0x230 is the linear velocity).
using ProxyUpdateFn = void(__fastcall*)(void* proxy, const float* step);
InlineHook g_proxy_update;
const void* g_player_vtable = nullptr;

void __fastcall hk_proxy_update(void* proxy, const float* step) {
    auto orig = reinterpret_cast<ProxyUpdateFn>(g_proxy_update.trampoline);
    auto* chr = field<uint8_t*>(proxy, kProxyChr);
    auto* body = field<uint8_t*>(proxy, kProxyBody);
    if (!chr || !body || field<const void*>(chr, 0) != g_player_vtable) {
        orig(proxy, step);
        return;
    }
    JumpTraceSample s;
    s.dt = g_frame_dt.load(std::memory_order_relaxed);
    s.step0 = step[0];
    s.step2 = step[2];
    s.body_state = field<int32_t>(body, 0x1B4);
    s.support = field<int32_t>(body, 0x120);
    s.walkable = field<uint8_t>(body, 0x1C1);
    s.normal_y = field<float>(body, 0x134);
    s.fall_vy = field<float>(body, 0x1A4);
    s.body_y = field<float>(body, 0x74);
    if (auto* crb = field<uint8_t*>(body, 0x1D0)) {
        if (auto* rb = field<uint8_t*>(crb, 0x20)) {
            const float vx = field<float>(rb, 0x230);
            const float vz = field<float>(rb, 0x238);
            s.body_vy = field<float>(rb, 0x234);
            s.body_vh = std::sqrt(vx * vx + vz * vz);
        }
    }
    const float dvx = field<float>(proxy, 0x60);
    const float dvz = field<float>(proxy, 0x68);
    s.desired_vy = field<float>(proxy, 0x64);
    s.desired_vh = std::sqrt(dvx * dvx + dvz * dvz);
    jump_trace_begin(proxy);
    orig(proxy, step);
    s.probe = field<uint8_t>(proxy, 0xC5);
    s.grounded = field<uint8_t>(proxy, 0xC4);
    s.final_x = field<float>(chr, 0x90);
    s.final_y = field<float>(chr, 0x94);
    s.final_z = field<float>(chr, 0x98);
    if (auto* status = field<uint8_t*>(chr, kChrStatus)) {
        s.jump_count = field<int32_t>(status, kStatusJumpCount);
        s.jump_type = field<int32_t>(status, 0xF0);
        s.jump_vx = field<float>(status, 0x6A0);
        s.jump_vy = field<float>(status, 0x6A4);
        s.jump_vz = field<float>(status, 0x6A8);
    }
    if (auto* flags = field<uint8_t*>(chr, 0xC0)) {
        s.flags25 = field<uint8_t>(flags, 0x25);
        s.flags31 = field<uint8_t>(flags, 0x31);
        s.flags32 = field<uint8_t>(flags, 0x32);
        s.flags33 = field<uint8_t>(flags, 0x33);
        s.flags34 = field<uint8_t>(flags, 0x34);
    }
    jump_trace_end(proxy, s);
}

// RTTI lookup of a class's primary vtable: the type descriptor holding the mangled name, the
// complete-object locator (signature 1, offset 0) that refers to it by RVA, and the vtable slot
// just after the pointer to that locator.
const void* find_vtable(const char* mangled) {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    std::vector<Section> data;
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) && (sec->Characteristics & IMAGE_SCN_MEM_READ)) {
            data.push_back({base + sec->VirtualAddress, sec->Misc.VirtualSize});
        }
    }
    const size_t len = std::strlen(mangled) + 1;
    uint32_t td_rva = 0;
    for (const Section& s : data) {
        for (size_t i = 0x10; i + len <= s.size && !td_rva; ++i) {
            if (std::memcmp(s.start + i, mangled, len) == 0) {
                td_rva = static_cast<uint32_t>(s.start + i - 0x10 - base);
            }
        }
    }
    if (!td_rva) {
        return nullptr;
    }
    uint64_t col = 0;
    for (const Section& s : data) {
        for (size_t i = 0; i + 0x18 <= s.size && !col; i += 4) {
            const uint8_t* p = s.start + i;
            const uint32_t self_rva = static_cast<uint32_t>(p - base);
            if (field<uint32_t>(p, 0) == 1 && field<uint32_t>(p, 4) == 0 && field<uint32_t>(p, 0xC) == td_rva &&
                field<uint32_t>(p, 0x14) == self_rva) {
                col = reinterpret_cast<uint64_t>(p);
            }
        }
    }
    if (!col) {
        return nullptr;
    }
    for (const Section& s : data) {
        for (size_t i = 0; i + 16 <= s.size; i += 8) {
            if (field<uint64_t>(s.start + i, 0) == col) {
                return s.start + i + 8;
            }
        }
    }
    return nullptr;
}

bool patch_jump_trace() {
    constexpr const char* kName = "hk_ChrPhysicalProxy_JumpTrace";
    g_player_vtable = find_vtable(".?AVPlayerCtrl@@");
    if (!g_player_vtable) {
        LOG_ERROR("%s: PlayerCtrl vtable not found", kName);
        return false;
    }
    uint8_t* site = find_unique(kProxyUpdate, sizeof(kProxyUpdate), kName, kProxyUpdateMask);
    if (!site || !g_proxy_update.install(site, reinterpret_cast<void*>(&hk_proxy_update), 5)) {
        LOG_ERROR("%s: apply failed", kName);
        return false;
    }
    LOG_INFO("%s: apply successful", kName);
    return true;
}

bool patch_snap() {
    constexpr const char* kName = "hk_ChrPhysicalProxy_GroundSnap";
    uint8_t* hit = find_unique(kSnapSite, sizeof(kSnapSite), kName, kSnapMask);
    if (!hit) {
        LOG_ERROR("%s: apply failed", kName);
        return false;
    }
    uint8_t* call = hit + 23;
    int32_t rel = 0;
    std::memcpy(&rel, call + 1, 4);
    uint8_t* target = call + 5 + rel;
    uint8_t* stub = near_arena_alloc(16);
    if (!stub) {
        LOG_ERROR("%s: trampoline arena exhausted", kName);
        return false;
    }
    stub[0] = 0xFF;
    stub[1] = 0x25;
    std::memset(stub + 2, 0, 4);
    const uint64_t addr = reinterpret_cast<uint64_t>(&hk_snap);
    std::memcpy(stub + 6, &addr, 8);
    const intptr_t new_rel = reinterpret_cast<intptr_t>(stub) - reinterpret_cast<intptr_t>(call + 5);
    if (new_rel < static_cast<intptr_t>(INT32_MIN) || new_rel > static_cast<intptr_t>(INT32_MAX)) {
        LOG_ERROR("%s: relative call does not fit", kName);
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old)) {
        LOG_ERROR("%s: VirtualProtect failed (%lu)", kName, GetLastError());
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
        if (!jump_trace_enabled()) {
            LOG_INFO("FPSUnlock is false; the game is left unchanged");
            return true;
        }
        // Stock game with the trace: the snap is observed but never changed.
        LOG_INFO("FPSUnlock is false; only the jump trace is installed");
        g_snap_observe_only = true;
        auto* exe = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        return near_arena_init(exe + 0x1000) && patch_snap() && patch_jump_trace();
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
    if (jump_trace_enabled()) {
        ok &= patch_jump_trace();
    }
    if (settings.forward_attack_fix) {
        ok &= hook_named(g_stick_history, "hk_ChrPadAttackInput_StickHistory", kStickHistory, sizeof(kStickHistory), 7,
                         reinterpret_cast<void*>(&hk_stick_history), kStickHistoryMask);
    }
    if (settings.tae_event_fix) {
        ok &= patch_tae();
    }
    if (settings.durability_fix) {
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
    }
    if (settings.physics_fps > 0) {
        ok &= hook_named(g_world_hook, "hk_hkpWorld_UpdateExpectedDeltaTime", kWorldCtor, sizeof(kWorldCtor), 7,
                         reinterpret_cast<void*>(&hk_world));
    }
    if (settings.cloth_fix) {
        ok &= hook_named(g_cloth_hook, "hk_PXClothWorld_FrametimeUpdate", kCloth, sizeof(kCloth), 6,
                         reinterpret_cast<void*>(&hk_cloth));
    }
    return ok;
}

void patches_remove() {
    g_frame_hook.remove();
    g_update_hook.remove();
    g_world_hook.remove();
    g_cloth_hook.remove();
    g_stick_history.remove();
    unpatch_tae();
    g_dur_weapon.remove();
    g_dur_armor.remove();
    g_dur_ring.remove();
    g_dur_ring_hit.remove();
    remove_ring_rate();
    unpatch_jump();
    unpatch_snap();
    g_proxy_update.remove();
}

#endif  // _M_X64
