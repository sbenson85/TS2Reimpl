// cTSRelationshipEntry (0x2c bytes, 3 vtables): differential tests.
//
// Standalone shape (docs/standalone-shape.md): the game has no constructor function for the entry; it builds it inline in the
// table's SetEntry 0x55ac60 (and Read 0x55b420). The original side's entry is built by that code (MakeOriginal: the original
// constructor 0x55ab10 builds a scratch table, the original SetEntry builds the entry with the game's vtables) and its code is
// called at its RVA; ours is built by our constructor (as our SetEntry does) and called through its own vtable slot, as the
// table and the game's callers reach it. The vtable words are the only bytes that differ by design (Capture records each as a
// flag: the game's on the original side, ours on our side, and ours are not the game's). RelationshipEntry_Slots covers the
// slots no other test calls; RelationshipEntry_Adapters the RVA hooks' entry points on an entry carrying the game's vtables.
//
// Every case runs the original on one entry and ours on the other, both seeded alike, and compares the return, the whole entry
// (fields and values, capacity too), every fake call in order and the stream's release count. Fakes can throw at a chosen call
// (EH unwinding), like tests/ts_relationship_table.cpp.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "ifaces/cIGZIStream.h"
#include "shape.h"
#include "ts/cTSRelationshipEntry.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace {
namespace Lane {
using Entry = cTSRelationshipEntry;
using Values = std::vector<uint32_t>;
using relentry::kPersistFace;
using relentry::kSerialFace;

// Slot `slot` of the vtable at `face` (the object's own), called with `this` = face. Byte returns are compared raw (al).
template <class R, class... A> R CallSlot(const void *face, int slot, A... args)
{
    using Fn = R(__thiscall *)(const void *, A...);
    return (*static_cast<Fn *const *>(face))[slot](face, args...);
}
uint8_t *Face(Entry *e, uint32_t offset) { return reinterpret_cast<uint8_t *>(e) + offset; }

constexpr uint32_t kGameVTables[3] = {Entry::kOriginalVTable, Entry::kOriginalPersistVTable, Entry::kOriginalSerialVTable};
// Our three vtables, from an entry built by our constructor.
const void *OurVTable(int i)
{
    static const void *ours[3];
    if (!ours[0]) {
        auto *e = new Entry(cGZPersistResourceKey{});
        std::memcpy(ours, e, sizeof(ours));
        e->Release(); // count 0: destroyed
    }
    return ours[i];
}

// The original's inline construction: a scratch table from the original constructor, then the original SetEntry (which
// builds the entry, retains it once for the caller's local and once for the map node, and drops the local); the table's
// original destructor (flags 0) drops the node's reference, leaving the entry at count 0 (no free: Release frees at <= 1 only
// when called). The key then gets the test's words.
Entry *MakeOriginal(difftest::Context &ctx, const cGZPersistResourceKey &key)
{
    alignas(4) uint8_t table[0x40];
    ctx.Original<void *(__thiscall *)(void *)>(0x55ab10)(table);
    void *node = nullptr;
    ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, void **)>(0x55ac60)(table, 0x1234, 0x56, &node);
    auto *entry = static_cast<Entry *>(static_cast<void **>(node)[3]);
    ++entry->mRefCount; // keep it past the table's destructor
    ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x55abf0)(table, 0);
    --entry->mRefCount;
    entry->mKey = key;
    return entry;
}

// Frees an entry without either side's code (its destruction is tested on its own).
void Destroy(Entry *e)
{
    e->mValues.~Values();
    ::operator delete(e, Entry::kSize);
}

struct Stub { void **vt; uint32_t id; uint32_t releases; };
std::vector<uint32_t> callLog;
int failAt = -1, tick;
uint64_t thrown;
std::vector<uint32_t> script;
size_t cursor;
bool queryWorks;
Entry *current; // the entry under test (Run sets it)
void *recordVT[3]{}, *inVT[10]{}, *outVT[10]{};
Stub record{recordVT, 1, 0}, inStream{inVT, 2, 0}, outStream{outVT, 3, 0};

