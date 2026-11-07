// Differential tests for cTSObjectTypeAttributeManager (src/ts/cTSObjectTypeAttributeManager.h, resource TATT).
//
// A world is a manager made by the route's Create (the original 0x4d8b60, or ours on the vtable route; see "routes" below) and
// filled from a seed with std::map operations on our type (same STL), holding attribute fakes (slots 1 AddRef / 2 Release
// counted and logged, 4 GetCount, 6 GetId, 7 GetData over a std::vector<uint16_t> of values; GetId can answer a second id on
// every other call). The manager carries copies of the route's own vtables; the primary copy's AddRef / Release log, and its
// slots 7 / 8 (FindAttributes / AddAttributes, called through `this`) are the route's own implementation. Streams are recording fakes: the reader hands out a scripted word
// list and, past a truncation point, fails its reads (they write 0 and set the error); the writer logs every value. Each pair
// of runs compares the result, the event log, the manager's map (ids, attribute fakes), count and key, every fake's count and
// values, and the sizes of the blocks freed.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "hook/hooks.h"
#include "shape.h"
#include "ts/cTSObjectTypeAttributeManager.h"

#include <windows.h>

#include <cstring>
#include <iterator>
#include <initializer_list>
#include <random>
#include <string>
#include <vector>

namespace {
using Mgr = cTSObjectTypeAttributeManager;
using Blob = std::vector<uint32_t>;

Blob gEv;
void Ev(std::initializer_list<uint32_t> items) { gEv.insert(gEv.end(), items); }
enum : uint32_t {
    evAddRef = 0x100, evRelease, evCount, evId, evData, evSkip, evGet32, evGet16, evSetMeta, evGetError, evSet8, evSet16,
    evSet32, evMgrAddRef, evMgrRelease, evQuery, evFind, evSave, evFlush, evSegRelease, evSegment
};

// ---- attribute fakes ----
constexpr unsigned kAttrs = 12;
struct Attr {
    void **vt;
    unsigned index;
    int32_t refs;
    uint32_t id;
    uint32_t altId;     // answered by every other GetId call when flip is set
    bool flip;
    uint32_t idCalls;
    std::vector<uint16_t> values;
};
Attr gAttrs[kAttrs];
void *gAttrVT[8];

uint32_t __fastcall AttrAddRef(Attr *a, void *) { Ev({evAddRef, a->index}); return uint32_t(++a->refs); }
uint32_t __fastcall AttrRelease(Attr *a, void *) { Ev({evRelease, a->index}); return uint32_t(--a->refs); }
uint32_t __fastcall AttrCount(Attr *a, void *) { Ev({evCount, a->index}); return uint32_t(a->values.size()); }
uint32_t __fastcall AttrId(Attr *a, void *)
{
    const uint32_t id = a->flip && (a->idCalls++ % 2) ? a->altId : a->id;
    Ev({evId, a->index, id});
    return id;
}
uint16_t *__fastcall AttrData(Attr *a, void *) { Ev({evData, a->index}); return a->values.data(); }

ifaces::cIEdithObjectTypeAttribute *AsAttr(Attr *a) { return reinterpret_cast<ifaces::cIEdithObjectTypeAttribute *>(a); }
uint32_t AttrIndex(const void *p)
{
    if (!p) return 0xffff;
    for (unsigned i = 0; i < kAttrs; ++i)
        if (p == &gAttrs[i]) return i;
    return 0xeeee;
}

// A small id space so the map's ids, the record's ids and the fakes' ids meet.
uint32_t PickId(std::mt19937 &r) { return r() % 5 == 0 ? r() : r() % 16; }

void ResetAttrs(std::mt19937 &r)
{
    gAttrVT[1] = reinterpret_cast<void *>(&AttrAddRef);
    gAttrVT[2] = reinterpret_cast<void *>(&AttrRelease);
    gAttrVT[4] = reinterpret_cast<void *>(&AttrCount);
    gAttrVT[6] = reinterpret_cast<void *>(&AttrId);
    gAttrVT[7] = reinterpret_cast<void *>(&AttrData);
    for (unsigned i = 0; i < kAttrs; ++i) {
        Attr &a = gAttrs[i];
        a.vt = gAttrVT;
        a.index = i;
        a.refs = 100;
        a.id = PickId(r);
        a.altId = r() % 2 ? PickId(r) : a.id + 1;
        a.flip = r() % 6 == 0;
        a.idCalls = 0;
        const unsigned n = r() % 8 == 0 ? 0x80 + r() % 3 - 1 : r() % 7;
        a.values.assign(n, 0);
        for (auto &v : a.values) v = uint16_t(r());
    }
}

// ---- the manager: routes, vtable copies, traps (tests/shape.h) ----
//
// Route 0 is the original side (a manager from the original Create carrying copies of the game's vtables), route 1 ours from
// our Create carrying copies of our vtables (the function under test called through our pristine vtable's slot), route 2 a
// manager from the original Create with copies of the game's vtables and the RVA hook's adapter called, an arity-matched trap in
// the slot it forwards to (any trap hit fails). The copies of the primary vtable get the logging AddRef / Release below; slots
// 7 / 8 (called through `this`) stay the route's own. Only vtable words are coded in the comparisons.
int gRoute = 0;
unsigned gTrapHits = 0;
constexpr unsigned kFaces = 2;
constexpr uint32_t kFaceOffset[kFaces] = {0, otam::kPersistFace};
constexpr uint32_t kGameVt[kFaces] = {Mgr::kVTable, Mgr::kPersistVTable};
constexpr unsigned kFaceSlots[kFaces] = {13, 12};
// Stack words per slot (a trap must return with the slot's `ret N`).
constexpr uint8_t kArity[kFaces][13] = {{2, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1}, {2, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0}};
void *const *gOurs[kFaces];
void *const *GameVt(unsigned face) { return shape::GameVTable(kGameVt[face]); }

void *gMgrVT[13];
void *gMgrVT2[13]; // the second manager's (AddAllFrom's source): AddRef / Release logged under another tag
void *gPersistCopy[12];
uint32_t __fastcall MgrAddRef(Mgr *m, void *) { Ev({evMgrAddRef}); return ++m->mRefCount; }
uint32_t __fastcall MgrRelease(Mgr *m, void *) { Ev({evMgrRelease}); return --m->mRefCount; }
uint32_t __fastcall Mgr2Release(Mgr *m, void *) { Ev({evMgrRelease, 2}); return --m->mRefCount; }

// The slots the bodies call through `this` (Read: slot 7, AddAllFrom: slot 8, Save: the persist face's GetKey): logged, then the
// route's own function (the game's on the original side and the adapter route, ours on the vtable route), so a self-call that
// skips the vtable shows as a missing event.
void *OwnSlot(unsigned face, unsigned slot) { return (gRoute == 1 ? gOurs[face] : GameVt(face))[slot]; }
void *__fastcall MgrFind(Mgr *m, void *, uint32_t id)
{
    Ev({0xf7, id});
    return reinterpret_cast<void *(__thiscall *)(Mgr *, uint32_t)>(OwnSlot(0, 7))(m, id);
}
uint32_t __fastcall MgrAdd(Mgr *m, void *, void *attributes)
{
    Ev({0xf8, AttrIndex(attributes)});
    return reinterpret_cast<uint32_t(__thiscall *)(Mgr *, void *)>(OwnSlot(0, 8))(m, attributes);
}
void *__fastcall PersistGetKey(void *face, void *, void *out)
{
    Ev({0xf3});
    return reinterpret_cast<void *(__thiscall *)(void *, void *)>(OwnSlot(1, 3))(face, out);
}

void SetupVTables()
{
    if (!gOurs[0])
        for (unsigned f = 0; f < kFaces; ++f) gOurs[f] = shape::Ours<Mgr>(kFaceOffset).face[f];
    void *const *own = gRoute == 1 ? gOurs[0] : GameVt(0);
    void *const *ownPersist = gRoute == 1 ? gOurs[1] : GameVt(1);
    std::memcpy(gMgrVT, own, sizeof(gMgrVT));
    gMgrVT[1] = reinterpret_cast<void *>(&MgrAddRef);
    gMgrVT[2] = reinterpret_cast<void *>(&MgrRelease);
    gMgrVT[7] = reinterpret_cast<void *>(&MgrFind);
    gMgrVT[8] = reinterpret_cast<void *>(&MgrAdd);
    std::memcpy(gMgrVT2, gMgrVT, sizeof(gMgrVT2));
    gMgrVT2[2] = reinterpret_cast<void *>(&Mgr2Release);
    std::memcpy(gPersistCopy, ownPersist, sizeof(gPersistCopy));
    gPersistCopy[3] = reinterpret_cast<void *>(&PersistGetKey);
}

// A vtable word as a code: the test's copies (1 / 2 the two managers' primary copies, 1 the persist copy), a class vtable of the
// face (the game's or ours: what a destructor stores; the adapter runs our destructor on a game-built manager), else raw.
uint32_t VtCode(const Mgr *m, unsigned face)
{
    const void *word = shape::VTableAt(const_cast<Mgr *>(m), kFaceOffset[face]);
    if (face == 0 && word == gMgrVT) return 1;
    if (face == 0 && word == gMgrVT2) return 2;
    if (face == 1 && word == gPersistCopy) return 1;
    if (word == GameVt(face) || word == gOurs[face]) return 0x7d00 + face;
    return uint32_t(uintptr_t(word));
}

uint32_t TrapHit(void *, unsigned)
{
    ++gTrapHits;
    Ev({0x7eaf});
    return 0;
}

// Runs the function at `rva` (a slot of face `face`) the route's way on `m`: the original RVA, our vtable's slot holding it, or
// the adapter with a trap in that slot of the manager's own copy.
template <class Fn, class... A> auto Call(difftest::Context &ctx, bool original, uint32_t rva, unsigned face, void *adapter, Mgr *m, A... args)
{
    void *self = reinterpret_cast<uint8_t *>(m) + kFaceOffset[face];
    if (original || (gRoute != 1 && !adapter)) return ctx.Original<Fn>(rva)(self, args...); // NO_ADAPTER: the original
    unsigned slot = shape::SlotOf(kGameVt[face], kFaceSlots[face], rva);
    if (slot == ~0u) ctx.Fail("rva %x is not in face %u's vtable", rva, face), slot = 0;
    if (gRoute == 1) return reinterpret_cast<Fn>(gOurs[face][slot])(self, args...);
    shape::VTableAt(m, kFaceOffset[face])[slot] = shape::TrapFor<TrapHit>(kArity[face][slot]);
    return reinterpret_cast<Fn>(adapter)(self, args...);
}
#define ADAPTER(name) reinterpret_cast<void *>(&cTSObjectTypeAttributeManagerAdapters::name)
// A slot whose adapter was dropped (lane GP: only the game's vtables reach the original, and no manager carries them while the
// Create hook is in; its status entry is "in_effect": "callers"): the adapter route runs the original on both sides.
#define NO_ADAPTER(name) nullptr

Mgr *NewManager(difftest::Context &ctx, std::mt19937 &r, unsigned entries, void **vt)
{
    Mgr *m = gRoute == 1 ? Mgr::Create() : ctx.Original<Mgr *(__cdecl *)()>(0x4d8b60)();
    shape::VTableAt(m, 0) = vt;
    shape::VTableAt(m, otam::kPersistFace) = gPersistCopy;
    m->mRefCount = r() % 3;
    for (auto &k : m->mKey) k = r() % 3 ? r() % 8 : r();
    for (unsigned i = 0; i < entries; ++i) {
        Attr *a = &gAttrs[r() % kAttrs];
        const uint32_t id = r() % 3 ? a->id : PickId(r);
        m->mAttributes.try_emplace(id, ObjectTypeAttributeRef(AsAttr(a)));
    }
    return m;
}
void FreeManager(Mgr *m) { cTSObjectTypeAttributeManagerAdapters::DeletingDestructor(m, nullptr, 1); }

unsigned PickEntries(std::mt19937 &r)
{
    switch (r() % 5) {
    case 0: return 0;
    case 1: return 1;
    default: return r() % 10;
    }
}

void CaptureManager(Blob &b, Mgr *m)
{
    b.push_back(0xa77a);
    b.push_back(VtCode(m, 0));
    b.push_back(VtCode(m, 1));
    b.push_back(m->mRefCount);
    for (uint32_t k : m->mKey) b.push_back(k);
    b.push_back(uint32_t(m->mAttributes.size()));
    for (const auto &e : m->mAttributes) b.insert(b.end(), {e.first, AttrIndex(e.second.Get())});
}

void CaptureAttrs(Blob &b)
{
    b.push_back(0xa7);
    for (const Attr &a : gAttrs) {
        b.insert(b.end(), {uint32_t(a.refs), a.idCalls, uint32_t(a.values.size())});
        for (uint16_t v : a.values) b.push_back(v);
    }
}

bool Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    if (at == a.size() && at == b.size()) return true;
    ctx.Fail("%s seed %u differs at word %zu (original %x, replacement %x; sizes %zu/%zu)", what, seed, at,
        at < a.size() ? a[at] : 0xdeadbeef, at < b.size() ? b[at] : 0xdeadbeef, a.size(), b.size());
    return false;
}

