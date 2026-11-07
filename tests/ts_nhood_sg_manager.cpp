// nTSSG::cNHoodOccupantManager (lane GO, src/ts/nTSSGNHoodOccupantManager.*): difftests in the standalone shape
// (docs/standalone-shape.md, "Tests"). Fixture: tests/ts_services_fixture.h (recording fakes for the framework object, the
// property set and notifier, the message server, the scene-graph system, the scene, the root and every held node).
//
// Routes: 0 the original (object from the original constructor 0x2a4a00, function at its RVA), 1 ours (object from our
// constructor through the Construct adapter, slot called through our own vtable in the slot where the game's vtable holds the
// RVA), 2 the adapter on an object from the original constructor whose vtable copy has an arity-matched trap in that slot.
// Every case compares the result, the event log (every fake call with its normalised arguments, the property names' text),
// every word of the object (vtable words coded, the id set by content, the three vectors by size and contents, heap words
// otherwise by presence), the fakes' reference counts and the trap hits.
#include "ts_services_fixture.h"

#include "app/AppServices2.h"
#include "code_patch.h"
#include "game/game.h"
#include "sg/DetailObject.h"
#include "sg/cRenderGroupNameIterator.h"
#include "shape.h"
#include "stack_out.h"
#include "simantics/ServiceRefVector.h"
#include "ts/nTSSGNHoodOccupantManager.h"
#include "world/WorldTileMath.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <set>
#include <vector>

#include <windows.h>

namespace {

// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{1, "dd"}, {8, "dd"}, {9, "dd"}, {10, "dd"}, {11, "dd"}, {12, "dd"}, {16, "dd"}, {17, "dd"}, {18, "dd"}, {21, "dd----d"}, {22, "dd----d"}};
using namespace svcfx;
using M = nTSSG::cNHoodOccupantManager;
using A = nTSSG::cNHoodOccupantManagerAdapters;

constexpr unsigned kFaces = 3;
constexpr uint32_t kFaceOffset[kFaces] = {0, 8, 0xc};
constexpr uint32_t kGameVT[kFaces] = {0xdb33d0, 0xdb33ec, 0xdb3420};
constexpr unsigned kSlotCount[kFaces] = {6, 12, 4};
constexpr uint32_t kGameRZUnknownVT = 0xda2fd0;
constexpr uint32_t kFrameworkCell = 0x10c9378;
constexpr uint32_t kObjTag = 0xe0000000;
constexpr uint32_t kNoFace = ~0u;

enum Fake : unsigned {
    kFw, kProps, kNotifier, kMsg, kSg, kScene, kRoot, kApp12, kLayer0, kLayer1, kLayer2, kLayer3, kFlora, kRef78, kSgMember,
    kApp11, kNode0, kNode1, kNode2, kPend0, kPend1, kRef98, kRefA8, kSpare
};

// ---- the world ----

void LogRaw(const as2::RawString *s)
{
    const char *text = s->mCapacity > 15 ? s->mPtr : s->mBuf;
    Ev(s->mSize);
    for (uint32_t i = 0; i < s->mSize; ++i) Ev(uint8_t(text[i]));
}

void HandOut(void **out, Obj &o)
{
    *out = &o;
    ++o.refs;
}
// framework slot 5 (class, iid, out)
uint32_t FwQuery(Obj *, uint32_t *a)
{
    auto **out = reinterpret_cast<void **>(a[2]);
    if (a[0] == M::kPropertyClass && a[1] == M::kPropertySetIid) HandOut(out, F(kProps));
    else if (a[0] == M::kPropertyClass && a[1] == M::kPropertyNotifierIid) HandOut(out, F(kNotifier));
    else if (a[0] == M::kSgSystemIdA && a[1] == M::kSgSystemIdB) HandOut(out, F(kSg));
    else HandOut(out, F(kMsg));
    return 1;
}
// property set slot 9 (name, value): found (value[0]) writes value[1]'s low byte into the boolean
uint32_t PropsGet(Obj *o, uint32_t *a)
{
    LogRaw(reinterpret_cast<const as2::RawString *>(a[0]));
    auto *any = reinterpret_cast<uint8_t *>(a[1]);
    Ev(*reinterpret_cast<uint32_t *>(any)); // the value's vtable (the game's AnyBoolean on both sides)
    Ev(any[4]);
    if (o->value[0]) any[4] = uint8_t(o->value[1]);
    return o->value[0];
}
// property notifier slot 4 (target, id, name, 0)
uint32_t NotifierWatch(Obj *, uint32_t *a)
{
    LogRaw(reinterpret_cast<const as2::RawString *>(a[2]));
    return 0;
}

void World(Globals &g, std::mt19937 &rng)
{
    g.Set(kFrameworkCell, &F(kFw));
    // the director's framework (slot 12 of the fixture's director, GetDirectorFrameworkObject) is fixture fake gFrameworkIndex:
    // other test files set it (e.g. ts_neighbor_manager_services.cpp) and leave it, so set it here (lane GW: BuildScene failed
    // in the full suite only, its framework slot 3 then called an unset slot of another fake)
    gFrameworkIndex = kFw;
    // the game option mask (0x10faf94, read by HasOptionBits; non-zero so nothing queries it): bit 7 set or clear, other bits
    g.SetWord(0x10faf94, (rng() % 2 ? 0x80u : 0u) | 0x100u | (rng() % 2 ? 0x40u : 0u));
    g.Set(M::kSgSystemCache, rng() % 2 ? &F(kSg) : nullptr);
    F(kFw).Set(5, "ddp", 0u, &FwQuery);
    F(kProps).value[0] = rng() % 3 != 0;
    F(kProps).value[1] = rng() % 3 == 0 ? 0x100 : rng() % 2 ? 1 : 0x80;
    F(kProps).Set(9, "pp", 0u, &PropsGet);
    F(kNotifier).Set(4, "pdp-", 0u, &NotifierWatch);
    F(kNotifier).Set(6, "p", 0u);
    F(kMsg).Set(5, "pd", 0u);
    F(kMsg).Set(6, "pd", 0u);
    F(kSg).Set(33, 0, &F(kScene));
    F(kScene).Set(9, "p", rng());
    F(kRoot).Set(45, "p", rng());
    F(kRoot).Set(46, "p", rng());
    F(kApp12).Set(21, 0, rng());
}

// ---- objects ----

struct Mem {
    alignas(8) uint8_t bytes[sizeof(M)];
    M *Obj() { return reinterpret_cast<M *>(bytes); }
};

const shape::OurTables<kFaces> &OurVT() { return shape::Ours<M>(kFaceOffset); }
void *gTrapCopy[kFaces][16];
int gTrapHits;

uint32_t &W(M *m, uint32_t off) { return *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(m) + off); }
uint8_t &B(M *m, uint32_t off) { return reinterpret_cast<uint8_t *>(m)[off]; }

// The route's object on `mem` (prefilled from `fill` so untouched bytes compare): the original constructor (routes 0, 2) or
// ours through the Construct adapter (route 1).
M *Build(difftest::Context &ctx, Mem &mem, int route, uint32_t fill)
{
    for (uint32_t off = 0; off < sizeof(M); off += 4) std::memcpy(mem.bytes + off, &(fill = fill * 1664525u + 1013904223u), 4);
    AddRange(mem.bytes, sizeof(M), kObjTag);
    if (route == 1) return A::Construct(mem.Obj(), nullptr);
    return static_cast<M *>(ctx.Original<void *(__thiscall *)(void *)>(0x2a4a00)(mem.bytes));
}

// Route 2: the game's vtables copied, an arity-matched trap wherever the game's vtable holds `rva`.
void InstallTraps(M *m, uint32_t rva, unsigned words)
{
    for (unsigned f = 0; f < kFaces; ++f) {
        void **game = shape::GameVTable(kGameVT[f]);
        for (unsigned s = 0; s < kSlotCount[f]; ++s)
            gTrapCopy[f][s] = game[s] == GameAddress<void>(rva) ? shape::TrapFor<shape::Count<gTrapHits>>(words) : game[s];
        shape::VTableAt(m, kFaceOffset[f]) = gTrapCopy[f];
    }
}

uint32_t VtWord(uint32_t w)
{
    const auto &ours = OurVT();
    for (unsigned f = 0; f < kFaces; ++f)
        if (w == shape::P(shape::GameVTable(kGameVT[f])) || w == shape::P(ours.face[f]) || w == shape::P(gTrapCopy[f]))
            return shape::kFaceCode | f;
    if (w == shape::P(GameAddress<void>(kGameRZUnknownVT)) || w == shape::P(ours.destroyed)) return shape::kFaceCode | 0xff;
    return w;
}

// Writes a vector's three words over the `bytes` at `first` (capacity = size).
void SetVector(M *m, uint32_t off, void *first, uint32_t bytes)
{
    W(m, off) = shape::P(first);
    W(m, off + 4) = shape::P(first) + bytes;
    W(m, off + 8) = shape::P(first) + bytes;
}

// Seeds the held objects and flags (the same on every route).
void Seed(M *m, std::mt19937 &rng, bool nodes)
{
    auto pick = [&](Fake f) -> cIGZUnknown * { return rng() % 4 ? reinterpret_cast<cIGZUnknown *>(&F(f)) : nullptr; };
    m->mInitialized = uint8_t(rng() % 3 ? 1 : 0);
    m->mRefCount = 2 + rng() % 5;
    m->mAppSlot12.mObject = pick(kApp12);
    m->mAppSlot11.mObject = pick(kApp11);
    m->mSgSystem.mObject = pick(kSgMember);
    m->mRoot.mObject = rng() % 5 ? reinterpret_cast<cIGZUnknown *>(&F(kRoot)) : nullptr;
    m->mRef78.mObject = pick(kRef78);
    m->mFloraNode.mObject = pick(kFlora);
    for (int i = 0; i < 4; ++i) {
        m->mLayerNodes[i].mObject = pick(Fake(kLayer0 + i));
        m->mLayerVisible[i] = uint8_t(rng() % 3 ? 1 : 0);
        m->mMergeNodes[i].mObject = rng() % 3 ? nullptr : reinterpret_cast<cIGZUnknown *>(&F(kRef98));
        m->mLayerBuilders[i].mObject = rng() % 3 ? nullptr : reinterpret_cast<cIGZUnknown *>(&F(kRefA8));
    }
    m->mMergeFlora = uint8_t(rng() % 3 ? 1 : 0);
    m->mDirty = uint8_t(rng());
    if (!nodes) return;
    // the node list and the pending list (the game's code fills them; built here by hand in an operator new block, freed by
    // either side's code: both CRTs use the process heap)
    if (const unsigned n = rng() % 4) {
        auto *p = static_cast<nTSSG::SGNodeEntry *>(::operator new(n * sizeof(nTSSG::SGNodeEntry)));
        for (unsigned i = 0; i < n; ++i) {
            ::new (&p[i]) nTSSG::SGNodeEntry;
            p[i].mNode.mObject = rng() % 4 ? reinterpret_cast<cIGZUnknown *>(&F(Fake(kNode0 + i % 3))) : nullptr;
            const uint32_t v = rng();
            std::memcpy(&p[i].mFaded, &v, 4);
        }
        SetVector(m, 0x88, p, n * sizeof(nTSSG::SGNodeEntry));
    }
    if (const unsigned n = rng() % 3) {
        auto *p = static_cast<void **>(::operator new(n * 4));
        for (unsigned i = 0; i < n; ++i) p[i] = &F(Fake(kPend0 + i % 2));
        SetVector(m, 0xbc, p, n * 4);
    }
}

// A vector's size and capacity in words, then its elements by `kind`: 'p' counted pointers (+0xbc), 'n' node entries (+0x88:
// the node, then the faded byte), 't' text pairs (+0x7c: each string's text; the test's fake strings by their own text).
const char *__fastcall FakeToChar(void *self, void *);
void CaptureVector(Blob &b, const uint32_t *v, char kind)
{
    const auto *first = reinterpret_cast<const uint32_t *>(v[0]);
    const auto *last = reinterpret_cast<const uint32_t *>(v[1]);
    const auto *end = reinterpret_cast<const uint32_t *>(v[2]);
    b.push_back(first != nullptr);
    b.push_back(uint32_t(last - first));
    b.push_back(uint32_t(end - first));
    if (kind == 'p')
        for (const uint32_t *p = first; p != last; ++p) b.push_back(Norm(*p));
    if (kind == 'n')
        for (const uint32_t *p = first; p < last; p += 2) b.push_back(Norm(p[0])), b.push_back(p[1] & 0xff);
    if (kind == 't')
        for (const auto *pair = reinterpret_cast<const nTSSG::SGTextPair *>(first);
             pair != reinterpret_cast<const nTSSG::SGTextPair *>(last); ++pair)
            for (const nTSSG::SGString *text : {&pair->mFirst, &pair->mSecond}) {
                const auto *w = reinterpret_cast<const uint32_t *>(text);
                const bool fake = (*reinterpret_cast<void *const *const *>(w))[5] == reinterpret_cast<const void *>(&FakeToChar);
                const std::string chars = fake ? std::string(reinterpret_cast<const char *>(uintptr_t(w[1])))
                                               : std::string(text->mString.mString);
                b.push_back(fake ? 0xfa4e0000u : VtWord(w[0]));
                b.push_back(uint32_t(chars.size()));
                for (char c : chars) b.push_back(uint8_t(c));
            }
}

// A live object: every word, the heap ones by content.
// skipUninitialised (Create only, a fresh heap block on each side): the original constructor 0x2a4a00 never stores to
// 0x14..0x23 (mViewRect), 0xb8..0xbb (mLayerDirty) or the padding bytes 0x25..0x27, 0x61..0x63 (lane GR: these two came
// to light once other tests left the heap dirty), 0xca..0xcb and 0xd2..0xd3, so those hold heap
// leftovers. NHoodSG_Construct compares those words too, on the same poisoned buffer for both constructors, which is
// where "left alone" is checked.
// The words CaptureLive / CaptureDestroyed log by kind (svcfx::Field): 'p' pointer, 'd' data, 'n' a pointer into a block
// the destructor freed.
constexpr char kLiveKinds[] = "-d--dddd ddppp--p dddddd-d dpppppp- -----ppp ppppppd- --ddd";
constexpr char kDestroyedKinds[] = "-d--dddd ddppp-dp -----d-d dpppppp- -----ppp ppppppd- --d-d";
void CaptureLive(Blob &b, M *m, bool skipUninitialised)
{
    for (uint32_t off = 0; off < sizeof(M); off += 4) {
        const uint32_t w = W(m, off);
        if (off == 0 || off == 8 || off == 0xc) {
            b.push_back(VtWord(w));
        } else if (off == 0x34) { // the id set: its keys in order
            const auto &ids = *reinterpret_cast<const std::set<uint32_t> *>(reinterpret_cast<uint8_t *>(m) + 0x34);
            b.push_back(uint32_t(ids.size()));
            for (uint32_t id : ids) b.push_back(id);
        } else if (off == 0x38) {
        } else if (off == 0x7c || off == 0x88 || off == 0xbc) {
            CaptureVector(b, &W(m, off), off == 0x7c ? 't' : off == 0x88 ? 'n' : 'p');
        } else if (off == 0x80 || off == 0x84 || off == 0x8c || off == 0x90 || off == 0xc0 || off == 0xc4) {
        } else if (skipUninitialised && ((off >= 0x14 && off <= 0x20) || off == 0xb8)) {
        } else if (skipUninitialised && (off == 0x24 || off == 0x60)) {
            b.push_back(w & 0xff); // `mov byte ptr [esi + 0x24], 0` / `[esi + 0x60], 0` at 0x2a4a50 / 0x2a4abb: 3 bytes left
        } else if (skipUninitialised && off == 0xc8) {
            b.push_back(w & 0xffff);
        } else if (skipUninitialised && off == 0xd0) {
            b.push_back(w & 0xffff);
        } else {
            svcfx::Field(b, w, kLiveKinds, off);
        }
    }
}

// A destroyed object: the heap words by presence only (the blocks are freed).
void CaptureDestroyed(Blob &b, M *m)
{
    for (uint32_t off = 0; off < sizeof(M); off += 4) {
        const uint32_t w = W(m, off);
        if (off == 0 || off == 8 || off == 0xc) b.push_back(VtWord(w));
        else if (off == 0x34) b.push_back(w != 0);
        else svcfx::Field(b, w, kDestroyedKinds, off);
    }
}

// Tears a route's object down after the comparison (released fakes no longer matter).
void Teardown(difftest::Context &ctx, M *m, int route)
{
    m->mInitialized = 0;
    if (route == 1) m->M::~M();
    else ctx.Original<void(__thiscall *)(void *)>(0x2a50f0)(m);
}

// One case on the three routes (`routes` 2 or 3): world, object, seed, call; compared against route 0.
template <class Call> void Run(difftest::Context &ctx, const char *what, uint32_t seed, int routes, bool nodes, Call call)
{
    AllocatorScope alloc;
    Blob blobs[3];
    for (int route = 0; route < routes; ++route) {
        ResetAll();
        std::mt19937 rng(seed * 2654435761u + 7);
        Globals g;
        World(g, rng);
        OurVT(); // our vtables, read once (needs the world: our constructor asks the property set)
        gEv.clear();
        Mem mem;
        M *m = Build(ctx, mem, route, rng());
        Seed(m, rng, nodes);
        gTrapHits = 0;
        gEv.clear();
        Blob &b = blobs[route];
        b.push_back(call(m, route, rng));
        b.insert(b.end(), gEv.begin(), gEv.end());
        CaptureLive(b, m, false);
        CaptureFakes(b, kValueKinds);
        b.push_back(uint32_t(gTrapHits));
        Teardown(ctx, m, route);
    }
    for (int route = 1; route < routes; ++route) Same(ctx, blobs[0], blobs[route], what, seed + (route == 2 ? 0x80000000u : 0));
}