void Note(uint32_t a, uint32_t b = 0) { callLog.push_back(a); callLog.push_back(b); }
void Step() { if (tick++ == failAt) { ++thrown; throw 73; } }

uint8_t __fastcall Query(Stub *self, void *, uint32_t iid, void **out)
{
    Note(0, iid); Step();
    *out = nullptr;
    if (!queryWorks) return 0;
    if (iid == 0x51b11bd) { *out = &inStream; return 0x41; }
    if (iid == 0xc51b11cb) { *out = &outStream; return 0x41; }
    return 0;
}
uint32_t __fastcall Release(Stub *self, void *) { Note(2, self->id); return ++self->releases; }
template <class T> uint8_t __fastcall Get(Stub *self, void *, T *out)
{
    Note(8, cursor < script.size() ? script[cursor] : 0xdead); Step();
    if (cursor < script.size()) { *out = static_cast<T>(script[cursor++]); return 0x41; }
    // A failed read leaves an element of the entry's own vector untouched, as cRZIStream's GetVoid does (DL): what Read's
    // resize stored there (zero, 0x55b370) stays observable. Stack locals (count, version, value) read zeros instead: the
    // original's are uninitialised.
    if (current && reinterpret_cast<const void *>(out) >= current->mValues.data() &&
        reinterpret_cast<const void *>(out) < current->mValues.data() + current->mValues.capacity())
        return 0;
    *out = 0; // a short stream reads zeros
    return 0;
}
uint8_t __fastcall Set(Stub *self, void *, uint32_t value) { Note(9, value); Step(); return 0x41; }
void SetupFakes()
{
    recordVT[0] = reinterpret_cast<void *>(&Query);
    inVT[2] = outVT[2] = reinterpret_cast<void *>(&Release);
    inVT[8] = reinterpret_cast<void *>(&Get<int32_t>);
    inVT[9] = reinterpret_cast<void *>(&Get<uint32_t>);
    outVT[9] = reinterpret_cast<void *>(&Set);
    record.releases = inStream.releases = outStream.releases = 0;
}

// A side's entry (side 0 the original's inline construction, side 1 ours), seeded alike.
Entry *Make(difftest::Context &ctx, int side, uint32_t seed, uint32_t count, uint32_t extra)
{
    std::mt19937 rng(seed);
    cGZPersistResourceKey key{rng(), rng(), rng(), rng()};
    Entry *entry = side == 0 ? MakeOriginal(ctx, key) : new Entry(key);
    entry->mRefCount = 1 + seed % 3;
    entry->mValues.reserve(count + extra);
    for (uint32_t i = 0; i < count; ++i) entry->mValues.push_back(rng());
    // Spare capacity holds nonzero words (fresh heap is often zero): a grow within capacity must store its own values (DL).
    for (size_t i = entry->mValues.size(); i < entry->mValues.capacity(); ++i) entry->mValues.data()[i] = rng() | 1;
    return entry;
}
// Everything observable about an entry (capacity too: both sides grow the vector the same way). The three vtable words as one
// flag each (the game's on the original side, ours on our side).
void Capture(difftest::Context &ctx, int side, std::vector<uint32_t> &out, const Entry *e)
{
    const void *const *words = reinterpret_cast<const void *const *>(e);
    for (int i = 0; i < 3; ++i) {
        const void *game = ctx.image.At<void>(kGameVTables[i]);
        out.push_back(uint32_t(words[i] == (side == 0 ? game : OurVTable(i)) && OurVTable(i) != game));
    }
    out.insert(out.end(), {e->mRefCount, e->mKey.type, e->mKey.group, e->mKey.instance, e->mKey.resource,
        uint32_t(e->mValues.size()), uint32_t(e->mValues.capacity())});
    out.insert(out.end(), e->mValues.begin(), e->mValues.end());
}

using Call = uint8_t (*)(difftest::Context &, int side, Entry *);

