#include "jump_trace.h"

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <share.h>

namespace {

FILE* g_file = nullptr;
std::mutex g_mu;
// Rows are written from the game's update thread only; the snap report comes from inside the
// same call, so a plain per-thread record is enough.
struct SnapReport {
    const void* proxy = nullptr;
    bool called = false;
    bool skipped = false;
    float in_y = 0.0f;
    float out_y = 0.0f;
};
thread_local SnapReport t_snap;

// Rows are kept while the body is in the air or the jump counter is raised, and for a short
// tail after. The last few rows before that are held back so each jump starts with its run-up.
constexpr int kLeadRows = 24;
constexpr double kTail = 0.3;
char g_lead[kLeadRows][512];
int g_lead_count = 0;
int g_lead_next = 0;
double g_active_until = -1.0;
long long g_frame = 0;
bool g_found_logged = false;
double g_last_flush = 0.0;
double g_last_row = 0.0;
LARGE_INTEGER g_qpc_start{};
double g_qpc_scale = 0.0;

// Wall-clock seconds since the trace opened, so rows have a time base with the fixes off too.
double trace_now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart - g_qpc_start.QuadPart) * g_qpc_scale;
}

void write_lead() {
    const int start = g_lead_count < kLeadRows ? 0 : g_lead_next;
    for (int i = 0; i < g_lead_count; ++i) {
        std::fputs(g_lead[(start + i) % kLeadRows], g_file);
    }
    g_lead_count = 0;
    g_lead_next = 0;
}

}  // namespace

bool jump_trace_open(const wchar_t* dll_path, const wchar_t* mode) {
    wchar_t path[MAX_PATH];
    lstrcpynW(path, dll_path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) {
        slash[1] = 0;
    } else {
        path[0] = 0;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t full[MAX_PATH];
    _snwprintf_s(full, _TRUNCATE, L"%sDS2-FPS-Unlock-jumptrace-%s-%04u%02u%02u-%02u%02u%02u.csv", path, mode,
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    g_file = _wfsopen(full, L"w", _SH_DENYNO);
    if (!g_file) {
        return false;
    }
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&g_qpc_start);
    g_qpc_scale = 1.0 / static_cast<double>(freq.QuadPart);
    std::fputs("frame,t,ft,dt,step0,step2,count,type,f25,f31,f32,f33,f34,jvx,jvy,jvz,dvy,dvh,"
               "bstate,support,walk,ny,fallvy,bodyy,bodyvy,bodyvh,probe,grounded,snap,snapskip,"
               "snapin,snapout,x,y,z\n",
               g_file);
    std::fflush(g_file);
    return true;
}

bool jump_trace_enabled() {
    return g_file != nullptr;
}

void jump_trace_begin(const void* proxy) {
    t_snap = SnapReport{};
    t_snap.proxy = proxy;
}

void jump_trace_snap(const void* proxy, bool skipped, float in_y, float out_y) {
    if (t_snap.proxy != proxy) {
        return;
    }
    t_snap.called = true;
    t_snap.skipped = skipped;
    t_snap.in_y = in_y;
    t_snap.out_y = out_y;
}

void jump_trace_end(const void* proxy, const JumpTraceSample& s) {
    if (!g_file || t_snap.proxy != proxy) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_file) {
        return;
    }
    if (!g_found_logged) {
        g_found_logged = true;
        LOG_INFO("JumpTrace: recording the player's body (%p)", proxy);
    }
    const double now = trace_now();
    const double ft = now - g_last_row;
    g_last_row = now;
    char row[512];
    std::snprintf(row, sizeof(row),
                  "%lld,%.4f,%.5f,%.5f,%.5f,%.5f,%d,%d,%02X,%02X,%02X,%02X,%02X,%.3f,%.3f,%.3f,%.3f,%.3f,"
                  "%d,%d,%u,%.3f,%.3f,%.4f,%.3f,%.3f,%u,%u,%u,%u,%.4f,%.4f,%.3f,%.4f,%.3f\n",
                  g_frame++, now, ft, s.dt, s.step0, s.step2, s.jump_count, s.jump_type, s.flags25, s.flags31,
                  s.flags32, s.flags33, s.flags34, s.jump_vx, s.jump_vy, s.jump_vz, s.desired_vy, s.desired_vh,
                  s.body_state, s.support, s.walkable, s.normal_y, s.fall_vy, s.body_y, s.body_vy, s.body_vh,
                  s.probe, s.grounded, t_snap.called ? 1u : 0u, t_snap.skipped ? 1u : 0u, t_snap.in_y,
                  t_snap.out_y, s.final_x, s.final_y, s.final_z);
    t_snap.proxy = nullptr;

    const bool airborne = s.jump_count != 0 || !s.grounded;
    if (airborne) {
        if (g_active_until < now) {
            std::fputs("#\n", g_file);
            write_lead();
        }
        g_active_until = now + kTail;
    }
    if (g_active_until >= now) {
        std::fputs(row, g_file);
        return;
    }
    if (g_active_until >= 0.0) {
        // An episode just ended: make sure it is on disk even if the game is killed later.
        g_active_until = -1.0;
        std::fflush(g_file);
        g_last_flush = now;
    }
    std::memcpy(g_lead[g_lead_next], row, sizeof(row));
    g_lead_next = (g_lead_next + 1) % kLeadRows;
    if (g_lead_count < kLeadRows) {
        ++g_lead_count;
    }
    if (now - g_last_flush > 2.0) {
        std::fflush(g_file);
        g_last_flush = now;
    }
}

void jump_trace_close() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
}