// The function at `rva` (a slot of face `face`, or kNoFace for a non-virtual member) on the route's way: the RVA, our vtable's
// slot, or the adapter with the trap.
template <class R, class... Args> R Call(difftest::Context &ctx, M *m, int route, unsigned face, uint32_t rva, void *adapter, Args... args)
{
    using Fn = R(__thiscall *)(void *, Args...);
    void *self = reinterpret_cast<uint8_t *>(m) + (face == kNoFace ? 0 : kFaceOffset[face]);
    if (route == 0) return ctx.Original<Fn>(rva)(self, args...);
    if (route == 1 && face != kNoFace) {
        const unsigned slot = shape::SlotOf(kGameVT[face], kSlotCount[face], rva);
        if (slot == ~0u) ctx.Fail("0x%x is in no slot of face %u", rva, face);
        return reinterpret_cast<Fn>(shape::VTableAt(m, kFaceOffset[face])[slot])(self, args...);
    }
    if (route == 2 && face != kNoFace) InstallTraps(m, rva, shape::Words<Args...>());
    return reinterpret_cast<Fn>(adapter)(self, args...);
}

#define SG_ADAPTER(name) reinterpret_cast<void *>(&A::name)

} // namespace

// ---- construction ----

DIFF_TEST(NHoodSG_Construct, 0x2a4a00)
{
    for (uint32_t seed = 0; seed < 60; ++seed) {
        AllocatorScope alloc;
        Blob blobs[2];
        for (int route = 0; route < 2; ++route) {
            ResetAll();
            std::mt19937 rng(seed * 7919u + 1);
            Globals g;
            World(g, rng);
            OurVT();
            gEv.clear();
            Mem mem;
            M *m = Build(ctx, mem, route, rng());
            Blob &b = blobs[route];
            b.insert(b.end(), gEv.begin(), gEv.end());
            CaptureLive(b, m, false);
            CaptureFakes(b, kValueKinds);
            Teardown(ctx, m, route);
        }
        Same(ctx, blobs[0], blobs[1], "Construct", seed);
    }
}

DIFF_TEST(NHoodSG_Create, 0x2a7b70)
{
    for (uint32_t seed = 0; seed < 30; ++seed) {
        AllocatorScope alloc;
        Blob blobs[2];
        for (int route = 0; route < 2; ++route) {
            ResetAll();
            std::mt19937 rng(seed * 104729u + 3);
            Globals g;
            World(g, rng);
            OurVT();
            gEv.clear();
            void *face = route == 0 ? ctx.Original<void *(__cdecl *)()>(0x2a7b70)() : M::Create();
            M *m = reinterpret_cast<M *>(static_cast<uint8_t *>(face) - 8);
            AddRange(m, sizeof(M), kObjTag);
            Blob &b = blobs[route];
            b.push_back(Norm(shape::P(face)));
            b.insert(b.end(), gEv.begin(), gEv.end());
            CaptureLive(b, m, true);
            CaptureFakes(b, kValueKinds);
            // the side's deleting destructor frees the block
            if (route == 0) ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x2a5310)(m, 1);
            else reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>(shape::VTableAt(m, 0)[3])(m, 1);
        }
        Same(ctx, blobs[0], blobs[1], "Create", seed);
    }
}

// ---- the interface ----

DIFF_TEST(NHoodSG_QueryInterface, 0x2a8da0)
{
    struct Q { uint32_t rva; unsigned face; void *adapter; };
    const Q qs[] = {{0x2a8da0, 0, SG_ADAPTER(QueryInterface)}, {0x2a8deb, 1, SG_ADAPTER(SceneQueryInterface)},
                    {0x2a8df3, 2, SG_ADAPTER(MessageQueryInterface)}};
    const uint32_t iids[] = {1, M::kSceneIid, M::kMessageTargetIid, 0x6bd0dc57, 0};
    for (uint32_t seed = 0; seed < 90; ++seed) {
        const Q &q = qs[seed % 3];
        const uint32_t iid = iids[(seed / 3) % 5];
        const bool nullOut = seed % 7 == 6;
        Run(ctx, "QueryInterface", seed, 3, false, [&](M *m, int route, std::mt19937 &) -> uint32_t {
            void *out = reinterpret_cast<void *>(0x5eed);
            const uint32_t r = Call<uint32_t>(ctx, m, route, q.face, q.rva, q.adapter, iid, nullOut ? nullptr : &out) & 0xff;
            Ev(svcfx::NormOut(shape::P(out), 0x5eed)); // left as set, or the face
            return r;
        });
    }
}

DIFF_TEST(NHoodSG_Initialize, 0x2a8100)
{
    for (uint32_t seed = 0; seed < 60; ++seed)
        Run(ctx, "Initialize", seed, 3, false, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            m->mInitialized = uint8_t(rng() % 2);
            return Call<uint32_t>(ctx, m, route, 1, 0x2a8100, SG_ADAPTER(Initialize)) & 0xff;
        });
}

DIFF_TEST(NHoodSG_Shutdown, 0x2a92c0)
{
    for (uint32_t seed = 0; seed < 120; ++seed)
        Run(ctx, "Shutdown", seed, 3, true, [&](M *m, int route, std::mt19937 &) -> uint32_t {
            return Call<uint32_t>(ctx, m, route, 1, 0x2a92c0, SG_ADAPTER(Shutdown)) & 0xff;
        });
}

DIFF_TEST(NHoodSG_SetLayerVisible, 0x2a7af0)
{
    for (uint32_t seed = 0; seed < 120; ++seed)
        Run(ctx, "SetLayerVisible", seed, 3, false, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            m->mRoot.mObject = reinterpret_cast<cIGZUnknown *>(&F(kRoot)); // a layer node always has a root (the slot uses it)
            const uint32_t index = rng() % 4;
            const uint32_t visible = (rng() % 3 ? 1u : 0u) | (rng() & 0xff00u); // the slot reads the low byte only
            Call<void>(ctx, m, route, 1, 0x2a7af0, SG_ADAPTER(SetLayerVisible), index, visible);
            return 0;
        });
}

DIFF_TEST(NHoodSG_Dirty, 0x2a7b30)
{
    for (uint32_t seed = 0; seed < 40; ++seed)
        Run(ctx, "Dirty", seed, 3, false, [&](M *m, int route, std::mt19937 &) -> uint32_t {
            if (seed % 2) Call<void>(ctx, m, route, 1, 0x2a7b30, SG_ADAPTER(MarkDirty));
            else Call<void>(ctx, m, route, 1, 0x2a7660, SG_ADAPTER(ClearDirty));
            return 0;
        });
}

DIFF_TEST(NHoodSG_DetachScene, 0x2a56d0)
{
    for (uint32_t seed = 0; seed < 150; ++seed)
        Run(ctx, "DetachScene", seed, 3, true, [&](M *m, int route, std::mt19937 &) -> uint32_t {
            Call<void>(ctx, m, route, kNoFace, 0x2a56d0, shape::MemberCode(&M::DetachScene));
            return 0;
        });
}

// Every slot of the three faces that no other test calls (the inherited cRZUnknown slots, the folded AddRef / Release and
// their thunks), on the game's vtable (route 0) and ours (route 1).
DIFF_TEST(NHoodSG_Slots, 0x1f9aa5)
{
    struct S { unsigned face, slot; };
    const S slots[] = {{0, 1}, {0, 2}, {0, 4}, {0, 5}, {1, 1}, {1, 2}, {2, 1}, {2, 2}};
    for (uint32_t seed = 0; seed < 80; ++seed) {
        const S &s = slots[seed % 8];
        Run(ctx, "Slots", seed, 2, false, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            m->mRefCount = rng() % 4 + (s.slot == 2 ? 2 : 0); // Release stays above 1 (no delete on the stack)
            void *self = reinterpret_cast<uint8_t *>(m) + kFaceOffset[s.face];
            void *fn = route == 0 ? shape::GameVTable(kGameVT[s.face])[s.slot] : shape::VTableAt(m, kFaceOffset[s.face])[s.slot];
            return reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(self);
        });
    }
}

// ---- destruction ----

namespace {
void **ImportSlot(uint8_t *base, const char *name)
{
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto *d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); d->Name != 0; ++d) {
        if (_stricmp(reinterpret_cast<const char *>(base + d->Name), "KERNEL32.dll") != 0) continue;
        const auto *names = reinterpret_cast<const IMAGE_THUNK_DATA32 *>(base + d->OriginalFirstThunk);
        auto *slots = reinterpret_cast<IMAGE_THUNK_DATA32 *>(base + d->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) continue;
            const auto *byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char *>(byName->Name), name) == 0) return reinterpret_cast<void **>(&slots->u1.Function);
        }
    }
    return nullptr;
}
const void *gWatched;
uint32_t gFreed;
decltype(&HeapFree) gRealFree;
BOOL WINAPI WatchingHeapFree(HANDLE heap, DWORD flags, LPVOID block)
{
    if (block && block == gWatched) ++gFreed;
    return gRealFree(heap, flags, block);
}
// HeapFree wrapped in the image's import table and this executable's (copied from tests/ts_nhood_occupants.cpp, lane FY).
struct FreeWatch {
    void **slots[2]{};
    void *saved[2]{};
    explicit FreeWatch(difftest::Context &ctx)
    {
        slots[0] = ImportSlot(ctx.image.At<uint8_t>(0), "HeapFree");
        slots[1] = ImportSlot(reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr)), "HeapFree");
        if (!slots[0] || !slots[1]) ctx.Fail("free watch: no HeapFree import slot");
        gRealFree = reinterpret_cast<decltype(&HeapFree)>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "HeapFree"));
        for (int i = 0; i < 2; ++i) {
            if (!slots[i]) continue;
            saved[i] = *slots[i];
            Write(slots[i], reinterpret_cast<void *>(&WatchingHeapFree));
        }
    }
    ~FreeWatch()
    {
        for (int i = 1; i >= 0; --i)
            if (slots[i]) Write(slots[i], saved[i]);
    }
    FreeWatch(const FreeWatch &) = delete;
    FreeWatch &operator=(const FreeWatch &) = delete;
    static void Write(void **slot, void *value)
    {
        DWORD old;
        VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old);
        *slot = value;
        VirtualProtect(slot, sizeof(void *), old, &old);
    }
};
} // namespace

// The destructor 0x2a50f0 (through the Destruct adapter) and the deleting destructor 0x2a5310 with flags 0 (our primary slot 3,
// or its adapter with a trap there), on seeded objects with texts and nodes; then flags 1 on heap objects (freed or not).
DIFF_TEST(NHoodSG_Destruct, 0x2a5310)
{
    for (uint32_t seed = 0; seed < 160; ++seed) {
        AllocatorScope alloc;
        const bool viaDestructor = seed % 4 == 3;
        Blob blobs[3];
        for (int route = 0; route < 3; ++route) {
            ResetAll();
            std::mt19937 rng(seed * 31337u + 5);
            Globals g;
            World(g, rng);
            OurVT();
            Mem mem;
            M *m = Build(ctx, mem, route, rng());
            Seed(m, rng, true);
            if (const unsigned texts = rng() % 3) {
                auto *p = static_cast<nTSSG::SGTextPair *>(::operator new(texts * sizeof(nTSSG::SGTextPair)));
                for (unsigned i = 0; i < texts; ++i) {
                    ::new (&p[i]) nTSSG::SGTextPair;
                    p[i].mFirst.mString.mString.assign(rng() % 2 ? "short" : "a text longer than fifteen characters");
                    p[i].mSecond.mString.mString.assign(rng() % 2 ? "b" : "another text longer than fifteen");
                }
                SetVector(m, 0x7c, p, texts * sizeof(nTSSG::SGTextPair));
            }
            gTrapHits = 0;
            gEv.clear();
            Blob &b = blobs[route];
            if (viaDestructor) {
                if (route == 0) ctx.Original<void(__thiscall *)(void *)>(0x2a50f0)(m);
                else {
                    if (route == 2) InstallTraps(m, 0x2a5310, 1);
                    A::Destruct(m, nullptr);
                }
            } else {
                b.push_back(Norm(Call<uint32_t>(ctx, m, route, 0, 0x2a5310, SG_ADAPTER(DeletingDestructor), 0u)));
            }
            b.insert(b.end(), gEv.begin(), gEv.end());
            CaptureDestroyed(b, m);
            CaptureFakes(b, kValueKinds);
            b.push_back(uint32_t(gTrapHits));
        }
        Same(ctx, blobs[0], blobs[1], "Destruct", seed);
        Same(ctx, blobs[0], blobs[2], "Destruct", seed + 0x80000000u);
    }
    // flags: a heap object from each side's creator, freed exactly when bit 0 is set
    for (uint32_t flags : {0u, 1u, 2u, 3u}) {
        AllocatorScope alloc;
        uint32_t freed[3]{};
        for (int route = 0; route < 3; ++route) {
            ResetAll();
            std::mt19937 rng(flags + 99);
            Globals g;
            World(g, rng);
            OurVT();
            void *face = route == 1 ? M::Create() : ctx.Original<void *(__cdecl *)()>(0x2a7b70)();
            M *m = reinterpret_cast<M *>(static_cast<uint8_t *>(face) - 8);
            {
                FreeWatch watch(ctx);
                gWatched = m;
                gFreed = 0;
                Call<uint32_t>(ctx, m, route, 0, 0x2a5310, SG_ADAPTER(DeletingDestructor), flags);
                freed[route] = gFreed;
                gWatched = nullptr;
            }
            if (!(flags & 1)) ::operator delete(m); // the block itself (either heap: both CRTs use the process heap)
        }
        ++ctx.cases;
        if (freed[0] != ((flags & 1) ? 1u : 0u) || freed[1] != freed[0] || freed[2] != freed[0])
            ctx.Fail("DeletingDestructor flags %u: freed %u / %u / %u", flags, freed[0], freed[1], freed[2]);
    }
}

// ---- Update: the merge helpers and slot 11 stood in for (slot 11 is reached through the object's vtable: the game's
// 0x2a9610 on routes 0 / 2, our UpdateFade on route 1; both stood in) ----

namespace {
void __fastcall StandMergeA(void *self, void *) { Ev(0x71); Ev(Norm(shape::P(self))); }
void __fastcall StandMergeB(void *self, void *) { Ev(0x72); Ev(Norm(shape::P(self))); }
uint32_t __fastcall StandSlot11(void *self, void *, uint32_t view)
{
    Ev(0x73);
    Ev(Norm(shape::P(self)));
    Ev(Norm(view));
    return 0;
}
} // namespace

DIFF_TEST(NHoodSG_Update, 0x2a86a0)
{
    shape::StandIn a(0x2a5860, &M::ClearLayers1And2, &StandMergeA);
    shape::StandIn b(0x2a5d70, &M::RebuildMergedLayers, &StandMergeB);
    shape::CodePatch c(0x2a9610, reinterpret_cast<void *>(&StandSlot11));
    for (uint32_t seed = 0; seed < 80; ++seed)
        Run(ctx, "Update", seed, 3, false, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            m->mBuildAll = uint8_t(rng() % 3 ? 1 : 0);
            m->mFloraPending = uint8_t(rng() % 3 ? 1 : 0);
            if (rng() % 4) F(kScene).Set(18, 0, &F(kSpare));
            else F(kScene).Set(18, 0, 0u);
            // our vtable's slot 11 is our UpdateFade (route 1): the same stand-in over its body
            std::unique_ptr<shape::CodePatch> ours;
            if (route == 1)
                ours = std::make_unique<shape::CodePatch>(shape::VirtualMemberCode(&M::UpdateFade, OurVT().face[1]),
                    reinterpret_cast<void *>(&StandSlot11));
            Call<void>(ctx, m, route, 1, 0x2a86a0, SG_ADAPTER(Update));
            return 0;
        });
}

// ---- messages and the occupant helpers ----
// Own fakes beside the fixture's (all of whose 24 are taken): the message, its standard face, the object it carries, the
// occupant, the cached singleton and its mode object, the shape registry, the occupant's node and the props' shape nodes.
// They are normalised by range (tag 0xd1nn0000) and their reference counts go into the log after the call.
// Stood in on both sides (still game code, or the recursion over a node's children whose slots 78 / 80 are past the
// fixture's 80): 0x2a7500 (add an occupant), 0x2a8e70 (remove one), 0x2a55e0 (colour a node), 0x2a5560 (node flags),
// 0xaeb370 (the shape registry), and BuildScene 0x2a8780 (reached through the vtable).