// One run: seed an entry, call, capture. total receives the number of fake steps of the call.
std::vector<uint32_t> Run(difftest::Context &ctx, Call call, int side, uint32_t seed, uint32_t count, uint32_t extra,
    const std::vector<uint32_t> &stream, bool works, int fail, int *total)
{
    SetupFakes();
    auto *entry = Make(ctx, side, seed, count, extra);
    script = stream; cursor = 0; callLog.clear(); tick = 0; failAt = fail; queryWorks = works;
    current = entry;
    uint8_t returned = 0; bool threw = false;
    try { returned = call(ctx, side, entry); } catch (int) { threw = true; }
    failAt = -1;
    current = nullptr;
    if (total) *total = tick;
    std::vector<uint32_t> blob{returned, uint32_t(threw), uint32_t(cursor), record.releases, inStream.releases, outStream.releases};
    blob.insert(blob.end(), callLog.begin(), callLog.end());
    Capture(ctx, side, blob, entry);
    Destroy(entry);
    return blob;
}

// Both sides over every script, shape, query result and throw position.
void Sweep(difftest::Context &ctx, Call call, const std::vector<std::vector<uint32_t>> &scripts, const char *name)
{
    for (size_t s = 0; s < scripts.size(); ++s) for (uint32_t shape = 0; shape < 8; ++shape) for (int works = 0; works < 2; ++works) {
        const uint32_t count = shape < 3 ? shape : shape * 3, extra = shape % 3 == 0 ? 0 : shape;
        int total = 0;
        for (int fail = -1; fail < total; fail += (fail < 0 || total <= 256) ? 1 : total / 256) { // a throw at each call; past 256 calls, at 256 spread over them
            const uint32_t seed = uint32_t(s * 97 + shape * 13 + works);
            auto original = Run(ctx, call, 0, seed, count, extra, scripts[s], works, fail, fail < 0 ? &total : nullptr); // only the clean run counts the calls
            auto ours = Run(ctx, call, 1, seed, count, extra, scripts[s], works, fail, nullptr);
            if (original != ours) ctx.Fail("%s script %zu shape %u works %d fail %d: result/entry/calls differ", name, s, shape, works, fail);
            ++ctx.cases;
        }
    }
}

std::vector<uint32_t> Counted(uint32_t version, uint32_t count, uint32_t first = 0x100)
{
    std::vector<uint32_t> result{version, count};
    for (uint32_t i = 0; i < count; ++i) result.push_back(first + i * 0x3001);
    return result;
}
} // namespace Lane
} // namespace

DIFF_TEST(RelationshipEntry_ReadFromRecord, 0x55bd20)
{
    using namespace Lane;
    auto scripts = std::vector<std::vector<uint32_t>>{{}, {2}, {2, 0}, {1}, {3, 5}, {0, 1, 2}, {2, 3, 7}, {2, 70, 1, 2},
        {0xffffffff}, Counted(2, 1), Counted(2, 5), Counted(2, 40), Counted(1, 4), Counted(3, 4), Counted(2, 1100)};
    thrown = 0;
    Sweep(ctx, [](difftest::Context &c, int side, Entry *e) -> uint8_t {
        return side ? CallSlot<uint8_t>(e, 4, static_cast<void *>(&record))
                    : c.Original<uint8_t(__thiscall *)(void *, void *)>(0x55bd20)(e, &record);
    }, scripts, "ReadFromRecord");
    std::printf("  ReadFromRecord: exceptions injected %llu\n", thrown);
}

DIFF_TEST(RelationshipEntry_WriteToRecord, 0x55be00)
{
    using namespace Lane;
    thrown = 0;
    Sweep(ctx, [](difftest::Context &c, int side, Entry *e) -> uint8_t {
        return side ? CallSlot<uint8_t>(e, 5, static_cast<void *>(&record))
                    : c.Original<uint8_t(__thiscall *)(void *, void *)>(0x55be00)(e, &record);
    }, {{}}, "WriteToRecord");
    std::printf("  WriteToRecord: exceptions injected %llu\n", thrown);
}

