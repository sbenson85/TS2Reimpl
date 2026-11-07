// Differential tests for the cTSPersistSystem functions of lane CT: StartBackgroundOpen (cITSPersistSystem 67),
// GetGroupIdForNameChars (12), GetGroupIdForFileName (13), GetGroupIdForPathChars (10), FlushRollbackSegments (49),
// FindBestSegment (33), CloseAllSegments (42) and the service Shutdown.
//
// Each run gets a world built from a seed: a random primary object (0x300 bytes) whose locks (+0x68, +0x98) have
// recording slots, the primary and ITS vtables with recording fakes, and twelve fake objects whose slots record their
// calls (kind, object, arguments). The original runs on one world and ours on another built from the same seed; the
// recorded calls, the return value and every word of the world afterwards must match, with addresses inside the world
// normalised to offsets. Callees nobody has replaced (table clears, SaveGroupMap, the base Shutdown, ...) are replaced
// by recording stand-ins in the image; both sides call them.
#include "difftest.h"
#include "rz/cRZFileName.h"
#include "rz/cRZString.h"
#include "ts/cTSPersistSystem.h"
#include "ts_persist_system_shape.h"
#include "shape.h"
#include "gz/cGZPersistResourceManager.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

namespace {

constexpr int kObjects = 12;
constexpr int kSlots = 80;
constexpr uint32_t kSub = 0xa8;
constexpr uint32_t kIidRollback = 0x8ceec300;
constexpr uint32_t kIidPacked = 0x286d4a6a;

struct World {
    alignas(8) uint8_t primary[0x300];
    void *vtLock[4];
    void *vtP[kSlots];
    void *vtI[kSlots];
    void *vtThread[16];
    alignas(8) uint8_t obj[kObjects][0x40];
    void *vt[kObjects][kSlots];
    uint32_t retObj[kObjects][kSlots];
    void *segs[8];
    void *segvec[3];
    void *listeners[4];
    uint32_t order[8];
    uint32_t args[4];
    char text[64];
    alignas(8) uint8_t setHead[0x20];
    alignas(8) uint8_t namedHead[0x10];
    alignas(8) uint8_t buckets[8 * 8];
};

World *gWorld = nullptr;
uint8_t *gBase = nullptr;
std::vector<uint32_t> gTrace;
uint32_t gStrings[2];

uint32_t Addr(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

uint32_t Map(uint32_t v)
{
    if (gWorld) {
        uint32_t base = Addr(gWorld);
        if (v >= base && v < base + sizeof(World)) return 0xa5000000u | (v - base);
    }
    return v;
}

void Note(int kind, const void *self)
{
    gTrace.push_back(static_cast<uint32_t>(kind));
    uint32_t m = Map(Addr(self));
    gTrace.push_back((m >> 24) == 0xa5 ? m : 0xffff0000u); // objects made by the call live on the heap: no address
}
void Rec(uint32_t v) { gTrace.push_back(Map(v)); }
void RecPtr(const void *p) { gTrace.push_back(Map(Addr(p))); }
void RecString(const char *s)
{
    size_t n = std::strlen(s);
    gTrace.push_back(static_cast<uint32_t>(n));
    for (size_t i = 0; i < n; ++i) gTrace.push_back(static_cast<uint8_t>(s[i]));
}

template <typename T> void Put(void *base, size_t off, T v)
{
    std::memcpy(static_cast<uint8_t *>(base) + off, &v, sizeof(v));
}
template <typename T> T Get(const void *base, size_t off)
{
    T v;
    std::memcpy(&v, static_cast<const uint8_t *>(base) + off, sizeof(v));
    return v;
}
template <typename F> void *Fn(F f) { return reinterpret_cast<void *>(f); }

int ObjIndex(const void *self)
{
    if (!gWorld) return -1;
    auto d = reinterpret_cast<const uint8_t *>(self) - reinterpret_cast<const uint8_t *>(gWorld->obj);
    if (d < 0 || d >= static_cast<ptrdiff_t>(sizeof(gWorld->obj))) return -1;
    return static_cast<int>(d / 0x40);
}
uint32_t RetOf(const void *self, int slot)
{
    int i = ObjIndex(self);
    return i >= 0 ? gWorld->retObj[i][slot] : 0;
}

template <int Slot> uint32_t __fastcall R0(void *self, void *)
{
    Note(Slot, self);
    return RetOf(self, Slot);
}
template <int Slot> uint32_t __fastcall R1(void *self, void *, uint32_t a)
{
    Note(Slot, self);
    Rec(a);
    return RetOf(self, Slot);
}
template <int Slot> uint32_t __fastcall R2(void *self, void *, uint32_t a, uint32_t b)
{
    Note(Slot, self);
    Rec(a);
    Rec(b);
    return RetOf(self, Slot);
}
void __fastcall LockEnter(void *self, void *) { Note(2301, self); }
void __fastcall LockLeave(void *self, void *) { Note(2302, self); }

// Slot 0 of a segment: hands out the rollback face (object 8) or the packed-file face (object 9) when the world says
// it works (low byte of the return value).
uint32_t __fastcall Query(void *self, void *, uint32_t iid, void **out)
{
    Note(0, self);
    Rec(iid);
    uint32_t r = RetOf(self, 0);
    if (r & 0xff) *out = iid == kIidRollback ? gWorld->obj[8] : (iid == kIidPacked ? gWorld->obj[9] : gWorld->obj[10]);
    return r;
}
// ITS slot 32: score of a segment for a key.
uint32_t __fastcall Score(void *self, void *, void *segment, const uint32_t *key)
{
    Note(32, self);
    RecPtr(segment);
    for (int i = 0; i < 4; ++i) Rec(key[i]);
    int i = ObjIndex(segment);
    return i >= 0 ? gWorld->retObj[i][32] % 4 : 0;
}
// Face of a segment with the rollback interface: slot 3 gives object 6.
uint32_t __fastcall GetSegmentOf(void *self, void *)
{
    Note(3, self);
    return Addr(gWorld->obj[6]);
}
// Slot 11 of object 6: GetPath(path); records the call and the (empty) string.
uint32_t __fastcall GetPath(void *self, void *, cRZString *path)
{
    Note(11, self);
    Rec(static_cast<uint32_t>(path->mString.size()));
    return 0;
}
// A segment whose GetPath writes the world's text.
uint32_t __fastcall GetPathText(void *self, void *, cRZString *path)
{
    Note(11, self);
    path->FromChar(gWorld->text);
    return 0;
}
// ITS slot 12: group id for a name; records the characters.
uint32_t __fastcall NameSlot(void *self, void *, const char *name)
{
    Note(12, self);
    RecString(name);
    return gWorld->args[0];
}
// Primary slot 35 / 36: the base segment list.
uint32_t __fastcall SegmentCount(void *self, void *)
{
    Note(35, self);
    return gWorld->args[1];
}
uint32_t __fastcall SegmentAt(void *self, void *, uint32_t i)
{
    Note(36, self);
    Rec(i);
    return Addr(gWorld->obj[gWorld->order[i & 7] % 8]);
}
// Primary slot 30: RemoveSegment; takes the first element of the segment vector away by moving its begin.
uint32_t __fastcall RemoveSegment(void *self, void *, void *segment)
{
    Note(30, self);
    RecPtr(segment);
    Put(gWorld->primary, 0xd4, Get<uint32_t>(gWorld->primary, 0xd4) + 4);
    return 1;
}
// Objects with a name for the thread: ITS thread fakes.
uint32_t __fastcall ThreadExit(void *self, void *, uint32_t code)
{
    Note(3, self);
    Rec(code);
    return 0;
}

const void *kTraps[kSlots];
template <int N> void __fastcall Trap(void *self, void *)
{
    std::printf("unexpected call: slot %d of object 0x%x\n", N, Map(Addr(self)));
    std::fflush(stdout);
    ExitProcess(3);
}
template <size_t... I> void FillTraps(std::index_sequence<I...>)
{
    ((kTraps[I] = reinterpret_cast<const void *>(&Trap<static_cast<int>(I)>)), ...);
}

// Overwrites a function's first five bytes with a jump to a stand-in, for the scope of the test.
struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(void *at, void *stand) : site(static_cast<uint8_t *>(at))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    CodePatch(const CodePatch &) = delete;
    CodePatch &operator=(const CodePatch &) = delete;
    ~CodePatch() { std::memcpy(site, saved, 5); }
};

// Overwrites dwords of the game's data (a vtable slot or a global) for the scope of the test.
struct DataPatch {
    uint32_t *site;
    uint32_t saved;
    DataPatch(void *at, uint32_t value) : site(static_cast<uint32_t *>(at))
    {
        DWORD old;
        VirtualProtect(site, 4, PAGE_READWRITE, &old);
        saved = *site;
        *site = value;
    }
    DataPatch(const DataPatch &) = delete;
    DataPatch &operator=(const DataPatch &) = delete;
    ~DataPatch() { *site = saved; }
};

void Build(World &w, uint32_t seed)
{
    std::memset(&w, 0, sizeof(w));
    static bool trapsReady = false;
    if (!trapsReady) {
        FillTraps(std::make_index_sequence<kSlots>());
        trapsReady = true;
    }
    std::mt19937 rng(seed);
    auto fill = [&rng](void *p, size_t n) {
        auto *b = static_cast<uint8_t *>(p);
        for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(rng());
    };
    fill(w.primary, sizeof(w.primary));
    fill(w.obj, sizeof(w.obj));
    for (int i = 0; i < kSlots; ++i) {
        w.vtP[i] = w.vtI[i] = const_cast<void *>(kTraps[i]);
        for (auto &v : w.vt) v[i] = const_cast<void *>(kTraps[i]);
    }
    for (int i = 0; i < 16; ++i) w.vtThread[i] = const_cast<void *>(kTraps[i]);
    for (int o = 0; o < kObjects; ++o) {
        Put(w.obj[o], 0, Addr(w.vt[o]));
        for (int s = 0; s < kSlots; ++s) w.retObj[o][s] = rng() % 2 ? (static_cast<uint32_t>(rng()) | 1u) : (static_cast<uint32_t>(rng()) & ~0xffu);
    }
    w.vtLock[1] = Fn(&LockEnter);
    w.vtLock[2] = Fn(&LockLeave);
    Put(w.primary, 0x68, Addr(w.vtLock));
    Put(w.primary, 0x98, Addr(w.vtLock));
    Put(w.primary, 0, Addr(w.vtP));
    Put(w.primary, 0xa8, Addr(w.vtI));
    for (auto &o : w.order) o = static_cast<uint32_t>(rng());
    for (auto &a : w.args) a = static_cast<uint32_t>(rng());
    w.args[1] = rng() % 7; // segment count
    // Generic objects: AddRef / Release.
    for (int o = 0; o < kObjects; ++o) {
        w.vt[o][1] = Fn(&R0<1>);
        w.vt[o][2] = Fn(&R0<2>);
    }
}

void Snapshot(World &w, std::vector<uint32_t> &t)
{
    const auto *b = reinterpret_cast<const uint8_t *>(&w);
    for (size_t i = 0; i + 4 <= sizeof(World); i += 4) {
        uint32_t v;
        std::memcpy(&v, b + i, 4);
        t.push_back(Map(v));
    }
}

struct Outcome {
    uint32_t ret = 0;
    std::vector<uint32_t> trace, after;
};
using Customise = void (*)(World &, std::mt19937 &);

// Where the world lives. Original and ours run on this one world, rebuilt from the seed before each (Compare), so
// they see it at the same address and Map (which renames words inside the world) is the same function on both
// sides: equal words stay equal and different words stay different, whatever the call wrote. Two worlds at two
// addresses made Map depend on the heap layout (a random word equal to an address inside one world was renamed in
// that world only; lane NB, tests/gz_persist_db_segment_multi_packed_files.cpp).
World &SharedWorld()
{
    static World *const w = new World;
    return *w;
}

// Builds the world from the seed (Build clears it first) and lets the test customise it; deterministic in the
// seed, so each side gets the same.
void Prepare(World &w, uint32_t seed, Customise customise)
{
    Build(w, seed);
    std::mt19937 rng(seed ^ 0x5eed);
    gWorld = &w;
    if (customise) customise(w, rng);
    gWorld = nullptr;
}

template <typename Call> Outcome Run(World &w, Call call)
{
    gWorld = &w;
    gTrace.clear();
    Outcome o;
    o.ret = call(w);
    o.trace = gTrace;
    Snapshot(w, o.after);
    gWorld = nullptr;
    return o;
}

template <typename OrigCall, typename OursCall>
void Compare(difftest::Context &ctx, const char *name, uint32_t seed, Customise customise, OrigCall orig, OursCall ours, bool compareRet = true)
{
    World &w = SharedWorld();
    gTrace.reserve(1 << 14);
    Prepare(w, seed, customise);
    Outcome a = Run(w, orig);
    Prepare(w, seed, customise);
    Outcome b = Run(w, ours);
    ++ctx.cases;
    if (compareRet && a.ret != b.ret) ctx.Fail("%s seed %u: original returned 0x%x, ours 0x%x", name, seed, a.ret, b.ret);
    if (a.trace != b.trace) {
        size_t i = 0;
        while (i < a.trace.size() && i < b.trace.size() && a.trace[i] == b.trace[i]) ++i;
        ctx.Fail("%s seed %u: calls differ at word %zu (original %zu words, ours %zu)", name, seed, i, a.trace.size(), b.trace.size());
    }
    if (a.after != b.after) {
        size_t i = 0;
        while (i < a.after.size() && a.after[i] == b.after[i]) ++i;
        ctx.Fail("%s seed %u: world word %zu differs afterwards (original 0x%x, ours 0x%x)", name, seed, i, a.after[i], b.after[i]);
    }
}

constexpr int kSeeds = 300;

std::string RandomText(std::mt19937 &rng, const std::string &needle)
{
    static const char alphabet[] = "abcdefXYZ019_-. /\\";
    std::string s;
    size_t n = rng() % 24;
    for (size_t i = 0; i < n; ++i) s += alphabet[rng() % (sizeof(alphabet) - 1)];
    if (!needle.empty() && rng() % 2) s.insert(rng() % (s.size() + 1), needle);
    return s;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// Slot 12: the group id of a name; slot 13: of a segment's file.

PS_TEST(TSPersistSystemCT_GetGroupIdForNameChars, 0x1e02f0)
{
    using F = uint32_t(__thiscall *)(void *, const char *);
    auto orig = ctx.Original<F>(0x1e02f0);
    std::string needle = reinterpret_cast<const char *>(ctx.image.At(0xd9f880));
    std::mt19937 rng(77);
    uint8_t dummy[0x10] = {};
    for (int n = 0; n < 4000; ++n) {
        std::string s = RandomText(rng, n % 3 ? needle : std::string());
        if (n % 17 == 0) s += s + s; // past the small-string size
        uint32_t a = orig(dummy, s.c_str());
        uint32_t b = psshape::Call(PSX(GetGroupIdForNameChars), dummy, s.c_str());
        ++ctx.cases;
        if (a != b) ctx.Fail("name '%s': original 0x%x, ours 0x%x", s.c_str(), a, b);
    }
}

namespace {
void FileNameSetup(World &w, std::mt19937 &rng)
{
    w.vtI[12] = Fn(&NameSlot);
    w.vt[0][11] = Fn(&GetPathText);
    std::string s = RandomText(rng, std::string(".package"));
    s = std::string(rng() % 2 ? "C:\\Games\\" : "dir/") + s + (rng() % 2 ? ".package" : "");
    std::strncpy(w.text, s.c_str(), sizeof(w.text) - 1);
}
} // namespace

PS_TEST(TSPersistSystemCT_GetGroupIdForFileName, 0x1e0200)
{
    using F = uint32_t(__thiscall *)(void *, void *);
    auto orig = ctx.Original<F>(0x1e0200);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        bool nullSegment = seed % 11 == 0;
        Compare(ctx, "GetGroupIdForFileName", seed, &FileNameSetup,
                [&](World &w) { return orig(w.primary + kSub, nullSegment ? nullptr : w.obj[0]); },
                [&](World &w) { return psshape::Call(PSX(GetGroupIdForFileName), w.primary + kSub, nullSegment ? nullptr : w.obj[0]); });
    }
}

// ---------------------------------------------------------------------------------------------------------
// Slot 25: the file name is a real cRZString (the game's vtable); ITS slots 11 and 24 record.

namespace {
uint32_t __fastcall GroupOfString(void *self, void *, cRZString *stem)
{
    Note(11, self);
    RecString(stem->ToChar());
    return gWorld->args[0];
}
uint32_t __fastcall ReleaseForGroup(void *self, void *, uint32_t group, uint32_t flag)
{
    Note(24, self);
    Rec(group);
    Rec(flag);
    return gWorld->args[0] ^ 0x5500u; // upper bits set: only the low byte is the result
}
void FileSetup(World &w, std::mt19937 &rng)
{
    w.vtI[11] = Fn(&GroupOfString);
    w.vtI[24] = Fn(&ReleaseForGroup);
    std::string s = std::string(rng() % 2 ? "C:\\Games\\" : "dir/") + RandomText(rng, std::string(".package")) + (rng() % 2 ? ".package" : "");
    std::strncpy(w.text, s.c_str(), sizeof(w.text) - 1);
    w.args[3] = rng() % 3;
}
} // namespace

PS_TEST(TSPersistSystemCT_ReleaseSegmentsForFile, 0x1de2d0)
{
    using F = uint8_t(__thiscall *)(void *, void *, uint32_t);
    auto orig = ctx.Original<F>(0x1de2d0);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        cRZString *names[2];
        for (auto &n : names) {
            n = new cRZString;
            n->Construct();
        }
        Compare(ctx, "ReleaseSegmentsForFile", seed, &FileSetup,
                [&](World &w) {
                    names[0]->FromChar(w.text);
                    return uint32_t(orig(w.primary + kSub, names[0], w.args[3])) & 0xff;
                },
                [&](World &w) {
                    names[1]->FromChar(w.text);
                    return uint32_t(psshape::Call(PSX(ReleaseSegmentsForFile), w.primary + kSub, names[1], w.args[3])) & 0xff;
                });
        for (auto *n : names) {
            n->Destruct();
            delete n;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------
// Slot 10: a sequence of paths from an empty table on each side (the table's nodes live on the heap, so the results and
// the counter are compared).

PS_TEST(TSPersistSystemCT_GetGroupIdForPathChars, 0x1dffb0)
{
    using F = uint32_t(__thiscall *)(void *, const char *);
    auto orig = ctx.Original<F>(0x1dffb0);
    auto build = ctx.Original<void(__thiscall *)(void *)>(0x1d8a50);
    for (uint32_t seed = 0; seed < 60; ++seed) {
        std::mt19937 rng(seed);
        alignas(8) uint8_t a[0x300] = {}, b[0x300] = {};
        Put(a, 0x208, 1u);
        Put(b, 0x208, 1u);
        build(a + 0x1e8);
        build(b + 0x1e8);
        std::vector<std::string> pool;
        for (int i = 0; i < 6; ++i) pool.push_back(RandomText(rng, std::string()));
        pool.push_back("abcdefgh1");
        pool.push_back("abcdefgh2");
        pool.push_back("ABCDEFGH1.ext");
        pool.push_back("abcdefgh1.other");
        pool.push_back("");
        for (int n = 0; n < 40; ++n) {
            std::string s = pool[rng() % pool.size()];
            if (rng() % 4 == 0) s = RandomText(rng, std::string());
            if (rng() % 3 == 0) for (auto &c : s) c = static_cast<char>(toupper(c));
            uint32_t x = orig(a + kSub, s.c_str());
            uint32_t y = psshape::Call(PSX(GetGroupIdForPathChars), b + kSub, s.c_str());
            ++ctx.cases;
            if (x != y) ctx.Fail("seed %u path '%s': original 0x%x, ours 0x%x", seed, s.c_str(), x, y);
        }
        if (Get<uint32_t>(a, 0x208) != Get<uint32_t>(b, 0x208)) ctx.Fail("seed %u: next group differs", seed);
        if (Get<uint32_t>(a, 0x1e8 + 8) != Get<uint32_t>(b, 0x1e8 + 8)) ctx.Fail("seed %u: table size differs", seed);
    }
}

// ---------------------------------------------------------------------------------------------------------
// Slot 49, 33.

namespace {

void FlushSetup(World &w, std::mt19937 &rng)
{
    int n = static_cast<int>(rng() % 6);
    for (int i = 0; i < 8; ++i) {
        w.segs[i] = w.obj[i % 6];
        w.vt[i][0] = Fn(&Query);
    }
    w.vt[8][3] = Fn(&GetSegmentOf);
    w.vt[8][4] = Fn(&R0<4>);
    w.vt[8][5] = Fn(&R0<5>);
    w.vt[6][11] = Fn(&GetPath);
    w.segvec[0] = w.segs;
    w.segvec[1] = w.segs + n;
    w.segvec[2] = w.segs + 8;
    // Make a flush fail sometimes and rollback on sometimes.
    for (int s : {4, 5}) w.retObj[8][s] = rng() % 3 ? (rng() | 1u) : 0u;
}

void BestSetup(World &w, std::mt19937 &rng)
{
    (void)rng;
    for (int i = 0; i < 8; ++i) w.vt[i][0] = Fn(&Query);
    w.vtI[32] = Fn(&Score);
    w.vtP[35] = Fn(&SegmentCount);
    w.vtP[36] = Fn(&SegmentAt);
    for (auto &k : w.args) (void)k;
    w.args[2] = 0;
    for (int i = 0; i < 8; ++i) w.vt[i][0] = Fn(&Query);
    Put(w.text, 0, 0u);
}

} // namespace

PS_TEST(TSPersistSystemCT_FlushRollbackSegments, 0x1e4c00)
{
    using F = uint8_t(__thiscall *)(void *, void *const *);
    auto orig = ctx.Original<F>(0x1e4c00);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "FlushRollbackSegments", seed, &FlushSetup,
                [&](World &w) { return uint32_t(orig(w.primary + kSub, w.segvec)) & 0xff; },
                [&](World &w) { return uint32_t(psshape::Call(PSX(FlushRollbackSegments), w.primary + kSub, w.segvec)) & 0xff; });
    }
}

PS_TEST(TSPersistSystemCT_FindBestSegment, 0x1deb30)
{
    using F = uint8_t(__thiscall *)(void *, const uint32_t *, uint32_t, void **);
    auto orig = ctx.Original<F>(0x1deb30);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "FindBestSegment", seed, &BestSetup,
                [&](World &w) {
                    void *out = w.obj[11];
                    uint32_t key[4] = {1, 2, 3, 4};
                    uint32_t r = orig(w.primary + kSub, key, 0x1234, &out);
                    RecPtr(out);
                    return r & 0xff;
                },
                [&](World &w) {
                    void *out = w.obj[11];
                    uint32_t key[4] = {1, 2, 3, 4};
                    uint32_t r = psshape::Call(PSX(FindBestSegment), w.primary + kSub, key, 0x1234, &out);
                    RecPtr(out);
                    return r & 0xff;
                });
    }
}