namespace {

enum Mine : unsigned {
    mMessage, mStandard, mData, mOccupant, mSingleton, mMode, mRegistry, mNode, mShape,
    mLotManager, mLot, mState, mEditor, mOwner, mHood, mSource11, mOther0, mOther1, mMineCount
};
svcfx::Obj gMine[mMineCount];
svcfx::Obj &X(unsigned i) { return gMine[i]; }
int32_t gPropValue[2];

void ResetMine()
{
    for (unsigned i = 0; i < mMineCount; ++i) {
        gMine[i].Reset(300 + i);
        AddRange(&gMine[i], sizeof(svcfx::Obj), 0xd1000000u + (i << 16));
    }
    AddRange(gPropValue, sizeof(gPropValue), 0xd2000000u);
}
void CaptureMine()
{
    for (const svcfx::Obj &o : gMine) Ev(uint32_t(o.refs));
}

// slot 0 (iid, out): value[0] says whether it answers, value[1] the fake index (in gMine) it hands out
uint32_t MineQuery(Obj *o, uint32_t *a)
{
    if (!o->value[0]) return 0;
    HandOut(reinterpret_cast<void **>(a[1]), X(o->value[1]));
    return o->value[0] | 0x100; // only al is read
}
// the shape registry: its own vtable (the fixture's dispatch would log the name's address, which differs by side); slot 3
// (name) logs the text and answers the bit
uint32_t gRegistryBit;
uint32_t __fastcall RegistryIndex(void *, void *, const char *name)
{
    Ev(0x88);
    for (const char *p = name; *p; ++p) Ev(uint8_t(*p));
    return gRegistryBit;
}
void *gRegistryVT[4] = {nullptr, nullptr, nullptr, reinterpret_cast<void *>(&RegistryIndex)};
void *gRegistry[1] = {gRegistryVT};

void __fastcall StandAdd(void *self, void *, void *occupant) { Ev(0x81); Ev(Norm(shape::P(self))); Ev(Norm(shape::P(occupant))); }
void __fastcall StandRemove(void *self, void *, void *occupant) { Ev(0x82); Ev(Norm(shape::P(self))); Ev(Norm(shape::P(occupant))); }
void *__cdecl StandRegistry() { Ev(0x83); return gRegistry; }
void __cdecl StandColor(void *node, uint32_t arg, const float *rgba)
{
    Ev(0x84);
    Ev(Norm(shape::P(node)));
    Ev(arg);
    for (int i = 0; i < 4; ++i) Ev(std::bit_cast<uint32_t>(rgba[i]));
}
void __cdecl StandFlags(void *node, uint32_t set, uint32_t clear) { Ev(0x85); Ev(Norm(shape::P(node))); Ev(set); Ev(clear); }
uint32_t __fastcall StandBuild(void *self, void *, uint32_t all) { Ev(0x86); Ev(Norm(shape::P(self))); Ev(all); return 0; }

// The text pairs at +0x7c (game code fills them with cRZStrings): fake strings whose slot 5 (ToChar) gives "<pair><a|b>";
// the vector is emptied again (and its block freed) when the case ends, before the object's destructor would destroy them.
const char *__fastcall FakeToChar(void *self, void *)
{
    Ev(0x87);
    Ev(Norm(shape::P(self)));
    return static_cast<const char *>(static_cast<void **>(self)[1]);
}
void *gFakeStringVT[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void *>(&FakeToChar)};
char gFakeText[4][2][4];
struct FakeTexts {
    M *m;
    void **block = nullptr;
    FakeTexts(M *obj, unsigned n) : m(obj)
    {
        if (!n) return;
        block = static_cast<void **>(::operator new(n * 0x40));
        std::memset(block, 0, n * 0x40);
        AddRange(block, n * 0x40, 0xd3000000u);
        for (unsigned i = 0; i < n; ++i)
            for (unsigned k = 0; k < 2; ++k) {
                std::snprintf(gFakeText[i][k], 4, "%u%c", i, 'a' + k);
                block[i * 16 + k * 8] = gFakeStringVT;
                block[i * 16 + k * 8 + 1] = gFakeText[i][k];
            }
        SetVector(m, 0x7c, block, n * 0x40);
    }
    ~FakeTexts()
    {
        W(m, 0x7c) = W(m, 0x80) = W(m, 0x84) = 0;
        ::operator delete(block);
    }
};
// ref78's slot 34 (second, first): the two texts
uint32_t Ref78Text(Obj *, uint32_t *a)
{
    for (int k = 0; k < 2; ++k)
        for (const char *p = reinterpret_cast<const char *>(a[k]); *p; ++p) Ev(uint8_t(*p));
    return 0;
}

struct MessageStandIns {
    shape::StandIn add{0x2a7500, &M::AddOccupant, &StandAdd};
    shape::StandIn remove{0x2a8e70, &M::RemoveOccupant, &StandRemove};
    shape::StandIn registry{0xaeb370, &nGZSceneGraph::RenderGroupRegistry, &StandRegistry};
    shape::StandIn color{0x2a55e0, &M::SetNodeColor, &StandColor};
    shape::StandIn flags{0x2a5560, &M::ChangeNodeFlags, &StandFlags};
    shape::CodePatch build{0x2a8780, reinterpret_cast<void *>(&StandBuild)};
};

// The occupant world: an occupant of type 0-3 with a node or none, the registry's bit (above 31 now and then: the shift
// takes it mod 32).
void OccupantWorld(std::mt19937 &rng)
{
    const uint32_t types[] = {0, 1, 2, 3};
    X(mOccupant).Set(0, 2, 0u);
    X(mOccupant).Set(4, 0, types[rng() % 4]);
    X(mOccupant).Set(11, 0, rng() % 3 ? static_cast<const void *>(&X(mNode)) : nullptr);
    gRegistryBit = rng() % 5 ? rng() % 32 : 32 + rng() % 8;
}

} // namespace

DIFF_TEST(NHoodSG_DoMessage, 0x2a7670)
{
    MessageStandIns stand;
    const uint32_t ids[] = {0x0d6429da, 0x4c095286, 0x2c7812db, 0x4d501e65, 0x6d501e6e, 0x2d5c6e19, 0xed5c6df0,
                            0xb23b7d66, 0xb23b7d6f, 0x522ced95, 0x6fbda451, 0x6fbda450, 0x12345678};
    constexpr unsigned kIds = sizeof(ids) / sizeof(ids[0]);
    for (uint32_t seed = 0; seed < 600; ++seed)
        Run(ctx, "DoMessage", seed, 3, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            Globals g;
            g.Set(0x10cc288, &X(mSingleton));
            const uint32_t id = ids[seed % kIds];
            m->mDirty = uint8_t(rng() % 3 ? 1 : 0);
            m->mBuildAll = uint8_t(rng() % 3 ? 0 : 1);
            m->mFloraPending = uint8_t(rng() % 2);
            // the message and its standard face
            X(mMessage).Set(0, "dp", 0u, &MineQuery);
            X(mMessage).value[0] = rng() % 10 != 0;
            X(mMessage).value[1] = mStandard;
            X(mStandard).Set(4, 0, id);
            X(mStandard).Set(32, 0, &X(mData));
            X(mStandard).Set(9, 0, rng());
            X(mStandard).Set(13, 0, rng() % 4 ? 1u : 0x100u);
            gPropValue[0] = int32_t(rng());
            gPropValue[1] = rng() % 3 ? 0 : int32_t(rng() % 200) - 100;
            X(mStandard).Set(15, 0, rng() % 5 ? static_cast<const void *>(gPropValue) : nullptr);
            X(mStandard).Set(14, 0, rng() % 3 ? 1u : rng() % 2 ? 0u : 0x101u);
            X(mData).Set(0, "dp", 0u, &MineQuery);
            X(mData).value[0] = rng() % 5 != 0;
            X(mData).value[1] = mOccupant;
            OccupantWorld(rng);
            X(mSingleton).Set(33, 0, &X(mMode));
            X(mMode).Set(17, 0, rng() % 3 ? 3u : 2u);
            X(mMode).Set(31, 0, rng() % 2 ? 1u : 0u);
            // the ref78 setting and texts
            F(kRef78).Set(34, "pp", 0u, &Ref78Text);
            F(kRef78).Set(35, "p", 0u);
            // the props' nodes (the fade loop): never null (the original asks every one), each answering its shape or not
            for (unsigned i = 0; i < 3; ++i) {
                F(Fake(kNode0 + i)).Set(0, "dp", 0u, &MineQuery);
                F(Fake(kNode0 + i)).value[0] = rng() % 4 != 0;
                F(Fake(kNode0 + i)).value[1] = mShape;
            }
            auto *nodes = reinterpret_cast<nTSSG::SGNodeEntry *>(W(m, 0x88));
            for (auto *e = nodes, *end = reinterpret_cast<nTSSG::SGNodeEntry *>(W(m, 0x8c)); e != end; ++e)
                if (!e->mNode.mObject) e->mNode.mObject = reinterpret_cast<cIGZUnknown *>(&F(kNode2));
            const unsigned texts = rng() % 4;
            FakeTexts fake(m, texts);
            // our vtable's BuildScene (route 1) stood in as the image's
            shape::CodePatch oursBuild(shape::VirtualMemberCode(&M::BuildScene, OurVT().face[1]),
                reinterpret_cast<void *>(&StandBuild));
            void *message = seed % 23 == 22 ? nullptr : &X(mMessage);
            const uint32_t r = Call<uint32_t>(ctx, m, route, 2, 0x2a7670, SG_ADAPTER(DoMessage), message) & 0xff;
            CaptureMine();
            return r;
        });
}

// The message helpers called directly (DoMessage reaches each too): RefreshOccupant 0x2a9ba0, HighlightOccupant 0x2a8040,
// ClearOccupantHighlight 0x2a8e30, ApplyTexts 0x2a9260, ApplyRef78Setting 0x2a8020; the original at its RVA, ours as our
// member.
DIFF_TEST(NHoodSG_MessageHelpers, 0x2a9ba0)
{
    MessageStandIns stand;
    for (uint32_t seed = 0; seed < 400; ++seed)
        Run(ctx, "MessageHelpers", seed, 2, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            OccupantWorld(rng);
            F(kRef78).Set(34, "pp", 0u, &Ref78Text);
            F(kRef78).Set(35, "p", 0u);
            FakeTexts fake(m, rng() % 4);
            auto *occupant = reinterpret_cast<cIGZUnknown *>(&X(mOccupant));
            const uint32_t argb = rng() % 4 ? rng() : rng() % 2 ? 0xffffffffu : 0u;
            switch (seed % 5) {
            case 0: Call<void>(ctx, m, route, kNoFace, 0x2a9ba0, shape::MemberCode(&M::RefreshOccupant), occupant); break;
            case 1: Call<void>(ctx, m, route, kNoFace, 0x2a8040, shape::MemberCode(&M::HighlightOccupant), occupant, argb); break;
            case 2: Call<void>(ctx, m, route, kNoFace, 0x2a8e30, shape::MemberCode(&M::ClearOccupantHighlight), occupant); break;
            case 3: Call<void>(ctx, m, route, kNoFace, 0x2a9260, shape::MemberCode(&M::ApplyTexts)); break;
            default: Call<void>(ctx, m, route, kNoFace, 0x2a8020, shape::MemberCode(&M::ApplyRef78Setting)); break;
            }
            CaptureMine();
            return 0;
        });
}

// ---- SetMergeFlora (slot 9) ----
// Stood in: 0x2a7f20 (an occupant's layer: here the fake's value[2], -1 for null), 0x2a5340 (merge an occupant) and
// 0x2a9100 (hand a pending node on). App slot 12's slot 11 fills the counted vector through our ServiceRefVector::PushBack
// (the allocator service's block, freed by either side's 0x1f1f70 / ReleaseAndFree).

namespace {
int32_t __cdecl StandLayerOf(void *occupant)
{
    Ev(0x89);
    Ev(Norm(shape::P(occupant)));
    return occupant ? int32_t(static_cast<Obj *>(occupant)->value[2]) : -1;
}
void __fastcall StandMerge(void *self, void *, void *occupant) { Ev(0x8a); Ev(Norm(shape::P(self))); Ev(Norm(shape::P(occupant))); }
void __fastcall StandHandOn(void *self, void *, void *node) { Ev(0x8b); Ev(Norm(shape::P(self))); Ev(Norm(shape::P(node))); }
unsigned gOccupantCount;
uint32_t AppOccupants(Obj *, uint32_t *a)
{
    auto *v = reinterpret_cast<ServiceRefVector *>(a[0]);
    const unsigned picks[] = {mOccupant, mNode, mShape};
    for (unsigned i = 0; i < gOccupantCount; ++i) v->PushBack(&X(picks[i % 3]));
    return 0;
}
} // namespace

DIFF_TEST(NHoodSG_SetMergeFlora, 0x2a93c0)
{
    shape::StandIn layer(0x2a7f20, &M::LayerOf, &StandLayerOf);
    shape::StandIn merge(0x2a5340, &M::MergeOccupant, &StandMerge);
    shape::StandIn handOn(0x2a9100, &M::HandOnPending, &StandHandOn);
    for (uint32_t seed = 0; seed < 400; ++seed)
        Run(ctx, "SetMergeFlora", seed, 3, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            m->mRoot.mObject = reinterpret_cast<cIGZUnknown *>(&F(kRoot));
            m->mAppSlot12.mObject = reinterpret_cast<cIGZUnknown *>(&F(kApp12));
            F(kApp12).Set(11, "p", 0u, &AppOccupants);
            gOccupantCount = rng() % 5;
            for (unsigned i : {mOccupant, mNode, mShape}) X(i).value[2] = rng() % 4;
            F(kRef98).Set(5, 0, 0u);
            F(kRefA8).Set(34, 0, 0u);
            for (int i = 0; i < 4; ++i) {
                m->mLayerDirty[i] = uint8_t(rng() % 2 ? 0 : 1 + rng() % 3);
                if (m->mLayerDirty[i]) m->mMergeNodes[i].mObject = reinterpret_cast<cIGZUnknown *>(&F(kRef98));
                if (m->mLayerDirty[i]) m->mLayerBuilders[i].mObject = reinterpret_cast<cIGZUnknown *>(&F(kRefA8));
            }
            const uint32_t merge = (rng() % 2) | (rng() % 4 == 0 ? 0x200u : 0u); // the slot reads the low byte
            Call<void>(ctx, m, route, 1, 0x2a93c0, SG_ADAPTER(SetMergeFlora), merge);
            CaptureMine();
            return 0;
        });
}

// ---- UpdateFade (slot 11) ----
// Fakes with float results and slots past the fixture's 80 (VObj, one vtable of 160 slots for every role): the camera
// (slots 69 inside, 75-78 bounds, through x87), the view (slot 140 position), the props' shapes (slot 0 QI for the detail
// face, slot 72 box), the detail objects (slot 89 level) and the detail registry (slot 43 limit). The props' nodes are the
// fixture's kNode0-2 (slot 0 hands out a shape). Stood in: 0x7ddd30 (the ground point; it asks the terrain), 0xaead80 (the
// detail registry) and the MessageStandIns. 0x19d340 (box contains point, pure) runs on both sides.

namespace {
struct VObj {
    void **vt;
    uint32_t id;
    int32_t refs;
    uint32_t answer; // QI result / camera inside / level
    VObj *hand;      // what QI hands out
    float val[6];
};
void *gVObjVT[160];
VObj gV[8]; // 0 camera, 1 view, 2-4 shapes, 5-7 detail objects
VObj gDetailRegistry;

uint32_t __fastcall VQuery(VObj *self, void *, uint32_t iid, void **out)
{
    Ev(0x90);
    Ev(self->id);
    Ev(iid);
    if (!self->answer) return 0;
    *out = self->hand;
    ++self->hand->refs;
    return self->answer;
}
uint32_t __fastcall VAddRef(VObj *self, void *) { return uint32_t(++self->refs); }
uint32_t __fastcall VRelease(VObj *self, void *) { return uint32_t(--self->refs); }
uint32_t __fastcall VAnswer(VObj *self, void *) { Ev(0x91); Ev(self->id); return self->answer; }
uint32_t __fastcall VInside(VObj *self, void *, const float *point)
{
    Ev(0x92);
    for (int i = 0; i < 3; ++i) Ev(std::bit_cast<uint32_t>(point[i]));
    return self->answer;
}
template <int I> float __fastcall VBound(VObj *self, void *) { Ev(0x93); Ev(I); return self->val[I]; }
uint32_t __fastcall VBox(VObj *self, void *, float *box)
{
    Ev(0x94);
    Ev(self->id);
    for (int i = 0; i < 6; ++i) box[i] = self->val[i];
    return 0;
}
uint32_t __fastcall VPosition(VObj *self, void *, float *out, uint32_t arg)
{
    Ev(0x95);
    Ev(arg);
    for (int i = 0; i < 3; ++i) out[i] = self->val[i];
    return 0;
}
float *__cdecl StandGround(float *out, const float *pos)
{
    Ev(0x96);
    for (int i = 0; i < 3; ++i) Ev(std::bit_cast<uint32_t>(pos[i]));
    out[0] = pos[0];
    out[1] = pos[1];
    out[2] = pos[2] + 1.0f;
    return out;
}
void *__cdecl StandDetailRegistry() { Ev(0x97); return &gDetailRegistry; }
// the framework's slot 5: the camera service for its ids (the cache 0x10cc45c empty), the world's answers otherwise
uint32_t FwCamera(Obj *o, uint32_t *a)
{
    if (a[0] != 0x4a037ee3 || a[1] != 0x49ff7d6f) return FwQuery(o, a);
    *reinterpret_cast<void **>(a[2]) = &gV[0];
    ++gV[0].refs;
    return 1;
}
// the fixture's node slot 0 (iid, out): hands out a shape
uint32_t NodeShape(Obj *o, uint32_t *a)
{
    if (!o->value[0]) return 0;
    VObj *shape = &gV[o->value[1]];
    *reinterpret_cast<void **>(a[1]) = shape;
    ++shape->refs;
    return 1;
}

void FadeWorld(std::mt19937 &rng)
{
    gVObjVT[0] = reinterpret_cast<void *>(&VQuery);
    gVObjVT[1] = reinterpret_cast<void *>(&VAddRef);
    gVObjVT[2] = reinterpret_cast<void *>(&VRelease);
    gVObjVT[43] = reinterpret_cast<void *>(&VAnswer);
    gVObjVT[89] = reinterpret_cast<void *>(&VAnswer);
    gVObjVT[69] = reinterpret_cast<void *>(&VInside);
    gVObjVT[72] = reinterpret_cast<void *>(&VBox);
    gVObjVT[75] = reinterpret_cast<void *>(&VBound<0>);
    gVObjVT[76] = reinterpret_cast<void *>(&VBound<1>);
    gVObjVT[77] = reinterpret_cast<void *>(&VBound<2>);
    gVObjVT[78] = reinterpret_cast<void *>(&VBound<3>);
    gVObjVT[140] = reinterpret_cast<void *>(&VPosition);
    auto unit = [&] { return float(rng() % 10000) / 10000.0f; };
    for (unsigned i = 0; i < 8; ++i) {
        gV[i] = VObj{gVObjVT, 400 + i, 100, 0, nullptr, {}};
        AddRange(&gV[i], sizeof(VObj), 0xd4000000u + (i << 16));
    }
    gDetailRegistry = VObj{gVObjVT, 420, 100, 1 + rng() % 4, nullptr, {}};
    // camera: inside (1 in 4), bounds min 0-50, max = min + 0-100 (minX, minY, maxX, maxY in slots 75-78)
    VObj &camera = gV[0];
    camera.answer = rng() % 4 == 0;
    camera.val[0] = unit() * 50;
    camera.val[1] = unit() * 50;
    camera.val[2] = camera.val[0] + unit() * 100;
    camera.val[3] = camera.val[1] + unit() * 100;
    // view position: x, y over -20..170, z 0..40
    VObj &view = gV[1];
    view.val[0] = unit() * 190 - 20;
    view.val[1] = unit() * 190 - 20;
    view.val[2] = unit() * 40;
    // shapes: a detail face or not, boxes near the view
    for (unsigned i = 2; i < 5; ++i) {
        VObj &shape = gV[i];
        shape.answer = rng() % 3 != 0;
        shape.hand = &gV[i + 3];
        const float cx = view.val[0] + unit() * 80 - 40, cy = view.val[1] + unit() * 80 - 40;
        shape.val[0] = cx;
        shape.val[1] = cy;
        shape.val[2] = unit() * 5;
        shape.val[3] = cx + unit() * 20;
        shape.val[4] = cy + unit() * 20;
        shape.val[5] = rng() % 8 == 0 ? -unit() * 5 : unit() * 30;
        if (rng() % 10 == 0) { // the view in the box
            shape.val[0] = view.val[0] - 1;
            shape.val[1] = view.val[1] - 1;
            shape.val[2] = view.val[2] - 1;
            shape.val[3] = view.val[0] + 1;
            shape.val[4] = view.val[1] + 1;
            shape.val[5] = view.val[2] + 1;
        }
        gV[i + 3].answer = rng() % 6; // the level
    }
}
} // namespace

