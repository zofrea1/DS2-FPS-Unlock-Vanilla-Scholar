#pragma once

#include <cstddef>
#include <cstdint>

// Gameplay fixes shared by both builds. Each one makes a per-frame rule behave as it does at
// 60 FPS, the rate the PC game was tuned for, at any frame rate.

// Game time, advanced once per gameplay update by the frame step.
void frame_clock_advance(float dt);

// Ground contact while rising. After each physics step the game decides whether the body is on the
// ground. While the body is being moved upward it takes Havok's walkable-support flag as the answer,
// and while grounded it snaps the body down onto the floor (a 0.1 unit sweep). That snap holds the
// body inside Havok's contact band, so the flag stays set and the loop repeats every frame. At 60
// FPS one 1/60 s step at the jump's takeoff speed (3.45 to 4 units/s) carries the body past the
// band and Havok drops support, every time in both games' stock traces, while the 1.4 units/s
// wind-up stays held; the cut-off is taken between them, at 2.4 units/s net of the fall speed. At a
// high frame rate each step moves the body only a fraction of that, so the loop never broke and
// the character stayed stuck on steep slopes. This reports the frames where 60 FPS would have
// broken it: the caller then leaves the position as it is and clears the grounded and support
// flags, as the game would have seen them at 60 FPS.
// Everything else, including landings, runs, rolls and the forced-ray cases, keeps the stock snap.
// `rise` is the commanded upward speed, `fall` the accumulated fall speed (negative), `walkable`
// whether the grounded answer came from Havok's flag rather than the forced ground ray.
bool ground_release(float rise, float fall, bool walkable);

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
