// Included from the anonymous namespace in patches.cpp and patches_x86.cpp, after bytes_match.
// A missed pattern logs enough to tell three cases apart: the exe on disk differs at that address,
// memory was changed after the file was mapped, or the real bytes appear only after startup (a stub
// unpacking the image). The first Present rechecks addresses that were known at startup.
constexpr size_t kDump = 64;
constexpr int kMaxHits = 4;
constexpr int kMaxMiss = 8;

struct MissSnap {
    const char* name;
    const uint8_t* pat;
    size_t n;
    const char* mask;
    uint32_t rva;
    uint8_t mem[kDump];
    bool have_mem;
};

MissSnap g_misses[kMaxMiss];
int g_miss_count = 0;
bool g_diag_header = false;
bool g_diag_needed = false;

// One site that did hook. Its saved bytes are what memory held before the patch, so a match against
// the file shows the file reader and this exe agree somewhere the scanner succeeded.
struct ControlSite {
    const char* name = nullptr;
    uint32_t rva = 0;
    uint8_t original[16]{};
    size_t n = 0;
};

ControlSite g_control;

void diag_set_control(const char* name, const void* site, const uint8_t* original, size_t n) {
    if (!name || !site || !original || n == 0 || n > sizeof(g_control.original)) {
        return;
    }
    auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    auto* bytes = static_cast<const uint8_t*>(site);
    if (bytes < base) {
        return;
    }
    g_control.name = name;
    g_control.rva = static_cast<uint32_t>(bytes - base);
    g_control.n = n;
    std::memcpy(g_control.original, original, n);
}

struct ExeMap {
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
    const uint8_t* view = nullptr;
    size_t size = 0;

    ExeMap() = default;
    ExeMap(const ExeMap&) = delete;
    ExeMap& operator=(const ExeMap&) = delete;

    ~ExeMap() {
        if (view) {
            UnmapViewOfFile(view);
        }
        if (mapping) {
            CloseHandle(mapping);
        }
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
    }

    bool open() {
        wchar_t path[MAX_PATH];
        if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) {
            return false;
        }
        file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            LOG_INFO("diag: exe file open failed (%lu)", GetLastError());
            return false;
        }
        LARGE_INTEGER bytes{};
        if (!GetFileSizeEx(file, &bytes) || bytes.QuadPart <= 0 || bytes.QuadPart > 0x7FFFFFFF) {
            LOG_INFO("diag: exe file size unusable");
            return false;
        }
        size = static_cast<size_t>(bytes.QuadPart);
        mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping) {
            LOG_INFO("diag: exe file map failed (%lu)", GetLastError());
            return false;
        }
        view = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
        if (!view) {
            LOG_INFO("diag: exe file view failed (%lu)", GetLastError());
            return false;
        }
        return true;
    }
};

bool file_headers(const ExeMap& exe, const IMAGE_NT_HEADERS*& nt) {
    if (!exe.view || exe.size < sizeof(IMAGE_DOS_HEADER)) {
        return false;
    }
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe.view);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return false;
    }
    const size_t off = static_cast<size_t>(dos->e_lfanew);
    if (off + sizeof(IMAGE_NT_HEADERS) > exe.size) {
        return false;
    }
    nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(exe.view + off);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    const size_t table = off + sizeof(IMAGE_NT_HEADERS);
    const size_t bytes = static_cast<size_t>(nt->FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
    return table + bytes <= exe.size;
}

size_t file_rva_bytes(const ExeMap& exe, uint32_t rva, uint8_t* out, size_t n) {
    const IMAGE_NT_HEADERS* nt = nullptr;
    if (!file_headers(exe, nt)) {
        return 0;
    }
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        const uint32_t span = sec->Misc.VirtualSize > sec->SizeOfRawData ? sec->Misc.VirtualSize : sec->SizeOfRawData;
        if (rva < sec->VirtualAddress || rva >= sec->VirtualAddress + span) {
            continue;
        }
        const uint32_t delta = rva - sec->VirtualAddress;
        if (delta >= sec->SizeOfRawData || sec->PointerToRawData == 0) {
            return 0;
        }
        const size_t off = static_cast<size_t>(sec->PointerToRawData) + delta;
        if (off >= exe.size) {
            return 0;
        }
        size_t avail = sec->SizeOfRawData - delta;
        if (avail > exe.size - off) {
            avail = exe.size - off;
        }
        const size_t take = n < avail ? n : avail;
        std::memcpy(out, exe.view + off, take);
        return take;
    }
    return 0;
}