DIFF_TEST(NHoodSG_UpdateFade, 0x2a9610)
{
    MessageStandIns stand;
    shape::StandIn ground(0x7ddd30, &worldmath::WithTileValue, &StandGround);
    shape::StandIn detail(0xaead80, &nGZSceneGraph::GetDetailObject, &StandDetailRegistry);
    for (uint32_t seed = 0; seed < 800; ++seed)
        Run(ctx, "UpdateFade", seed, 3, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            FadeWorld(rng);
            Globals g;
            g.Set(0x10cc45c, rng() % 8 ? &gV[0] : nullptr);
            F(kFw).Set(5, "ddp", 0u, &FwCamera);
            m->mBuildAll = uint8_t(rng() % 6 == 0);
            const uint32_t fades[] = {0, 2500, 900, 400, 10000, 0x80000400u, 1};
            m->mFadeDistanceSq = fades[rng() % 7];
            if (rng() % 3 == 0) {
                // Props a hair inside the fade distance (100): x distance 100 - j ulps (2^-17), so (d / fade)^4 lands
                // 0-400 ulps below 1.0, either side of the original's 32-ulp "nearly one" compare. The view (0, 50, 10)
                // is outside the camera bounds (x 600-1000) and inside each box's y range; the boxes reach z 30.
                m->mFadeDistanceSq = 10000;
                gV[0].answer = 0;
                const float bounds[4] = {600, 0, 1000, 100};
                for (unsigned k = 0; k < 4; ++k) gV[0].val[k] = bounds[k];
                gV[1].val[0] = 0;
                gV[1].val[1] = 50;
                gV[1].val[2] = 10;
                for (unsigned i = 2; i < 5; ++i) {
                    const float x = -(100.0f - float(rng() % 41) * 0x1p-17f);
                    const float box[6] = {x, 40, 0, x, 60, 30};
                    for (unsigned k = 0; k < 6; ++k) gV[i].val[k] = box[k];
                }
            }
            for (unsigned i = 0; i < 3; ++i) {
                F(Fake(kNode0 + i)).Set(0, "dp", 0u, &NodeShape);
                F(Fake(kNode0 + i)).value[0] = rng() % 5 != 0;
                F(Fake(kNode0 + i)).value[1] = 2 + i;
            }
            auto *nodes = reinterpret_cast<nTSSG::SGNodeEntry *>(W(m, 0x88));
            for (auto *e = nodes, *end = reinterpret_cast<nTSSG::SGNodeEntry *>(W(m, 0x8c)); e != end; ++e) {
                if (!e->mNode.mObject) e->mNode.mObject = reinterpret_cast<cIGZUnknown *>(&F(kNode1));
                e->mFaded = uint8_t(rng() % 2);
            }
            void *view = rng() % 10 ? &gV[1] : nullptr;
            Call<void>(ctx, m, route, 1, 0x2a9610, SG_ADAPTER(UpdateFade), view);
            for (const VObj &v : gV) Ev(uint32_t(v.refs));
            Ev(Norm(*GameAddress<uint32_t>(0x10cc45c)));
            return 0;
        });
}

// ---- BuildScene (slot 5) ----
// The root and the four layer nodes come from the framework's slot 3 (kRoot, kLayer0-3 in turn); the root's slot 81 is
// past the fixture's 80, so kRoot runs on a longer copy of its vtable. Stood in: 0x211da3 (the state service), 0x2a8570
// (footprint in a rectangle: a fixed answer per footprint and mode; the rectangle's words logged when it is set), 0x2a81f0,
// 0x2a5a40, 0x2a5d70 (this only), 0x2a5ef0, 0x2a63a0 (this, occupant), 0xae7e60 (the root), with SetMergeFlora's 0x2a5340
// and 0x2a9100. DetachScene (ours / the image's) runs for real. The neighbourhood names stay within 15 characters (a longer
// one would put a heap pointer, different per side, into the string's words).

namespace {
unsigned gCreated;
bool gRectSet;
uint32_t gFootprintAnswers;
uint32_t FwCreate(Obj *, uint32_t *a)
{
    const Fake made[] = {kRoot, kLayer0, kLayer1, kLayer2, kLayer3};
    HandOut(reinterpret_cast<void **>(a[2]), F(made[gCreated++ % 5]));
    return 1;
}
uint32_t __fastcall RootPriority(Obj *self, void *, uint32_t priority) { Ev(0x98); Ev(self->id); Ev(priority); return 0; }
void *gRootVT[160];
void *__cdecl StandState() { Ev(0x99); return &X(mState); }
uint32_t __cdecl StandFootprintIn(void *footprint, const int32_t *rect, uint32_t whole)
{
    Ev(0x9a);
    Ev(Norm(shape::P(footprint)));
    Ev(Norm(shape::P(rect)));
    Ev(whole);
    if (gRectSet || Norm(shape::P(rect)) != 0x5ac00000)
        for (int i = 0; i < 4; ++i) Ev(uint32_t(rect[i]));
    const uint32_t index = (shape::P(footprint) & 0xff) % 8;
    return (gFootprintAnswers >> (index * 2 + (whole & 1))) & 1;
}
void __fastcall Stand0(void *self, void *) { Ev(0x9b); Ev(Norm(shape::P(self))); }
void __fastcall StandAddFlora(void *self, void *, void *o) { Ev(0x9c); Ev(Norm(shape::P(self))); Ev(Norm(shape::P(o))); }
void __fastcall StandAddOther(void *self, void *, void *o) { Ev(0x9d); Ev(Norm(shape::P(self))); Ev(Norm(shape::P(o))); }
void __fastcall Stand5a40(void *self, void *) { Ev(0x9e); Ev(Norm(shape::P(self))); }
void __fastcall Stand5d70(void *self, void *) { Ev(0x9f); Ev(Norm(shape::P(self))); }
void __cdecl StandSceneRoot(void *root) { Ev(0xa0); Ev(Norm(shape::P(root))); }
uint32_t RectFill(Obj *o, uint32_t *a)
{
    auto *rect = reinterpret_cast<int32_t *>(a[0]);
    for (int i = 0; i < 4; ++i) rect[i] = int32_t(o->value[i]);
    if (o == &X(mOwner)) gRectSet = true; // the lot rectangle (a local) now set
    return 0;
}
const char *const kHoodNames[] = {"", "N001", "Pleasantview", "fifteen chars!!"};
uint32_t gOthersCount;
uint32_t AppOthers(Obj *, uint32_t *a)
{
    auto *v = reinterpret_cast<ServiceRefVector *>(a[0]);
    const unsigned picks[] = {mOther0, mOther1};
    for (unsigned i = 0; i < gOthersCount; ++i) v->PushBack(&X(picks[i % 2]));
    return 0;
}
} // namespace

DIFF_TEST(NHoodSG_BuildScene, 0x2a8780)
{
    shape::CodePatch state(0x211da3, reinterpret_cast<void *>(&StandState));
    shape::StandIn footprint(0x2a8570, &M::FootprintIn, &StandFootprintIn);
    shape::StandIn prepare(0x2a81f0, &M::PrepareBuildAll, &Stand0);
    shape::StandIn layers(0x2a5a40, &M::BuildFloraNode, &Stand5a40);
    shape::StandIn remerge(0x2a5d70, &M::RebuildMergedLayers, &Stand5d70);
    shape::StandIn flora(0x2a5ef0, &M::AddFloraOccupant, &StandAddFlora);
    shape::StandIn others(0x2a63a0, &M::AddOtherOccupant, &StandAddOther);
    shape::CodePatch sceneRoot(0xae7e60, reinterpret_cast<void *>(&StandSceneRoot));
    shape::StandIn merge(0x2a5340, &M::MergeOccupant, &StandMerge);
    shape::StandIn handOn(0x2a9100, &M::HandOnPending, &StandHandOn);
    for (uint32_t seed = 0; seed < 600; ++seed)
        Run(ctx, "BuildScene", seed, 3, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            Globals g;
            gCreated = 0;
            const uint32_t all = (rng() % 3 == 0 ? 1u : 0u) | (rng() % 4 == 0 ? 0x300u : 0u); // the low byte counts
            // the lot (one lot only): the manager's current lot or none, the editor's verdict, the view rectangle's owner
            g.Set(0x10cc2a8, &X(mLotManager));
            X(mLotManager).Set(9, 0, rng() % 6 ? static_cast<const void *>(&X(mLot)) : nullptr);
            X(mState).Set(43, 0, &X(mEditor));
            X(mEditor).Set(12, "p", rng() % 6 ? 1u : 0x100u);
            const bool owner = rng() % 5 != 0;
            X(mState).Set(37, 0, owner ? static_cast<const void *>(&X(mOwner)) : nullptr);
            X(mOwner).Set(13, "p", 0u, &RectFill);
            X(mLot).Set(12, "p", 0u, &RectFill);
            X(mLot).Set(23, 0, rng());
            for (int i = 0; i < 4; ++i) X(mOwner).value[i] = rng() % 200, X(mLot).value[i] = rng() % 200;
            gRectSet = false;
            gFootprintAnswers = rng();
            // the singleton: the neighbourhood (slot 6, its name slot 65), the sources (slots 12 and 11)
            g.Set(0x10cc288, &X(mSingleton));
            X(mSingleton).Set(6, 0, &X(mHood));
            X(mHood).Set(65, 0, kHoodNames[rng() % 4]);
            X(mSingleton).Set(12, 0, rng() % 4 ? static_cast<const void *>(&F(kApp12)) : &F(kSpare));
            X(mSingleton).Set(11, 0, rng() % 3 ? static_cast<const void *>(&X(mSource11)) : nullptr);
            F(kSpare).Set(11, "p", 0u, &AppOccupants);
            F(kSpare).Set(17, "p", 0u, &AppOthers);
            F(kApp12).Set(11, "p", 0u, &AppOccupants);
            F(kApp12).Set(17, "p", 0u, &AppOthers);
            gOccupantCount = rng() % 5;
            gOthersCount = rng() % 4;
            for (unsigned i : {mOccupant, mNode, mShape, mOther0, mOther1}) X(i).Set(3, 0, 0x1000u + i + rng() % 2 * 8);
            // Their footprints are tokens 0x1000 + n: registered as an object of their own.
            svcfx::AddRange(reinterpret_cast<const void *>(uintptr_t(0x1000)), 0x40, 0x10000000);
            // the framework makes the nodes; the root's slot 81; layer 0's children count; the scene and its cache
            F(kFw).Set(3, "ddp", 0u, &FwCreate);
            F(kLayer0).Set(34, 0, rng() % 3);
            F(kScene).Set(7, "p", 0u);
            for (unsigned i = 0; i < 80; ++i) gRootVT[i] = F(kRoot).table[i];
            gRootVT[81] = reinterpret_cast<void *>(&RootPriority);
            F(kRoot).vt = gRootVT;
            g.Set(0x10cc460, rng() % 2 ? static_cast<const void *>(&F(kScene)) : nullptr);
            // the dirty layers (SetMergeFlora's world)
            F(kRef98).Set(5, 0, 0u);
            F(kRefA8).Set(34, 0, 0u);
            for (int i = 0; i < 4; ++i) {
                m->mLayerDirty[i] = uint8_t(rng() % 2 ? 0 : 1 + rng() % 3);
                if (m->mLayerDirty[i]) m->mLayerBuilders[i].mObject = reinterpret_cast<cIGZUnknown *>(&F(kRefA8));
            }
            m->mMergeFlora = uint8_t(rng() % 3 != 0);
            Call<void>(ctx, m, route, 1, 0x2a8780, SG_ADAPTER(BuildScene), all);
            CaptureMine();
            Ev(Norm(*GameAddress<uint32_t>(0x10cc460)));
            Ev(gCreated);
            // the name's string words (SSO: the text inline)
            for (uint32_t off = 0x40; off < 0x58; off += 4) Ev(W(m, off));
            return 0;
        });
}

// ---- the scene-building helpers (lane GW) ----
// Free (cdecl) helpers: the original at its RVA (recursing into itself) against our static member (recursing into ours), on
// own fakes: TNode, a scene node with the slots the recursions use (7 event, 38 next sibling, 42 children range, 48 feature,
// 78 / 80 flags) in a random tree; TPlace, an occupant (slot 3: its placement) that is its own placement (slot 6 position,
// slot 8 footprint).

namespace {
struct TNode {
    void **vt;
    uint32_t id;
    uint32_t answer;      // slot 48's al (upper bits garbage)
    TNode *next;          // slot 38
    TNode *first;         // slot 42's first child
    TNode *end;           // slot 42's end (and the last child's next)
};
void *gTNodeVT[90];
TNode gTNodes[24];
TNode gTEnd;

uint32_t __fastcall TFeature(TNode *self, void *, uint32_t what)
{
    Ev(0xb0);
    Ev(self->id);
    Ev(what);
    return self->answer;
}
uint32_t __fastcall TSetFlags(TNode *self, void *, uint32_t flags) { Ev(0xb1); Ev(self->id); Ev(flags); return 0; }
uint32_t __fastcall TClearFlags(TNode *self, void *, uint32_t flags) { Ev(0xb2); Ev(self->id); Ev(flags); return 0; }
uint32_t __fastcall TChildren(TNode *self, void *, void **out)
{
    Ev(0xb3);
    Ev(self->id);
    out[0] = self->first;
    out[1] = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
    out[2] = self->end;
    return shape::P(out);
}
TNode *__fastcall TNext(TNode *self, void *) { Ev(0xb4); Ev(self->id); return self->next; }
uint32_t __fastcall TEvent(TNode *self, void *, uint32_t id, uint32_t arg, const uint32_t *event)
{
    Ev(0xb5);
    Ev(self->id);
    Ev(id);
    Ev(arg);
    Ev(event[0] == shape::P(GameAddress<void>(0xdb07d4)));
    for (int i = 1; i < 5; ++i) Ev(event[i]);
    return 0;
}

// A random tree under gTNodes[0]: each node 0-3 children; a range ends at gTEnd or null.
TNode *BuildTree(std::mt19937 &rng)
{
    gTNodeVT[7] = reinterpret_cast<void *>(&TEvent);
    gTNodeVT[38] = reinterpret_cast<void *>(&TNext);
    gTNodeVT[42] = reinterpret_cast<void *>(&TChildren);
    gTNodeVT[48] = reinterpret_cast<void *>(&TFeature);
    gTNodeVT[78] = reinterpret_cast<void *>(&TSetFlags);
    gTNodeVT[80] = reinterpret_cast<void *>(&TClearFlags);
    gTEnd = TNode{gTNodeVT, 999, 0, nullptr, nullptr, nullptr};
    unsigned used = 1;
    for (unsigned i = 0; i < 24; ++i) {
        TNode &n = gTNodes[i];
        n.vt = gTNodeVT;
        n.id = 500 + i;
        n.answer = (rng() % 3 ? 1u : 0u) | (rng() % 2 ? 0xab00u : 0u);
        n.end = rng() % 4 ? &gTEnd : nullptr;
        n.first = n.end;
    }
    for (unsigned i = 0; i < 24 && i < used; ++i) {
        TNode &n = gTNodes[i];
        const unsigned kids = i < 6 ? rng() % 4 : 0;
        TNode *prev = nullptr;
        for (unsigned k = 0; k < kids && used < 24; ++k) {
            TNode &c = gTNodes[used++];
            c.next = n.end;
            if (prev) prev->next = &c;
            else n.first = &c;
            prev = &c;
        }
    }
    for (unsigned i = used; i < 24; ++i) gTNodes[i].next = nullptr;
    gTNodes[0].next = nullptr;
    return &gTNodes[0];
}

struct TPlace {
    void **vt;
    float position[3];
    int32_t rect[4];
};
void *gTPlaceVT[10];
TPlace *__fastcall TPlacement(TPlace *self, void *) { Ev(0xb6); return self; }
uint32_t __fastcall TPosition(TPlace *self, void *, float *out)
{
    Ev(0xb7);
    for (int i = 0; i < 3; ++i) out[i] = self->position[i];
    return 0;
}
uint32_t __fastcall TRect(TPlace *self, void *, int32_t *out)
{
    Ev(0xb8);
    for (int i = 0; i < 4; ++i) out[i] = self->rect[i];
    return 0;
}
void TPlaceTable()
{
    gTPlaceVT[3] = reinterpret_cast<void *>(&TPlacement);
    gTPlaceVT[6] = reinterpret_cast<void *>(&TPosition);
    gTPlaceVT[8] = reinterpret_cast<void *>(&TRect);
}

// A coordinate for LayerOf: around the 640 cells' edges, halves, integers, negatives, far out.
float LayerCoordinate(std::mt19937 &rng)
{
    const int32_t cell = int32_t(rng() % 9) - 2;
    switch (rng() % 6) {
    case 0: return float(cell * 640);
    case 1: return float(cell * 640) - 0.5f;
    case 2: return float(cell * 640) + (rng() % 2 ? 0.5f : -0.25f);
    case 3: return std::bit_cast<float>(std::bit_cast<uint32_t>(float(cell * 640)) + (rng() % 5) - 2);
    case 4: return float(int32_t(rng() % 4000) - 500) + float(rng() % 1000) / 1000.0f;
    default: return float(int32_t(rng() % 3000000) - 1000000);
    }
}
} // namespace

DIFF_TEST(NHoodSG_NodeFlags, 0x2a5560)
{
    for (uint32_t seed = 0; seed < 400; ++seed) {
        Blob b[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed * 2654435761u + 3);
            TNode *root = BuildTree(rng);
            const uint32_t set = rng(), clear = rng() % 3 ? rng() : 0u;
            gEv.clear();
            if (side == 0) ctx.Original<void(__cdecl *)(void *, uint32_t, uint32_t)>(0x2a5560)(root, set, clear);
            else M::ChangeNodeFlags(root, set, clear);
            b[side] = gEv;
        }
        Same(ctx, b[0], b[1], "NodeFlags", seed);
    }
}