DIFF_TEST(RelationshipEntry_SetValueCount, 0x55bf10)
{
    using namespace Lane;
    for (uint32_t shape = 0; shape < 12; ++shape) for (uint32_t target = 0; target < 12; ++target) {
        const uint32_t count = shape * 2, extra = shape % 4;
        const uint32_t n = target == 0 ? 0 : target == 1 ? count : target == 2 ? count + 1 : target == 3 ? count + extra :
            target == 4 ? count + extra + 1 : target == 5 ? (count ? count - 1 : 0) : target == 6 ? 1500 + shape :
            target == 7 ? 1024 : target * shape + 3;
        std::vector<uint32_t> blobs[2];
        for (int side = 0; side < 2; ++side) {
            auto *entry = Make(ctx, side, shape * 31 + target, count + (shape == 11 ? 1100 : 0), extra);
            difftest::FreeRecorder rec;
            const uint8_t r = side ? CallSlot<uint8_t>(entry, 7, n) :
                ctx.Original<uint8_t(__thiscall *)(void *, uint32_t)>(0x55bf10)(entry, n);
            rec.Stop();
            blobs[side] = {r};
            Capture(ctx, side, blobs[side], entry);
            Destroy(entry);
        }
        if (blobs[0] != blobs[1]) ctx.Fail("SetValueCount shape %u target %u (n %u): entry differs", shape, target, n);
        ++ctx.cases;
    }
}

DIFF_TEST(RelationshipEntry_Read, 0x55b370)
{
    using namespace Lane;
    auto scripts = std::vector<std::vector<uint32_t>>{{}, {0}, {1}, {3, 1, 2, 3}, {3, 1}, {0xffffffff}, {0x80000000, 1},
        {2, 0xdeadbeef, 0x7fffffff}, {70, 4, 5}};
    for (uint32_t n : {5u, 12u, 40u, 1100u}) {
        std::vector<uint32_t> s{n};
        for (uint32_t i = 0; i < n; ++i) s.push_back(i * 0x9e3779b1u + 7);
        scripts.push_back(s);
    }
    thrown = 0;
    Sweep(ctx, [](difftest::Context &c, int side, Entry *e) -> uint8_t {
        void *stream = &inStream;
        return side ? CallSlot<uint8_t>(Face(e, kSerialFace), 4, stream) :
            c.Original<uint8_t(__thiscall *)(void *, void *)>(0x55b370)(Face(e, kSerialFace), stream);
    }, scripts, "Read");
    std::printf("  Read: exceptions injected %llu\n", thrown);
}