// Runs `run(original, rng)` on a fresh world per side and compares; `run` returns the result word and may add to the blob.
// The original side and both of our routes, each compared with the original (`routes` 2: the vtable route only).
template <class Run> void Pair(difftest::Context &ctx, const char *what, uint32_t seed, Run run, int routes = 3)
{
    Blob blobs[3];
    for (int route = 0; route < routes; ++route) {
        gRoute = route;
        gTrapHits = 0;
        std::mt19937 r(seed * 2654435761u + 7);
        ResetAttrs(r);
        SetupVTables();
        gEv.clear();
        Blob &b = blobs[route];
        b.push_back(run(route == 0, r, b));
        CaptureAttrs(b);
        b.push_back(0xe7e7);
        b.insert(b.end(), gEv.begin(), gEv.end());
        if (gTrapHits) ctx.Fail("%s seed %u route %d: %u trap hits", what, seed, route, gTrapHits);
    }
    gRoute = 0;
    Same(ctx, blobs[0], blobs[1], (std::string(what) + " (vtable route)").c_str(), seed);
    if (routes > 2) Same(ctx, blobs[0], blobs[2], (std::string(what) + " (adapter route)").c_str(), seed);
}

// ---- streams ----
struct Stream {
    void **vt;
} gStream;
void *gStreamVT[21];
std::vector<uint32_t> gWords;  // the reader's script
size_t gNext = 0, gTruncate = 0; // reads at or past gTruncate fail
bool gError = false;            // the stream's error flag
bool gForceError = false;       // GetError answers true anyway
uint8_t gWriteAnswer = 1;