DIFF_TEST(NHoodSG_NodeColor, 0x2a55e0)
{
    for (uint32_t seed = 0; seed < 400; ++seed) {
        Blob b[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed * 2654435761u + 5);
            TNode *root = BuildTree(rng);
            const uint32_t arg = rng() % 2 ? 0u : rng();
            float rgba[4];
            for (float &f : rgba) f = float(rng() % 256) / 255.0f;
            gEv.clear();
            if (side == 0) ctx.Original<void(__cdecl *)(void *, uint32_t, const float *)>(0x2a55e0)(root, arg, rgba);
            else M::SetNodeColor(root, arg, rgba);
            b[side] = gEv;
        }
        Same(ctx, b[0], b[1], "NodeColor", seed);
    }
}

DIFF_TEST(NHoodSG_LayerOf, 0x2a7f20)
{
    TPlaceTable();
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        std::mt19937 rng(seed * 2654435761u + 9);
        TPlace place{gTPlaceVT, {LayerCoordinate(rng), LayerCoordinate(rng), float(rng() % 100)}, {}};
        void *occupant = seed % 50 == 49 ? nullptr : &place;
        Blob b[2];
        for (int side = 0; side < 2; ++side) {
            gEv.clear();
            const int32_t r = side == 0 ? ctx.Original<int32_t(__cdecl *)(void *)>(0x2a7f20)(occupant) : M::LayerOf(occupant);
            b[side] = gEv;
            b[side].push_back(uint32_t(r));
        }
        Same(ctx, b[0], b[1], "LayerOf", seed);
    }
}

DIFF_TEST(NHoodSG_FootprintIn, 0x2a8570)
{
    TPlaceTable();
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        std::mt19937 rng(seed * 2654435761u + 11);
        TPlace place{gTPlaceVT, {}, {}};
        int32_t rect[4];
        auto coordinate = [&] { return int32_t(rng() % 12) - 2; };
        for (int i = 0; i < 2; ++i) {
            const int32_t a = coordinate(), c = coordinate();
            place.rect[i] = rng() % 8 ? (std::min)(a, c) : (std::max)(a, c); // now and then an inverted footprint
            place.rect[i + 2] = rng() % 8 ? (std::max)(a, c) : (std::min)(a, c);
            const int32_t r0 = coordinate(), r1 = coordinate();
            rect[i] = (std::min)(r0, r1);
            rect[i + 2] = (std::max)(r0, r1);
        }
        const uint32_t whole = (rng() % 2) | (rng() % 4 == 0 ? 0x100u : 0u); // the function reads the low byte
        Blob b[2];
        for (int side = 0; side < 2; ++side) {
            gEv.clear();
            uint32_t r;
            if (side == 0) r = ctx.Original<uint32_t(__cdecl *)(void *, const int32_t *, uint32_t)>(0x2a8570)(&place, rect, whole);
            else r = M::FootprintIn(&place, rect, uint8_t(whole));
            b[side] = gEv;
            b[side].push_back(r & 0xff); // al only: the original leaves an intersection coordinate in eax's upper bytes
        }
        Same(ctx, b[0], b[1], "FootprintIn", seed);
    }
}

// MergeOccupant 0x2a5340 (LayerOf stood in on both sides: the fake's value[2], -1 for null) and ClearLayers1And2 0x2a5860,
// the original at its RVA against our member. The pending queue gets spare capacity or none (the growth path is
// RefPtrVector's reallocating insert on our side, 0x18c0c0 on the image's).
namespace {
uint32_t gSpare;
void SpareCapacity(M *m, std::mt19937 &rng)
{
    void **first = m->mPending.mFirst;
    const uint32_t n = m->mPending.Size(), extra = rng() % 3;
    if (!extra) return;
    auto **p = static_cast<void **>(::operator new((n + extra) * 4));
    for (uint32_t i = 0; i < n; ++i) p[i] = first[i];
    ::operator delete(first);
    m->mPending.mFirst = p;
    m->mPending.mLast = p + n;
    m->mPending.mEnd = p + n + extra;
}
// layer nodes' slots: 52 has children (value[0] of them), 42 the range (written to `out`, or to another block whose address
// comes back: the original reads the returned pointer), 46 remove child (one fewer)
uint32_t gOtherRange[3];
uint32_t HasChildren(Obj *o, uint32_t *) { return o->value[0] ? 1u | (o->value[1] & 0xff00u) : 0x300u; }
uint32_t ChildRange(Obj *o, uint32_t *a)
{
    uint32_t *out = o->value[1] & 1 ? gOtherRange : reinterpret_cast<uint32_t *>(a[0]);
    out[0] = shape::P(&X(o->value[0] % 2 ? mNode : mShape));
    out[1] = 0x12345678;
    out[2] = 0;
    return shape::P(out);
}
uint32_t RemoveChild(Obj *o, uint32_t *) { --o->value[0]; return 0; }
} // namespace

DIFF_TEST(NHoodSG_MergeOccupant, 0x2a5340)
{
    shape::StandIn layer(0x2a7f20, &M::LayerOf, &StandLayerOf);
    for (uint32_t seed = 0; seed < 600; ++seed)
        Run(ctx, "MergeOccupant", seed, 2, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            SpareCapacity(m, rng);
            m->mOptionBit7 = uint8_t(rng() % 3 ? rng() % 2 : rng());
            F(kRef98).Set(3, "pd", rng());
            F(kSpare).Set(3, "pd", rng());
            for (int i = 0; i < 4; ++i)
                m->mMergeNodes[i].mObject = reinterpret_cast<cIGZUnknown *>(&F(rng() % 2 ? kRef98 : kSpare));
            X(mOccupant).value[2] = rng() % 4;
            auto *occupant = rng() % 8 ? reinterpret_cast<cIGZUnknown *>(&X(mOccupant)) : nullptr;
            Call<void>(ctx, m, route, kNoFace, 0x2a5340, shape::MemberCode(&M::MergeOccupant), occupant);
            CaptureMine();
            return 0;
        });
}

DIFF_TEST(NHoodSG_ClearLayers, 0x2a5860)
{
    for (uint32_t seed = 0; seed < 300; ++seed)
        Run(ctx, "ClearLayers", seed, 2, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            for (Fake f : {kLayer0, kLayer1, kLayer2, kLayer3}) {
                F(f).value[0] = rng() % 4;
                F(f).value[1] = rng();
                F(f).Set(52, 0, 0u, &HasChildren);
                F(f).Set(42, "p", 0u, &ChildRange);
                F(f).Set(46, "p", 0u, &RemoveChild);
            }
            Call<void>(ctx, m, route, kNoFace, 0x2a5860, shape::MemberCode(&M::ClearLayers1And2));
            CaptureMine();
            return 0;
        });
}

// AddOccupant 0x2a7500, RemoveOccupant 0x2a8e70, HandOnPending 0x2a9100: the original at its RVA against our member.
// The occupant (X(mOccupant)) answers the four faces by a mask (value[3]); each face handed out is one of gMine's objects
// (value[2] its layer for the stood-in LayerOf). Stood in: the node builders 0x2a5ef0, 0x2a69b0, 0x2a7130, 0x2a63a0 (game
// code on both sides), LayerOf (both sides).
namespace {
uint32_t FaceQuery(Obj *o, uint32_t *a)
{
    const uint32_t iids[] = {0x4bd0e45b, 0xabd4c3f4, 0x6bd4d6ce, 0x2c0c10c8};
    const unsigned faces[] = {mOther0, mOther1, mSource11, mHood};
    unsigned i = 0;
    while (i < 4 && iids[i] != a[0]) ++i;
    Ev(0xc0);
    Ev(i);
    if (i == 4) return 0x100; // al 0
    if (!(o->value[3] >> i & 1)) {
        if (o->value[7] >> i & 1) HandOut(reinterpret_cast<void **>(a[1]), X(faces[i]));
        return 0x100;
    }
    if (o->value[4] >> i & 1) { // answers yes with a null face
        *reinterpret_cast<void **>(a[1]) = nullptr;
        return 1;
    }
    HandOut(reinterpret_cast<void **>(a[1]), X(faces[i]));
    return 1;
}
void __fastcall StandFaceB(void *self, void *, void *o) { Ev(0xc1); Ev(Norm(shape::P(self))); Ev(Norm(shape::P(o))); }
void __fastcall StandFaceC(void *self, void *, void *o) { Ev(0xc2); Ev(Norm(shape::P(self))); Ev(Norm(shape::P(o))); }

// The node's object 0xbb263d5 (slot 24): a class with a virtual base, counted through the base (vbtable at +4: the base at
// +4 + 8), its own slot 8 logged.
struct VBObj {
    void **vt;
    const int32_t *vbptr;
    uint32_t pad;
    void **baseVt;
    int32_t refs;
};
int32_t kVbTable[2] = {-4, 8}; // entry 0 (the vbptr's own offset) varied per seed: only entry 1 locates the base
VBObj gVB;
uint32_t __fastcall VBDetach(VBObj *, void *, uint32_t arg) { Ev(0xc3); Ev(arg); return 0; }
uint32_t __fastcall VBAddRef(void **base, void *) { Ev(0xc4); return uint32_t(++reinterpret_cast<VBObj *>(base - 3)->refs); }
uint32_t __fastcall VBRelease(void **base, void *) { Ev(0xc5); return uint32_t(--reinterpret_cast<VBObj *>(base - 3)->refs); }
void *gVBVT[10] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void *>(&VBDetach)};
void *gVBBaseVT[3] = {nullptr, reinterpret_cast<void *>(&VBAddRef), reinterpret_cast<void *>(&VBRelease)};
uint32_t NodeObject(Obj *o, uint32_t *a)
{
    *reinterpret_cast<void **>(a[1]) = o->value[5] ? &gVB : nullptr;
    if (o->value[5]) ++gVB.refs;
    return 0;
}

void OccupantFaces(std::mt19937 &rng)
{
    X(mOccupant).Set(0, "dp", 0u, &FaceQuery);
    X(mOccupant).value[3] = rng() % 16;
    X(mOccupant).value[4] = rng() % 6 == 0 ? rng() % 16 : 0;
    X(mOccupant).Set(4, 0, rng() % 4);
    X(mOccupant).Set(10, 1, 0u);
    X(mOccupant).Set(11, 0, rng() % 3 ? static_cast<const void *>(&X(mNode)) : nullptr);
    for (unsigned i : {mOther0, mOther1, mSource11, mHood}) X(i).value[2] = rng() % 4;
    X(mNode).value[5] = rng() % 4 != 0;
    X(mNode).Set(24, "dp-", 0u, &NodeObject);
    kVbTable[0] = rng() % 2 ? -4 : 0;
    gVB = VBObj{gVBVT, kVbTable, 0, gVBBaseVT, 50};
    // a refusing QI that still writes a counted face (value[7], by face): both sides release it, in their order
    X(mOccupant).value[7] = rng() % 4 == 0 ? rng() % 16 : 0;
}

// HandOnPending's world: the flora node (slot 93: the per-layer targets' table), the targets (slots 34 / 36 log their texts),
// the merge nodes' slot 6 (answers, and fills the list from the allocator service with 0-3 entries of short or long names).
struct TFlora {
    void **vt;
    void **table;
};
void *gTFloraVT[100];
void *gTargetTable[4];
TFlora gTFlora;
void ***__fastcall FloraTargets(TFlora *self, void *, uint32_t *scratch)
{
    Ev(0xc6);
    scratch[0] = scratch[1] = scratch[2] = 0x77;
    return &self->table;
}
uint32_t __fastcall TFloraCount(TFlora *, void *) { Ev(0xc9); return 1; }
uint32_t TargetNames(Obj *o, uint32_t *a)
{
    Ev(0xc7);
    Ev(o->id);
    for (int k = 0; k < 2; ++k)
        for (const char *p = reinterpret_cast<const char *>(a[k]); *p; ++p) Ev(uint8_t(*p));
    return 0;
}
const char *const kPropNames[] = {"", "tree", "fifteen chars!!", "sixteen chars!!!", "a name past fifteen", "another long prop name"};
void FillServiceString(as2::RawString &s, const char *text)
{
    const uint32_t n = uint32_t(std::strlen(text));
    s.mSize = n;
    if (n <= 15) {
        s.mCapacity = 15;
        std::memcpy(s.mBuf, text, n + 1);
    } else {
        s.mCapacity = n;
        s.mPtr = static_cast<char *>(as2::AllocateViaAllocatorService(n + 1, 0));
        gAllocated.push_back(s.mPtr);
        AddRange(s.mPtr, n + 1, 0xd6000000u + (uint32_t(gAllocated.size()) << 16));
        std::memcpy(s.mPtr, text, n + 1);
    }
}
uint32_t gListSeed;
uint32_t MergeList(Obj *o, uint32_t *a)
{
    Ev(0xc8);
    Ev(o->id);
    Ev(Norm(a[0]));
    std::mt19937 rng(gListSeed);
    auto *list = reinterpret_cast<uint32_t *>(a[1]);
    Ev(list[0]);
    Ev(list[1]);
    Ev(list[2]);
    const uint32_t n = rng() % 4;
    if (n || rng() % 2) {
        auto *block = static_cast<uint8_t *>(as2::AllocateViaAllocatorService((n ? n : 1) * 0x34, 0));
        gAllocated.push_back(block);
        AddRange(block, (n ? n : 1) * 0x34, 0xd6000000u + (uint32_t(gAllocated.size()) << 16));
        std::memset(block, 0xcd, (n ? n : 1) * 0x34);
        for (uint32_t i = 0; i < n; ++i) {
            auto *e = reinterpret_cast<as2::RawString *>(block + i * 0x34);
            FillServiceString(e[0], kPropNames[rng() % 6]);
            FillServiceString(e[1], kPropNames[rng() % 6]);
        }
        list[0] = shape::P(block);
        list[1] = list[2] = shape::P(block + n * 0x34);
    }
    return o->value[6] ? 1 : 0x200;
}
} // namespace

DIFF_TEST(NHoodSG_AddOccupant, 0x2a7500)
{
    shape::StandIn layer(0x2a7f20, &M::LayerOf, &StandLayerOf);
    shape::StandIn flora(0x2a5ef0, &M::AddFloraOccupant, &StandAddFlora);
    shape::StandIn faceB(0x2a69b0, &M::AddRoadOccupant, &StandFaceB);
    shape::StandIn faceC(0x2a7130, &M::AddFaceCOccupant, &StandFaceC);
    shape::StandIn other(0x2a63a0, &M::AddOtherOccupant, &StandAddOther);
    for (uint32_t seed = 0; seed < 400; ++seed)
        Run(ctx, "AddOccupant", seed, 2, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            OccupantFaces(rng);
            Call<void>(ctx, m, route, kNoFace, 0x2a7500, shape::MemberCode(&M::AddOccupant),
                reinterpret_cast<cIGZUnknown *>(&X(mOccupant)));
            CaptureMine();
            return 0;
        });
}

DIFF_TEST(NHoodSG_RemoveOccupant, 0x2a8e70)
{
    shape::StandIn layer(0x2a7f20, &M::LayerOf, &StandLayerOf);
    for (uint32_t seed = 0; seed < 600; ++seed)
        Run(ctx, "RemoveOccupant", seed, 2, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            OccupantFaces(rng);
            for (Fake f : {kLayer0, kLayer1, kLayer2, kLayer3}) F(f).Set(46, "p", 0u);
            // the original removes the node from the type's layer node without a test: give that one a node
            const uint32_t type = X(mOccupant).ret[4];
            if (!m->mLayerNodes[type].mObject) m->mLayerNodes[type].mObject = reinterpret_cast<cIGZUnknown *>(&F(Fake(kLayer0 + type)));
            for (int i = 0; i < 4; ++i) m->mLayerDirty[i] = uint8_t(rng() % 3 == 0);
            const uint32_t before = W(m, 0xb4);
            Call<void>(ctx, m, route, kNoFace, 0x2a8e70, shape::MemberCode(&M::RemoveOccupant),
                reinterpret_cast<cIGZUnknown *>(&X(mOccupant)));
            CaptureMine();
            Ev(uint32_t(gVB.refs));
            // the byte before the flags, +0xb7 (a null flora face's layer -1 marks it): the top byte of mLayerBuilders[3],
            // logged and put back so the teardown releases the real pointer
            Ev(W(m, 0xb4) >> 24);
            W(m, 0xb4) = before;
            return 0;
        });
}

DIFF_TEST(NHoodSG_HandOnPending, 0x2a9100)
{
    shape::StandIn layer(0x2a7f20, &M::LayerOf, &StandLayerOf);
    gTFloraVT[93] = reinterpret_cast<void *>(&FloraTargets);
    gTFloraVT[1] = reinterpret_cast<void *>(&TFloraCount); // the destructor releases +0x94
    gTFloraVT[2] = reinterpret_cast<void *>(&TFloraCount);
    for (uint32_t seed = 0; seed < 600; ++seed)
        Run(ctx, "HandOnPending", seed, 2, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            gAllocated.clear();
            gTFlora = TFlora{gTFloraVT, gTargetTable};
            AddRange(&gTFlora, sizeof(gTFlora), 0xd5000000u);
            const Fake targets[] = {kNode0, kNode1, kNode2, kSpare};
            for (unsigned i = 0; i < 4; ++i) {
                gTargetTable[i] = rng() % 6 ? static_cast<void *>(&F(targets[i])) : nullptr;
                F(targets[i]).Set(34, "pp", 0u, &TargetNames);
                F(targets[i]).Set(36, "pp", 0u, &TargetNames);
            }
            m->mFloraNode.mObject = reinterpret_cast<cIGZUnknown *>(&gTFlora);
            for (int i = 0; i < 4; ++i)
                m->mMergeNodes[i].mObject = reinterpret_cast<cIGZUnknown *>(&F(rng() % 2 ? kRef98 : kRefA8));
            for (Fake f : {kRef98, kRefA8}) {
                F(f).Set(6, "pp", 0u, &MergeList);
                F(f).value[6] = rng() % 4 != 0;
            }
            gListSeed = rng();
            X(mOccupant).value[2] = rng() % 4;
            // a target is called without a null test (the original's too) when the merge node lists entries: keep one there
            const uint32_t layer = X(mOccupant).value[2];
            if (!gTargetTable[layer] && reinterpret_cast<Obj *>(m->mMergeNodes[layer].mObject)->value[6])
                gTargetTable[layer] = &F(kSpare);
            void *occupant = rng() % 8 ? static_cast<void *>(&X(mOccupant)) : nullptr;
            Call<void>(ctx, m, route, kNoFace, 0x2a9100, shape::MemberCode(&M::HandOnPending), occupant);
            CaptureMine();
            for (void *block : gAllocated) Ev(BlockFreed(block));
            gAllocated.clear();
            return 0;
        });
}

