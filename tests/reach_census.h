#pragma once
// census: which of the original image's functions a stretch of a difftest enters (lane RA, milestone O-b's progress gauge).
//
//   const census::Db &db = census::Load();      // db/functions.jsonl (+ names, library matches, hooks), once per process
//   census::Hits hits;                          // the functions entered, by RVA
//   {
//       census::Armed armed(db, hits, skip);    // a breakpoint byte (0xcc) on every game function's first byte
//       ... run code ...                        // each first entry: the byte put back, the RVA recorded, execution resumes
//   }                                           // the bytes not hit put back, the image's protection restored
//   census::Gauge g = census::Measure(db, originalHits, ourHits);
//   census::Print("vase", g, db, 12);
//
// The functions armed are db/functions.jsonl's (Ghidra's function starts) less the library code (db/names.jsonl source
// "library", db/library-matches.jsonl, db/crt-equivalents.jsonl: our toolchain's, never reimplemented) and the RVAs the
// caller skips (stood-in functions: their first bytes are the test's jump). A start already patched by a StandIn /
// CodePatch is armed over the patch and restored to it. The census is per thread of execution: one vectored handler,
// one armed set at a time (the harness runs tests on one thread).
//
// The measure (Measure): over the functions either side entered (O the original side's, R ours),
//   still original  = R less the hooked RVAs (src/hook/tables: in the game our hook runs in its place): game code our side
//                     still runs, through OriginalFunction or code reached from it;
//   ours            = everything else on the path: O \ R (the original's functions our side never needed) and R's hooked
//                     ones;
// counted in functions and in bytes (db sizes). ours / path in bytes is the share of the path that runs our code in the
// game. It moves as rows land: a function moves from "still original" to "ours" when its replacement is called (or
// hooked) and nothing of ours reaches it any more.
#include "hook/hooks.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace census {

struct Function {
    uint32_t rva = 0, size = 0;
};

struct Db {
    bool loaded = false;
    std::string where;                     // the db directory found, or why none
    std::vector<Function> functions;       // sorted by RVA, library code left out
    std::unordered_map<uint32_t, std::string> names;
    std::unordered_set<uint32_t> hooked;   // RegisteredHooks()' RVAs
    unsigned library = 0;                  // functions left out as library code
    uint32_t SizeOf(uint32_t rva) const
    {
        auto it = std::lower_bound(functions.begin(), functions.end(), rva,
                                   [](const Function &f, uint32_t r) { return f.rva < r; });
        return it != functions.end() && it->rva == rva ? it->size : 0;
    }
    const char *NameOf(uint32_t rva) const
    {
        auto it = names.find(rva);
        return it == names.end() ? "?" : it->second.c_str();
    }
};

using Hits = std::unordered_set<uint32_t>;

namespace detail {

// The value after `"key"` on a JSON line: a hex string ("0x..."), a number, or a string.
inline bool Field(const std::string &line, const char *key, std::string &out)
{
    const std::string k = std::string("\"") + key + "\"";
    size_t at = line.find(k);
    if (at == std::string::npos) return false;
    at = line.find(':', at + k.size());
    if (at == std::string::npos) return false;
    ++at;
    while (at < line.size() && line[at] == ' ') ++at;
    if (at < line.size() && line[at] == '"') {
        const size_t end = line.find('"', at + 1);
        if (end == std::string::npos) return false;
        out = line.substr(at + 1, end - at - 1);
        return true;
    }
    size_t end = at;
    while (end < line.size() && line[end] != ',' && line[end] != '}') ++end;
    out = line.substr(at, end - at);
    return true;
}

inline uint32_t Number(const std::string &s) { return uint32_t(std::strtoul(s.c_str(), nullptr, 0)); }

template <class Each> bool ReadLines(const std::string &path, Each &&each)
{
    FILE *f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
    std::string line;
    char buf[65536];
    while (std::fgets(buf, sizeof buf, f)) {
        line += buf;
        if (line.empty() || line.back() != '\n') {
            if (!std::feof(f)) continue;
        }
        each(line);
        line.clear();
    }
    if (!line.empty()) each(line);
    std::fclose(f);
    return true;
}

// db\ above the test exe (a worktree's build has its own checkout above it).
inline std::string FindDb()
{
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dir = exe;
    for (int up = 0; up < 8; ++up) {
        const size_t slash = dir.find_last_of("\\/");
        if (slash == std::string::npos) break;
        dir.resize(slash);
        const std::string probe = dir + "\\db";
        if (GetFileAttributesA((probe + "\\functions.jsonl").c_str()) != INVALID_FILE_ATTRIBUTES) return probe;
    }
    return "";
}

} // namespace detail

