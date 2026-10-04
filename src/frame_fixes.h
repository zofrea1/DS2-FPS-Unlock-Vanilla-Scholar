#pragma once

#include <cstddef>
#include <cstdint>

// Gameplay fixes shared by both builds. Each one makes a per-frame rule behave as it does at
// 60 FPS, the rate the PC game was tuned for, at any frame rate.

// Game time, advanced once per gameplay update by the frame step.
void frame_clock_advance(float dt);

// Ground snap. After the physics step the game sweeps 0.1 units down and pulls the body onto any
// floor it finds, once per frame. At a high frame rate a jump rises only a few hundredths of a unit
// per frame, so the snap caught the takeoff and the character stayed on the ground. The snap is
// skipped while the character's jump is in progress (the game's own jump counter, which also
// gates the jump's velocity) and, for other upward moves, while the body rose since its last
// position. Everything else (running, slopes, landing, rolls) keeps the game's snap unchanged.
// Returns true when this call should leave the position as it is.
bool ground_snap_skip(void* proxy, float in_y, float dt, bool jumping);
// Records where the body ended up this frame.
void ground_snap_record(void* proxy, float y);

// Forward-tilt history for guard break and jump attack. The game keeps the last 16 frames of the
// movement stick and accepts forward + R1/R2 only if it finds the stick near neutral within a
// time window in that history. 16 frames are 0.27 s at 60 FPS but 0.13 s at 120 and 0.07 s at
// 240, so a normal push fell out of the history before the window ended. After the game records a
// frame, this folds it into the previous entry until that entry covers about 1/60 s, so the 16
// entries always span the time they do at 60 FPS.
// `history` points at entry 0 of 16 entries of {x, y, magnitude, dt}.
struct StickHistorySave {
    float first[4];
    float last[4];
};
void stick_history_before(const float* history, StickHistorySave& save);
void stick_history_after(float* history, const StickHistorySave& save, float dt);

// Animation events (TAE). Each game update the TAE player dispatches, track by track, every event
// in the TAE frame it is on (TAE frames are usually 1/30 s), at least one frame's window per
// update. A track remembers only the last event it dispatched and flags an event as starting when
// it differs, so when two events overlap on one track both are flagged as starting on every
// dispatch. That happens once per TAE frame at 30 FPS, twice at 60 (what PC players know), and
// about five times at 144, so their start actions (damage, effects, objects) repeat more often
// at high frame rates. The handlers still run on every update; only the repeated start flag of an
// event the track already dispatched last time is cleared, except on dispatches that would have
// happened at 60 FPS. Those come from a per-track allowance that refills at 60 per second, plus
// every dispatch that crosses into a new TAE frame.
// `last_event` is the track's own last-dispatched event before this dispatch. Returns the
// enclosing dispatch's context, which tae_dispatch_end restores.
void* tae_dispatch_begin(void* track, const void* last_event, bool advanced);
void tae_dispatch_end(void* outer);
// Called before each handler call with the event and the address of its start flag.
void tae_filter_event(void* track, const void* event, uint8_t* start_flag);
