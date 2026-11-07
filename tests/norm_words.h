#pragma once
// Normalised words for the recording fakes (svcfx: tests/ts_services_fixture.h; drawfx: tests/drawfx_core.h). Lane PE,
// 2026-10-28, replacing the value-guessing Norm and its audit (docs/lane/test.md, "Normalised words").
//
// A word the fakes log has a declared kind; nothing is guessed from its value:
//   - data (an integer, a float's bits, an id, a colour) is logged as it is, whatever its value;
//   - a pointer is logged as a record of the object it points into and the byte offset, one word:
//       null                          0
//       a fake (svcfx, drawfx)        its id (200 + index for svcfx) at offset 0; svcfx's interior: 0xfa000000 | i << 12 | off
//       a registered block (AddRange) tag | offset (the tag is the object's id: the same object gets the same tag on both
//                                     sides, and AddRange checks tags cannot alias: below)
//       a heap block                  kHeapTag | number << 16 | offset: the blocks this thread allocates from StartHeap()
//                                     (svcfx::ResetAll, each side) are registered by the allocator itself; a block's number
//                                     is the order in which the side's records first reach it (0 for the first block
//                                     logged), so blocks compare up to a consistent renaming: the same objects reached in
//                                     the same order compare equal whatever their addresses and whatever else either side
//                                     allocated, and a pointer to another object than the first time, or to a new one where
//                                     the other side reaches a known one, differs
//       the thread's stack            kStack (both sides' frames differ: a stack object has no comparable offset)
//       the original image            the address itself (it is mapped at kGameBase on both sides: offset = address - base)
//       the runner's own image        kModuleTag | offset (statics of the tests and of src/)
//     anything else is an unregistered pointer: an error naming the address and where it was logged, and the test fails
//     (Errors(), through difftest::FailureNoteSources). It is never collapsed into something that could compare equal.
//   - a word whose kind the test did not declare is an error too when it is not zero, naming what to declare. Zero needs no
//     kind: null and 0 are the same record, so either reading logs the same thing on both sides.
// The hint in an undeclared word's error (Looks) says what the value resembles, for the person declaring it; it is never used
// to decide how to log the word.
#include "difftest.h"
#include "game/game.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <source_location>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace normwords {

constexpr uint32_t kStack = 0x5ac00000;
constexpr uint32_t kModuleTag = 0xe8000000; // | offset (the runner image is under 128 MB)
constexpr uint32_t kFakeInterior = 0xfa000000; // svcfx: | index << 12 | offset, a pointer inside a fake
constexpr uint32_t kUnregistered = 0xffffbad0; // logged in place of an unregistered pointer (the test fails anyway)
constexpr uint32_t kUndeclared = 0xffffbad1;   // logged in place of an undeclared word that is not zero (likewise)
constexpr uint32_t kUntouched = 0xffffbad2;    // an out pointer the call left as the test set it (svcfx::NormOut)
constexpr uint32_t kLowestTag = 0x10000;       // tags below would alias null and the fakes' ids
constexpr uint32_t kHeapTag = 0x80000000;      // | rank << 16 | offset: a heap block allocated since StartHeap()
constexpr uint32_t kHeapSpan = 0x20000000;     // ranks below 0x2000, offsets up to 0xffff

struct Range {
    uintptr_t lo, hi;
    uint32_t tag;
};

// ---- errors: the test fails with them (harness/main.cpp asks every FailureNoteSources entry's errors()) ----
constexpr unsigned kMaxErrors = 16;
inline std::vector<std::string> gErrors, gErrorKeys;
inline unsigned gErrorTotal = 0;
// `key` names the word (its slot and argument, or its call site): one error per word, the first value it had.
inline void Error(const std::string &text, const std::string &key)
{
    difftest::NotCounted bookkeeping;
    ++gErrorTotal;
    for (const std::string &k : gErrorKeys)
        if (k == key) return;
    if (gErrors.size() < kMaxErrors) gErrors.push_back(text), gErrorKeys.push_back(key);
}
// For self-tests: the errors so far, and none left for the runner.
inline std::vector<std::string> TakeErrors()
{
    std::vector<std::string> out;
    out.swap(gErrors);
    gErrorKeys.clear();
    gErrorTotal = 0;
    return out;
}
inline void StopHeap();
inline void Begin() { TakeErrors(), StopHeap(); }
inline void Report(bool, std::vector<std::string> &) {}
inline void Errors(std::vector<std::string> &out)
{
    for (const std::string &e : gErrors) out.push_back("normalised words: " + e);
    if (gErrorTotal > gErrors.size())
        out.push_back("normalised words: " + std::to_string(gErrorTotal - gErrors.size()) + " more (repeats or past the first " +
                      std::to_string(kMaxErrors) + ")");
    TakeErrors();
}
inline const bool gHooked = (difftest::FailureNoteSources().push_back({&Begin, &Report, &Errors}), true);