// ---------------------------------------------------------------------------------------------------------
// Slot 42.

namespace {

// mSegmentsByGroup (+0x114): its clear (0x1e8ce0) is the toolchain's unordered_map::clear, which ours runs itself, so
// both sides get a real table there with w.args[3] groups (0..5), each value an empty vector (0x1e6a40, the value
// destructor, does nothing for one; TSPersistSystem3_vf57 covers it with values). Nodes through the game's new /
// delete, both redirected to ours.
struct GroupValue {
    void *first = nullptr, *last = nullptr, *end = nullptr;
};
struct GroupHash {
    size_t operator()(uint32_t key) const { return key; }
};
using GroupTable = std::unordered_map<uint32_t, GroupValue, GroupHash>;
static_assert(sizeof(GroupTable) == 0x20, "unordered_map layout");
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

void CloseSetup(World &w, std::mt19937 &rng)
{
    int n = static_cast<int>(rng() % 5);
    for (int i = 0; i < 8; ++i) w.segs[i] = w.obj[i % 6];
    Put(w.primary, 0xd4, Addr(w.segs));
    Put(w.primary, 0xd8, Addr(w.segs + n));
    Put(w.primary, 0xdc, Addr(w.segs + 8));
    for (int i = 0; i < 6; ++i) w.vt[i][4] = Fn(&R0<4>);
    w.vtP[30] = Fn(&RemoveSegment);
    w.args[2] = static_cast<uint32_t>(rng() % 4); // how many names the named map gets
    w.args[3] = static_cast<uint32_t>(rng() % 6); // how many groups mSegmentsByGroup gets
    // (The two tables are built inside the call by MakeTables, so heap addresses stay out of the same-start check.)
    Put(w.primary, 0xf4, 0u);
}

using BuildFn = void(__thiscall *)(void *);
using EmplaceFn = void(__thiscall *)(void *, void *, const void *);
BuildFn gBuildTable = nullptr;
EmplaceFn gEmplace = nullptr;

// The named map (cRZString keys, built by the game's table constructor 0x1d8a50, filled through its try_emplace
// 0x1d7390) and the segment id table (empty).
void MakeTables(World &w)
{
    gBuildTable(w.primary + 0xf4);
    gBuildTable(w.primary + 0x134);
    auto *groups = new (w.primary + 0x114) GroupTable();
    for (uint32_t g = 0; g < w.args[3]; ++g) groups->emplace(0x40u + g * 3, GroupValue{});
    for (uint32_t i = 0; i < w.args[2]; ++i) {
        cRZString key;
        key.Construct();
        char text[16];
        std::snprintf(text, sizeof(text), "name%u", i);
        key.FromChar(text);
        struct {
            void *node;
            uint8_t inserted;
        } result;
        gEmplace(w.primary + 0xf4, &result, &key);
        key.Destruct();
    }
}

// After the call: what the tables must look like (the heap pointers are reduced to facts and cleared).
void NoteTables(World &w)
{
    auto *named = w.primary + 0xf4;
    auto *head = Get<uint32_t *>(named, 4);
    Rec(Get<uint32_t>(named, 8));                  // size
    Rec(head[0] == Addr(head) && head[1] == Addr(head));
    auto *buckets = Get<uint32_t *>(named, 0xc);
    auto *bucketsEnd = Get<uint32_t *>(named, 0x10);
    Rec(static_cast<uint32_t>(bucketsEnd - buckets));
    uint32_t allHead = 1;
    for (auto *b = buckets; b != bucketsEnd; ++b) allHead &= *b == Addr(head);
    Rec(allHead);
    auto *groups = w.primary + 0x114;
    auto *groupHead = Get<uint32_t *>(groups, 4);
    Rec(Get<uint32_t>(groups, 8));
    Rec(groupHead[0] == Addr(groupHead) && groupHead[1] == Addr(groupHead));
    uint32_t groupBucketsHead = 1;
    for (auto *b = Get<uint32_t *>(groups, 0xc); b != Get<uint32_t *>(groups, 0x10); ++b)
        groupBucketsHead &= *b == Addr(groupHead);
    Rec(groupBucketsHead);
    reinterpret_cast<GroupTable *>(groups)->~GroupTable();
    std::memset(groups, 0, 0x20);
    std::memset(named, 0, 0x20);
    std::memset(w.primary + 0x134, 0, 0x20);
}

} // namespace