inline const Db &Load()
{
    static Db db;
    if (db.loaded || !db.where.empty()) return db;
    const std::string dir = detail::FindDb();
    if (dir.empty()) {
        db.where = "no db\\functions.jsonl above the test exe";
        return db;
    }
    db.where = dir;
    std::unordered_set<uint32_t> library;
    std::string v;
    detail::ReadLines(dir + "\\names.jsonl", [&](const std::string &line) {
        if (!detail::Field(line, "rva", v)) return;
        const uint32_t rva = detail::Number(v);
        std::string source;
        if (detail::Field(line, "source", source) && source == "library") library.insert(rva);
        if (detail::Field(line, "name", v)) db.names[rva] = v;
    });
    for (const char *file : {"\\library-matches.jsonl", "\\crt-equivalents.jsonl"})
        detail::ReadLines(dir + file, [&](const std::string &line) {
            if (detail::Field(line, "rva", v)) library.insert(detail::Number(v));
        });
    detail::ReadLines(dir + "\\functions.jsonl", [&](const std::string &line) {
        if (!detail::Field(line, "rva", v)) return;
        Function f;
        f.rva = detail::Number(v);
        if (detail::Field(line, "size", v)) f.size = detail::Number(v);
        if (library.count(f.rva)) {
            ++db.library;
            return;
        }
        if (f.size == 0) return;
        if (!db.names.count(f.rva) && detail::Field(line, "name", v)) db.names[f.rva] = v;
        db.functions.push_back(f);
    });
    std::sort(db.functions.begin(), db.functions.end(), [](const Function &a, const Function &b) { return a.rva < b.rva; });
    for (const HookSpec *h : RegisteredHooks()) db.hooked.insert(h->rva);
    db.loaded = !db.functions.empty();
    return db;
}

namespace detail {

struct ArmedSet {
    uintptr_t base = 0;
    std::vector<uintptr_t> at; // sorted addresses
    std::vector<uint8_t> saved;
    std::vector<uint8_t> hit;
    Hits *hits = nullptr;
};
inline ArmedSet *gArmed = nullptr;

inline LONG CALLBACK OnBreak(EXCEPTION_POINTERS *e)
{
    ArmedSet *s = gArmed;
    if (!s || e->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    const uintptr_t where = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
    auto it = std::lower_bound(s->at.begin(), s->at.end(), where);
    if (it == s->at.end() || *it != where) return EXCEPTION_CONTINUE_SEARCH;
    const size_t i = size_t(it - s->at.begin());
    if (s->hit[i]) return EXCEPTION_CONTINUE_SEARCH;
    s->hit[i] = 1;
    *reinterpret_cast<uint8_t *>(where) = s->saved[i];
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(where), 1);
    s->hits->insert(uint32_t(where - s->base));
    e->ContextRecord->Eip = DWORD(where);
    return EXCEPTION_CONTINUE_EXECUTION;
}

} // namespace detail

