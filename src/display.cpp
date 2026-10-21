#include "display.h"

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#pragma comment(lib, "user32.lib")

namespace {

unsigned g_fullscreen_hz = 0;

}  // namespace

void display_init(int configured_hz, bool log_aspect) {
    DEVMODEW current{};
    current.dmSize = sizeof(current);
    const bool have_current = EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &current) && current.dmPelsHeight != 0;
    DEVMODEW stored{};
    stored.dmSize = sizeof(stored);
    const bool have_stored = EnumDisplaySettingsW(nullptr, ENUM_REGISTRY_SETTINGS, &stored);

    unsigned desktop_hz = have_current && current.dmDisplayFrequency > 1 ? current.dmDisplayFrequency : 0;
    if (have_stored && stored.dmDisplayFrequency > desktop_hz) {
        desktop_hz = stored.dmDisplayFrequency;
    }
    g_fullscreen_hz = configured_hz > 0 ? static_cast<unsigned>(configured_hz) : desktop_hz;

    if (have_current) {
        LOG_INFO("   Display %lux%lu at %lu Hz; fullscreen asks for %u Hz%s", current.dmPelsWidth, current.dmPelsHeight,
                 current.dmDisplayFrequency, g_fullscreen_hz,
                 configured_hz > 0 ? " (FullscreenRefreshRate)" : " (the desktop's rate)");
    }
    if (!log_aspect || !have_current) {
        return;
    }
    // The game lists every fullscreen mode the display offers, so the player picks the matching one in its options.
    const float aspect = static_cast<float>(current.dmPelsWidth) / static_cast<float>(current.dmPelsHeight);
    const float wide = 16.0f / 9.0f;
    if (aspect > wide - 0.01f && aspect < wide + 0.01f) {
        LOG_INFO("   Display %lux%lu is 16:9", current.dmPelsWidth, current.dmPelsHeight);
    } else {
        LOG_INFO("   Display %lux%lu (%.3f): choose %lux%lu as the game's fullscreen resolution to fill it",
                 current.dmPelsWidth, current.dmPelsHeight, aspect, current.dmPelsWidth, current.dmPelsHeight);
    }
}

unsigned display_fullscreen_hz() {
    return g_fullscreen_hz;
}