// Where a value pointer points: (attribute, word offset) for an attribute's values, a marker for anything else (the
// replacement's and the original's scratch buffers live in different frames).
void LogTarget(const void *p)
{
    for (const Attr &a : gAttrs) {
        if (a.values.empty()) continue;
        const uint16_t *begin = a.values.data();
        if (p >= begin && p < begin + a.values.size()) {
            Ev({a.index, uint32_t(static_cast<const uint16_t *>(p) - begin)});
            return;
        }
    }
    Ev({0x5c, 0});
}
bool NextWord(uint32_t &v)
{
    if (gNext >= gTruncate || gNext >= gWords.size()) {
        gError = true;
        v = 0;
        ++gNext;
        return false;
    }
    v = gWords[gNext++];
    return true;
}
uint32_t __fastcall SSkip(Stream *, void *, uint32_t n) { Ev({evSkip, n}); return 1; }
uint8_t __fastcall SGet32(Stream *, void *, uint32_t *out)
{
    // A failed read writes 0: the original reads uninitialised locals after a failure, so a fake that wrote nothing would
    // compare stack garbage.
    uint32_t v;
    const bool ok = NextWord(v);
    *out = v;
    Ev({evGet32, v, ok});
    return ok;
}
uint8_t __fastcall SGet16(Stream *, void *, int16_t *out)
{
    uint32_t v;
    const bool ok = NextWord(v);
    *out = int16_t(v);
    Ev({evGet16, v & 0xffff, ok});
    LogTarget(out);
    return ok;
}
void __fastcall SSetMeta(Stream *, void *, void *metadata)
{
    uint32_t words[5];
    std::memcpy(words, metadata, sizeof(words));
    Ev({evSetMeta, words[0] == uint32_t(uintptr_t(GameAddress<void>(0xe6180c))), words[1], words[2], words[3], words[4]});
    ::operator delete(metadata, 0x14);
}
uint32_t __fastcall SGetError(Stream *, void *)
{
    Ev({evGetError});
    return 0xabcd00u | uint32_t(gError || gForceError);
}
uint32_t __fastcall SSet8(Stream *, void *, uint32_t v) { Ev({evSet8, v & 0xff}); return gWriteAnswer; }
uint32_t __fastcall SSet16(Stream *, void *, uint32_t v) { Ev({evSet16, v & 0xffff}); return gWriteAnswer; }
uint32_t __fastcall SSet32(Stream *, void *, uint32_t v) { Ev({evSet32, v}); return gWriteAnswer; }