// The census for a scope: every function of `db` (less `skip`) armed in the image at `base`; the ones entered go into
// `hits`. Without a db it does nothing.
class Armed {
public:
    Armed(const Db &db, uintptr_t base, Hits &hits, const std::vector<uint32_t> &skip = {})
    {
        if (!db.loaded || detail::gArmed) return;
        set_.base = base;
        set_.hits = &hits;
        set_.at.reserve(db.functions.size());
        for (const Function &f : db.functions)
            if (std::find(skip.begin(), skip.end(), f.rva) == skip.end()) set_.at.push_back(base + f.rva);
        if (set_.at.empty()) return;
        lo_ = set_.at.front() & ~uintptr_t(0xfff);
        hi_ = (set_.at.back() + 0x1000) & ~uintptr_t(0xfff);
        // The protection of each region, restored afterwards.
        for (uintptr_t p = lo_; p < hi_;) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(reinterpret_cast<void *>(p), &mbi, sizeof mbi)) break;
            const uintptr_t end = (std::min<uintptr_t>)(hi_, reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize);
            regions_.push_back({p, end - p, mbi.Protect});
            p = end;
        }
        DWORD old = 0;
        if (!VirtualProtect(reinterpret_cast<void *>(lo_), hi_ - lo_, PAGE_EXECUTE_READWRITE, &old)) {
            regions_.clear();
            return;
        }
        set_.saved.resize(set_.at.size());
        set_.hit.assign(set_.at.size(), 0);
        for (size_t i = 0; i < set_.at.size(); ++i) {
            uint8_t *p = reinterpret_cast<uint8_t *>(set_.at[i]);
            set_.saved[i] = *p;
            *p = 0xcc;
        }
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(lo_), hi_ - lo_);
        handler_ = AddVectoredExceptionHandler(1, &detail::OnBreak);
        detail::gArmed = &set_;
        armed_ = true;
    }
    ~Armed()
    {
        if (!armed_) return;
        detail::gArmed = nullptr;
        RemoveVectoredExceptionHandler(handler_);
        for (size_t i = 0; i < set_.at.size(); ++i)
            if (!set_.hit[i]) *reinterpret_cast<uint8_t *>(set_.at[i]) = set_.saved[i];
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(lo_), hi_ - lo_);
        DWORD old = 0;
        for (const Region &r : regions_) VirtualProtect(reinterpret_cast<void *>(r.at), r.size, r.protect, &old);
    }
    bool armed() const { return armed_; }
    Armed(const Armed &) = delete;
    Armed &operator=(const Armed &) = delete;

private:
    struct Region {
        uintptr_t at;
        size_t size;
        DWORD protect;
    };
    detail::ArmedSet set_;
    std::vector<Region> regions_;
    uintptr_t lo_ = 0, hi_ = 0;
    void *handler_ = nullptr;
    bool armed_ = false;
};

struct Gauge {
    unsigned pathCount = 0, oursCount = 0, stillCount = 0;
    uint64_t pathBytes = 0, oursBytes = 0, stillBytes = 0;
    unsigned originalCount = 0, ourCount = 0; // |O|, |R|
    unsigned hookedReached = 0;               // R's hooked functions (ours in the game)
    std::vector<uint32_t> still;              // the still-original RVAs, largest first
};

inline Gauge Measure(const Db &db, const Hits &original, const Hits &ours)
{
    Gauge g;
    g.originalCount = unsigned(original.size());
    g.ourCount = unsigned(ours.size());
    Hits path = original;
    path.insert(ours.begin(), ours.end());
    for (uint32_t rva : path) {
        const uint32_t size = db.SizeOf(rva);
        ++g.pathCount;
        g.pathBytes += size;
        const bool reached = ours.count(rva) != 0;
        const bool hooked = db.hooked.count(rva) != 0;
        if (reached && hooked) ++g.hookedReached;
        if (reached && !hooked) {
            ++g.stillCount;
            g.stillBytes += size;
            g.still.push_back(rva);
        } else {
            ++g.oursCount;
            g.oursBytes += size;
        }
    }
    std::sort(g.still.begin(), g.still.end(), [&](uint32_t a, uint32_t b) {
        const uint32_t sa = db.SizeOf(a), sb = db.SizeOf(b);
        return sa != sb ? sa > sb : a < b;
    });
    return g;
}

// One line of totals, then the `top` largest still-original functions.
inline void Print(const char *label, const Gauge &g, const Db &db, unsigned top)
{
    const double share = g.pathBytes ? 100.0 * double(g.oursBytes) / double(g.pathBytes) : 0.0;
    std::printf("  gauge %s: path %u functions %llu bytes (original side %u, ours %u); ours in game %u / %llu bytes "
                "(%.1f%%, %u of them hooked and reached); still original %u / %llu bytes\n",
                label, g.pathCount, (unsigned long long)g.pathBytes, g.originalCount, g.ourCount, g.oursCount,
                (unsigned long long)g.oursBytes, share, g.hookedReached, g.stillCount, (unsigned long long)g.stillBytes);
    for (unsigned i = 0; i < g.still.size() && i < top; ++i)
        std::printf("    still original 0x%06x %5u %s\n", g.still[i], db.SizeOf(g.still[i]), db.NameOf(g.still[i]));
}

} // namespace census