// RebuildMergedLayers 0x2a5d70: app slot 12's slots 13 and 15 fill counted vectors (our ServiceRefVector::PushBack, freed by
// either side); stood in: 0x2a69b0, 0x2a7130 (the face builders), 0x2a7be0 (a layer's merged node: hands out kFlora or
// kRef78 or nothing), ClearLayers1And2 (both sides), 0xaeb070.
namespace {
unsigned gListCounts[2];
uint32_t AppListB(Obj *, uint32_t *a)
{
    for (unsigned i = 0; i < gListCounts[0]; ++i) reinterpret_cast<ServiceRefVector *>(a[0])->PushBack(&X(i % 2 ? mOther0 : mHood));
    return 0;
}
uint32_t AppListC(Obj *, uint32_t *a)
{
    for (unsigned i = 0; i < gListCounts[1]; ++i) reinterpret_cast<ServiceRefVector *>(a[0])->PushBack(&X(i % 2 ? mOther1 : mLot));
    return 0;
}
unsigned gMergedPick;
void __fastcall StandMergeLayer(void *self, void *, void *layer, void **out)
{
    Ev(0xca);
    Ev(Norm(shape::P(self)));
    Ev(Norm(shape::P(layer)));
    Ev(Norm(shape::P(*out)));
    const unsigned pick = gMergedPick++ % 3;
    if (pick == 0) HandOut(out, F(kFlora));
    else if (pick == 1) HandOut(out, F(kRef78));
}
void __cdecl StandLayerPriority(void *layer, uint32_t value) { Ev(0xcb); Ev(Norm(shape::P(layer))); Ev(value); }
} // namespace

DIFF_TEST(NHoodSG_RebuildMergedLayers, 0x2a5d70)
{
    shape::StandIn faceB(0x2a69b0, &M::AddRoadOccupant, &StandFaceB);
    shape::StandIn faceC(0x2a7130, &M::AddFaceCOccupant, &StandFaceC);
    shape::StandIn merge(0x2a7be0, &M::MergeLayerChildren, &StandMergeLayer);
    shape::StandIn clear(0x2a5860, &M::ClearLayers1And2, &StandMergeA);
    shape::CodePatch priority(0xaeb070, reinterpret_cast<void *>(&StandLayerPriority));
    for (uint32_t seed = 0; seed < 400; ++seed)
        Run(ctx, "RebuildMergedLayers", seed, 2, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            m->mBuildAll = uint8_t(rng() % 4 ? 1 : 0);
            m->mAppSlot12.mObject = reinterpret_cast<cIGZUnknown *>(&F(kApp12));
            F(kApp12).Set(13, "p", 0u, &AppListB);
            F(kApp12).Set(15, "p", 0u, &AppListC);
            gListCounts[0] = rng() % 4;
            gListCounts[1] = rng() % 4;
            gMergedPick = rng() % 3;
            // layers 1 and 2 are called without a test (the original's too)
            m->mLayerNodes[1].mObject = reinterpret_cast<cIGZUnknown *>(&F(kLayer1));
            m->mLayerNodes[2].mObject = reinterpret_cast<cIGZUnknown *>(&F(kLayer2));
            F(kLayer1).Set(45, "p", 0u);
            F(kLayer2).Set(45, "p", 0u);
            Call<void>(ctx, m, route, kNoFace, 0x2a5d70, shape::MemberCode(&M::RebuildMergedLayers));
            CaptureMine();
            return 0;
        });
}

// BuildFloraNode 0x2a5a40: the framework's slot 3 makes the flora node (GNode: own vtable, its slots past the fixture's 80
// logged), its parts (gMine's objects) and the merge nodes (kRef98 / kRefA8, whose QI hands out the builder); the targets
// are kNode0-2 and kSpare (slot 41). Stood in: 0x2a8640 (writes three words), 0x1a72c0 (logs the angles, writes a rotation)
// and 0xaeac50 (logs). The scene cache is set or empty (then the scene-graph system's slot 33 gives kScene).
namespace {
struct GNode {
    void **vt;
    uint32_t id;
    int32_t refs;
    void *table;
    uint32_t junk[3];
};
void *gGNodeVT[100];
GNode gGNode;
void *gGTargets[4];
uint32_t __fastcall GAddRef(GNode *self, void *) { return uint32_t(++self->refs); }
uint32_t __fastcall GRelease(GNode *self, void *) { return uint32_t(--self->refs); }
uint32_t __fastcall GPart(GNode *, void *, void *part) { Ev(0xd0); Ev(Norm(shape::P(part))); return 0; }
uint32_t __fastcall GPlace(GNode *, void *, const uint32_t *w) { Ev(0xd1); for (int i = 0; i < 3; ++i) Ev(w[i]); return 0; }
uint32_t __fastcall GRotate(GNode *, void *, const uint32_t *w) { Ev(0xd2); for (int i = 0; i < 4; ++i) Ev(w[i]); return 0; }
uint32_t __fastcall GGroup(GNode *, void *, const char *text)
{
    Ev(0xd3);
    for (const char *p = text; *p; ++p) Ev(uint8_t(*p));
    return 0;
}
uint32_t __fastcall GFinish(GNode *, void *, uint32_t flags) { Ev(0xd4); Ev(flags); return 0; }
uint32_t *__fastcall GTargets(GNode *self, void *, uint32_t *out)
{
    Ev(0xd5);
    out[0] = shape::P(self->table);
    out[1] = out[2] = 0x66;
    return self->junk; // the original reads the local, not the returned pointer
}
unsigned gMadeMerge;
uint32_t FwCreateFlora(Obj *, uint32_t *a)
{
    auto **out = reinterpret_cast<void **>(a[2]);
    if (a[0] == 0x65245517) {
        *out = &gGNode;
        ++gGNode.refs;
    } else if (a[0] == 0x4ef8f0c0) {
        HandOut(out, F(gMadeMerge++ % 2 ? kRef98 : kRefA8));
    } else {
        HandOut(out, X(gMadeMerge++ % 2 ? mOther0 : mOther1));
    }
    return 1;
}
void __cdecl StandPlacement(uint32_t *w)
{
    Ev(0xd6);
    for (int i = 0; i < 3; ++i) Ev(w[i]), w[i] = 0x100 + i;
}
void __cdecl StandRotation(float *out, const float *angles)
{
    Ev(0xd7);
    for (int i = 0; i < 3; ++i) Ev(std::bit_cast<uint32_t>(angles[i]));
    for (int i = 0; i < 4; ++i) out[i] = angles[2] + float(i);
}
void __cdecl StandSetting(void *node, uint32_t a, uint32_t b) { Ev(0xd8); Ev(Norm(shape::P(node))); Ev(a); Ev(b); }
} // namespace

DIFF_TEST(NHoodSG_BuildFloraNode, 0x2a5a40)
{
    shape::CodePatch placement(0x2a8640, reinterpret_cast<void *>(&StandPlacement));
    shape::CodePatch rotation(0x1a72c0, reinterpret_cast<void *>(&StandRotation));
    shape::CodePatch setting(0xaeac50, reinterpret_cast<void *>(&StandSetting));
    gGNodeVT[1] = reinterpret_cast<void *>(&GAddRef);
    gGNodeVT[2] = reinterpret_cast<void *>(&GRelease);
    gGNodeVT[64] = reinterpret_cast<void *>(&GPlace);
    gGNodeVT[65] = reinterpret_cast<void *>(&GRotate);
    gGNodeVT[77] = reinterpret_cast<void *>(&GGroup);
    gGNodeVT[91] = reinterpret_cast<void *>(&GFinish);
    gGNodeVT[93] = reinterpret_cast<void *>(&GTargets);
    gGNodeVT[94] = reinterpret_cast<void *>(&GPart);
    for (uint32_t seed = 0; seed < 400; ++seed)
        Run(ctx, "BuildFloraNode", seed, 2, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            Globals g;
            gGNode = GNode{gGNodeVT, 450, 10, gGTargets, {}};
            AddRange(&gGNode, sizeof(gGNode), 0xd7000000u);
            gMadeMerge = rng() % 2;
            F(kFw).Set(3, "ddp", 0u, &FwCreateFlora);
            const Fake targets[] = {kNode0, kNode1, kNode2, kSpare};
            for (unsigned i = 0; i < 4; ++i) {
                gGTargets[i] = &F(targets[i]);
                F(targets[i]).Set(41, "p-", 0u);
            }
            for (Fake f : {kRef98, kRefA8}) {
                F(f).Set(0, "dp", 0u, &MineQuery);
                F(f).value[0] = rng() % 4 != 0;
                F(f).value[1] = f == kRef98 ? mShape : mNode;
            }
            m->mBuildAll = uint8_t(rng() % 3 == 0);
            m->mLotByte = rng() % 5 ? rng() % 4 : rng();
            m->mRoot.mObject = reinterpret_cast<cIGZUnknown *>(&F(kRoot));
            F(kScene).Set(11, "pp", 0u);
            g.Set(0x10cc460, rng() % 2 ? static_cast<const void *>(&F(kScene)) : nullptr);
            Call<void>(ctx, m, route, kNoFace, 0x2a5a40, shape::MemberCode(&M::BuildFloraNode));
            CaptureMine();
            Ev(uint32_t(gGNode.refs));
            Ev(Norm(*GameAddress<uint32_t>(0x10cc460)));
            return 0;
        });
}

// ---- the node builders (lane HB) ----
// The original at its RVA against our member, on gMine's and the fixture's fakes. Fakes that need slots past the fixture's 80
// get a longer copy of their table (Extend). Callees still game code are stood in (CodePatch, the same on both sides).

namespace {
void *gBigVT[6][160];
// Gives `o` a 160-slot table: its fixture slots copied (call after its Set calls), the rest null for the caller to fill.
void **Extend(Obj &o, unsigned k)
{
    for (unsigned i = 0; i < 160; ++i) gBigVT[k][i] = i < svcfx::kSlots ? o.table[i] : nullptr;
    o.vt = gBigVT[k];
    return gBigVT[k];
}
uint32_t __fastcall BigCount(Obj *self, void *) { Ev(0xe0); Ev(self->id); return self->value[0]; }
uint32_t __fastcall BigPart(Obj *self, void *, void *part) { Ev(0xe1); Ev(self->id); Ev(Norm(shape::P(part))); return 0; }
void LogFloats(const float *f, int n)
{
    for (int i = 0; i < n; ++i) Ev(std::bit_cast<uint32_t>(f[i]));
}

// MergeLayerChildren 0x2a7be0: the framework makes the combiner (kRef98), the group (kFlora, slot 94 and QI past the
// fixture) and the part (kNode0); the layer's slot 44 iterator is X(mState) (the end X(mOwner), the middle word X(mEditor)
// or null) over 0-4 children X(mOther0 / mOther1 / mHood), whose QI hands out X(mShape) or X(mNode) (slot 98: value[0]);
// the combiner's builder face is X(mSource11); its props list comes from the allocator service (CombinerProps).
unsigned gIterAt, gIterCount;
uint32_t FwMerge(Obj *, uint32_t *a)
{
    auto **out = reinterpret_cast<void **>(a[2]);
    if (a[0] == 0xafd79c10) HandOut(out, F(kRef98));
    else if (a[0] == 0x65245517) HandOut(out, F(kFlora));
    else HandOut(out, F(kNode0));
    return 1;
}
uint32_t ChildIter(Obj *, uint32_t *a)
{
    auto *words = reinterpret_cast<uint32_t *>(a[0]);
    const auto *filter = reinterpret_cast<const uint32_t *>(a[2]);
    Ev(0xe2);
    Ev(filter[0]);
    Ev(filter[1]);
    words[0] = shape::P(&X(mState));
    ++X(mState).refs;
    words[1] = gIterCount % 2 ? shape::P(&X(mEditor)) : 0;
    if (words[1]) ++X(mEditor).refs;
    words[2] = shape::P(&X(mOwner));
    ++X(mOwner).refs;
    return 0;
}
uint32_t IterAtEnd(Obj *, uint32_t *) { return gIterAt >= gIterCount ? 0x301 : 0x300; }
uint32_t IterGet(Obj *, uint32_t *)
{
    const unsigned picks[] = {mOther0, mOther1, mHood};
    return shape::P(&X(picks[gIterAt % 3]));
}
uint32_t IterNext(Obj *, uint32_t *)
{
    ++gIterAt;
    return 0;
}
// a child's QI: hands out gMine's value[1] (always: the original asks the shape without a test), answers value[0]
uint32_t ChildShape(Obj *o, uint32_t *a)
{
    HandOut(reinterpret_cast<void **>(a[1]), X(o->value[1]));
    return o->value[0];
}
uint32_t CombinerProps(Obj *o, uint32_t *a)
{
    uint32_t args[2] = {0, a[0]};
    return MergeList(o, args);
}
} // namespace

DIFF_TEST(NHoodSG_MergeLayerChildren, 0x2a7be0)
{
    for (uint32_t seed = 0; seed < 500; ++seed)
        Run(ctx, "MergeLayerChildren", seed, 2, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            gAllocated.clear();
            F(kFw).Set(3, "ddp", 0u, &FwMerge);
            F(kLayer1).Set(44, "p-p", 0u, &ChildIter);
            X(mState).Set(4, "p", 0u, &IterAtEnd);
            X(mState).Set(5, 0, 0u, &IterGet);
            X(mState).Set(6, 0, 0u, &IterNext);
            gIterAt = 0;
            gIterCount = rng() % 5;
            for (unsigned i : {mOther0, mOther1, mHood}) {
                X(i).Set(0, "dp", 0u, &ChildShape);
                X(i).value[0] = rng() % 2 ? 1u : 0x100u;
                X(i).value[1] = rng() % 2 ? mShape : mNode;
            }
            const uint32_t counts[] = {0, 1, 2, 0xffffffffu, 0x80000000u};
            X(mShape).value[0] = counts[rng() % 5];
            X(mNode).value[0] = counts[rng() % 5];
            Extend(X(mShape), 0)[98] = reinterpret_cast<void *>(&BigCount);
            Extend(X(mNode), 1)[98] = reinterpret_cast<void *>(&BigCount);
            F(kRef98).Set(0, "dp", 0u, &MineQuery);
            F(kRef98).value[0] = 1; // the builder is used without a test (the original's too)
            F(kRef98).value[1] = mSource11;
            F(kRef98).Set(3, "p", 0u);
            F(kRef98).Set(4, 0, 0u);
            F(kRef98).Set(5, "p", 0u, &CombinerProps);
            F(kRef98).value[6] = 1;
            gListSeed = rng();
            X(mSource11).Set(34, 0, 0u);
            F(kNode0).Set(41, "p-", 0u);
            F(kNode0).Set(34, "pp", 0u, &TargetNames);
            F(kNode0).Set(36, "pp", 0u, &TargetNames);
            F(kFlora).Set(0, "dp", 0u, &MineQuery);
            F(kFlora).value[0] = rng() % 3 != 0;
            F(kFlora).value[1] = mLot;
            Extend(F(kFlora), 2)[94] = reinterpret_cast<void *>(&BigPart);
            cIGZUnknown *out = rng() % 4 ? nullptr : reinterpret_cast<cIGZUnknown *>(&X(mMode));
            const uint32_t result = Call<bool>(ctx, m, route, kNoFace, 0x2a7be0, shape::MemberCode(&M::MergeLayerChildren),
                reinterpret_cast<cIGZUnknown *>(&F(kLayer1)), &out);
            Ev(Norm(shape::P(out)));
            Ev(gIterAt);
            CaptureMine();
            for (void *block : gAllocated) Ev(BlockFreed(block));
            gAllocated.clear();
            return result;
        });
}