void ResetStream(bool reading)
{
    for (auto &s : gStreamVT) s = nullptr;
    if (reading) {
        gStreamVT[3] = reinterpret_cast<void *>(&SSkip);
        gStreamVT[6] = reinterpret_cast<void *>(&SGet16);
        gStreamVT[9] = reinterpret_cast<void *>(&SGet32);
    } else {
        gStreamVT[5] = reinterpret_cast<void *>(&SSet8);
        gStreamVT[7] = reinterpret_cast<void *>(&SSet16);
        gStreamVT[9] = reinterpret_cast<void *>(&SSet32);
    }
    gStreamVT[18] = reinterpret_cast<void *>(&SGetError);
    gStreamVT[19] = reinterpret_cast<void *>(&SSetMeta);
    gStream.vt = gStreamVT;
    gNext = 0;
    gError = false;
}

// A record for Read: header words, then entries whose ids are known or not and whose value counts sit around the capacity.
void Script(std::mt19937 &r, Mgr *m)
{
    gWords.clear();
    gWords.push_back(r() % 4 ? Mgr::kClassId : r());
    gWords.push_back(r() % 3 ? Mgr::kRecordVersion : r() % 0x60);
    gWords.push_back(r() % 4 ? 0 : r());
    const unsigned entries = r() % 6 == 0 ? 0 : r() % 6 == 0 ? 1 : r() % 12;
    gWords.push_back(entries);
    for (unsigned e = 0; e < entries; ++e) {
        uint32_t id;
        uint32_t capacity = Mgr::kScratchValues;
        if (!m->mAttributes.empty() && r() % 3) {
            auto it = m->mAttributes.begin();
            std::advance(it, r() % m->mAttributes.size());
            id = it->first;
            capacity = uint32_t(reinterpret_cast<Attr *>(it->second.Get())->values.size());
        } else {
            id = PickId(r);
        }
        gWords.push_back(id);
        uint32_t n;
        switch (r() % 6) {
        case 0: n = capacity; break;
        case 1: n = capacity + 1; break; // one too many: the read fails there
        case 2: n = 0; break;
        default: n = capacity ? r() % (capacity + 1) : 0; break;
        }
        if (r() % 25 == 0) n = 0x80 + r() % 2; // the scratch's own edge
        gWords.push_back(n);
        for (uint32_t i = 0; i < n && i < 0x90; ++i) gWords.push_back(r() & 0xffff);
    }
    gTruncate = r() % 4 == 0 ? r() % (gWords.size() + 1) : gWords.size() + 8;
    gForceError = r() % 8 == 0;
}