PS_TEST(TSPersistSystemCT_CloseAllSegments, 0x1dcc60)
{
    using F = void(__thiscall *)(void *);
    auto orig = ctx.Original<F>(0x1dcc60);
    CodePatch gameNew(ctx.image.At(0x824632), Fn(&GameNew));
    CodePatch gameDelete(ctx.image.At(0x824624), Fn(&GameDelete));
    gBuildTable = ctx.Original<BuildFn>(0x1d8a50);
    gEmplace = ctx.Original<EmplaceFn>(0x1d7390);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "CloseAllSegments", seed, &CloseSetup,
                [&](World &w) {
                    MakeTables(w);
                    orig(w.primary + kSub);
                    NoteTables(w);
                    return 0u;
                },
                [&](World &w) {
                    MakeTables(w);
                    psshape::Call(PSX(CloseAllSegments), w.primary + kSub);
                    NoteTables(w);
                    return 0u;
                });
    }
}

// ---------------------------------------------------------------------------------------------------------
// Slot 67: the thread class's vtable (slots 2, 10, 12) and the object are the game's, so the fakes are put into the
// game's vtable for the run.

namespace {

uint32_t __fastcall GStart(void *, void *, uint32_t proc, uint32_t arg)
{
    gTrace.push_back(7001);
    // Ours starts the thread on BackgroundOpenThread, the replacement of 0x1defe0 (verified by
    // TSPersistSystemIP_BackgroundOpenThread): the same procedure, so recorded as 0x1defe0. Any other address stays as is.
    if (proc == Addr(Fn(&cTSPersistSystem::BackgroundOpenThread))) proc = Addr(gBase) + 0x1defe0u;
    gTrace.push_back(proc - Addr(gBase));
    Rec(arg);
    return 1;
}
uint32_t __fastcall GPriority(void *, void *, int32_t level)
{
    gTrace.push_back(7002);
    gTrace.push_back(static_cast<uint32_t>(level));
    return 1;
}
uint32_t __fastcall GName(void *, void *, const char *name)
{
    gTrace.push_back(7003);
    RecString(name);
    return 1;
}
uint32_t __fastcall GRunning(void *self, void *)
{
    Note(11, self);
    return gWorld->args[2];
}

void BackgroundSetup(World &w, std::mt19937 &rng)
{
    if (rng() % 2) {
        Put(w.primary, 0x1e4, Addr(w.obj[0]));
        w.vt[0][2] = Fn(&GStart);
        w.vt[0][10] = Fn(&GPriority);
        w.vt[0][11] = Fn(&GRunning);
        w.vt[0][12] = Fn(&GName);
    } else {
        Put(w.primary, 0x1e4, 0u);
    }
    w.args[2] = rng() % 2 ? 0u : 1u;
}

// After the run: the thread object made by the call is on the heap; reduce it to its vtable.
void NormaliseThread(World &w)
{
    uint32_t t = Get<uint32_t>(w.primary, 0x1e4);
    if (t && t != Addr(w.obj[0])) {
        uint32_t vtable = Get<uint32_t>(reinterpret_cast<void *>(static_cast<uintptr_t>(t)), 0);
        gTrace.push_back(vtable);
        Put(w.primary, 0x1e4, 0xdead0001u);
    }
}

} // namespace