inline std::string Site(const std::source_location &where)
{
    const char *file = where.file_name();
    for (const char *p = file; *p; ++p)
        if (*p == '\\' || *p == '/') file = p + 1;
    return std::string(file) + ":" + std::to_string(where.line());
}

// ---- the fixed objects ----
struct Span {
    uintptr_t lo = 0, hi = 0;
};
inline Span ImageOf(uintptr_t base)
{
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + uintptr_t(dos->e_lfanew));
    return {base, base + nt->OptionalHeader.SizeOfImage};
}
inline const Span &Original()
{
    static const Span span = ImageOf(kGameBase);
    return span;
}
inline const Span &Module()
{
    static const Span span = ImageOf(uintptr_t(GetModuleHandleW(nullptr)));
    return span;
}
inline bool OnStack(uint32_t value)
{
    const auto *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
    return value >= uintptr_t(tib->StackLimit) && value < uintptr_t(tib->StackBase);
}

// ---- heap blocks, registered by the allocator (difftest::WatchHeap: the original's HeapAlloc and our CRT's alike) ----
struct HeapBlock {
    uintptr_t lo, hi; // hi: one past the end, which belongs to the block too (an end pointer)
    uint32_t ordinal; // allocation order since StartHeap (diagnostics)
    uint32_t number;  // 1 + the order in which records first reached it; 0 until then
};
constexpr size_t kMaxHeapBlocks = 1 << 15;
inline HeapBlock gHeap[kMaxHeapBlocks]; // the live blocks, in no order
inline size_t gHeapLive = 0;
inline uint32_t gHeapNext = 0;
inline uint32_t gHeapNumbered = 0;
inline bool gHeapOverflow = false;
// The live blocks by start address: an open-addressing table (linear probing) of 1 + their index in gHeap, so that every
// allocation and free costs a probe or two instead of a scan of the live blocks (a test that logs many calls keeps tens of
// thousands alive). Twice the blocks' capacity keeps the probes short. An entry is gHeapGen << 16 | (1 + index): bumping
// the generation empties the table (StartHeap runs at every side's start).
constexpr size_t kHeapIndexSize = kMaxHeapBlocks * 2;
inline uint32_t gHeapIndex[kHeapIndexSize];
inline uint32_t gHeapGen = 1;
inline size_t HeapHash(uintptr_t lo) { return size_t((uint32_t(lo) >> 3) * 0x9e3779b1u >> 16) & (kHeapIndexSize - 1); }
inline size_t HeapEntry(size_t h) { return gHeapIndex[h] >> 16 == gHeapGen ? gHeapIndex[h] & 0xffff : 0; } // 0 empty
inline size_t HeapSlotOf(uintptr_t lo) // the index entry holding lo, or the empty entry where it would go
{
    size_t h = HeapHash(lo);
    while (HeapEntry(h) && gHeap[HeapEntry(h) - 1].lo != lo) h = (h + 1) & (kHeapIndexSize - 1);
    return h;
}
inline void HeapSetEntry(size_t h, size_t entry) { gHeapIndex[h] = entry ? gHeapGen << 16 | uint32_t(entry) : 0; }
inline void HeapUnindex(size_t h) // backward-shift deletion: the entries after the hole that probed past it move up
{
    HeapSetEntry(h, 0);
    for (size_t j = (h + 1) & (kHeapIndexSize - 1); HeapEntry(j); j = (j + 1) & (kHeapIndexSize - 1)) {
        const size_t home = HeapHash(gHeap[HeapEntry(j) - 1].lo);
        if (((j - home) & (kHeapIndexSize - 1)) >= ((j - h) & (kHeapIndexSize - 1)))
            HeapSetEntry(h, HeapEntry(j)), HeapSetEntry(j, 0), h = j;
    }
}
// The callbacks run inside the heap wrappers: they never allocate.
inline HeapBlock *HeapAt(uintptr_t lo)
{
    const size_t e = HeapEntry(HeapSlotOf(lo));
    return e ? &gHeap[e - 1] : nullptr;
}
// A block registered again (reported by two layers, or freed where no watcher saw it) replaces the old entry.
inline void HeapAllocated(void *p, size_t bytes)
{
    if (HeapBlock *b = HeapAt(uintptr_t(p))) return void(*b = {uintptr_t(p), uintptr_t(p) + bytes, gHeapNext++, 0});
    if (gHeapLive == kMaxHeapBlocks) return void(gHeapOverflow = true);
    HeapSetEntry(HeapSlotOf(uintptr_t(p)), gHeapLive + 1);
    gHeap[gHeapLive++] = {uintptr_t(p), uintptr_t(p) + bytes, gHeapNext++, 0};
}
inline void HeapResized(void *p, size_t bytes)
{
    if (HeapBlock *b = HeapAt(uintptr_t(p))) b->hi = b->lo + bytes;
}
inline void HeapFreed(void *p)
{
    const size_t h = HeapSlotOf(uintptr_t(p));
    if (!HeapEntry(h)) return;
    const size_t at = HeapEntry(h) - 1;
    HeapUnindex(h);
    if (at != --gHeapLive) { // the last block moves into the hole
        gHeap[at] = gHeap[gHeapLive];
        HeapSetEntry(HeapSlotOf(gHeap[at].lo), at + 1);
    }
}
inline void HeapClear()
{
    gHeapLive = 0, gHeapNext = 0, gHeapNumbered = 0, gHeapOverflow = false;
    if (++gHeapGen == 0x10000) gHeapGen = 1, std::memset(gHeapIndex, 0, sizeof(gHeapIndex));
}
inline const difftest::HeapWatcher gHeapWatcher{&HeapAllocated, &HeapResized, &HeapFreed};
// From here on (a side's start) this thread's heap blocks are numbered from 0; the blocks allocated before are no longer
// registered (a pointer into one is unregistered).
inline void StartHeap()
{
    difftest::WatchHeap(nullptr);
    HeapClear();
    difftest::WatchHeap(&gHeapWatcher);
}
inline void StopHeap()
{
    difftest::WatchHeap(nullptr);
    HeapClear();
}
// The innermost block `value` points into, else the block it ends (an end pointer). Inside first: blocks can be adjacent
// (a pool allocator's), and the next block's start is not the previous one's end. Innermost: a pool's chunk (from the
// heap) holds the blocks it hands out (from the pool's own allocation, watched too), and the block is the object.
inline HeapBlock *HeapBlockOf(uint32_t value)
{
    HeapBlock *best = nullptr;
    for (size_t i = gHeapLive; i-- > 0;)
        if (value >= gHeap[i].lo && value < gHeap[i].hi && (!best || gHeap[i].hi - gHeap[i].lo < best->hi - best->lo))
            best = &gHeap[i];
    if (best) return best;
    for (size_t i = gHeapLive; i-- > 0;)
        if (value == gHeap[i].hi) return &gHeap[i];
    return nullptr;
}