using SerialFn = uint8_t(__thiscall *)(void *, void *);
} // namespace

DIFF_TEST(ObjectTypeAttributeManager_Create, 0x4d8b60)
{
    SetupVTables(); // learns our vtables
    using DeleteFn = Mgr *(__thiscall *)(void *, uint32_t);
    Blob blobs[2];
    for (int side = 0; side < 2; ++side) {
        // Ours: the Create hook's entry (our class, our constructor).
        Mgr *m = side == 0 ? ctx.Original<Mgr *(__cdecl *)()>(0x4d8b60)() : Mgr::Create();
        Blob &b = blobs[side];
        const uint32_t *w = reinterpret_cast<const uint32_t *>(m);
        // Each side's own vtables: the game's in the original's manager, ours in ours.
        for (unsigned f = 0; f < kFaces; ++f)
            b.push_back(shape::VTableAt(m, kFaceOffset[f]) == (side == 0 ? GameVt(f) : gOurs[f]) ? 0x7d00 + f : w[f]);
        b.push_back(w[2]);
        b.push_back(w[4]);
        for (unsigned i = 5; i < 9; ++i) b.push_back(w[i]);
        const uint32_t *head = reinterpret_cast<const uint32_t *>(w[3]);
        b.insert(b.end(), {head[0] == w[3], head[1] == w[3], head[2] == w[3], *reinterpret_cast<const uint16_t *>(&head[3])});
        b.push_back(uint32_t(m->mAttributes.size()));
        // The block is the size of the object, and the head node one of the map's (0x18).
        b.push_back(uint32_t(HeapSize(GetProcessHeap(), 0, m)) >= Mgr::kSize);
        // The original's manager by the adapter (a game-built manager reaching 0x4d8a30), ours through slot 12 of its vtable.
        difftest::FreeRecorder rec;
        Mgr *res = side == 0 ? cTSObjectTypeAttributeManagerAdapters::DeletingDestructor(m, nullptr, 1)
                             : reinterpret_cast<DeleteFn>(gOurs[0][12])(m, 1);
        rec.Stop();
        b.push_back(rec.Freed(m) && res == m);
    }
    Same(ctx, blobs[0], blobs[1], "Create", 0);
}

DIFF_TEST(ObjectTypeAttributeManager_DeletingDestructor, 0x4d8a30)
{
    for (uint32_t seed = 0; seed < 200; ++seed)
        Pair(ctx, "DeletingDestructor", seed, [&](bool original, std::mt19937 &r, Blob &b) {
            Mgr *m = NewManager(ctx, r, PickEntries(r), gMgrVT);
            const uint32_t flags = seed % 3;
            gEv.clear();
            difftest::FreeRecorder rec;
            Mgr *res = Call<Mgr *(__thiscall *)(void *, uint32_t)>(ctx, original, 0x4d8a30, 0, ADAPTER(DeletingDestructor), m, flags);
            rec.Stop();
            const bool freed = rec.Freed(m);
            b.push_back(freed);
            size_t sizes[64];
            const size_t count = rec.SortedSizes(sizes, 64);
            b.push_back(uint32_t(rec.Count()));
            for (size_t i = 0; i < count; ++i) b.push_back(uint32_t(sizes[i]));
            if (!freed) {
                b.push_back(VtCode(m, 0));
                b.push_back(VtCode(m, 1));
                ::operator delete(m, Mgr::kSize);
            }
            return uint32_t(res == m);
        });
}