// AddFaceCOccupant 0x2a7130: the occupant X(mOccupant) (record X(mData), whose slot 3 is the placement X(mLot); its model
// name, offset and rotation), the placement's resource (VB2Obj: two virtual bases, the second answers a group) or none, the
// scene-graph system kSg (slot 23 finds X(mMode) or nothing), the model X(mEditor) (0xaeb9a0 stood in) whose QI may give the
// shape X(mShape), else 0xaeb8a0 (stood in) gives X(mShape) or X(mOwner); the node X(mOther0) from the framework. Stood in:
// 0xaeb9a0, 0xaeb8a0, 0x211ce1 (the scene kScene), 0x2a53f0 (the node attached).
namespace {
struct VB2Obj {
    void **vt;
    const int32_t *vbptr;
    uint32_t pad;
    void **base1Vt; // +0xc: vbtable[1] = 8
    int32_t refs;
    void **base2Vt; // +0x14: vbtable[2] = 0x10
    uint32_t group;
    uint32_t answers;
};
int32_t kVb2Table[3] = {-4, 8, 0x10};
VB2Obj gVB2;
uint32_t __fastcall VB2AddRef(void **base, void *) { Ev(0xe4); return uint32_t(++reinterpret_cast<VB2Obj *>(base - 3)->refs); }
uint32_t __fastcall VB2Release(void **base, void *) { Ev(0xe5); return uint32_t(--reinterpret_cast<VB2Obj *>(base - 3)->refs); }
uint32_t __fastcall VB2Group(void **base, void *, uint32_t *out)
{
    VB2Obj *o = reinterpret_cast<VB2Obj *>(base - 5);
    Ev(0xe6);
    *out = o->group;
    return o->answers;
}
void *gVB2BaseVT[3] = {nullptr, reinterpret_cast<void *>(&VB2AddRef), reinterpret_cast<void *>(&VB2Release)};
void *gVB2GroupVT[40];
const char *const kModelNames[] = {"", "tree01", "fifteen chars!!", "a model name longer than fifteen"};
const char *gModelName;
float gFloats[3][4];
uint32_t NameOut(Obj *, uint32_t *a)
{
    reinterpret_cast<cRZString *>(a[0])->mString.assign(gModelName);
    return 0;
}
uint32_t WriteFloats0(Obj *, uint32_t *a) { std::memcpy(reinterpret_cast<float *>(a[0]), gFloats[0], 12); return 0; }
uint32_t WriteFloats1(Obj *, uint32_t *a) { std::memcpy(reinterpret_cast<float *>(a[0]), gFloats[1], 12); return 0; }
uint32_t Rotation(Obj *, uint32_t *a)
{
    Ev(0xe7);
    LogFloats(reinterpret_cast<float *>(a[0]), 4);
    if (gFloats[2][3] != 0) std::memcpy(reinterpret_cast<float *>(a[0]), gFloats[2], 16);
    return 0;
}
uint32_t NodeFloats3(Obj *, uint32_t *a) { Ev(0xe8); LogFloats(reinterpret_cast<float *>(a[0]), 3); return 0; }
uint32_t NodeFloats4(Obj *, uint32_t *a) { Ev(0xe9); LogFloats(reinterpret_cast<float *>(a[0]), 4); return 0; }
bool gFindHit;
// own slot (the name's address is a heap block on one side or the stack: logged as text)
uint32_t __fastcall SgFind(Obj *, void *, const char *name, const uint32_t *group, uint32_t a, uint32_t type, void **out,
    uint32_t b)
{
    Ev(0xea);
    for (const char *p = name; *p; ++p) Ev(uint8_t(*p));
    Ev(group != nullptr);
    if (group) Ev(*group);
    Ev(a);
    Ev(type);
    Ev(Norm(shape::P(out)));
    Ev(b);
    if (gFindHit) HandOut(out, X(mMode));
    return 0;
}
uint32_t FwFaceC(Obj *, uint32_t *a)
{
    HandOut(reinterpret_cast<void **>(a[2]), X(mOther0));
    return 1;
}
void *__cdecl StandModelOf(void *found) { Ev(0xeb); Ev(Norm(shape::P(found))); return &X(mEditor); }
bool gAltShape;
void *__cdecl StandShapeOf(void *model) { Ev(0xec); Ev(Norm(shape::P(model))); return &X(gAltShape ? mOwner : mShape); }
void *__cdecl StandScene() { Ev(0xed); return &F(kScene); }
void __fastcall StandAttach(void *self, void *, void *at, void *node)
{
    Ev(0xee);
    Ev(Norm(shape::P(self)));
    Ev(Norm(shape::P(at)));
    Ev(Norm(shape::P(node)));
}
} // namespace

DIFF_TEST(NHoodSG_AddFaceCOccupant, 0x2a7130)
{
    shape::CodePatch modelOf(0xaeb9a0, reinterpret_cast<void *>(&StandModelOf));
    shape::CodePatch shapeOf(0xaeb8a0, reinterpret_cast<void *>(&StandShapeOf));
    shape::CodePatch scene(0x211ce1, reinterpret_cast<void *>(&StandScene));
    shape::CodePatch attach(0x2a53f0, reinterpret_cast<void *>(&StandAttach));
    gVB2GroupVT[34] = reinterpret_cast<void *>(&VB2Group);
    for (uint32_t seed = 0; seed < 600; ++seed)
        Run(ctx, "AddFaceCOccupant", seed, 2, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            gModelName = kModelNames[rng() % 4];
            for (auto &row : gFloats)
                for (float &f : row) f = float(int(rng() % 2001) - 1000) / 8.0f;
            if (rng() % 3 == 0) gFloats[2][3] = 0; // the rotation left as passed in
            X(mOccupant).Set(3, 0, &X(mData));
            X(mOccupant).Set(6, "p", 0u, &NameOut);
            X(mOccupant).Set(5, "p", 0u, &WriteFloats1);
            X(mOccupant).Set(8, "p", 0u, &Rotation);
            X(mData).Set(3, 0, &X(mLot));
            gVB2 = VB2Obj{nullptr, kVb2Table, 0, gVB2BaseVT, 40, gVB2GroupVT, rng(), rng() % 2 ? 1u : 0x100u};
            AddRange(&gVB2, sizeof(gVB2), 0xd8000000u);
            X(mLot).Set(12, 0, rng() % 4 ? static_cast<const void *>(&gVB2) : nullptr);
            X(mLot).Set(6, "p", 0u, &WriteFloats0);
            X(mLot).Set(10, 1, 0u);
            Extend(F(kSg), 3)[23] = reinterpret_cast<void *>(&SgFind);
            gFindHit = rng() % 5 != 0;
            X(mMode).Set(52, 0, rng() % 4 ? 1u : 0x100u);
            X(mMode).Set(46, "p", 0u);
            X(mEditor).Set(0, "dp", 0u, &MineQuery);
            X(mEditor).value[0] = rng() % 2;
            X(mEditor).value[1] = mShape;
            X(mEditor).Set(46, "p", 0u);
            gAltShape = rng() % 2;
            X(mShape).Set(77, "p", 0u);
            X(mOwner).Set(77, "p", 0u);
            F(kScene).Set(11, "pp", 0u);
            F(kFw).Set(3, "ddp", 0u, &FwFaceC);
            X(mOther0).Set(64, "p", 0u, &NodeFloats3);
            X(mOther0).Set(65, "p", 0u, &NodeFloats4);
            const uint32_t result = Call<bool>(ctx, m, route, kNoFace, 0x2a7130, shape::MemberCode(&M::AddFaceCOccupant),
                reinterpret_cast<cIGZUnknown *>(&X(mOccupant)));
            CaptureMine();
            Ev(uint32_t(gVB2.refs));
            Ev(Norm(*GameAddress<uint32_t>(M::kSgSystemCache)));
            return result;
        });
}

// PrepareBuildAll 0x2a81f0: +0x78 set or not; the system's slot 23 (SgFind) finds X(mMode) (slot 52 valid) or nothing; the
// shape (0xaeba70 stood in) X(mShape) or none, its count (slot 98) and its parts' table (slot 93: kRef78, X(mData) or null
// first); the text pairs at +0x7c (real cRZString pairs, 0-3 with spare room or none); the part's entries (slot 54: the
// iterator X(mState) over 0-4 entries, a name and a key) and the registry (game global 0x10f8fcc, X(mRegistry), slot 33: the
// key's text). After the call the pairs are logged by text and destroyed by the test (heap words differ by side).
namespace {
void *gPartsTable[2];
void **gPartsCell;
void *__fastcall ShapeParts(Obj *self, void *, uint32_t *scratch)
{
    Ev(0xf0);
    Ev(self->id);
    scratch[0] = scratch[1] = scratch[2] = 0x55;
    return &gPartsCell;
}
void *gCube;
void *__cdecl StandCubeShape(void *found) { Ev(0xf1); Ev(Norm(shape::P(found))); return gCube; }
const char *const kTextNames[] = {"", "Lot", "fifteen chars!!", "sixteen chars!!!", "a text well past fifteen characters"};
uint32_t RefEntries(Obj *, uint32_t *a)
{
    auto *words = reinterpret_cast<uint32_t *>(a[0]);
    words[0] = shape::P(&X(mState));
    ++X(mState).refs;
    words[1] = gIterCount % 2 ? shape::P(&X(mEditor)) : 0;
    if (words[1]) ++X(mEditor).refs;
    words[2] = shape::P(&X(mOwner));
    ++X(mOwner).refs;
    return 0;
}
uint32_t gEntrySeed;
uint32_t IterEntry(Obj *, uint32_t *a)
{
    auto *entry = reinterpret_cast<const char **>(a[0]);
    std::mt19937 rng(gEntrySeed + gIterAt);
    entry[0] = kTextNames[rng() % 5];
    entry[1] = reinterpret_cast<const char *>(uintptr_t(rng() % 5));
    // the entry is 16 bytes (the original's buffer at [ebp-0x38], up to the iterator at [ebp-0x28]); the game fills all of
    // it (the smoke run that found an 8-byte buffer in ours, lane HB). FillOut measures those 16 bytes from the original's
    // frame and fills the two words after the ones written here on both routes (with an 8-byte buffer in ours: an access
    // to 0xa5a5a5a5, lane HI)
    difftest::FillOut(entry, 8);
    return 0;
}
const char *gKeyText;
uint32_t RegistryText(Obj *, uint32_t *a)
{
    reinterpret_cast<uint32_t *>(a[0])[0] = 0x66;
    gKeyText = kTextNames[(a[1] + 2) % 5];
    return shape::P(&gKeyText);
}
void LogText(const cRZString &s)
{
    Ev(uint32_t(s.mString.size()));
    for (char c : s.mString) Ev(uint8_t(c));
}
} // namespace

DIFF_TEST(NHoodSG_PrepareBuildAll, 0x2a81f0)
{
    shape::CodePatch cube(0xaeba70, reinterpret_cast<void *>(&StandCubeShape));
    for (uint32_t seed = 0; seed < 600; ++seed)
        Run(ctx, "PrepareBuildAll", seed, 2, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            Globals g;
            m->mRef78.mObject = rng() % 6 ? nullptr : reinterpret_cast<cIGZUnknown *>(&F(kRef78));
            Extend(F(kSg), 3)[23] = reinterpret_cast<void *>(&SgFind);
            gFindHit = rng() % 5 != 0;
            X(mMode).Set(52, 0, rng() % 5 ? 1u : 0x100u);
            gCube = rng() % 6 ? static_cast<void *>(&X(mShape)) : nullptr;
            const uint32_t counts[] = {0, 1, 2, 0xffffffffu};
            X(mShape).value[0] = counts[rng() % 4];
            void **big = Extend(X(mShape), 0);
            big[98] = reinterpret_cast<void *>(&BigCount);
            big[93] = reinterpret_cast<void *>(&ShapeParts);
            const unsigned pick = rng() % 5;
            gPartsTable[0] = pick == 0 ? nullptr : pick % 2 ? static_cast<void *>(&F(kRef78)) : &X(mData);
            gPartsCell = gPartsTable;
            for (Obj *part : {&F(kRef78), &X(mData)}) {
                part->Set(54, "p-", 0u, &RefEntries);
                part->Set(35, "p", 0u);
            }
            X(mState).Set(4, "p", 0u, &IterAtEnd);
            X(mState).Set(5, "p", 0u, &IterEntry);
            X(mState).Set(6, 0, 0u, &IterNext);
            gIterAt = 0;
            gIterCount = rng() % 5;
            gEntrySeed = rng();
            g.Set(0x10f8fcc, rng() % 4 ? static_cast<const void *>(&X(mRegistry)) : nullptr);
            X(mRegistry).Set(33, "pd", 0u, &RegistryText);
            F(kMsg).Set(5, 0, &X(mRegistry)); // the registry's query when the cache is empty: the framework hands out kMsg
            // the pairs already there: 0-3, with 0-2 spare
            if (const unsigned n = rng() % 4, spare = rng() % 3; n + spare) {
                auto *block = static_cast<nTSSG::SGTextPair *>(::operator new((n + spare) * sizeof(nTSSG::SGTextPair)));
                for (unsigned i = 0; i < n; ++i) {
                    ::new (&block[i]) nTSSG::SGTextPair;
                    block[i].mFirst.mString.mString.assign(kTextNames[rng() % 5]);
                    block[i].mSecond.mString.mString.assign(kTextNames[rng() % 5]);
                }
                W(m, 0x7c) = shape::P(block);
                W(m, 0x80) = shape::P(block + n);
                W(m, 0x84) = shape::P(block + n + spare);
            }
            Call<void>(ctx, m, route, kNoFace, 0x2a81f0, shape::MemberCode(&M::PrepareBuildAll));
            CaptureMine();
            Ev(gIterAt);
            Ev(Norm(*GameAddress<uint32_t>(0x10f8fcc)));
            // the pairs by text, their refcount words and the room left; then destroyed here
            Ev(uint32_t(m->mTexts.size()));
            Ev(uint32_t(m->mTexts.capacity()));
            for (const nTSSG::SGTextPair &pair : m->mTexts) {
                LogText(pair.mFirst.mString);
                LogText(pair.mSecond.mString);
                const auto *words = reinterpret_cast<const uint32_t *>(&pair);
                for (unsigned w : {0u, 7u, 8u, 15u}) Ev(words[w]); // the vtables and the words after the strings
            }
            m->mTexts.~vector();
            ::new (&m->mTexts) std::vector<nTSSG::SGTextPair>();
            return 0;
        });
}

// AddFloraOccupant 0x2a5ef0: the flora occupant X(mOccupant) (model name or none (slot 5), placement X(mLot) as in
// AddFaceCOccupant, its turn (slot 7, a float on x87: own slot)); the logger (option bit 7: 0x211da3 stood in, X(mState)
// slot 66 gives X(mRegistry)); +0x30 (kSgMember, slot 23 finds X(mMode) or nothing); the shape chain X(mShape) -> X(mNode)
// -> X(mOwner) (0xaeba70 gives the first; owner X(mEditor); slot 40's object X(mHood) hands out the next, refusing now and
// then; each shape's count (slot 98) and validity (slot 52) drawn so the chain ends). Stood in: 0x211da3, 0xaeba70, 0x211ce1,
// 0x2a8640, 0x1a72c0, 0x2a53f0, 0x2a9000, 0xaeac50, 0x2a9c60 (the fade queue's push: logs the entry), LayerOf.
namespace {
uint32_t gNameAnswer;
uint32_t FloraName(Obj *, uint32_t *a)
{
    reinterpret_cast<cRZString *>(a[0])->mString.assign(gModelName);
    return gNameAnswer;
}
float gTurn;
float __fastcall FloraTurn(Obj *, void *) { Ev(0xf2); return gTurn; }
uint32_t LogModel(Obj *, uint32_t *a)
{
    Ev(0xf3);
    LogText(*reinterpret_cast<const cRZString *>(a[0]));
    LogText(*reinterpret_cast<const cRZString *>(a[1]));
    return 0;
}
unsigned gNextShape, gRefusals;
uint32_t NextShape(Obj *, uint32_t *a)
{
    Ev(0xf4);
    if (gRefusals) {
        --gRefusals;
        return 0x100;
    }
    HandOut(reinterpret_cast<void **>(a[1]), X(gNextShape++ ? mOwner : mNode));
    return 1;
}
void __fastcall StandTell(void *self, void *, void *occupant) { Ev(0xf5); Ev(Norm(shape::P(self))); Ev(Norm(shape::P(occupant))); }
void __fastcall StandQueue(void *self, void *, const nTSSG::SGNodeEntry *entry)
{
    Ev(0xf6);
    Ev(Norm(shape::P(self)));
    Ev(Norm(shape::P(entry->mNode.mObject)));
    Ev(entry->mFaded);
}
} // namespace

DIFF_TEST(NHoodSG_AddFloraOccupant, 0x2a5ef0)
{
    shape::CodePatch state(0x211da3, reinterpret_cast<void *>(&StandState));
    shape::CodePatch cube(0xaeba70, reinterpret_cast<void *>(&StandCubeShape));
    shape::CodePatch scene(0x211ce1, reinterpret_cast<void *>(&StandScene));
    shape::CodePatch placement(0x2a8640, reinterpret_cast<void *>(&StandPlacement));
    shape::CodePatch rotation(0x1a72c0, reinterpret_cast<void *>(&StandRotation));
    shape::CodePatch attach(0x2a53f0, reinterpret_cast<void *>(&StandAttach));
    shape::CodePatch tell(0x2a9000, reinterpret_cast<void *>(&StandTell));
    shape::CodePatch setting(0xaeac50, reinterpret_cast<void *>(&StandSetting));
    shape::CodePatch queue(0x2a9c60, reinterpret_cast<void *>(&StandQueue));
    shape::StandIn layer(0x2a7f20, &M::LayerOf, &StandLayerOf);
    gVB2GroupVT[34] = reinterpret_cast<void *>(&VB2Group);
    for (uint32_t seed = 0; seed < 800; ++seed)
        Run(ctx, "AddFloraOccupant", seed, 2, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            m->mOptionBit7 = uint8_t(rng() % 2);
            m->mBuildAll = uint8_t(rng() % 2);
            m->mLotByte = rng() % 5 ? rng() % 4 : rng();
            for (int i = 0; i < 4; ++i) m->mLayerDirty[i] = uint8_t(rng() % 2);
            gModelName = kModelNames[rng() % 4];
            gNameAnswer = rng() % 5 ? 1u : 0x100u;
            gTurn = float(int(rng() % 721) - 360) / 4.0f;
            for (auto &row : gFloats)
                for (float &f : row) f = float(int(rng() % 2001) - 1000) / 8.0f;
            X(mOccupant).Set(5, "p", 0u, &FloraName);
            X(mOccupant).Set(3, 0, &X(mLot));
            X(mOccupant).value[2] = rng() % 4;
            Extend(X(mOccupant), 4)[7] = reinterpret_cast<void *>(&FloraTurn);
            X(mState).Set(66, 0, &X(mRegistry));
            X(mRegistry).Set(7, "pp-d", 0u, &LogModel);
            gVB2 = VB2Obj{nullptr, kVb2Table, 0, gVB2BaseVT, 40, gVB2GroupVT, rng(), rng() % 2 ? 1u : 0x100u};
            AddRange(&gVB2, sizeof(gVB2), 0xd8000000u);
            X(mLot).Set(12, 0, rng() % 4 ? static_cast<const void *>(&gVB2) : nullptr);
            X(mLot).Set(6, "p", 0u, &WriteFloats0);
            m->mSgSystem.mObject = reinterpret_cast<cIGZUnknown *>(&F(kSgMember));
            Extend(F(kSgMember), 3)[23] = reinterpret_cast<void *>(&SgFind);
            gFindHit = rng() % 5 != 0;
            gCube = &X(mShape);
            gNextShape = 0;
            gRefusals = rng() % 3;
            X(mEditor).Set(46, "p", 0u);
            X(mHood).Set(0, "dp", 0u, &NextShape);
            const unsigned shapes[] = {mShape, mNode, mOwner};
            for (unsigned k = 0; k < 3; ++k) {
                Obj &s = X(shapes[k]);
                s.Set(34, 0, &X(mEditor));
                s.Set(40, 0, &X(mHood));
                s.Set(77, "p", 0u);
                s.Set(64, "p", 0u, &NodeFloats3);
                s.Set(65, "p", 0u, &NodeFloats4);
                const bool valid = rng() % 4 != 0;
                s.value[0] = rng() % 2 ? 0u : 1u + rng() % 2;
                if (k == 2 && valid) s.value[0] = 1; // the chain's end
                s.Set(52, 0, valid ? 1u : 0x100u);
                Extend(s, k)[98] = reinterpret_cast<void *>(&BigCount);
            }
            Call<void>(ctx, m, route, kNoFace, 0x2a5ef0, shape::MemberCode(&M::AddFloraOccupant),
                reinterpret_cast<cIGZUnknown *>(&X(mOccupant)));
            CaptureMine();
            Ev(uint32_t(gVB2.refs));
            Ev(gNextShape);
            return 0;
        });
}

