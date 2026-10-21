#include "iat.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstring>

bool IatHook::install(const char* dll, const char* name, void* detour) {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) {
        return false;
    }
    for (auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc) {
        if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), dll) != 0 || !desc->OriginalFirstThunk) {
            continue;
        }
        auto* hint = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        for (; hint->u1.AddressOfData; ++hint, ++iat) {
            if (IMAGE_SNAP_BY_ORDINAL(hint->u1.Ordinal)) {
                continue;
            }
            auto* by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + hint->u1.AddressOfData);
            if (std::strcmp(by_name->Name, name) != 0) {
                continue;
            }
            DWORD old = 0;
            if (!VirtualProtect(&iat->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
                return false;
            }
            original = reinterpret_cast<void*>(iat->u1.Function);
            slot = reinterpret_cast<void**>(&iat->u1.Function);
            iat->u1.Function = reinterpret_cast<ULONG_PTR>(detour);
            VirtualProtect(&iat->u1.Function, sizeof(void*), old, &old);
            return original != nullptr;
        }
    }
    return false;
}

void IatHook::remove() {
    if (!slot || !original) {
        return;
    }
    DWORD old = 0;
    if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
        *slot = original;
        VirtualProtect(slot, sizeof(void*), old, &old);
    }
    slot = nullptr;
    original = nullptr;
}