DIFF_TEST(ObjectTypeAttributeManager_QueryInterface, 0x4d8c30)
{
    const uint32_t ids[] = {0, 1, 2, 0x2c97cc10, 0xac98034e, 0xe4fda3d4, 0x456b8f1d, 0x456b8f1c, 0x456b8f1e, 0x2c97cc11, 0xffffffff};
    uint32_t seed = 0;
    for (int face = 0; face < 2; ++face)
        for (uint32_t id : ids)
            Pair(ctx, "QueryInterface", seed++, [&](bool original, std::mt19937 &r, Blob &b) {
                Mgr *m = NewManager(ctx, r, 1, gMgrVT);
                void *out = reinterpret_cast<void *>(0x1234);
                using QIFn = uint8_t(__thiscall *)(void *, uint32_t, void **);
                const uint8_t res = face == 0 ? Call<QIFn>(ctx, original, 0x4d8c30, 0, ADAPTER(QueryInterface), m, id, &out)
                                              : Call<QIFn>(ctx, original, 0x4d8c89, 1, NO_ADAPTER(PersistQueryInterface), m, id, &out);
                const void *persist = reinterpret_cast<uint8_t *>(m) + otam::kPersistFace;
                b.push_back(out == m ? 1 : out == persist ? 2 : out == reinterpret_cast<void *>(0x1234) ? 3 : 0xbad);
                CaptureManager(b, m);
                FreeManager(m);
                return uint32_t(res);
            });
}

DIFF_TEST(ObjectTypeAttributeManager_GetGZCLSID, 0x4d8c20)
{
    Pair(ctx, "GetGZCLSID", 0, [&](bool original, std::mt19937 &r, Blob &) {
        Mgr *m = NewManager(ctx, r, 0, gMgrVT);
        const uint32_t res = Call<uint32_t(__thiscall *)(void *)>(ctx, original, 0x4d8c20, 0, ADAPTER(GetGZCLSID), m);
        FreeManager(m);
        return res;
    });
}

DIFF_TEST(ObjectTypeAttributeManager_FindAttributes, 0x4d8b10)
{
    for (uint32_t seed = 0; seed < 300; ++seed)
        Pair(ctx, "FindAttributes", seed, [&](bool original, std::mt19937 &r, Blob &b) {
            Mgr *m = NewManager(ctx, r, PickEntries(r), gMgrVT);
            uint32_t res = 0;
            for (int k = 0; k < 6; ++k) {
                const uint32_t id = PickId(r);
                void *found = Call<void *(__thiscall *)(void *, uint32_t)>(ctx, original, 0x4d8b10, 0, NO_ADAPTER(FindAttributes), m, id);
                b.push_back(AttrIndex(found));
            }
            CaptureManager(b, m);
            FreeManager(m);
            return res;
        });
}

DIFF_TEST(ObjectTypeAttributeManager_AddAttributes, 0x4d8ed0)
{
    for (uint32_t seed = 0; seed < 400; ++seed)
        Pair(ctx, "AddAttributes", seed, [&](bool original, std::mt19937 &r, Blob &b) {
            Mgr *m = NewManager(ctx, r, PickEntries(r), gMgrVT);
            gEv.clear();
            uint32_t all = 0;
            for (int k = 0; k < 5; ++k) {
                Attr *a = &gAttrs[r() % kAttrs];
                // Answers are compared in al only (the replace path sets al; the insert path movs the byte too).
                const uint8_t res = uint8_t(Call<uint32_t(__thiscall *)(void *, void *)>(ctx, original, 0x4d8ed0, 0, NO_ADAPTER(AddAttributes), m,
                    static_cast<void *>(a)));
                all = all * 3 + res;
            }
            CaptureManager(b, m);
            FreeManager(m);
            return all;
        });
}

DIFF_TEST(ObjectTypeAttributeManager_Clear, 0x4d9030)
{
    for (uint32_t seed = 0; seed < 100; ++seed)
        Pair(ctx, "Clear", seed, [&](bool original, std::mt19937 &r, Blob &b) {
            Mgr *m = NewManager(ctx, r, PickEntries(r), gMgrVT);
            gEv.clear();
            difftest::FreeRecorder rec;
            Call<void(__thiscall *)(void *)>(ctx, original, 0x4d9030, 0, ADAPTER(Clear), m);
            rec.Stop();
            size_t sizes[64];
            const size_t count = rec.SortedSizes(sizes, 64);
            for (size_t i = 0; i < count; ++i) b.push_back(uint32_t(sizes[i]));
            CaptureManager(b, m);
            // The map stays usable: one insert after the clear.
            m->mAttributes.try_emplace(3, ObjectTypeAttributeRef(AsAttr(&gAttrs[0])));
            CaptureManager(b, m);
            FreeManager(m);
            return uint32_t(rec.Count());
        });
}