// AddOtherOccupant 0x2a63a0: the occupant X(mOccupant) (record X(mLot) as above, name, turn); the cached system kSg (slot 66:
// the logger X(mRegistry), answering or not; slot 23 SgFind); the found X(mMode); the model X(mEditor) or none (0xaeb9a0
// stood in); the shape from its QI (X(mShape)) or 0xaeb8a0 (stood in: X(mOwner), X(mShape) or none); for build-all
// 0xb3c070 (logged) and 0x267560's extra (VB3Obj: a first virtual base counted, owner X(mNode), its slot 15) or none; the
// singleton X(mSingleton)'s mode object X(mState) (slots 31, 29) or none; the shape's top (slot 72) around 15. Stood in also:
// FootprintIn, 0x2a8640, 0x1a72c0, 0x2a53f0, 0xaeac50, 0x2a9c60.
namespace {
struct VB3Obj {
    void **vt;            // slot 15 logged
    const int32_t *vbptr; // vbtable[1] = 8: the first base at +0xc
    uint32_t pad;
    void **baseVt;        // slots 1, 2 (counted), 18 (owner)
    int32_t refs;
};
int32_t kVb3Table[2] = {-4, 8};
VB3Obj gVB3;
uint32_t __fastcall VB3AddRef(void **base, void *) { Ev(0xf7); return uint32_t(++reinterpret_cast<VB3Obj *>(base - 3)->refs); }
uint32_t __fastcall VB3Release(void **base, void *) { Ev(0xf8); return uint32_t(--reinterpret_cast<VB3Obj *>(base - 3)->refs); }
void *__fastcall VB3Owner(void **base, void *) { Ev(0xf9); Ev(Norm(shape::P(base))); return &X(mNode); }
uint32_t __fastcall VB3Done(VB3Obj *self, void *) { Ev(0xfa); Ev(Norm(shape::P(self))); return 0; }
void *gVB3VT[16] = {};
void *gVB3BaseVT[19] = {};
void *gModelPick;
void *__cdecl StandModelPick(void *found) { Ev(0xfb); Ev(Norm(shape::P(found))); return gModelPick; }
unsigned gShapePick;
void *__cdecl StandShapePick(void *model)
{
    Ev(0xfc);
    Ev(Norm(shape::P(model)));
    return gShapePick == 0 ? nullptr : &X(gShapePick == 1 ? mOwner : mShape);
}
void __cdecl StandPrepareFound(void *found) { Ev(0xfd); Ev(Norm(shape::P(found))); }
void *gExtra;
void *__cdecl StandExtra(void *found) { Ev(0xfe); Ev(Norm(shape::P(found))); return gExtra; }
uint32_t gLogAnswer;
uint32_t LogModelAnswer(Obj *o, uint32_t *a)
{
    LogModel(o, a);
    return gLogAnswer;
}
float gTop;
uint32_t ShapeBounds(Obj *, uint32_t *a)
{
    auto *box = reinterpret_cast<float *>(a[0]);
    for (int i = 0; i < 5; ++i) box[i] = float(i);
    box[5] = gTop;
    return 0;
}
} // namespace

DIFF_TEST(NHoodSG_AddOtherOccupant, 0x2a63a0)
{
    shape::CodePatch modelOf(0xaeb9a0, reinterpret_cast<void *>(&StandModelPick));
    shape::CodePatch shapeOf(0xaeb8a0, reinterpret_cast<void *>(&StandShapePick));
    shape::CodePatch prepare(0xb3c070, reinterpret_cast<void *>(&StandPrepareFound));
    shape::CodePatch extra(0x267560, reinterpret_cast<void *>(&StandExtra));
    shape::CodePatch placement(0x2a8640, reinterpret_cast<void *>(&StandPlacement));
    shape::CodePatch rotation(0x1a72c0, reinterpret_cast<void *>(&StandRotation));
    shape::CodePatch attach(0x2a53f0, reinterpret_cast<void *>(&StandAttach));
    shape::CodePatch setting(0xaeac50, reinterpret_cast<void *>(&StandSetting));
    shape::CodePatch queue(0x2a9c60, reinterpret_cast<void *>(&StandQueue));
    shape::StandIn footprint(0x2a8570, &M::FootprintIn, &StandFootprintIn);
    gVB2GroupVT[34] = reinterpret_cast<void *>(&VB2Group);
    gVB3VT[15] = reinterpret_cast<void *>(&VB3Done);
    gVB3BaseVT[1] = reinterpret_cast<void *>(&VB3AddRef);
    gVB3BaseVT[2] = reinterpret_cast<void *>(&VB3Release);
    gVB3BaseVT[18] = reinterpret_cast<void *>(&VB3Owner);
    for (uint32_t seed = 0; seed < 800; ++seed)
        Run(ctx, "AddOtherOccupant", seed, 2, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            Globals g;
            m->mOptionBit7 = uint8_t(rng() % 2);
            m->mBuildAll = uint8_t(rng() % 2);
            m->mLotByte = rng() % 5 ? rng() % 4 : rng();
            for (int i = 0; i < 4; ++i) m->mViewRect[i] = int32_t(rng() % 100);
            gModelName = kModelNames[rng() % 4];
            gNameAnswer = rng() % 6 ? 1u : 0x100u;
            gTurn = float(int(rng() % 721) - 360) / 4.0f;
            for (auto &row : gFloats)
                for (float &f : row) f = float(int(rng() % 2001) - 1000) / 8.0f;
            X(mOccupant).Set(5, "p", 0u, &FloraName);
            X(mOccupant).Set(3, 0, &X(mLot));
            Extend(X(mOccupant), 4)[7] = reinterpret_cast<void *>(&FloraTurn);
            gVB2 = VB2Obj{nullptr, kVb2Table, 0, gVB2BaseVT, 40, gVB2GroupVT, rng(), rng() % 2 ? 1u : 0x100u};
            AddRange(&gVB2, sizeof(gVB2), 0xd8000000u);
            X(mLot).Set(12, 0, rng() % 4 ? static_cast<const void *>(&gVB2) : nullptr);
            X(mLot).Set(6, "p", 0u, &WriteFloats0);
            F(kSg).Set(66, 0, &X(mRegistry));
            X(mRegistry).Set(7, "pp-d", 0u, &LogModelAnswer);
            gLogAnswer = rng() % 2 ? 1u : 0x100u;
            Extend(F(kSg), 3)[23] = reinterpret_cast<void *>(&SgFind);
            gFindHit = rng() % 6 != 0;
            X(mMode).Set(52, 0, rng() % 5 ? 1u : 0x100u);
            X(mMode).Set(46, "p", 0u);
            gModelPick = rng() % 6 ? static_cast<void *>(&X(mEditor)) : nullptr;
            X(mEditor).Set(0, "dp", 0u, &MineQuery);
            X(mEditor).value[0] = rng() % 2;
            X(mEditor).value[1] = mShape;
            X(mEditor).Set(46, "p", 0u);
            gShapePick = rng() % 4 == 0 ? 0 : 1 + rng() % 2;
            gVB3 = VB3Obj{gVB3VT, kVb3Table, 0, gVB3BaseVT, 30};
            AddRange(&gVB3, sizeof(gVB3), 0xd9000000u);
            gExtra = rng() % 3 ? static_cast<void *>(&gVB3) : nullptr;
            X(mNode).Set(23, "p", 0u);
            gTop = std::array<float, 4>{10.0f, 15.0f, 15.5f, 100.0f}[rng() % 4];
            gFootprintAnswers = rng();
            gRectSet = true;
            for (unsigned k : {mShape, mOwner}) {
                Obj &s = X(k);
                s.Set(22, "p", 0u);
                s.Set(77, "p", 0u);
                s.Set(64, "p", 0u, &NodeFloats3);
                s.Set(65, "p", 0u, &NodeFloats4);
                s.Set(72, "p", 0u, &ShapeBounds);
                Extend(s, k == mShape ? 0 : 1)[94] = reinterpret_cast<void *>(&BigPart);
            }
            g.Set(0x10cc288, &X(mSingleton));
            X(mSingleton).Set(33, 0, rng() % 4 ? static_cast<const void *>(&X(mState)) : nullptr);
            X(mState).Set(31, 0, rng() % 3 ? 1u : 2u);
            X(mState).Set(29, 0, rng() % 2);
            const uint32_t result = Call<bool>(ctx, m, route, kNoFace, 0x2a63a0, shape::MemberCode(&M::AddOtherOccupant),
                reinterpret_cast<cIGZUnknown *>(&X(mOccupant)));
            CaptureMine();
            Ev(uint32_t(gVB2.refs));
            Ev(uint32_t(gVB3.refs));
            return result;
        });
}

// AddRoadOccupant 0x2a69b0: the framework (slot 3) makes the builders (kNode1, slot 32), the meshes (MeshObj: counted
// through a virtual base, its own slots logged), the parts (kNode0, slot 41) and the group (kFlora, slots 94 and 77); one
// creation may be refused (never the group's: the original uses it untested). The occupant X(mOccupant): its quad (slot 5),
// id (slot 7), 0-4 pieces (slots 15, 16: answered 1, 0x100 or 0x201), its record X(mLot) (slot 10). The hood's name at +0x3c
// (short, so both sides' strings stay inline). The names are built by each side's cRZString::Sprintf (the game's 0x1ad6f7
// on the original side, ours, verified, on ours) and logged as text by the stand-in for 0xaed340. Stood in: 0x2a5900 (the
// normal), 0xaed340, 0xaeb070, 0x2a53f0.
namespace {
struct MeshObj {
    void **vt;
    const int32_t *vbptr; // vbtable[1] = 8: the counted base at +0xc
    uint32_t pad;
    void **baseVt;
    int32_t refs;
};
int32_t kMeshVbTable[2] = {-4, 8};
MeshObj gMesh;
void *gMeshVT[40];
void *gMeshBaseVT[3];
uint32_t __fastcall MeshAddRef(void **base, void *) { Ev(0x130); return uint32_t(++reinterpret_cast<MeshObj *>(base - 3)->refs); }
uint32_t __fastcall MeshRelease(void **base, void *) { Ev(0x131); return uint32_t(--reinterpret_cast<MeshObj *>(base - 3)->refs); }
uint32_t __fastcall MeshArg1(MeshObj *self, void *, uint32_t a) { Ev(0x132); Ev(Norm(shape::P(self))); Ev(a); return 0; }
uint32_t __fastcall MeshArgs2(MeshObj *self, void *, uint32_t a, uint32_t b)
{
    Ev(0x133);
    Ev(Norm(shape::P(self)));
    Ev(a);
    Ev(b);
    return 0;
}
uint32_t __fastcall MeshNone(MeshObj *self, void *) { Ev(0x134); Ev(Norm(shape::P(self))); return 0; }
uint32_t __fastcall MeshFloats3(MeshObj *self, void *, const float *f) { Ev(0x135); Ev(Norm(shape::P(self))); LogFloats(f, 3); return 0; }
uint32_t __fastcall MeshFloats2(MeshObj *self, void *, const float *f, uint32_t a)
{
    Ev(0x136);
    Ev(Norm(shape::P(self)));
    LogFloats(f, 2);
    Ev(a);
    return 0;
}
uint32_t __fastcall MeshText(MeshObj *self, void *, const char *text)
{
    Ev(0x137);
    Ev(Norm(shape::P(self)));
    for (const char *p = text; *p; ++p) Ev(uint8_t(*p));
    return 0;
}
// distinct slots log their number first (a slot mix-up shows)
template <unsigned S> uint32_t __fastcall MeshSlot1(MeshObj *self, void *e, uint32_t a) { Ev(S); return MeshArg1(self, e, a); }
template <unsigned S> uint32_t __fastcall MeshSlot0(MeshObj *self, void *e) { Ev(S); return MeshNone(self, e); }
template <unsigned S> uint32_t __fastcall MeshSlotV(MeshObj *self, void *e, const float *f) { Ev(S); return MeshFloats3(self, e, f); }

int gFailAt, gMade;
uint32_t FwRoad(Obj *, uint32_t *a)
{
    auto **out = reinterpret_cast<void **>(a[2]);
    Ev(0x138);
    Ev(a[0]);
    Ev(a[1]);
    const bool group = a[0] == 0x65245517;
    if (!group && gMade++ == gFailAt) return 0;
    if (a[0] == 0x7ba3838c) HandOut(out, F(kNode1));
    else if (a[0] == 0x9bffc10d) *out = &gMesh, ++gMesh.refs;
    else if (a[0] == 0xfc6eb1f7) HandOut(out, F(kNode0));
    else HandOut(out, F(kFlora));
    return 1;
}
float gQuad[4][5];
float gPiece[4][15];
uint32_t gPieceId[4], gPieceAnswer[4];
uint32_t RoadQuad(Obj *, uint32_t *a) { std::memcpy(reinterpret_cast<void *>(a[0]), gQuad, sizeof(gQuad)); return 0; }
uint32_t RoadPiece(Obj *, uint32_t *a)
{
    const uint32_t i = a[0] % 4;
    auto *out = reinterpret_cast<uint32_t *>(a[1]);
    out[0] = gPieceId[i];
    std::memcpy(out + 1, gPiece[i], sizeof(gPiece[i]));
    return gPieceAnswer[i];
}
void __cdecl StandNormal(float *out, const float *a, const float *b, const float *c)
{
    Ev(0x139);
    LogFloats(a, 3);
    LogFloats(b, 3);
    LogFloats(c, 3);
    out[0] = a[0] - b[1];
    out[1] = c[2];
    out[2] = 0.5f;
}
void __cdecl StandNameBuilder(const char *text, void *builder, void *part)
{
    Ev(0x13a);
    for (const char *p = text; *p; ++p) Ev(uint8_t(*p));
    Ev(Norm(shape::P(builder)));
    Ev(Norm(shape::P(part)));
}
const char *const kRoadHoodNames[] = {"", "N001", "Pleasantview"};
} // namespace

DIFF_TEST(NHoodSG_AddRoadOccupant, 0x2a69b0)
{
    shape::CodePatch normal(0x2a5900, reinterpret_cast<void *>(&StandNormal));
    shape::CodePatch names(0xaed340, reinterpret_cast<void *>(&StandNameBuilder));
    shape::CodePatch priority(0xaeb070, reinterpret_cast<void *>(&StandLayerPriority));
    shape::CodePatch attach(0x2a53f0, reinterpret_cast<void *>(&StandAttach));
    gMeshBaseVT[1] = reinterpret_cast<void *>(&MeshAddRef);
    gMeshBaseVT[2] = reinterpret_cast<void *>(&MeshRelease);
    gMeshVT[0] = reinterpret_cast<void *>(&MeshSlot1<0x140>);
    gMeshVT[1] = reinterpret_cast<void *>(&MeshSlot0<0x141>);
    gMeshVT[9] = reinterpret_cast<void *>(&MeshSlotV<0x149>);
    gMeshVT[10] = reinterpret_cast<void *>(&MeshSlotV<0x14a>);
    gMeshVT[11] = reinterpret_cast<void *>(&MeshFloats2);
    gMeshVT[27] = reinterpret_cast<void *>(&MeshText);
    gMeshVT[28] = reinterpret_cast<void *>(&MeshSlot0<0x15c>);
    gMeshVT[29] = reinterpret_cast<void *>(&MeshSlot0<0x15d>);
    gMeshVT[35] = reinterpret_cast<void *>(&MeshSlot1<0x163>);
    gMeshVT[36] = reinterpret_cast<void *>(&MeshSlot1<0x164>);
    gMeshVT[37] = reinterpret_cast<void *>(&MeshArgs2);
    for (uint32_t seed = 0; seed < 600; ++seed)
        Run(ctx, "AddRoadOccupant", seed, 2, true, [&](M *m, int route, std::mt19937 &rng) -> uint32_t {
            ResetMine();
            m->mNeighborhoodName.mString.mString.assign(kRoadHoodNames[rng() % 3]);
            for (auto &v : gQuad)
                for (float &f : v) f = float(int(rng() % 2001) - 1000) / 8.0f;
            for (auto &p : gPiece)
                for (float &f : p) f = float(int(rng() % 2001) - 1000) / 8.0f;
            const uint32_t answers[] = {1, 0x100, 0x201};
            for (int i = 0; i < 4; ++i) gPieceId[i] = rng(), gPieceAnswer[i] = answers[rng() % 3];
            gMade = 0;
            gFailAt = rng() % 3 ? -1 : int(rng() % 12);
            gMesh = MeshObj{gMeshVT, kMeshVbTable, 0, gMeshBaseVT, 50};
            AddRange(&gMesh, sizeof(gMesh), 0xda000000u);
            F(kFw).Set(3, "ddp", 0u, &FwRoad);
            F(kNode1).Set(32, "p", 0u);
            F(kNode0).Set(41, "p-", 0u);
            F(kFlora).Set(77, "p", 0u);
            Extend(F(kFlora), 2)[94] = reinterpret_cast<void *>(&BigPart);
            X(mOccupant).Set(5, "p", 0u, &RoadQuad);
            X(mOccupant).Set(7, 0, rng());
            X(mOccupant).Set(15, 0, rng() % 5);
            X(mOccupant).Set(16, "dp", 0u, &RoadPiece);
            X(mOccupant).Set(3, 0, &X(mLot));
            X(mLot).Set(10, 1, 0u);
            const uint32_t result = Call<bool>(ctx, m, route, kNoFace, 0x2a69b0, shape::MemberCode(&M::AddRoadOccupant),
                reinterpret_cast<cIGZUnknown *>(&X(mOccupant)));
            CaptureMine();
            Ev(uint32_t(gMesh.refs));
            Ev(uint32_t(F(kNode0).refs));
            Ev(uint32_t(F(kNode1).refs));
            Ev(uint32_t(F(kFlora).refs));
            Ev(uint32_t(gMade));
            return result;
        });
}