PS_TEST(TSPersistSystemCT_StartBackgroundOpen, 0x1e2c40)
{
    using F = void(__thiscall *)(void *);
    auto orig = ctx.Original<F>(0x1e2c40);
    gBase = static_cast<uint8_t *>(ctx.image.At(0));
    auto *vtable = static_cast<uint32_t *>(ctx.image.At(0xda3168));
    DataPatch d2(vtable + 2, Addr(Fn(&GStart)));
    DataPatch d10(vtable + 10, Addr(Fn(&GPriority)));
    DataPatch d11(vtable + 11, Addr(Fn(&GRunning)));
    DataPatch d12(vtable + 12, Addr(Fn(&GName)));
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "StartBackgroundOpen", seed, &BackgroundSetup,
                [&](World &w) {
                    orig(w.primary + kSub);
                    NormaliseThread(w);
                    return 0u;
                },
                [&](World &w) {
                    psshape::Call(PSX(StartBackgroundOpen), w.primary + kSub);
                    NormaliseThread(w);
                    return 0u;
                });
    }
}

// ---------------------------------------------------------------------------------------------------------
// Service Shutdown (this = primary + 4).

namespace {

// A listener's OnPersistEvent(event, block): the block is a stack object, so its four words are recorded.
uint32_t __fastcall OnEvent(void *self, void *, uint32_t event, const uint32_t *block)
{
    Note(3, self);
    Rec(event);
    for (int i = 0; i < 4; ++i) Rec(block[i]);
    return 0;
}
void __fastcall StandInSave(void *self, void *) { Note(5001, self); }
void __cdecl StandInGlobal() { gTrace.push_back(5004); }
void __fastcall StandInFlush(void *self, void *) { Note(5005, self); }
void __fastcall StandInSet(void *self, void *, void *a, void *b)
{
    Note(5006, self);
    RecPtr(a);
    RecPtr(b);
}
uint32_t __fastcall StandInBase(void *self, void *)
{
    Note(5007, self);
    return gWorld->args[3];
}
uint32_t __fastcall ServiceSlot8(void *self, void *, void *hooks)
{
    Note(8, self);
    RecPtr(hooks);
    return 0;
}

// The keyed-object table (+0x248, 0..3 counted objects put in through the game's SetKeyedObject 0x1e5200) is built inside
// the call too; NoteKeyed reduces it to its size and clears it.
void MakeKeyed(World &w, difftest::Context &ctx)
{
    using SetFn = void(__thiscall *)(void *, uint32_t, void *);
    gBuildTable(w.primary + 0x248);
    auto set = ctx.Original<SetFn>(0x1e5200);
    for (int i = 0; i < w.text[1]; ++i) set(w.primary + kSub, 100u + i, w.obj[i]);
}
void NoteKeyed(World &w)
{
    Rec(Get<uint32_t>(w.primary, 0x248 + 8));
    std::memset(w.primary + 0x248, 0, 0x20);
}

void MakeThread(World &w)
{
    if (!w.text[0]) return;
    void *t = ::operator new(8);
    Put(t, 0, Addr(w.vtThread));
    Put(w.primary, 0x1e4, Addr(t));
}

void ShutdownSetup(World &w, std::mt19937 &rng)
{
    // Listeners: objects 0..2 (slot 3 on event, release).
    int n = static_cast<int>(rng() % 4);
    // (The listener loop of NotifyListeners has no null check, so the entries are objects.)
    for (int i = 0; i < 3; ++i) w.listeners[i] = w.obj[i];
    for (int i = 0; i < 3; ++i) w.vt[i][3] = Fn(&OnEvent);
    Put(w.primary, 0x230, Addr(w.listeners));
    Put(w.primary, 0x234, Addr(w.listeners + n));
    Put(w.primary, 0x238, Addr(w.listeners + 4));
    // Loader (object 5), handlers (objects 3, 4).
    Put(w.primary, 0x1c0, rng() % 4 ? Addr(w.obj[5]) : 0u);
    w.vt[5][4] = Fn(&R0<4>);
    Put(w.primary, 0xcc, rng() % 3 ? Addr(w.obj[3]) : 0u);
    Put(w.primary, 0xd0, rng() % 3 ? Addr(w.obj[4]) : 0u);
    // The caches (ReleaseSegments, ours or the original's): none, so it does nothing.
    for (uint32_t off : {0xe0u, 0xe4u, 0xe8u, 0xecu}) Put(w.primary, off, 0u);
    // The set at +0x23c.
    Put(w.primary, 0x23c, Addr(w.setHead));
    Put(w.primary, 0x240, static_cast<uint32_t>(rng() % 5));
    Put(w.setHead, 0, static_cast<uint32_t>(rng()));
    Put(w.setHead, 4, Addr(w.obj[7]));
    Put(w.setHead, 8, static_cast<uint32_t>(rng()));
    // The thread: a heap object with a fake vtable (freed by the call).
    // (made by MakeThread at the start of the call, so that the heap address stays out of the same-start check)
    w.text[0] = rng() % 3 ? 1 : 0;
    w.text[1] = static_cast<char>(rng() % 4);
    std::memset(w.primary + 0x1c8, 0, 0x18); // the flush timer is not running: Stop does nothing
    Put(w.primary, 0x248, 0u);
    w.vtThread[3] = Fn(&ThreadExit);
    w.vtThread[11] = Fn(&GRunning);
    Put(w.primary, 0x1e4, 0u);
    w.args[2] = rng() % 2;
    w.vt[7][8] = Fn(&ServiceSlot8);
}

} // namespace