// The record of a pointer that is not a fake (the fixture checks its fakes first): see the header. False when it points
// into no registered object: then the caller reports Unregistered with where it was logged.
inline bool Pointer(uint32_t value, const std::vector<Range> &ranges, uint32_t &record)
{
    record = 0;
    if (!value) return true;
    for (const Range &r : ranges)
        if (value >= r.lo && value < r.hi) return record = r.tag | uint32_t(value - r.lo), true;
    if (HeapBlock *b = HeapBlockOf(value)) {
        const uint32_t offset = uint32_t(value - b->lo);
        const uint32_t number = b->number ? b->number - 1 : gHeapNumbered;
        if (number >= kHeapSpan >> 16 || offset > 0xffff) return record = kUnregistered, false;
        if (!b->number) b->number = ++gHeapNumbered;
        return record = kHeapTag | number << 16 | offset, true;
    }
    if (OnStack(value)) return record = kStack, true;
    if (value >= Original().lo && value < Original().hi) return record = value, true;
    if (value >= Module().lo && value < Module().hi) return record = kModuleTag | uint32_t(value - Module().lo), true;
    record = kUnregistered;
    return false;
}
inline FILE *KindsFile(); // below: the sweep file
// The sweep's note of a pointer that did point into a registered object (what kind of object), a few per site: a word
// declared 'p' that only ever held data shows up as a site with no such lines.
inline void Registered(uint32_t record, const std::string &where)
{
    difftest::NotCounted bookkeeping;
    const char *kind = record < 0x10000                         ? "fake"
                       : (record & 0xff000000) == kFakeInterior ? "fake interior"
                       : record == kStack                        ? "stack"
                       : record >= kHeapTag && record < kHeapTag + kHeapSpan ? "heap"
                       : (record & 0xf8000000) == kModuleTag     ? "module"
                       : record >= Original().lo && record < Original().hi ? "original"
                                                                 : "range";
    const std::string line = "PTR\t" + where + "\t" + kind;
    static std::unordered_set<std::string> seen;
    if (seen.insert(line).second) std::fprintf(KindsFile(), "%s\n", line.c_str()), std::fflush(KindsFile());
}
inline void Unregistered(uint32_t value, const std::string &where)
{
    char text[480], why[160];
    if (const HeapBlock *b = HeapBlockOf(value))
        std::snprintf(why, sizeof(why), "live heap block (allocation #%u of this side) at +%#x: past the heap records' range", b->ordinal,
                      unsigned(value - b->lo));
    else
        std::snprintf(why, sizeof(why), "%s",
                      value < 0x10000 ? "below 0x10000: data passed as a pointer?"
                      : gHeapOverflow ? "more heap blocks than the registry holds"
                                      : "memory allocated before the side started (ResetAll), by another thread, or not "
                                        "from the heap");
    std::snprintf(text, sizeof(text),
                  "%s: pointer %08x points into no registered object (%s): register the block it points into (AddRange "
                  "with a tag) or, if the word is data, declare it as data",
                  where.c_str(), value, why);
    Error(text, where);
    if (FILE *const sweep = KindsFile()) {
        difftest::NotCounted bookkeeping;
        char line[400];
        std::snprintf(line, sizeof(line), "UNREG\t%s\t%.12s\t%08x", where.c_str(), why, value);
        static std::unordered_set<std::string> seen;
        static std::unordered_map<std::string, unsigned> perSite;
        if (!seen.count(line) && perSite[where] < 12)
            seen.insert(line), ++perSite[where], std::fprintf(sweep, "%s\n", line), std::fflush(sweep);
    }
}

