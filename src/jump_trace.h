#pragma once

#include <cstdint>

// Diagnostic only (JumpTrace = true in the INI). Writes one CSV row per physics update of the
// player's body while it is in the air or its jump counter is raised, plus a short lead-in, to a
// CSV next to the DLL. Both builds fill the same columns.
struct JumpTraceSample {
    float dt = 0.0f;
    float step0 = 0.0f;
    float step2 = 0.0f;
    int jump_count = 0;
    int jump_type = 0;
    uint8_t flags25 = 0;
    uint8_t flags31 = 0;
    uint8_t flags32 = 0;
    uint8_t flags33 = 0;
    uint8_t flags34 = 0;
    float jump_vx = 0.0f;
    float jump_vy = 0.0f;
    float jump_vz = 0.0f;
    float desired_vy = 0.0f;
    float desired_vh = 0.0f;
    int body_state = 0;
    int support = 0;
    uint8_t walkable = 0;
    float normal_y = 0.0f;
    float fall_vy = 0.0f;
    float body_y = 0.0f;
    float body_vy = 0.0f;
    float body_vh = 0.0f;
    uint8_t probe = 0;
    uint8_t grounded = 0;
    float final_x = 0.0f;
    float final_y = 0.0f;
    float final_z = 0.0f;
};

// The file is DS2-FPS-Unlock-jumptrace-<mode>-<date>-<time>.csv, so runs never overwrite each other.
bool jump_trace_open(const wchar_t* dll_path, const wchar_t* mode);
bool jump_trace_enabled();
void jump_trace_close();
// Brackets one physics update of the player's body. The snap hook reports through
// jump_trace_snap while it is open.
void jump_trace_begin(const void* proxy);
void jump_trace_snap(const void* proxy, bool skipped, float in_y, float out_y);
void jump_trace_end(const void* proxy, const JumpTraceSample& s);
