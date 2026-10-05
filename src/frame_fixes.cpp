#include "frame_fixes.h"

#include <atomic>
#include <cstring>

namespace {

constexpr float kRefRate = 60.0f;
constexpr float kRefFrame = 1.0f / kRefRate;

double g_now = 0.0;

// Commanded upward speed at which one 60 FPS step leaves Havok's contact band. Between the
// wind-up speed that stays held (1.46) and the takeoff speeds that are always released (3.45+).
constexpr float kReleaseSpeed = 2.4f;

uint32_t bits(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

}  // namespace

void frame_clock_advance(float dt) {
    if (dt > 0.0f && dt < 1.0f) {
        g_now += dt;
    }
}

bool ground_release(float rise, float fall, bool walkable) {
    return walkable && rise > 0.0f && rise + fall >= kReleaseSpeed;
}

void stick_history_before(const float* history, StickHistorySave& save) {
    std::memcpy(save.first, history, sizeof(save.first));
    std::memcpy(save.last, history + 15 * 4, sizeof(save.last));
}

void stick_history_after(float* history, const StickHistorySave& save, float dt) {
    // Only when the game shifted the history and recorded this frame in entry 0. While a tilt
    // is being held after a detection the game fills the history with 1.0 instead.
    if (std::memcmp(history + 4, save.first, sizeof(save.first)) != 0 || bits(history[3]) != bits(dt)) {
        return;
    }
    const float prev = save.first[3];
    // Fold this frame into the previous entry while that brings the entry nearer to 1/60 s.
    if (!(prev >= 0.0f && prev + 0.5f * dt < kRefFrame)) {
        return;
    }
    std::memmove(history + 4, history + 8, 14 * 4 * sizeof(float));
    std::memcpy(history + 15 * 4, save.last, sizeof(save.last));
    history[3] = prev + dt;
}

namespace {

// Per TAE track: when it was last dispatched, its 60-per-second allowance, and the events it
// dispatched last time and in the dispatch in progress. Tracks are created and freed with the
// animation events that own them, so entries are found by address and reused when stale.
constexpr int kTaeEvents = 16;
struct TaeTrack {
    void* track = nullptr;
    double last = -1.0;
    float allowance = 0.0f;
    bool on_schedule = false;
    int seen_count = 0;
    int now_count = 0;
    const void* seen[kTaeEvents];
    const void* now[kTaeEvents];
};
constexpr int kTaeTracks = 4096;
constexpr int kTaeProbe = 32;
constexpr double kTaeStale = 0.5;
// A dispatch is on the 60 FPS schedule when a full 1/60 s of allowance is available. The slack
// keeps every update on schedule at a jittery 60 FPS. Holding up to two dispatches' worth lets the
// count per TAE frame average what 60 FPS gives at any frame rate and animation speed.
constexpr float kTaeSlack = 0.002f;
constexpr float kTaeAllowanceCap = 2.0f * kRefFrame;
TaeTrack g_tae[kTaeTracks];
std::atomic_flag g_tae_lock = ATOMIC_FLAG_INIT;
thread_local TaeTrack* t_tae = nullptr;

TaeTrack* tae_track(void* track) {
    const uintptr_t key = reinterpret_cast<uintptr_t>(track);
    const size_t start = static_cast<size_t>((key >> 4) * 0x9E3779B97F4A7C15ull >> 20) & (kTaeTracks - 1);
    TaeTrack* reuse = nullptr;
    for (int i = 0; i < kTaeProbe; ++i) {
        TaeTrack& t = g_tae[(start + i) & (kTaeTracks - 1)];
        if (t.track == track) {
            return &t;
        }
        if (!reuse && (!t.track || g_now - t.last > kTaeStale)) {
            reuse = &t;
        }
    }
    if (reuse) {
        reuse->track = track;
        reuse->last = -1.0;
        reuse->allowance = kRefFrame;
        reuse->seen_count = 0;
    }
    return reuse;
}

bool contains(const void* const* list, int count, const void* value) {
    for (int i = 0; i < count; ++i) {
        if (list[i] == value) {
            return true;
        }
    }
    return false;
}

}  // namespace

void* tae_dispatch_begin(void* track, const void* last_event, bool advanced) {
    void* outer = t_tae;
    while (g_tae_lock.test_and_set(std::memory_order_acquire)) {
    }
    TaeTrack* t = tae_track(track);
    g_tae_lock.clear(std::memory_order_release);
    t_tae = t;
    if (!t) {
        return outer;
    }
    if (t->last >= 0.0) {
        const float elapsed = static_cast<float>(g_now - t->last);
        t->allowance = elapsed < kTaeAllowanceCap - t->allowance ? t->allowance + elapsed : kTaeAllowanceCap;
    }
    t->last = g_now;
    t->on_schedule = advanced || t->allowance >= kRefFrame - kTaeSlack;
    if (t->on_schedule) {
        t->allowance -= kRefFrame;
        if (t->allowance < -kRefFrame) {
            t->allowance = -kRefFrame;
        }
    }
    // Nothing was dispatched last time (or the track is new): no event counts as already seen.
    if (!last_event) {
        t->seen_count = 0;
    }
    t->now_count = 0;
    return outer;
}

void tae_dispatch_end(void* outer) {
    TaeTrack* t = t_tae;
    t_tae = static_cast<TaeTrack*>(outer);
    if (!t) {
        return;
    }
    std::memcpy(t->seen, t->now, sizeof(t->now[0]) * t->now_count);
    t->seen_count = t->now_count;
}

void tae_filter_event(void* track, const void* event, uint8_t* start_flag) {
    TaeTrack* t = t_tae;
    if (!t || t->track != track || !event) {
        return;
    }
    if (*start_flag && !t->on_schedule && contains(t->seen, t->seen_count, event)) {
        *start_flag = 0;
    }
    if (t->now_count < kTaeEvents && !contains(t->now, t->now_count, event)) {
        t->now[t->now_count++] = event;
    }
}