// What an undeclared word resembles, for the error that asks for its kind (a hint, never used to log it).
inline std::string Looks(uint32_t value, const std::vector<Range> &ranges)
{
    char text[120];
    for (const Range &r : ranges)
        if (value >= r.lo && value < r.hi) {
            std::snprintf(text, sizeof(text), "inside block tag %08x at +%#x: a pointer?", r.tag, unsigned(value - r.lo));
            return text;
        }
    if (value < 0x10000) return "below 0x10000: data?";
    if (const HeapBlock *b = HeapBlockOf(value)) {
        std::snprintf(text, sizeof(text), "inside live heap block (allocation #%u) at +%#x: a pointer?", b->ordinal, unsigned(value - b->lo));
        return text;
    }
    if (OnStack(value)) return "a stack address: a pointer?";
    if (value >= Original().lo && value < Original().hi) {
        std::snprintf(text, sizeof(text), "in the original image (rva %#x): a pointer?", unsigned(value - Original().lo));
        return text;
    }
    if (value >= Module().lo && value < Module().hi) return "in the runner's image: a pointer?";
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(reinterpret_cast<const void *>(uintptr_t(value)), &info, sizeof(info)) == sizeof(info) &&
        info.State == MEM_FREE)
        return "free address space: data?";
    return "mapped memory in no registered block";
}

// Sweeps for declaring kinds (TS2_NORM_KINDS=<file>): every undeclared word's name and what it looked like, each pair once
// per run, appended to the file (lane PE's migration; the hint is for the person declaring, never a decision).
inline FILE *KindsFile()
{
    static FILE *file = [] {
        char path[MAX_PATH];
        const DWORD n = GetEnvironmentVariableA("TS2_NORM_KINDS", path, MAX_PATH);
        return n && n < MAX_PATH ? std::fopen(path, "a") : nullptr;
    }();
    return file;
}