bool file_off_to_rva(const ExeMap& exe, uint32_t off, uint32_t& rva) {
    const IMAGE_NT_HEADERS* nt = nullptr;
    if (!file_headers(exe, nt)) {
        return false;
    }
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (sec->PointerToRawData == 0 || sec->SizeOfRawData == 0) {
            continue;
        }
        if (off < sec->PointerToRawData || off >= sec->PointerToRawData + sec->SizeOfRawData) {
            continue;
        }
        rva = sec->VirtualAddress + (off - sec->PointerToRawData);
        return true;
    }
    return false;
}

bool page_readable(const void* p) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT) {
        return false;
    }
    if (mbi.Protect & PAGE_GUARD) {
        return false;
    }
    const DWORD prot = mbi.Protect & 0xFF;
    return prot == PAGE_READONLY || prot == PAGE_READWRITE || prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_READ ||
           prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY;
}

size_t copy_mem(const void* p, uint8_t* out, size_t n) {
    const auto* cur = static_cast<const uint8_t*>(p);
    size_t got = 0;
    while (got < n) {
        if (!page_readable(cur + got)) {
            break;
        }
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(cur + got, &mbi, sizeof(mbi)) != sizeof(mbi)) {
            break;
        }
        const auto* region_end = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
        if (region_end <= cur + got) {
            break;
        }
        while (got < n && cur + got < region_end) {
            out[got] = cur[got];
            ++got;
        }
    }
    return got;
}

void log_hex_line(const char* name, const char* what, const uint8_t* p, size_t n) {
    char text[192];
    size_t o = 0;
    static const char kH[] = "0123456789ABCDEF";
    const size_t take = n > 32 ? 32 : n;
    for (size_t i = 0; i < take && o + 4 < sizeof(text); ++i) {
        if (i != 0) {
            text[o++] = ' ';
        }
        text[o++] = kH[p[i] >> 4];
        text[o++] = kH[p[i] & 0x0F];
    }
    text[o] = '\0';
    LOG_INFO("diag %s %s: %s", name, what, text);
    if (n > 32) {
        log_hex_line(name, "next", p + 32, n - 32);
    }
}

size_t prefix_match(const uint8_t* have, const uint8_t* pat, const char* mask, size_t n) {
    size_t matched = 0;
    for (size_t i = 0; i < n; ++i) {
        if (mask && mask[i] == '?') {
            ++matched;
            continue;
        }
        if (have[i] != pat[i]) {
            break;
        }
        ++matched;
    }
    return matched;
}

void log_page(const char* name, const void* p) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) {
        LOG_INFO("diag %s VirtualQuery failed (%lu)", name, GetLastError());
        return;
    }
    LOG_INFO("diag %s page base %p size 0x%llX state 0x%lX protect 0x%lX", name, mbi.BaseAddress,
             static_cast<unsigned long long>(mbi.RegionSize), mbi.State, mbi.Protect);
}

void search_image(const uint8_t* pat, size_t n, const char* mask, uint32_t* hits, int& stored, int& total) {
    stored = 0;
    total = 0;
    auto* image = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(image + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++sec) {
        uint8_t* start = image + sec->VirtualAddress;
        uint8_t* end = start + sec->Misc.VirtualSize;
        for (uint8_t* p = start; p < end;) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) {
                break;
            }
            uint8_t* region_end = static_cast<uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
            uint8_t* chunk_end = region_end < end ? region_end : end;
            if (page_readable(p) && chunk_end > p) {
                const size_t chunk = static_cast<size_t>(chunk_end - p);
                if (chunk >= n) {
                    for (size_t i = 0; i + n <= chunk; ++i) {
                        if (!bytes_match(p + i, pat, mask, n)) {
                            continue;
                        }
                        if (stored < kMaxHits) {
                            hits[stored++] = static_cast<uint32_t>((p + i) - image);
                        }
                        ++total;
                    }
                }
            }
            if (chunk_end <= p) {
                break;
            }
            p = chunk_end;
        }
    }
}

void search_file(const ExeMap& exe, const uint8_t* pat, size_t n, const char* mask, uint32_t* hits, int& stored,
                 int& total) {
    stored = 0;
    total = 0;
    if (!exe.view || exe.size < n) {
        return;
    }
    for (size_t i = 0; i + n <= exe.size; ++i) {
        if (!bytes_match(exe.view + i, pat, mask, n)) {
            continue;
        }
        uint32_t rva = 0;
        if (!file_off_to_rva(exe, static_cast<uint32_t>(i), rva)) {
            continue;
        }
        if (stored < kMaxHits) {
            hits[stored++] = rva;
        }
        ++total;
    }
}

