#pragma once
// Normalisation audit for svcfx::Norm and drawfx::Norm (lane LU, 2026-10-23).
//
// Norm rewrites a word that falls inside a block registered with AddRange as tag | offset, so that both sides of a difftest,
// whose blocks sit at different addresses, log the same thing for the same pointer. A word that is data, not a pointer (a
// random value a fake returns or is handed, an id, a colour) and happens to fall inside one side's block is rewritten on
// that side only, and the test fails at a different seed in every run, or only in the full suite, where the heap is laid out
// differently (lane LQ: GZBuffer_Run, one run in six; lane LU: SndMgr_Run seed 829). Norm cannot tell data from pointers;
// this audit makes such a failure name its cause, and finds the tests that risk it before they fail:
//   - every block registered goes into a short history (kBlocks), and every word >= 0x10000 that Norm leaves as it is into
//     a ring (kRaw);
//   - a word left as it is that lies inside a block the other side registered, and a block the other side registers over a
//     word left as it is, are noted (value, tag, block). "The other side" is the build just before or after this one: a
//     clear of the fixture's ranges (gRanges.clear(), directly or through ResetAll) after anything was registered or left
//     as it is starts a new build, and only words and blocks one build apart are paired. Earlier cases' blocks are freed
//     and their addresses reused by objects nobody registers, so pairing across cases would flood the notes with pointers;
//   - in sweeps (TS2_FAILURE_NOTES=1), a word left as it is that points at no memory at all (VirtualQuery: free) is data,
//     whatever the test meant: noted too. A full-width random value is free address space most of the time, so a test
//     that passes such values through Norm shows up here in every sweep, long before one lands inside a block;
//   - Same() (svcfx and drawfx) explains a first difference that is exactly this case: one side's tag | offset against the
//     other side's raw word at that offset of a block with that tag (Explain below);
//   - when a test fails, the runner appends the notes to its failures (difftest::FailureNoteSources). TS2_FAILURE_NOTES=1
//     prints passing tests' notes too, as "note:" lines: the sweep for tests that pass data through Norm.
// The rule for tests (docs/lane/test.md, "Normalised words"): pass only pointers through Norm, compare data words as they
// are (svcfx::Obj::Data for a fake's arguments), and draw random values that reach a Norm'd sink small.
#include "difftest.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace normaudit {

struct Block {
    uintptr_t lo, hi;
    uint32_t tag;
    unsigned gen;
};
struct RawWord {
    uint32_t value;
    unsigned gen;
};
struct Note {
    uint32_t value;
    Block block;
    bool blockLater; // the block was registered after the word was logged as it is
};

constexpr unsigned kBlocks = 64;
constexpr unsigned kRaw = 4096;
constexpr unsigned kNotes = 6;
constexpr uint32_t kLowest = 0x10000; // no block lies below (the first 64 KB are never mapped)

inline Block gBlocks[kBlocks];
inline unsigned gBlockCount = 0, gBlockNext = 0;
inline RawWord gRaw[kRaw];
inline unsigned gRawCount = 0, gRawNext = 0;
inline Note gNotes[kNotes];
inline unsigned gNoteCount = 0, gNoteTotal = 0;
inline unsigned gGen = 0;
inline bool gDirty = false; // something registered or left as it is since the last clear
inline uint32_t gFree[kNotes];
inline unsigned gFreeCount = 0, gFreeTotal = 0;
// Mapped regions seen lately (most words left as they are are pointers into the same few): skips VirtualQuery for them.
inline uintptr_t gMappedLo[4], gMappedHi[4];
inline unsigned gMappedNext = 0;
// The free-address check costs a VirtualQuery per new region and names every test that passes data through Norm, failing or
// not (most are harmless today): sweeps only (TS2_FAILURE_NOTES=1). The other notes and Explain always run.
inline bool gSweep = GetEnvironmentVariableA("TS2_FAILURE_NOTES", nullptr, 0) != 0;

// One side and the other: builds one generation apart.
inline bool Adjacent(unsigned a, unsigned b) { return a - b == 1 || b - a == 1; }

inline void AddNote(uint32_t value, const Block &b, bool blockLater)
{
    ++gNoteTotal;
    for (unsigned i = 0; i < gNoteCount; ++i)
        if (gNotes[i].value == value && gNotes[i].block.lo == b.lo) return;
    if (gNoteCount < kNotes) gNotes[gNoteCount++] = {value, b, blockLater};
    else gNotes[(gNoteTotal - 1) % kNotes] = {value, b, blockLater}; // keep the latest
}

// Whether `value` lies in no allocation at all (free address space): then it cannot be a pointer the code dereferences.
inline bool PointsAtNothing(uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        if (value >= gMappedLo[i] && value < gMappedHi[i]) return false;
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(reinterpret_cast<const void *>(uintptr_t(value)), &info, sizeof(info)) != sizeof(info)) return false;
    if (info.State == MEM_FREE) return true;
    gMappedLo[gMappedNext] = uintptr_t(info.BaseAddress);
    gMappedHi[gMappedNext] = uintptr_t(info.BaseAddress) + info.RegionSize;
    gMappedNext = (gMappedNext + 1) % 4;
    return false;
}

