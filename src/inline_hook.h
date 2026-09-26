#pragma once

#include <cstddef>
#include <cstdint>

// 5-byte relative jump at `target` into a nearby absolute gateway, which jumps
// to `detour`. `stolen` original bytes are replayed by the trampoline, then
// execution continues at target+stolen. Stolen bytes must not be RIP-relative.
struct InlineHook {
    void* target = nullptr;
    void* trampoline = nullptr;
    uint8_t original[16]{};
    size_t stolen = 0;
    bool installed = false;

    bool install(void* target_site, void* detour, size_t stolen_bytes);
    void remove();
};

// One executable page allocated within ±2GB of `near_addr`, shared by hooks.
bool near_arena_init(void* near_addr);
// Bytes from that page. Null if the arena is not up or the page is full.
uint8_t* near_arena_alloc(size_t bytes);