namespace {
// QueryInterface (main and thunks): the returned face, the AddRef, false for other ids. Original side: the RVA at the face;
// ours: slot 0 of the face's own vtable.
void QueryCases(difftest::Context &ctx, uint32_t rva, uint32_t face, const char *name)
{
    for (uint32_t iid : {1u, 0x456b8f1du, 0x456b8f1cu, 0u, 2u, 0xe4fda3d4u, 0x8c87073fu, 0xffffffffu, 0x456b8f1eu}) for (uint32_t seed = 0; seed < 4; ++seed) {
        std::vector<uint32_t> blobs[2];
        for (int side = 0; side < 2; ++side) {
            auto *entry = Lane::Make(ctx, side, seed, seed, 1);
            entry->mRefCount = seed;
            void *out = reinterpret_cast<void *>(0x5a5a5a5a);
            uint8_t *target = Lane::Face(entry, face);
            const uint8_t r = side ? Lane::CallSlot<uint8_t>(target, 0, iid, &out) :
                ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(rva)(target, iid, &out);
            const bool written = out != reinterpret_cast<void *>(0x5a5a5a5a); // the sentinel stays when refused
            blobs[side] = {r, written ? uint32_t(reinterpret_cast<uint8_t *>(out) - reinterpret_cast<uint8_t *>(entry)) : 0xfffffff0u};
            Lane::Capture(ctx, side, blobs[side], entry);
            Lane::Destroy(entry);
        }
        if (blobs[0] != blobs[1]) ctx.Fail("%s iid %08x seed %u: differs", name, iid, seed);
        ++ctx.cases;
    }
}
// Release (main and thunks): the result, whether the entry was freed and the freed block sizes.
void ReleaseCases(difftest::Context &ctx, uint32_t rva, uint32_t face, const char *name)
{
    for (uint32_t refs : {0u, 1u, 2u, 3u, 0xffffffffu}) for (uint32_t shape = 0; shape < 6; ++shape) {
        std::vector<uint32_t> blobs[2];
        for (int side = 0; side < 2; ++side) {
            auto *entry = Lane::Make(ctx, side, shape, shape * 2, shape % 3);
            if (shape == 5) entry->mValues.reserve(1200);
            entry->mRefCount = refs;
            uint8_t *self = Lane::Face(entry, face);
            difftest::FreeRecorder rec;
            const uint32_t r = side ? Lane::CallSlot<uint32_t>(self, 2) : ctx.Original<uint32_t(__thiscall *)(void *)>(rva)(self);
            rec.Stop();
            const bool freed = rec.Freed(entry);
            size_t sizes[4]{};
            const size_t freedCount = rec.SortedSizes(sizes, 4);
            blobs[side] = {r, uint32_t(freed), uint32_t(freedCount), uint32_t(sizes[0]), uint32_t(sizes[1])};
            if (!freed) { Lane::Capture(ctx, side, blobs[side], entry); Lane::Destroy(entry); }
        }
        if (blobs[0] != blobs[1]) ctx.Fail("%s refs %u shape %u: differs", name, refs, shape);
        ++ctx.cases;
    }
}
} // namespace

DIFF_TEST(RelationshipEntry_QueryInterface, 0x55b2c0)
{
    QueryCases(ctx, 0x55b2c0, 0, "QueryInterface");
}

DIFF_TEST(RelationshipEntry_Release, 0x55ba70)
{
    ReleaseCases(ctx, 0x55ba70, 0, "Release");
}

// The +4 / +8 faces' QueryInterface / Release: ours are the compiler's adjustor thunks in our vtables (no hook).
DIFF_TEST(RelationshipEntry_PersistFaceQueryInterface, 0x55b2f2)
{
    QueryCases(ctx, 0x55b2f2, Lane::kPersistFace, "PersistFace::QueryInterface");
}

DIFF_TEST(RelationshipEntry_PersistFaceRelease, 0x55bade)
{
    ReleaseCases(ctx, 0x55bade, Lane::kPersistFace, "PersistFace::Release");
}

DIFF_TEST(RelationshipEntry_SerialFaceQueryInterface, 0x55b2fa)
{
    QueryCases(ctx, 0x55b2fa, Lane::kSerialFace, "SerialFace::QueryInterface");
}

DIFF_TEST(RelationshipEntry_SerialFaceRelease, 0x55bae6)
{
    ReleaseCases(ctx, 0x55bae6, Lane::kSerialFace, "SerialFace::Release");
}

// ===== Every slot no other test calls, through each side's own vtable =====
// Primary: AddRef 0x15e520, GetPersistResource 0x1c04e0, GetValueCount 0x4e9710, GetValue 0x55aed0, SetValue 0x55bee0. The persist
// face: the AddRef thunk 0x1f4daf and the whole key face (GetKey 0x18a670, SetKey 0x4f3d80, GetType 0x145770, GetGroup 0x149cf0,
// GetInstance 0x149d00, SetType 0x1eeb70, SetGroup 0x1eec40, SetInstance 0x47d410, GetRefCount 0x46390). The serializable face:
// the AddRef thunk 0x1f4db7, Write 0x18a7a0 (false), GetGZCLSID 0x55aff0. Same seeded state on both sides; compared: the result
// (raw al for byte results), what an out-parameter received, the entry after the call and its vtable words.
namespace {
struct SlotCall {
    uint32_t face;
    int slot;
};
constexpr SlotCall kSlotCalls[] = {
    {0, 1}, {0, 3}, {0, 6}, {0, 8}, {0, 9}, {4, 1}, {4, 3}, {4, 4}, {4, 5}, {4, 6}, {4, 7}, {4, 8}, {4, 9}, {4, 10}, {4, 11},
    {8, 1}, {8, 3}, {8, 5},
};
} // namespace