// AddRange registered a block (on the side being built).
inline void Registered(uintptr_t lo, uintptr_t hi, uint32_t tag)
{
    gDirty = true;
    const Block b{lo, hi, tag, gGen};
    for (unsigned i = 0; i < gRawCount; ++i)
        if (gRaw[i].value >= lo && gRaw[i].value < hi && Adjacent(gRaw[i].gen, gGen)) AddNote(gRaw[i].value, b, true);
    gBlocks[gBlockNext] = b;
    gBlockNext = (gBlockNext + 1) % kBlocks;
    if (gBlockCount < kBlocks) ++gBlockCount;
}

// Norm left `value` as it is: no block registered now holds it.
inline void Raw(uint32_t value)
{
    if (value < kLowest) return;
    gDirty = true;
    for (unsigned i = 0; i < gBlockCount; ++i)
        if (value >= gBlocks[i].lo && value < gBlocks[i].hi && Adjacent(gBlocks[i].gen, gGen)) AddNote(value, gBlocks[i], false);
    gRaw[gRawNext] = {value, gGen};
    gRawNext = (gRawNext + 1) % kRaw;
    if (gRawCount < kRaw) ++gRawCount;
    if (gSweep && PointsAtNothing(value)) {
        ++gFreeTotal;
        if (gFreeCount < kNotes) gFree[gFreeCount++] = value;
    }
}

// Whether `mapped` (one side's word) is the normalised form of `raw` (the other side's) in a block of the history: the
// word was data inside one side's block. Then a description, else "".
inline std::string Explain(uint32_t orig, uint32_t ours)
{
    for (int pass = 0; pass < 2; ++pass) {
        const uint32_t mapped = pass ? ours : orig, raw = pass ? orig : ours;
        for (unsigned i = 0; i < gBlockCount; ++i) {
            const Block &b = gBlocks[i];
            if (raw >= b.lo && raw < b.hi && mapped == (b.tag | uint32_t(raw - b.lo))) {
                char text[320];
                std::snprintf(text, sizeof(text),
                              "Norm: %s side's %08x is the other side's %08x inside its block %08x..%08x (tag %08x): a data "
                              "word normalised as a pointer on one side only, not a behaviour difference (docs/lane/test.md, "
                              "\"Normalised words\")",
                              pass ? "ours" : "the original", mapped, raw, unsigned(b.lo), unsigned(b.hi), b.tag);
                return text;
            }
        }
    }
    return {};
}

// The fixture's ranges were cleared: the next build starts, unless nothing happened since the last clear (tests clear
// before and after each side).
inline void Cleared()
{
    if (gDirty) ++gGen;
    gDirty = false;
}

// The fixtures' range list (svcfx::gRanges, drawfx::gRanges): a vector whose clear() may start a build, since tests clear
// it themselves between sides as often as through ResetAll.
template <class T> struct Ranges : std::vector<T> {
    void clear() noexcept
    {
        std::vector<T>::clear();
        Cleared();
    }
};

inline void Begin()
{
    gBlockCount = gBlockNext = gRawCount = gRawNext = gNoteCount = gNoteTotal = gGen = gFreeCount = gFreeTotal = 0;
    gDirty = false;
    for (unsigned i = 0; i < 4; ++i) gMappedLo[i] = gMappedHi[i] = 0; // the address space changes between tests
}
inline void Report(bool failed, std::vector<std::string> &out)
{
    char text[400];
    if (gNoteCount) {
        std::snprintf(text, sizeof(text),
                      "%s%u word(s) Norm left as they were lay inside a block registered on the other side (a data word "
                      "inside one side's block is normalised on that side only: docs/lane/test.md, \"Normalised words\"); "
                      "latest:",
                      failed ? "possible cause: " : "", gNoteTotal);
        out.emplace_back(text);
        for (unsigned i = 0; i < gNoteCount; ++i) {
            const Note &n = gNotes[i];
            std::snprintf(text, sizeof(text), "  %08x %s block %08x..%08x (tag %08x)", n.value,
                          n.blockLater ? "logged as it was, then a block registered over it:"
                                       : "logged as it was, inside the earlier",
                          unsigned(n.block.lo), unsigned(n.block.hi), n.block.tag);
            out.emplace_back(text);
        }
    }
    if (gFreeCount) {
        int at = std::snprintf(text, sizeof(text),
                               "%s%u word(s) passed through Norm point at no memory: data, which one side rewrites when it "
                               "falls inside its block (log it as it is or draw it small: docs/lane/test.md, \"Normalised "
                               "words\"); first:",
                               failed ? "possible cause: " : "", gFreeTotal);
        for (unsigned i = 0; i < gFreeCount && at > 0 && unsigned(at) < sizeof(text) - 10; ++i)
            at += std::snprintf(text + at, sizeof(text) - at, " %08x", gFree[i]);
        out.emplace_back(text);
    }
}

inline const bool gHooked = (difftest::FailureNoteSources().push_back({&Begin, &Report}), true);

} // namespace normaudit
