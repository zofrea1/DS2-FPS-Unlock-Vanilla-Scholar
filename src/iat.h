#pragma once

// One import of the game EXE, imported by name from `dll`, pointed at `detour`. The slot's previous value is
// returned in `original` (which may be this DLL's own export when the game imports it from this DLL).
struct IatHook {
    void** slot = nullptr;
    void* original = nullptr;

    bool install(const char* dll, const char* name, void* detour);
    void remove();
};