DIFF_TEST(ObjectTypeAttributeManager_AddAllFrom, 0x4d8a80)
{
    // The other object: a fake whose QueryInterface (slot 0) answers a second manager, null, or fails; or the manager itself.
    struct Other {
        void **vt;
    };
    static void *otherVT[3];
    static Mgr *answer;
    static int mode;
    struct F {
        static uint8_t __fastcall Query(Other *, void *, uint32_t iid, void **out)
        {
            Ev({evQuery, iid, uint32_t(mode)});
            if (mode == 0) return 0;
            *out = answer;
            return mode == 3 ? 0 : 1; // 3: fails but writes the pointer (released all the same)
        }
    };
    otherVT[0] = reinterpret_cast<void *>(&F::Query);
    Other other{otherVT};
    for (uint32_t seed = 0; seed < 300; ++seed)
        Pair(ctx, "AddAllFrom", seed, [&](bool original, std::mt19937 &r, Blob &b) {
            Mgr *m = NewManager(ctx, r, PickEntries(r), gMgrVT);
            Mgr *source = NewManager(ctx, r, PickEntries(r), gMgrVT2);
            mode = int(r() % 5);
            answer = mode == 4 ? m : source;
            if (mode == 4) mode = 1;
            // A query that succeeds with a null object would crash the original (it walks the answer's map unchecked); a
            // failing one that still wrote the pointer instead (released all the same).
            if (mode == 2) mode = 3;
            gEv.clear();
            Call<void(__thiscall *)(void *, void *)>(ctx, original, 0x4d8a80, 0, NO_ADAPTER(AddAllFrom), m, static_cast<void *>(&other));
            CaptureManager(b, m);
            CaptureManager(b, source);
            FreeManager(source);
            FreeManager(m);
            return 0u;
        });
}

DIFF_TEST(ObjectTypeAttributeManager_Write, 0x4d9060)
{
    for (uint32_t seed = 0; seed < 300; ++seed)
        Pair(ctx, "Write", seed, [&](bool original, std::mt19937 &r, Blob &b) {
            Mgr *m = NewManager(ctx, r, PickEntries(r), gMgrVT);
            // Slot 5 (GetGZCLSID, called through this) answers something other than the class id now and then.
            static uint32_t clsid;
            struct G {
                static uint32_t __fastcall Id(Mgr *, void *) { return clsid; }
            };
            clsid = r() % 3 ? Mgr::kClassId : r();
            gMgrVT[5] = reinterpret_cast<void *>(&G::Id);
            ResetStream(false);
            gForceError = r() % 4 == 0;
            gWriteAnswer = uint8_t(r() % 2);
            gEv.clear();
            const uint8_t res = Call<SerialFn>(ctx, original, 0x4d9060, 0, NO_ADAPTER(Write), m, static_cast<void *>(&gStream));
            CaptureManager(b, m);
            FreeManager(m);
            return uint32_t(res);
        });
}

DIFF_TEST(ObjectTypeAttributeManager_Read, 0x4d8ca0)
{
    for (uint32_t seed = 0; seed < 600; ++seed)
        Pair(ctx, "Read", seed, [&](bool original, std::mt19937 &r, Blob &b) {
            Mgr *m = NewManager(ctx, r, PickEntries(r), gMgrVT);
            ResetStream(true);
            Script(r, m);
            gEv.clear();
            const uint8_t res = Call<SerialFn>(ctx, original, 0x4d8ca0, 0, NO_ADAPTER(Read), m, static_cast<void *>(&gStream));
            b.push_back(uint32_t(gNext));
            CaptureManager(b, m);
            FreeManager(m);
            return uint32_t(res);
        });
}

namespace {
// ---- Save: the persist system (cache 0x10faf6c) slots 16 SaveResource / 34 FindSegment, and a DB segment ----
struct Persist {
    void **vt;
} gPersist;
void *gPersistVT[40];
struct Segment {
    void **vt;
} gSegment;
void *gSegmentVT[12];
Mgr *gSaving;
uint32_t gFindAnswer, gSaveAnswer, gFlushAnswer;
bool gGiveSegment;
uint32_t NormFace(const void *p)
{
    if (!p) return 0;
    if (p == gSaving) return 1;
    if (p == reinterpret_cast<uint8_t *>(gSaving) + otam::kPersistFace) return 2;
    if (p == &gSegment) return 3;
    return 0xbad;
}
uint32_t __fastcall PFind(Persist *, void *, const uint32_t *key, uint32_t iid, void **out)
{
    Ev({evSegment, key[0], key[1], key[2], key[3], iid});
    if (gGiveSegment) *out = &gSegment;
    return gFindAnswer;
}
uint32_t __fastcall PSave(Persist *, void *, void *resource, void *segment)
{
    Ev({evSave, NormFace(resource), NormFace(segment)});
    return gSaveAnswer;
}
uint32_t __fastcall SegFlush(Segment *, void *) { Ev({evFlush}); return gFlushAnswer; }
uint32_t __fastcall SegRelease(Segment *, void *) { Ev({evSegRelease}); return 0; }
} // namespace