PS_TEST(TSPersistSystemCT_Shutdown, 0x1e53c0)
{
    using F = uint8_t(__thiscall *)(void *);
    auto orig = ctx.Original<F>(0x1e53c0);
    CodePatch p1(ctx.image.At(0x1ea190), Fn(&StandInSave));
    CodePatch p1ours(shape::MemberCode(&cTSPersistSystem::SaveGroupMap), Fn(&StandInSave));
    gBuildTable = ctx.Original<BuildFn>(0x1d8a50);
    CodePatch p4(ctx.image.At(0x1f42b0), Fn(&StandInGlobal));
    CodePatch p5(ctx.image.At(0x1dedf0), Fn(&StandInFlush));
    CodePatch p5ours(shape::MemberCode(&cTSPersistSystem::SaveChangeRecords), Fn(&StandInFlush));
    CodePatch p6(ctx.image.At(0x1d5aa0), Fn(&StandInSet));
    CodePatch p7(ctx.image.At(0x9b57e0), Fn(&StandInBase));
    CodePatch p7ours(shape::MemberCode(&::cGZPersistResourceManager::Shutdown), Fn(&StandInBase)); // ours (lane NP)
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        // The service object the call reaches through the cached global: object 7 of the world.
        // The service object comes from the cached global (0x10f5194), or, when that is empty, from the getter's
        // global (0x10c9378), and is then cached.
        const bool cachedAlready = seed % 3 != 0;
        Compare(ctx, "Shutdown", seed, &ShutdownSetup,
                [&](World &w) {
                    DataPatch g(ctx.image.At(0x10f5194), cachedAlready ? Addr(w.obj[7]) : 0u);
                    DataPatch g2(ctx.image.At(0x10c9378), Addr(w.obj[7]));
                    MakeThread(w);
                    MakeKeyed(w, ctx);
                    uint32_t r = orig(w.primary + 4) & 0xff;
                    Rec(*static_cast<uint32_t *>(ctx.image.At(0x10f5194)));
                    NoteKeyed(w);
                    return r;
                },
                [&](World &w) {
                    DataPatch g(ctx.image.At(0x10f5194), cachedAlready ? Addr(w.obj[7]) : 0u);
                    DataPatch g2(ctx.image.At(0x10c9378), Addr(w.obj[7]));
                    MakeThread(w);
                    MakeKeyed(w, ctx);
                    uint32_t r = psshape::Call(PSX(Shutdown), w.primary + 4) & 0xff;
                    Rec(*static_cast<uint32_t *>(ctx.image.At(0x10f5194)));
                    NoteKeyed(w);
                    return r;
                });
    }
}