DIFF_TEST(RelationshipEntry_Slots, 0x55aed0)
{
    using namespace Lane;
    for (uint32_t seed = 0; seed < 40; ++seed) {
        for (const SlotCall &c : kSlotCalls) {
            std::vector<uint32_t> blobs[2];
            for (int side = 0; side < 2; ++side) {
                auto *entry = Make(ctx, side, seed, seed % 6, seed % 3);
                std::mt19937 rng(seed * 3 + 1);
                entry->mRefCount = rng() % 5;
                const uint32_t a = rng() % 8, b = rng();
                const cGZPersistResourceKey newKey = {rng(), rng(), rng(), rng()};
                uint8_t *face = Face(entry, c.face);
                std::vector<uint32_t> &blob = blobs[side];
                blob.push_back(c.face << 8 | uint32_t(c.slot));
                if (c.slot == 1) {
                    blob.push_back(CallSlot<uint32_t>(face, 1));
                } else if (c.face == 0 && c.slot == 3) { // GetPersistResource: the +4 face
                    blob.push_back(uint32_t(CallSlot<uint8_t *>(face, 3) - Face(entry, 0)));
                } else if (c.face == 0 && c.slot == 6) {
                    blob.push_back(CallSlot<uint32_t>(face, 6));
                } else if (c.face == 0 && c.slot == 8) {
                    blob.push_back(CallSlot<uint32_t>(face, 8, a));
                } else if (c.face == 0) { // SetValue
                    blob.push_back(CallSlot<uint8_t>(face, 9, a, b));
                } else if (c.face == 4 && c.slot == 3) { // GetKey: the key by value (hidden pointer, returned)
                    cGZPersistResourceKey out = {1, 2, 3, 4};
                    void *r = CallSlot<void *>(face, 3, static_cast<void *>(&out));
                    blob.insert(blob.end(), {uint32_t(r == &out), out.type, out.group, out.instance, out.resource});
                } else if (c.face == 4 && c.slot == 4) {
                    blob.push_back(CallSlot<uint8_t>(face, 4, &newKey));
                } else if (c.face == 4 && (c.slot <= 7 || c.slot == 11)) {
                    blob.push_back(CallSlot<uint32_t>(face, c.slot));
                } else if (c.face == 4) { // SetType / SetGroup / SetInstance
                    blob.push_back(CallSlot<uint8_t>(face, c.slot, b));
                } else if (c.slot == 3) { // Write: false, the stream untouched
                    blob.push_back(CallSlot<uint8_t>(face, 3, static_cast<void *>(&outStream)));
                } else {
                    blob.push_back(CallSlot<uint32_t>(face, 5));
                }
                Capture(ctx, side, blob, entry);
                Destroy(entry);
            }
            if (blobs[0] != blobs[1]) ctx.Fail("Slots seed %u face %u slot %d: differs", seed, c.face, c.slot);
            ++ctx.cases;
        }
    }
}

// ===== The adapters: our functions behind the old entry points, on an entry built by the original =====
// What runs when the game calls an old entry point directly or holds an entry with its own vtables. The entry on both sides is
// the original's; the original side calls the RVA, ours the adapter the hook installs. The game's vtables are copied with a trap
// in every slot an adapter forwards to, so an adapter that dispatched through the object's vtable (in game: back into its own
// hook) logs it. AddRef (QueryInterface's call) stays the game's.
namespace {
void *gTrapVT[10];
void *gTrapSerialVT[6];
// Each slot's trap takes the slot's stack arguments (ret N), as the slot itself.
uint32_t TrapHit(void *, unsigned words) { Lane::Note(0x7a7a7a7au, words); return 0; }
void *Trap(unsigned words) { return shape::TrapFor<TrapHit>(words); }
} // namespace