DIFF_TEST(ObjectTypeAttributeManager_Save, 0x4d8e20)
{
    void **cache = GameAddress<void *>(0x10faf6c);
    void *saved = *cache;
    for (auto &s : gPersistVT) s = nullptr;
    gPersistVT[16] = reinterpret_cast<void *>(&PSave);
    gPersistVT[34] = reinterpret_cast<void *>(&PFind);
    gPersist.vt = gPersistVT;
    for (auto &s : gSegmentVT) s = nullptr;
    gSegmentVT[2] = reinterpret_cast<void *>(&SegRelease);
    gSegmentVT[10] = reinterpret_cast<void *>(&SegFlush);
    gSegment.vt = gSegmentVT;
    *cache = &gPersist;
    for (uint32_t seed = 0; seed < 64; ++seed)
        Pair(ctx, "Save", seed, [&](bool original, std::mt19937 &r, Blob &b) {
            Mgr *m = NewManager(ctx, r, PickEntries(r), gMgrVT);
            gSaving = m;
            gFindAnswer = (r() & 0xffffff00) | (r() % 3 ? 1 : 0);
            gGiveSegment = gFindAnswer & 1 ? true : r() % 2;
            gSaveAnswer = r() % 4 ? (r() & 0xffffff00) | (r() % 2) : r();
            gFlushAnswer = r();
            gEv.clear();
            const uint8_t res = Call<uint8_t(__thiscall *)(void *)>(ctx, original, 0x4d8e20, 0, NO_ADAPTER(Save), m);
            CaptureManager(b, m);
            FreeManager(m);
            return uint32_t(res);
        });
    *cache = saved;
}

// ---- the slots no other test calls, through each side's own vtable ----
//
// The primary face's AddRef / Release (the game's folded 0x145260 / 0x313300) and GetPersistFace (slot 6, folded 0x1c04e0), and
// the persist face's slots 1-11 (the AddRef / Release thunks 0x1448e9 / 0x2928a5, the folded key accessors and GetRefCount). The
// original side calls the game's vtable slot on a game-built manager, ours our vtable's slot on ours; Release's deletion goes
// through slot 12 of the manager's copy, a recording fake on both sides. Arguments: a key buffer (GetKey's hidden result, SetKey's
// key) or a value.
namespace {
Mgr *__fastcall MgrDelete(Mgr *, void *, uint32_t flags)
{
    Ev({0xde1, flags});
    return nullptr;
}
} // namespace

DIFF_TEST(ObjectTypeAttributeManager_Slots, 0x1c04e0)
{
    struct Case {
        unsigned face, slot, args; // args: 0 none, 1 a value, 2 a key pointer
    };
    const Case cases[] = {{0, 1, 0}, {0, 2, 0}, {0, 6, 0}, {1, 1, 0}, {1, 2, 0}, {1, 3, 2}, {1, 4, 2}, {1, 5, 0},
                          {1, 6, 0}, {1, 7, 0}, {1, 8, 1}, {1, 9, 1}, {1, 10, 1}, {1, 11, 0}};
    uint32_t seed = 0;
    for (const Case &c : cases)
        for (uint32_t k = 0; k < 24; ++k, ++seed)
            Pair(
                ctx, "Slots", seed,
                [&](bool original, std::mt19937 &r, Blob &b) {
                    Mgr *m = NewManager(ctx, r, PickEntries(r), gMgrVT);
                    gMgrVT[12] = reinterpret_cast<void *>(&MgrDelete);
                    void *const fn = original ? GameVt(c.face)[c.slot] : gOurs[c.face][c.slot];
                    void *self = reinterpret_cast<uint8_t *>(m) + kFaceOffset[c.face];
                    uint32_t key[4] = {0x5e5e5e5e, 0x5e5e5e5e, 0x5e5e5e5e, 0x5e5e5e5e};
                    if (c.args == 2 && c.slot == 4)
                        for (auto &w : key) w = r();
                    const uint32_t value = r();
                    gEv.clear();
                    uint32_t res;
                    if (c.args == 0) res = reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(self);
                    else if (c.args == 1) res = reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t)>(fn)(self, value);
                    else res = reinterpret_cast<uint32_t(__thiscall *)(void *, void *)>(fn)(self, key);
                    // Pointers as positions: the manager, its persist face, the key buffer.
                    if (res == uint32_t(uintptr_t(m))) res = 0xa000;
                    else if (res == uint32_t(uintptr_t(m)) + otam::kPersistFace) res = 0xa004;
                    else if (res == uint32_t(uintptr_t(key))) res = 0xa0c0;
                    else if (c.face == 1 && (c.slot == 4 || (c.slot >= 8 && c.slot <= 10))) res &= 0xff; // the setters answer al
                    b.insert(b.end(), key, key + 4);
                    CaptureManager(b, m);
                    FreeManager(m);
                    return res;
                },
                2);
}