void log_hit_list(const char* name, const char* where, const uint32_t* hits, int stored, int total) {
    LOG_INFO("diag %s %s matches %d", name, where, total);
    for (int i = 0; i < stored; ++i) {
        LOG_INFO("diag %s %s RVA 0x%X", name, where, hits[i]);
    }
}

bool interior_anchor(const uint8_t* pat, const char* mask, size_t n, size_t& off) {
    constexpr size_t kLen = 12;
    if (n < kLen) {
        return false;
    }
    const size_t begin = n > 16 ? 8 : 0;
    for (size_t start = begin; start + kLen <= n; ++start) {
        bool ok = true;
        if (mask) {
            for (size_t i = 0; i < kLen; ++i) {
                if (mask[start + i] == '?') {
                    ok = false;
                    break;
                }
            }
        }
        (void)pat;
        if (ok) {
            off = start;
            return true;
        }
    }
    return false;
}

void log_diag_header(const ExeMap& exe) {
    if (g_diag_header) {
        return;
    }
    g_diag_header = true;
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    LOG_INFO("diag image base %p SizeOfImage 0x%X entry RVA 0x%X timestamp %lu", static_cast<void*>(base),
             nt->OptionalHeader.SizeOfImage, nt->OptionalHeader.AddressOfEntryPoint, nt->FileHeader.TimeDateStamp);
    uint8_t entry[16];
    const size_t entry_n = copy_mem(base + nt->OptionalHeader.AddressOfEntryPoint, entry, sizeof(entry));
    if (entry_n != 0) {
        log_hex_line("entry", "memory", entry, entry_n);
    }
    const IMAGE_NT_HEADERS* file_nt = nullptr;
    if (exe.view && file_headers(exe, file_nt)) {
        LOG_INFO("diag file timestamp %lu SizeOfImage 0x%X entry RVA 0x%X bytes %llu", file_nt->FileHeader.TimeDateStamp,
                 file_nt->OptionalHeader.SizeOfImage, file_nt->OptionalHeader.AddressOfEntryPoint,
                 static_cast<unsigned long long>(exe.size));
        uint8_t file_entry[16];
        const size_t file_n = file_rva_bytes(exe, file_nt->OptionalHeader.AddressOfEntryPoint, file_entry, sizeof(file_entry));
        if (file_n != 0) {
            log_hex_line("entry", "file", file_entry, file_n);
        }
    }
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        char sec_name[9];
        std::memcpy(sec_name, sec->Name, 8);
        sec_name[8] = '\0';
        const bool scan = (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        LOG_INFO("diag section %s VA 0x%X vsize 0x%X raw 0x%X rawptr 0x%X chars 0x%lX %s", sec_name, sec->VirtualAddress,
                 sec->Misc.VirtualSize, sec->SizeOfRawData, sec->PointerToRawData, sec->Characteristics,
                 scan ? "scanned" : "not scanned");
    }
    if (g_control.n > 0) {
        uint8_t fileb[16]{};
        const size_t fn = file_rva_bytes(exe, g_control.rva, fileb, g_control.n);
        const bool same = fn == g_control.n && std::memcmp(fileb, g_control.original, fn) == 0;
        LOG_INFO("diag control %s RVA 0x%X file %s the hooked bytes", g_control.name, g_control.rva,
                 same ? "matches" : "differs from");
    }
}