DIFF_TEST(RelationshipEntry_Adapters, 0x55bd20)
{
    using namespace Lane;
    using A = cTSRelationshipEntryAdapters;
    for (uint32_t seed = 0; seed < 48; ++seed) {
        std::vector<uint32_t> blobs[2];
        for (int side = 0; side < 2; ++side) {
            auto *entry = Make(ctx, 0, seed, seed % 7, seed % 2); // both entries are the original's
            entry->mRefCount = 4;
            std::memcpy(gTrapVT, ctx.image.At<void>(Entry::kOriginalVTable), sizeof(gTrapVT));
            for (int slot : {0, 9}) gTrapVT[slot] = Trap(2);
            for (int slot : {4, 5, 7, 8}) gTrapVT[slot] = Trap(1);
            gTrapVT[2] = Trap(0);
            std::memcpy(gTrapSerialVT, ctx.image.At<void>(Entry::kOriginalSerialVTable), sizeof(gTrapSerialVT));
            gTrapSerialVT[4] = Trap(1);
            gTrapSerialVT[5] = Trap(0);
            *reinterpret_cast<void **>(Face(entry, 0)) = gTrapVT;
            *reinterpret_cast<void **>(Face(entry, kSerialFace)) = gTrapSerialVT;
            auto *serial = reinterpret_cast<cIGZSerializable *>(Face(entry, kSerialFace));
            SetupFakes();
            callLog.clear(); tick = 0; failAt = -1; queryWorks = true; cursor = 0;
            std::mt19937 rng(seed);
            std::vector<uint32_t> &b = blobs[side];
            const uint32_t id = std::vector<uint32_t>{1, 0x456b8f1d, 0xe4fda3d4, 0}[rng() % 4];
            void *out = nullptr;
            b.push_back(side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0x55b2c0)(entry, id, &out)
                                  : A::QueryInterface(entry, nullptr, id, &out));
            b.push_back(out ? uint32_t(static_cast<uint8_t *>(out) - Face(entry, 0)) : 0xffffffffu);
            const uint32_t index = rng() % 8, value = rng();
            b.push_back(side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x55aed0)(entry, index)
                                  : A::GetValue(entry, nullptr, index));
            b.push_back(side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t)>(0x55bee0)(entry, index, value)
                                  : A::SetValue(entry, nullptr, index, value));
            // WriteToRecord, SetValueCount, ReadFromRecord: adapters dropped (lane GT; only the game's vtable reaches them): the
            // original on both sides, keeping the entry's state the same for the adapters after them. Ours are tested above.
            b.push_back(ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x55be00)(entry, &record));
            const uint32_t count = rng() % 9;
            b.push_back(ctx.Original<uint8_t(__thiscall *)(void *, uint32_t)>(0x55bf10)(entry, count));
            script = Counted(2, seed % 5);
            b.push_back(ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x55bd20)(entry, &record));
            script = {seed % 4, 7, 8, 9}; cursor = 0;
            b.push_back(side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x55b370)(serial, &inStream)
                                  : A::Read(serial, nullptr, reinterpret_cast<ifaces::cIGZIStream *>(&inStream)));
            b.push_back(side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x55aff0)(serial) : A::GetGZCLSID(serial, nullptr));
            b.push_back(side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x55ba70)(entry) : A::Release(entry, nullptr));
            b.insert(b.end(), callLog.begin(), callLog.end());
            b.insert(b.end(), {entry->mRefCount, uint32_t(entry->mValues.size())});
            b.insert(b.end(), entry->mValues.begin(), entry->mValues.end());
            entry->mRefCount = 1;
            difftest::FreeRecorder rec;
            const uint32_t last = side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x55ba70)(entry) : A::Release(entry, nullptr);
            rec.Stop();
            b.insert(b.end(), {last, uint32_t(rec.Freed(entry)), uint32_t(callLog.size())});
        }
        if (blobs[0] != blobs[1]) ctx.Fail("Adapters seed %u: differs", seed);
        ++ctx.cases;
    }
}
