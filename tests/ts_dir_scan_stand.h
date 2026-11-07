#pragma once
// Stand-ins for the game's directory scan, shared by the cTSNeighborhoodManager tests that list folders
// (tests/ts_neighborhood_manager_load.cpp: Load; tests/ts_neighborhood_manager_fu.cpp: DeleteFiles).
//
// The scan object (thiscall, the caller's stack): 0x1c48da builds it for a directory (and three words), 0x1c63d4 finds the
// entries matching a pattern, 0x1c5047 hands out entry i, 0x1c509f gives an entry's full path, 0x1c49eb destroys it. The
// stand-ins keep their state in the scan object's words (1 its number, 2 the entry count, 3 its first entry in gEntries),
// log every call to the recording fixture's event log (tests/ts_services_fixture.h; codes gEvBase + 0..4, so each test keeps
// its own) and make the entries up through the test's two choices:
//   gCount(dir, pattern): how many entries this find lists (it may change `dir`, e.g. add a separator, before the entries
//   are made); called once per find, after the find is logged;
//   gMake(entry, dir, pattern, i): fills entry i (its flags at +0x24: 4 a folder, 8 a file; its path).
// Both draw from the test's own generator, so the sides see the same entries while they make the same calls.
#include "code_patch.h"
#include "rz/DirectoryScan.h"
#include "ts/cTSNeighborhoodManager.h"
#include "ts_services_fixture.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace dirscan {

struct Entry {
    uint8_t pad[0x24];
    uint8_t flags; // +0x24
    std::string path;
};
constexpr unsigned kEntries = 96;
inline Entry gEntries[kEntries];
inline unsigned gEntryCount = 0;
inline std::vector<std::string> gDirs; // by the scan's number

inline uint32_t gEvBase = 0; // the scan's event codes: +0 new, +1 find, +2 entry, +3 path, +4 end
inline unsigned (*gCount)(std::string &dir, const std::string &pattern) = nullptr;
inline void (*gMake)(Entry &entry, const std::string &dir, const std::string &pattern, unsigned i) = nullptr;

// Before each side: no entries, no scans.
inline void Reset()
{
    gEntryCount = 0;
    gDirs.clear();
}

inline void EvName(const NeighborhoodName *s)
{
    svcfx::Ev(uint32_t(s->mString.size()));
    for (char c : s->mString) svcfx::Ev(uint8_t(c));
}

inline void *__fastcall StandNew(uint32_t *self, void *, const NeighborhoodName *dir, uint32_t a, uint32_t b, uint32_t c)
{
    svcfx::Ev(gEvBase + 0), EvName(dir), svcfx::Ev(a), svcfx::Ev(b), svcfx::Ev(c);
    std::memset(self, 0, 0x5c);
    self[1] = uint32_t(gDirs.size());
    gDirs.push_back(dir->mString);
    return self;
}
inline uint32_t __fastcall StandFind(uint32_t *self, void *, const NeighborhoodName *pattern, uint32_t flag)
{
    svcfx::Ev(gEvBase + 1), svcfx::Ev(self[1]), EvName(pattern), svcfx::Ev(flag);
    std::string dir = gDirs[self[1]];
    const unsigned n = gCount(dir, pattern->mString);
    self[2] = 0;
    self[3] = gEntryCount;
    for (unsigned i = 0; i < n && gEntryCount < kEntries; ++i) {
        gMake(gEntries[gEntryCount++], dir, pattern->mString, i);
        ++self[2];
    }
    return 1;
}
inline void *__fastcall StandEntry(uint32_t *self, void *, uint32_t index)
{
    svcfx::Ev(gEvBase + 2), svcfx::Ev(self[1]), svcfx::Ev(index);
    return &gEntries[self[3] + index];
}
inline NeighborhoodName *__fastcall StandPath(Entry *entry, void *, NeighborhoodName *out)
{
    svcfx::Ev(gEvBase + 3), svcfx::Ev(uint32_t(entry - gEntries));
    out->mString = entry->path;
    return out;
}
inline void __fastcall StandEnd(uint32_t *self, void *) { svcfx::Ev(gEvBase + 4), svcfx::Ev(self[1]); }

// The five stand-ins over the image's scan functions and ours (src/rz/DirectoryScan.h: the callers build and destroy it
// through its adapters), for the scope of a test.
struct Patches {
    shape::StandIn scanNew, find, entry, path, end;
    Patches()
        : scanNew(0x1c48da, &DirectoryScanAdapters::Construct, &StandNew), find(0x1c63d4, &DirectoryScan::SetPattern, &StandFind),
          entry(0x1c5047, &DirectoryScan::EntryAt, &StandEntry), path(0x1c509f, &dscan::Entry::FullPath, &StandPath),
          end(0x1c49eb, &DirectoryScanAdapters::Destruct, &StandEnd)
    {
    }
};

} // namespace dirscan