// An undeclared word that is not zero: `what` names it, `declare` says how to declare its kind. Returns the word to log in
// its place (kUndeclared, the same on both sides: the error is the failure, not a difference it would cause).
inline uint32_t Undeclared(uint32_t value, const std::string &what, const std::string &declare, const std::vector<Range> &ranges)
{
    FILE *const sweep = KindsFile();
    // The sweep's lines per word, and the values looked at per word (Looks is slow: VirtualQuery; a few hundred values of
    // one word show what it holds).
    static std::unordered_map<std::string, unsigned> perWord, looked;
    bool known = gErrors.size() >= kMaxErrors; // (a word past the first errors only counts)
    for (const std::string &k : gErrorKeys) known = known || k == what;
    if (sweep ? ++looked[what] > 256 : known) return ++gErrorTotal, kUndeclared;
    const std::string looks = Looks(value, ranges);
    char text[480];
    std::snprintf(text, sizeof(text), "%s = %08x has no declared kind (it looks like: %s): declare it %s", what.c_str(), value,
                  looks.c_str(), declare.c_str());
    Error(text, what);
    if (sweep) {
        difftest::NotCounted bookkeeping;
        std::string line = what + "\t" + looks.substr(0, looks.find(':'));
        if (looks.rfind("mapped", 0) == 0) { // the values themselves, a few per word: constant ones are pointers, varied data
            char hex[12];
            std::snprintf(hex, sizeof(hex), "\t%08x", value);
            line += hex;
        }
        static std::unordered_set<std::string> seen;
        if (!seen.count(line) && perWord[what] < 12)
            seen.insert(line), ++perWord[what], std::fprintf(sweep, "%s\n", line.c_str()), std::fflush(sweep);
    }
    return kUndeclared;
}

inline uint32_t LowBit(uint32_t v) { return v & (0u - v); }
// AddRange's checks: a tag is an object's id, so two objects must never log the same word. The tag leaves the offset's bits
// clear (tag | offset == tag + offset), is not below kLowestTag, and its words [tag, tag + size) overlap no other tag's, the
// stack record's, the original image's addresses or the runner image's records.
inline void CheckTag(const std::vector<Range> &ranges, uintptr_t lo, uintptr_t hi, uint32_t tag, const std::source_location &where)
{
    const uint64_t size = hi - lo, first = tag, last = uint64_t(tag) + (size ? size : 1);
    char text[400];
    auto fail = [&](const char *why) {
        std::snprintf(text, sizeof(text), "%s: AddRange tag %08x for a block of %#llx bytes: %s", Site(where).c_str(), tag,
                      static_cast<unsigned long long>(size), why);
        Error(text, text);
    };
    if (tag < kLowestTag) return fail("a tag below 0x10000 aliases null and the fakes' ids");
    if (size > 1 && LowBit(tag) && LowBit(tag) < size) return fail("the offsets reach into the tag's own bits");
    auto overlaps = [&](uint64_t a, uint64_t b) { return first < b && a < last; };
    if (overlaps(kStack, kStack + 1)) return fail("its words include the stack record");
    if (overlaps(kFakeInterior, kFakeInterior + 0x100000)) return fail("its words overlap the fakes' interior records");
    if (overlaps(kHeapTag, uint64_t(kHeapTag) + kHeapSpan)) return fail("its words overlap the heap blocks' records");
    if (overlaps(kUnregistered, kUntouched + 1)) return fail("its words include the unregistered / undeclared / untouched words");
    if (overlaps(Original().lo, Original().hi)) return fail("its words overlap the original image's addresses");
    if (overlaps(kModuleTag, kModuleTag + (Module().hi - Module().lo))) return fail("its words overlap the runner image's records");
    for (const Range &r : ranges)
        if (r.tag != tag && overlaps(r.tag, uint64_t(r.tag) + (r.hi - r.lo ? r.hi - r.lo : 1))) {
            char why[120];
            std::snprintf(why, sizeof(why), "its words overlap tag %08x's (a block of %#x bytes)", r.tag, unsigned(r.hi - r.lo));
            return fail(why);
        }
}

} // namespace normwords