void diagnose_miss(const char* name, const uint8_t* pat, size_t n, const char* mask, uint32_t known_rva) {
    g_diag_needed = true;
    ExeMap exe;
    const bool opened = exe.open();
    log_diag_header(exe);
    auto* image = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    if (known_rva != 0) {
        uint8_t* site = image + known_rva;
        log_page(name, site);
        uint8_t mem[kDump]{};
        uint8_t fileb[kDump]{};
        const size_t mem_n = copy_mem(site, mem, kDump);
        const size_t file_n = opened ? file_rva_bytes(exe, known_rva, fileb, kDump) : 0;
        if (mem_n != 0) {
            log_hex_line(name, "memory", mem, mem_n);
        } else {
            LOG_INFO("diag %s memory unreadable at RVA 0x%X", name, known_rva);
        }
        if (file_n != 0) {
            log_hex_line(name, "file", fileb, file_n);
        } else {
            LOG_INFO("diag %s file has no bytes at RVA 0x%X", name, known_rva);
        }
        const size_t both = mem_n < file_n ? mem_n : file_n;
        const bool same = both > 0 && mem_n == file_n && std::memcmp(mem, fileb, both) == 0;
        LOG_INFO("diag %s RVA 0x%X %s", name, known_rva, same ? "mem==file" : "mem!=file");
        if (mem_n >= n) {
            LOG_INFO("diag %s memory pattern prefix %zu/%zu", name, prefix_match(mem, pat, mask, n), n);
        }
        if (file_n >= n) {
            LOG_INFO("diag %s file pattern prefix %zu/%zu", name, prefix_match(fileb, pat, mask, n), n);
        }
        if (g_miss_count < kMaxMiss) {
            MissSnap& miss = g_misses[g_miss_count++];
            miss.name = name;
            miss.pat = pat;
            miss.n = n;
            miss.mask = mask;
            miss.rva = known_rva;
            miss.have_mem = mem_n == kDump;
            if (miss.have_mem) {
                std::memcpy(miss.mem, mem, kDump);
            }
        }
    }
    uint32_t hits[kMaxHits];
    int stored = 0;
    int total = 0;
    search_image(pat, n, mask, hits, stored, total);
    log_hit_list(name, "memory pattern", hits, stored, total);
    if (opened) {
        search_file(exe, pat, n, mask, hits, stored, total);
        log_hit_list(name, "file pattern", hits, stored, total);
    }
    size_t anchor = 0;
    if (interior_anchor(pat, mask, n, anchor)) {
        constexpr size_t kAnchor = 12;
        search_image(pat + anchor, kAnchor, nullptr, hits, stored, total);
        LOG_INFO("diag %s interior +%zu matches %d", name, anchor, total);
        for (int i = 0; i < stored; ++i) {
            const uint32_t entry = hits[i] - static_cast<uint32_t>(anchor);
            LOG_INFO("diag %s interior entry RVA 0x%X", name, entry);
            uint8_t body[kDump];
            const size_t got = copy_mem(image + entry, body, kDump > 32 ? 32 : kDump);
            if (got != 0) {
                log_hex_line(name, "interior entry", body, got);
            }
        }
    }
}

void log_game_modules() {
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
        return;
    }
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (!slash) {
        return;
    }
    const size_t dir_len = static_cast<size_t>(slash - exe + 1);
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) {
        LOG_INFO("diag modules snapshot failed (%lu)", GetLastError());
        return;
    }
    MODULEENTRY32W mod{};
    mod.dwSize = sizeof(mod);
    int logged = 0;
    for (BOOL have = Module32FirstW(snap, &mod); have; have = Module32NextW(snap, &mod)) {
        const bool in_game = _wcsnicmp(mod.szExePath, exe, dir_len) == 0;
        const bool proxy = _wcsicmp(mod.szModule, L"dxgi.dll") == 0 || _wcsicmp(mod.szModule, L"d3d11.dll") == 0 ||
                           _wcsicmp(mod.szModule, L"d3d9.dll") == 0 || _wcsicmp(mod.szModule, L"dinput8.dll") == 0 ||
                           _wcsicmp(mod.szModule, L"xinput1_3.dll") == 0;
        if (!in_game && !proxy) {
            continue;
        }
        char path[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, mod.szExePath, -1, path, MAX_PATH, nullptr, nullptr);
        LOG_INFO("diag module base %p %s", static_cast<void*>(mod.modBaseAddr), path);
        if (++logged >= 40) {
            break;
        }
    }
    CloseHandle(snap);
    LOG_INFO("diag modules logged %d", logged);
}

void diag_runtime() {
    static bool done = false;
    if (done || !g_diag_needed) {
        return;
    }
    done = true;
    auto* image = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    if (g_miss_count == 0) {
        LOG_INFO("diag runtime: hook misses had no saved address");
        log_game_modules();
        return;
    }
    LOG_INFO("diag runtime: %d hook misses from startup", g_miss_count);
    for (int i = 0; i < g_miss_count; ++i) {
        const MissSnap& miss = g_misses[i];
        uint8_t now[kDump];
        const size_t got = copy_mem(image + miss.rva, now, kDump);
        if (miss.have_mem && got == kDump && std::memcmp(now, miss.mem, kDump) == 0) {
            LOG_INFO("diag %s runtime RVA 0x%X memory unchanged since startup", miss.name, miss.rva);
        } else if (got != 0) {
            LOG_INFO("diag %s runtime RVA 0x%X memory changed since startup", miss.name, miss.rva);
            log_hex_line(miss.name, "runtime", now, got);
        } else {
            LOG_INFO("diag %s runtime RVA 0x%X unreadable", miss.name, miss.rva);
        }
        if (got >= miss.n && bytes_match(now, miss.pat, miss.mask, miss.n)) {
            LOG_INFO("diag %s runtime pattern matches at RVA 0x%X", miss.name, miss.rva);
        }
    }
    log_game_modules();
}
