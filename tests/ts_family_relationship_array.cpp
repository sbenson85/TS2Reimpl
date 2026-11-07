// Differential tests for cTSFamilyRelationshipArray (src/ts/cTSFamilyRelationshipArray.*).
//
// Standalone shape (docs/standalone-shape.md): the original side's array is built by the original constructor and the code
// is called at its RVA; ours is built by our constructor (the constructor hook's entry) and called through its own vtable
// slot, as the game's callers reach it. The vtable words are the only bytes that differ by design (PushVTableWords).
// FamilyRelationshipArray_Slots covers the slots no other test calls; FamilyRelationshipArray_Adapters the RVA hooks'
// entry points on an object carrying the game's vtables.
//
// The world: an array built on each side, its family map filled with recording relationship
// fakes (slot 1 AddRef / 2 Release logged with the fake's id, slot 14 returns a serializable face whose slots 3 / 4 log
// and answer from a seeded generator), a recording stream (16-bit ids from a small range so duplicates occur), and the
// relationship constructor 0x4f1fd0 patched to build such a fake. A run compares the return value, the event log and the
// array's state (map contents in order, dirty flag, refcounts of every fake).
#include "game_allocator.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "hook/hooks.h"
#include "ifaces/cITSFamilyRelationship.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "ts/cTSFamilyRelationship.h"
#include "ts/cTSFamilyRelationshipArray.h"

#include <windows.h>

#include <cstring>
#include <map>
#include <random>
#include <vector>

namespace {

using Blob = std::vector<uint32_t>;

enum : uint32_t {
    evAddRef = 1, evRelease, evSerRead, evSerWrite, evRead16, evRead32, evWrite16, evWrite32, evSetMeta, evQuery,
    evPersist, evCreate, evOwnRead, evOwnWrite, evOwnAddRef,
};

Blob gEv;
void Ev(std::initializer_list<uint32_t> words) { gEv.insert(gEv.end(), words); }
std::mt19937 gRng;

// ---- relationship fakes ----
struct Rel {
    void **vt;
    uint32_t id;
    int32_t refs;
    void **serVt; // +0x0c: the serializable face slot 14 hands out
};
static_assert(sizeof(Rel) <= 0x28, "a fake must fit the original's allocation");
void *gRelVT[16];
void *gSerVT[8];
constexpr uint32_t kMaxRels = 64;
Rel gRels[kMaxRels];
uint32_t gCreated = 0;

uint32_t RelId(const void *p)
{
    if (!p) return 0;
    return static_cast<const Rel *>(p)->id;
}
uint32_t __fastcall RelAddRef(Rel *self, void *)
{
    Ev({evAddRef, self->id});
    return uint32_t(++self->refs);
}
uint32_t __fastcall RelRelease(Rel *self, void *)
{
    Ev({evRelease, self->id});
    return uint32_t(--self->refs);
}
void *__fastcall RelSerial(Rel *self, void *) { return &self->serVt; }
Rel *SerOwner(void *face) { return reinterpret_cast<Rel *>(static_cast<uint8_t *>(face) - 0xc); }
uint8_t __fastcall SerWrite(void *face, void *, void *stream)
{
    const uint8_t r = gRng() % 5 != 0;
    Ev({evSerWrite, SerOwner(face)->id, stream ? 1u : 0u, r});
    return r;
}
uint8_t __fastcall SerRead(void *face, void *, void *stream)
{
    const uint8_t r = gRng() % 5 != 0;
    Ev({evSerRead, SerOwner(face)->id, stream ? 1u : 0u, r});
    return r;
}
void InitRel(Rel *r, uint32_t id)
{
    r->vt = gRelVT;
    r->id = id;
    r->refs = 0;
    r->serVt = gSerVT;
}
void ResetFakes()
{
    for (auto &s : gRelVT) s = nullptr;
    gRelVT[1] = reinterpret_cast<void *>(&RelAddRef);
    gRelVT[2] = reinterpret_cast<void *>(&RelRelease);
    gRelVT[14] = reinterpret_cast<void *>(&RelSerial);
    for (auto &s : gSerVT) s = nullptr;
    gSerVT[3] = reinterpret_cast<void *>(&SerWrite);
    gSerVT[4] = reinterpret_cast<void *>(&SerRead);
    for (uint32_t i = 0; i < kMaxRels; ++i) InitRel(&gRels[i], 100 + i);
    gCreated = 0;
}
// Stand-in for cTSFamilyRelationship's constructor (0x4f1fd0): the new object becomes a fake numbered by creation.
void *__fastcall FakeRelConstruct(void *self, void *)
{
    InitRel(static_cast<Rel *>(self), 500 + gCreated++);
    Ev({evCreate, static_cast<Rel *>(self)->id});
    return self;
}
// Stand-in for cTSFamilyRelationship::Create (ours): the same fake, on fresh memory.
void *__cdecl FakeRelCreate()
{
    return FakeRelConstruct(::operator new(0x28), nullptr);
}

// ---- stream fake ----
struct Stream {
    void **vt;
    uint32_t reads32 = 0;
    uint32_t maxCount = 5;
} gStream;
void *gStreamVT[24];
uint8_t __fastcall Read16(Stream *, void *, uint16_t *out)
{
    const uint16_t v = uint16_t(gRng() % 3 == 0 ? gRng() : gRng() % 6);
    Ev({evRead16, v});
    *out = v;
    return 1;
}
uint8_t __fastcall Read32(Stream *s, void *, uint32_t *out)
{
    const uint32_t n = s->reads32++;
    const uint32_t v = n == 0 ? gRng() : gRng() % (s->maxCount + 1);
    Ev({evRead32, n, v});
    *out = v;
    return 1;
}
uint8_t __fastcall Write16(Stream *, void *, uint32_t v) // the argument slot is a dword; only the low word is the value
{
    Ev({evWrite16, v & 0xffff});
    return 1;
}
uint8_t __fastcall Write32(Stream *, void *, uint32_t v)
{
    Ev({evWrite32, v});
    return 1;
}
void __fastcall SetMeta(Stream *, void *, void *metadata)
{
    uint32_t words[5];
    std::memcpy(words, metadata, sizeof(words));
    Ev({evSetMeta, words[0] ? 1u : 0u, words[1], words[2], words[3], words[4]});
    ::operator delete(metadata, 0x14);
}
void ResetStream(bool reading, uint32_t maxCount)
{
    for (auto &s : gStreamVT) s = nullptr;
    gStreamVT[7] = reading ? reinterpret_cast<void *>(&Read16) : reinterpret_cast<void *>(&Write16);
    gStreamVT[9] = reading ? reinterpret_cast<void *>(&Read32) : reinterpret_cast<void *>(&Write32);
    gStreamVT[19] = reinterpret_cast<void *>(&SetMeta);
    gStream.vt = gStreamVT;
    gStream.reads32 = 0;
    gStream.maxCount = maxCount;
}

// ---- the array ----
// Side 0 is the original: the object is built by the original constructor and carries the game's vtables, and the code
// under test is called at its RVA. Side 1 is ours: built by our constructor (the hook entry the game calls), carrying our
// vtables, and every call goes through the object's own vtable slot, as the game's callers reach it.
using famrel::kContainerFace;
using famrel::kPersistFace;
using famrel::kSerialFace;

int gSide = 0; // the side a run is on (the fakes that create arrays build them the side's way)

// Our side's construction: the constructor hook's entry, on fresh memory.
void ConstructSide(int side, difftest::Context &ctx, void *memory)
{
    if (side == 0) ctx.Original<void *(__thiscall *)(void *)>(0x4f2070)(memory);
    else cTSFamilyRelationshipArrayAdapters::Construct(memory, nullptr);
}

// Slot `slot` of the vtable at `face` (the object's own), called with `this` = face. Byte returns are compared raw (al).
template <class R, class... A> R CallSlot(const void *face, int slot, A... args)
{
    using Fn = R(__thiscall *)(const void *, A...);
    return (*static_cast<Fn *const *>(face))[slot](face, args...);
}

// Our four vtables, from an object built by our constructor (the Slots test checks each slot's behaviour against the
// original's; the Construct and Create tests check the constructor installs exactly these).
struct OurVTables {
    const void *face[4];
};
OurVTables OursFromConstructor()
{
    alignas(8) uint8_t raw[sizeof(cTSFamilyRelationshipArray)];
    auto *a = cTSFamilyRelationshipArrayAdapters::Construct(raw, nullptr);
    OurVTables v;
    std::memcpy(v.face, raw, sizeof(v.face));
    a->~cTSFamilyRelationshipArray();
    return v;
}
constexpr uint32_t kOriginalVTables[4] = {cTSFamilyRelationshipArray::kOriginalVTable, cTSFamilyRelationshipArray::kOriginalContainerVTable,
                                          cTSFamilyRelationshipArray::kOriginalPersistVTable, cTSFamilyRelationshipArray::kOriginalSerialVTable};
// The four vtable words of a side's object, as one flag each: the game's on the original side, ours on our side (and ours
// are not the game's).
void PushVTableWords(Blob &b, difftest::Context &ctx, int side, const uint32_t *words)
{
    const OurVTables ours = OursFromConstructor();
    for (int i = 0; i < 4; ++i) {
        const uint32_t game = uint32_t(uintptr_t(ctx.image.At<void>(kOriginalVTables[i])));
        const uint32_t want = side == 0 ? game : uint32_t(uintptr_t(ours.face[i]));
        b.push_back(words[i] == want && uint32_t(uintptr_t(ours.face[i])) != game);
    }
}

struct Array {
    alignas(8) uint8_t raw[sizeof(cTSFamilyRelationshipArray)];
    cTSFamilyRelationshipArray *Get() { return reinterpret_cast<cTSFamilyRelationshipArray *>(raw); }
    uint8_t *Face(uint32_t offset) { return raw + offset; }
    void Build(difftest::Context &ctx, std::mt19937 &rng, unsigned count, int side)
    {
        std::memset(raw, 0xcd, sizeof(raw));
        ConstructSide(side, ctx, raw);
        auto *a = Get();
        for (unsigned i = 0; i < count; ++i) {
            const uint16_t key = uint16_t(rng() % 3 == 0 ? rng() : rng() % 8);
            Rel *rel = rng() % 7 == 0 ? nullptr : &gRels[rng() % 24];
            a->mRelations.try_emplace(key, FamilyRelationRef(reinterpret_cast<ifaces::cITSFamilyRelationship *>(rel)));
        }
        a->mRefCount = rng() % 4;
        a->mDirty = uint8_t(rng() % 2);
    }
    void Destroy() { Get()->mRelations.~FamilyRelationMap(); }
};

void Capture(Blob &b, cTSFamilyRelationshipArray *a)
{
    b.insert(b.end(), {0xeeee0000u, uint32_t(a->mRelations.size()), a->mDirty, a->mRefCount});
    for (const auto &e : a->mRelations) b.insert(b.end(), {e.first, RelId(e.second.Get())});
    for (const auto &r : gRels) b.push_back(uint32_t(r.refs));
}

void Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    if (a == b) return;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    ctx.Fail("%s seed %u: differ at word %u (sizes %u/%u): %08x vs %08x", what, seed, unsigned(i), unsigned(a.size()),
             unsigned(b.size()), i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0);
}

// Redirects a function of the mapped image to a stand-in (a jmp over its first five bytes) while it lives.
struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(difftest::Context &ctx, uint32_t rva, void *stand) : CodePatch(ctx.image.At<uint8_t>(rva), stand) {}
    CodePatch(uint8_t *where, void *stand) : site(where)
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    CodePatch(const CodePatch &) = delete;
    ~CodePatch() { std::memcpy(site, saved, 5); }
};

using SerialFn = uint8_t(__thiscall *)(void *, void *);

uint32_t __fastcall OwnAddRef(cTSFamilyRelationshipArray *self, void *)
{
    Ev({evOwnAddRef});
    return ++self->mRefCount;
}

} // namespace

DIFF_TEST(FamilyRelationshipArray_Construct, 0x4f2070)
{
    for (uint32_t seed = 0; seed < 4; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            alignas(8) uint8_t raw[sizeof(cTSFamilyRelationshipArray)];
            std::memset(raw, int(0x11 * (seed + 1)), sizeof(raw));
            auto *a = reinterpret_cast<cTSFamilyRelationshipArray *>(raw);
            void *r = side == 0 ? ctx.Original<void *(__thiscall *)(void *)>(0x4f2070)(raw) : cTSFamilyRelationshipArrayAdapters::Construct(raw, nullptr);
            blobs[side].push_back(r == raw ? 1u : 0u);
            uint32_t words[12];
            std::memcpy(words, raw, sizeof(words));
            PushVTableWords(blobs[side], ctx, side, words);
            for (int i = 4; i < 10; ++i) blobs[side].push_back(words[i]); // +0x28 is the head node pointer
            const uint8_t *head = reinterpret_cast<const uint8_t *>(words[10]);
            uint32_t links[3];
            std::memcpy(links, head, sizeof(links));
            blobs[side].insert(blobs[side].end(), {links[0] == words[10], links[1] == words[10], links[2] == words[10],
                                                    head[0xc], head[0xd], words[11]});
            a->mRelations.~FamilyRelationMap();
        }
        Same(ctx, blobs[0], blobs[1], "Construct", seed);
    }
}

DIFF_TEST(FamilyRelationshipArray_QueryInterface, 0x4f2f00)
{
    static void *vt[18];
    const uint32_t ids[] = {0, 1, 2, 0x8c87073f, 0x8c870740, 0x8c87073e, 0x456b8f1d, 0x8db5bf48, 0xe4fda3d4, 0xe4fda3d5, 0xffffffff};
    for (uint32_t id : ids) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            ResetFakes();
            std::mt19937 rng(id);
            Array arr;
            arr.Build(ctx, rng, 0, side);
            // The side's own vtable with a recording AddRef.
            std::memcpy(vt, *reinterpret_cast<void **>(arr.raw), sizeof(vt));
            vt[1] = reinterpret_cast<void *>(&OwnAddRef);
            *reinterpret_cast<void **>(arr.raw) = vt;
            void *out = reinterpret_cast<void *>(0x1234);
            gEv.clear();
            const uint8_t r = side == 0
                ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0x4f2f00)(arr.raw, id, &out)
                : CallSlot<uint8_t>(arr.raw, 0, id, &out);
            blobs[side] = gEv;
            blobs[side].insert(blobs[side].end(), {r, uint32_t(static_cast<uint8_t *>(out) - arr.raw), arr.Get()->mRefCount});
            arr.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "QueryInterface", id);
    }
}

DIFF_TEST(FamilyRelationshipArray_Release, 0x4f3770)
{
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            ResetFakes();
            std::mt19937 rng(seed);
            auto *arr = static_cast<Array *>(::operator new(sizeof(Array)));
            arr->Build(ctx, rng, rng() % 12, side);
            const uint32_t refs = seed % 6 == 5 ? 0xffffffffu : seed % 4;
            arr->Get()->mRefCount = refs;
            using ReleaseFn = uint32_t(__thiscall *)(void *);
            const ReleaseFn release = side == 0 ? ctx.Original<ReleaseFn>(0x4f3770) : (*reinterpret_cast<ReleaseFn **>(arr->raw))[2];
            // Release makes no virtual call on itself: the vtable words are cleared (the destructor writes them back).
            *reinterpret_cast<void **>(arr->raw) = nullptr;
            *reinterpret_cast<void **>(arr->Face(kSerialFace)) = nullptr;
            gEv.clear();
            difftest::FreeRecorder rec;
            const uint32_t r = release(arr->raw);
            rec.Stop();
            blobs[side] = gEv;
            blobs[side].push_back(r);
            const bool freed = rec.Freed(arr);
            blobs[side].push_back(freed);
            if (!freed) {
                Capture(blobs[side], arr->Get());
                arr->Destroy();
                ::operator delete(arr);
            } else {
                for (const auto &rel : gRels) blobs[side].push_back(uint32_t(rel.refs));
            }
        }
        Same(ctx, blobs[0], blobs[1], "Release", seed);
    }
}

DIFF_TEST(FamilyRelationshipArray_SetRelationship, 0x4f21a0)
{
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            ResetFakes();
            std::mt19937 rng(seed);
            Array arr;
            arr.Build(ctx, rng, rng() % 10, side);
            const uint16_t key = uint16_t(rng() % 4 == 0 ? rng() : rng() % 9);
            Rel *rel = rng() % 6 == 0 ? nullptr : &gRels[rng() % 24];
            if (rng() % 5 == 0) { // the same object that is already there
                auto it = arr.Get()->mRelations.find(key);
                if (it != arr.Get()->mRelations.end()) rel = reinterpret_cast<Rel *>(it->second.Get());
            }
            gEv.clear();
            const uint8_t r = side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void *)>(0x4f21a0)(arr.raw, key, rel)
                                        : CallSlot<uint8_t>(arr.raw, 3, uint32_t(key), static_cast<void *>(rel));
            blobs[side] = gEv;
            blobs[side].push_back(r);
            Capture(blobs[side], arr.Get());
            arr.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "SetRelationship", seed);
    }
}

DIFF_TEST(FamilyRelationshipArray_RemoveRelationship, 0x4f38a0)
{
    for (uint32_t seed = 0; seed < 200; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            ResetFakes();
            std::mt19937 rng(seed);
            Array arr;
            arr.Build(ctx, rng, rng() % 10, gSide);
            const uint16_t key = uint16_t(rng() % 4 == 0 ? rng() : rng() % 9);
            gEv.clear();
            const uint8_t r = side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t)>(0x4f38a0)(arr.raw, key)
                                        : CallSlot<uint8_t>(arr.raw, 4, uint32_t(key));
            blobs[side] = gEv;
            blobs[side].push_back(r);
            Capture(blobs[side], arr.Get());
            arr.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "RemoveRelationship", seed);
    }
}

DIFF_TEST(FamilyRelationshipArray_GetRelationship, 0x4f2db0)
{
    for (uint32_t seed = 0; seed < 200; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            ResetFakes();
            std::mt19937 rng(seed);
            Array arr;
            arr.Build(ctx, rng, rng() % 10, gSide);
            const uint16_t key = uint16_t(rng() % 4 == 0 ? rng() : rng() % 9);
            gEv.clear();
            void *r = side == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x4f2db0)(arr.raw, key)
                                : CallSlot<void *>(arr.raw, 7, uint32_t(key));
            blobs[side] = gEv;
            blobs[side].push_back(RelId(r));
            Capture(blobs[side], arr.Get());
            arr.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "GetRelationship", seed);
    }
}

DIFF_TEST(FamilyRelationshipArray_SetDirty, 0x4f2e80)
{
    for (uint32_t seed = 0; seed < 8; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            ResetFakes();
            std::mt19937 rng(seed);
            Array arr;
            arr.Build(ctx, rng, 2, gSide);
            arr.Get()->mDirty = uint8_t(seed * 37);
            if (side == 0) ctx.Original<void(__thiscall *)(void *)>(0x4f2e80)(arr.raw);
            else CallSlot<void>(arr.raw, 6);
            Capture(blobs[side], arr.Get());
            arr.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "SetDirty", seed);
    }
}

DIFF_TEST(FamilyRelationshipArray_Clear, 0x4f3d90)
{
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            ResetFakes();
            std::mt19937 rng(seed);
            Array arr;
            arr.Build(ctx, rng, rng() % 12, gSide);
            gEv.clear();
            const uint8_t r = side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *)>(0x4f3d90)(arr.raw) : CallSlot<uint8_t>(arr.raw, 17);
            blobs[side] = gEv;
            blobs[side].push_back(r);
            Capture(blobs[side], arr.Get());
            // The head must be a valid empty tree again.
            const uint8_t *head = *reinterpret_cast<uint8_t *const *>(&arr.raw[0x28]);
            uint32_t links[3];
            std::memcpy(links, head, sizeof(links));
            blobs[side].insert(blobs[side].end(), {links[0] == uint32_t(uintptr_t(head)), links[1] == uint32_t(uintptr_t(head)),
                                                    links[2] == uint32_t(uintptr_t(head))});
            arr.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Clear", seed);
    }
}

DIFF_TEST(FamilyRelationshipArray_Write, 0x4f3e70)
{
    for (uint32_t seed = 0; seed < 200; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            ResetFakes();
            std::mt19937 rng(seed);
            Array arr;
            arr.Build(ctx, rng, rng() % 10, gSide);
            // Null relationships would crash the original (it calls slot 14 unchecked): give every entry an object.
            for (auto &e : arr.Get()->mRelations)
                if (!e.second.Get()) e.second.Assign(reinterpret_cast<ifaces::cITSFamilyRelationship *>(&gRels[30 + e.first % 20]));
            ResetStream(false, 0);
            gRng.seed(seed * 3 + 1);
            gEv.clear();
            void *face = arr.Face(kSerialFace);
            const uint8_t r = side == 0 ? ctx.Original<SerialFn>(0x4f3e70)(face, &gStream) : CallSlot<uint8_t>(face, 3, static_cast<void *>(&gStream));
            blobs[side] = gEv;
            blobs[side].push_back(r);
            Capture(blobs[side], arr.Get());
            arr.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Write", seed);
    }
}

DIFF_TEST(FamilyRelationshipArray_Read, 0x4f30b0)
{
    CodePatch patch(ctx, 0x4f1fd0, reinterpret_cast<void *>(&FakeRelConstruct));
    // Ours calls cTSFamilyRelationship::Create (a real constructor has no address to patch), redirected to a stand-in.
    CodePatch patchOurs(reinterpret_cast<uint8_t *>(&cTSFamilyRelationship::Create), reinterpret_cast<void *>(&FakeRelCreate));
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            ResetFakes();
            std::mt19937 rng(seed);
            Array arr;
            arr.Build(ctx, rng, rng() % 6, gSide);
            ResetStream(true, seed % 4 == 0 ? 12 : 5);
            gRng.seed(seed * 5 + 2);
            gEv.clear();
            void *face = arr.Face(kSerialFace);
            const uint8_t r = side == 0 ? ctx.Original<SerialFn>(0x4f30b0)(face, &gStream) : CallSlot<uint8_t>(face, 4, static_cast<void *>(&gStream));
            blobs[side] = gEv;
            blobs[side].push_back(r);
            Capture(blobs[side], arr.Get());
            arr.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Read", seed);
    }
}

DIFF_TEST(FamilyRelationshipArray_GetGZCLSID, 0x4f2770)
{
    ResetFakes();
    std::mt19937 rng(1);
    Array arr[2];
    arr[0].Build(ctx, rng, 0, 0);
    arr[1].Build(ctx, rng, 0, 1);
    const uint32_t a = ctx.Original<uint32_t(__thiscall *)(void *)>(0x4f2770)(arr[0].Face(kSerialFace));
    const uint32_t b = CallSlot<uint32_t>(arr[1].Face(kSerialFace), 5);
    // The COM director's constructor calls the old entry directly (0x415ae0): the adapter answers the same.
    const uint32_t c = cTSFamilyRelationshipArrayAdapters::GetGZCLSID(reinterpret_cast<cIGZSerializable *>(arr[1].Face(kSerialFace)), nullptr);
    Same(ctx, Blob{a, a}, Blob{b, c}, "GetGZCLSID", 0);
    for (auto &x : arr) x.Destroy();
}

// ---- slots 13 / 14: ask the source for a stream face, then the array's own serializable slot 4 / 3 ----
namespace {
struct Source {
    void **vt;
    bool answer;
    bool nullObject;
} gSource;
void *gSourceVT[4];
Rel gStreamObj;
uint8_t __fastcall SourceQuery(Source *self, void *, uint32_t id, void **out)
{
    Ev({evQuery, id, self->answer});
    if (self->answer) *out = self->nullObject ? nullptr : &gStreamObj;
    return self->answer;
}
void *gOwnSerialVT[6];
uint8_t __fastcall OwnRead(void *, void *, void *stream)
{
    const uint8_t r = uint8_t(gRng());
    Ev({evOwnRead, RelId(stream), r});
    return r;
}
uint8_t __fastcall OwnWrite(void *, void *, void *stream)
{
    const uint8_t r = uint8_t(gRng());
    Ev({evOwnWrite, RelId(stream), r});
    return r;
}
void RunStreamSlot(difftest::Context &ctx, uint32_t rva, bool reading)
{
    for (uint32_t seed = 0; seed < 24; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            ResetFakes();
            InitRel(&gStreamObj, 900);
            std::mt19937 rng(seed);
            Array arr;
            arr.Build(ctx, rng, 2, gSide);
            for (auto &s : gOwnSerialVT) s = nullptr;
            gOwnSerialVT[3] = reinterpret_cast<void *>(&OwnWrite);
            gOwnSerialVT[4] = reinterpret_cast<void *>(&OwnRead);
            *reinterpret_cast<void **>(arr.Face(kSerialFace)) = gOwnSerialVT;
            for (auto &s : gSourceVT) s = nullptr;
            gSourceVT[0] = reinterpret_cast<void *>(&SourceQuery);
            gSource.vt = gSourceVT;
            gSource.answer = seed % 3 != 0;
            gSource.nullObject = seed % 5 == 1;
            gRng.seed(seed);
            gEv.clear();
            uint8_t r;
            if (side == 0) r = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(rva)(arr.raw, &gSource);
            else r = CallSlot<uint8_t>(arr.raw, reading ? 13 : 14, static_cast<void *>(&gSource));
            blobs[side] = gEv;
            blobs[side].insert(blobs[side].end(), {r, uint32_t(gStreamObj.refs)});
            Capture(blobs[side], arr.Get());
            arr.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], reading ? "ReadFrom" : "WriteTo", seed);
    }
}

// ---- slot 16: the persist system (cache 0x10faf6c) ----
struct Persist {
    void **vt;
} gPersist;
void *gPersistVT[20];
uint32_t gPersistBase = 0;
uint32_t __fastcall PersistSave(Persist *, void *, void *resource, uint32_t flag)
{
    const uint32_t r = gRng();
    Ev({evPersist, uint32_t(uintptr_t(resource)) - gPersistBase, flag, r});
    return r;
}
} // namespace

DIFF_TEST(FamilyRelationshipArray_ReadFrom, 0x4f3b10)
{
    RunStreamSlot(ctx, 0x4f3b10, true);
}

DIFF_TEST(FamilyRelationshipArray_WriteTo, 0x4f3c10)
{
    RunStreamSlot(ctx, 0x4f3c10, false);
}

DIFF_TEST(FamilyRelationshipArray_Save, 0x4f3c90)
{
    void **cache = GameAddress<void *>(0x10faf6c);
    void *saved = *cache;
    for (auto &s : gPersistVT) s = nullptr;
    gPersistVT[16] = reinterpret_cast<void *>(&PersistSave);
    gPersist.vt = gPersistVT;
    *cache = &gPersist;
    for (uint32_t seed = 0; seed < 16; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            ResetFakes();
            std::mt19937 rng(seed);
            Array arr;
            arr.Build(ctx, rng, 1, gSide);
            arr.Get()->mDirty = uint8_t(1 + seed);
            gPersistBase = uint32_t(uintptr_t(arr.raw));
            gRng.seed(seed);
            gEv.clear();
            // The original leaves the persist call's eax untouched and callers use al: compare the low byte on both sides.
            const uint8_t r = side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *)>(0x4f3c90)(arr.raw) : CallSlot<uint8_t>(arr.raw, 16);
            blobs[side] = gEv;
            blobs[side].push_back(r);
            Capture(blobs[side], arr.Get());
            arr.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Save", seed);
    }
    *cache = saved;
}

// ===== Create, the SimIdVector destructor, Unrelate, Relate and the container face =====
//
// The world: arrays built by the original constructor on each side with relationship fakes that keep a tie list (slots 3
// GetTie, 5 GetChildren, 6 GetSiblings, 7 SetTie, 8 RemoveSim, logged by relationship id; the sim arguments are dwords of
// which only the low 16 bits count). The framework (director 0x10c9390, slot 12) hands out a new fake relationship for class
// 0x8c870733 and an array built by the original constructor for 0x8c870743; the persist system (0x10faf6c) answers
// GetResource from a script and logs SaveResource with a snapshot of the array it was given; the data import-export contexts
// are scripted fakes. Arrays under test get a vtable copy whose slots 8 and 9 are the side's own Unrelate / Relate, so the
// recursion stays on the side under test.
namespace {

enum : uint32_t {
    evGetTie = 40, evGetChildren, evGetSiblings, evSetTie, evRemoveSim, evClassObject, evQuery2, evHeld, evGroup, evCopyResolved,
    evCopyAll, evAddPending, evIsKnown, evMapId, evGetResource, evSaveResource, evSavedEntry, evSavedKey,
};

using TieList = std::vector<std::pair<int32_t, uint16_t>>;
std::map<uint32_t, TieList> gTies; // relationship id -> its (type, sim) ties in insertion order
constexpr unsigned kMaxMade = 24;
Rel gMade[kMaxMade];               // what the framework creates, ids 700 +
unsigned gMadeCount = 0;
difftest::Context *gCtx = nullptr;

TieList &TiesOf(const Rel *r) { return gTies[r->id]; }
uint8_t __fastcall RelGetTie(Rel *self, void *, int32_t type, uint16_t *out)
{
    for (const auto &tie : TiesOf(self)) {
        if (tie.first != type) continue;
        Ev({evGetTie, self->id, uint32_t(type), 1, tie.second});
        *out = tie.second;
        return 0x77; // any nonzero byte
    }
    Ev({evGetTie, self->id, uint32_t(type), 0});
    return 0;
}
uint8_t ListTies(Rel *self, SimIdVector *out, int32_t type, uint32_t event)
{
    for (const auto &tie : TiesOf(self))
        if (tie.first == type) out->push_back(tie.second);
    Ev({event, self->id, uint32_t(out->size())});
    return out->empty() ? 0 : 0x31;
}
uint8_t __fastcall RelGetChildren(Rel *self, void *, SimIdVector *out) { return ListTies(self, out, 4, evGetChildren); }
uint8_t __fastcall RelGetSiblings(Rel *self, void *, SimIdVector *out) { return ListTies(self, out, 3, evGetSiblings); }
uint8_t __fastcall RelSetTie(Rel *self, void *, int32_t type, uint32_t sim)
{
    const uint16_t low = uint16_t(sim);
    Ev({evSetTie, self->id, uint32_t(type), low});
    auto &ties = TiesOf(self);
    for (auto &tie : ties) {
        if (tie.first != type) continue;
        if (type == 3 || type == 4) {
            if (tie.second == low) return 0;
        } else {
            tie.second = low;
            return 1;
        }
    }
    ties.emplace_back(type, low);
    return 1;
}
uint8_t __fastcall RelRemoveSim(Rel *self, void *, uint32_t sim)
{
    const uint16_t low = uint16_t(sim);
    Ev({evRemoveSim, self->id, low});
    auto &ties = TiesOf(self);
    for (auto it = ties.begin(); it != ties.end(); ++it) {
        if (it->second != low) continue;
        ties.erase(it);
        return 1;
    }
    return 0;
}
void ResetTieFakes()
{
    gRelVT[3] = reinterpret_cast<void *>(&RelGetTie);
    gRelVT[5] = reinterpret_cast<void *>(&RelGetChildren);
    gRelVT[6] = reinterpret_cast<void *>(&RelGetSiblings);
    gRelVT[7] = reinterpret_cast<void *>(&RelSetTie);
    gRelVT[8] = reinterpret_cast<void *>(&RelRemoveSim);
    gTies.clear();
    gMadeCount = 0;
}

// ---- the other fakes: contexts, segment, persist system, framework ----
struct Fake {
    void **vt;
    uint32_t id;
    int32_t refs;
};
enum : uint32_t { kCtxId = 800, kNbrId, kSegId, kPersistId, kFrameworkId };
Fake gCtxObj, gNbrObj, gSegObj, gPersistObj, gFrameworkObj;
void *gCtxVT[16], *gNbrVT[34], *gSegVT[4], *gPersistVT2[20], *gFrameworkVT[4], *gDirectorVT[13], *gArrayVT[18];

struct Script {
    bool qiFail = false, heldFail = false;
    uint32_t group = 0;
    std::vector<uint32_t> resolved, all;
    uint32_t known = 0, mapFail = 0, mapOffset = 0, createFail = 0;
    bool hit[2] = {false, false};
    uint32_t saveResult = 0;
    cTSFamilyRelationshipArray *loaded = nullptr;
    unsigned mapCalls = 0, resourceCalls = 0, createCalls = 0;
} gS;

uint32_t __fastcall FakeRelease(Fake *self, void *)
{
    Ev({evRelease, self->id});
    return uint32_t(--self->refs);
}
uint8_t __fastcall CtxQuery(Fake *, void *, uint32_t iid, void **out)
{
    Ev({evQuery2, iid});
    if (gS.qiFail) return 0;
    ++gNbrObj.refs;
    *out = &gNbrObj;
    return 0x11;
}
uint8_t __fastcall CtxHeld(Fake *, void *, uint32_t iid, void **out)
{
    Ev({evHeld, iid});
    if (gS.heldFail) return 0;
    ++gSegObj.refs;
    *out = &gSegObj;
    return 0x22;
}
uint32_t __fastcall CtxGroup(Fake *, void *)
{
    Ev({evGroup});
    return gS.group;
}
void FillVector(void *out, const std::vector<uint32_t> &ids)
{
    static_cast<ServiceWordVector *>(out)->assign(ids.begin(), ids.end());
}
void __fastcall NbrCopyResolved(Fake *, void *, void *out)
{
    Ev({evCopyResolved, uint32_t(gS.resolved.size())});
    FillVector(out, gS.resolved);
}
void __fastcall NbrCopyAll(Fake *, void *, void *out)
{
    Ev({evCopyAll, uint32_t(gS.all.size())});
    FillVector(out, gS.all);
}
void __fastcall NbrAddPending(Fake *, void *, int32_t sim) { Ev({evAddPending, uint32_t(sim)}); }
uint8_t __fastcall NbrIsKnown(Fake *, void *, int32_t sim)
{
    const bool known = gS.known >> (uint32_t(sim) & 31) & 1;
    Ev({evIsKnown, uint32_t(sim), known});
    return known ? 0x44 : 0;
}
uint8_t __fastcall NbrMapId(Fake *, void *, uint32_t id, uint32_t *out)
{
    const bool ok = !(gS.mapFail >> (gS.mapCalls++ & 31) & 1);
    Ev({evMapId, id, ok});
    if (!ok) return 0;
    *out = id + gS.mapOffset;
    return 0x55;
}
uint8_t __fastcall PersistGetResource(Fake *, void *, const uint32_t *key, uint32_t iid, void **out, uint32_t a, uint32_t b)
{
    const unsigned n = gS.resourceCalls++;
    Ev({evGetResource, key[0], key[1], key[2], key[3], iid, a, b});
    if (n > 1 || !gS.hit[n]) return 0;
    *out = gS.loaded;
    return 0x2a;
}
// The resource saved is the persist face (+8) of an array: log what it holds before the code under test drops it.
uint8_t __fastcall PersistSave2(Fake *, void *, uint8_t *resource, Fake *segment)
{
    auto *copy = reinterpret_cast<cTSFamilyRelationshipArray *>(resource - 8);
    Ev({evSaveResource, segment ? segment->id : 0, copy->mDirty, copy->mRefCount, uint32_t(copy->mRelations.size())});
    for (const auto &entry : copy->mRelations) Ev({evSavedEntry, entry.first, RelId(entry.second.Get())});
    Ev({evSavedKey, copy->mKey.type, copy->mKey.group, copy->mKey.instance, copy->mKey.resource});
    return uint8_t(gS.saveResult);
}
void *__fastcall DirectorFramework2(void *, void *) { return &gFrameworkObj; }
uint8_t __fastcall FrameworkGetClassObject(Fake *, void *, uint32_t classId, uint32_t iid, void **out)
{
    const unsigned n = gS.createCalls++;
    Ev({evClassObject, classId, iid});
    if (gS.createFail >> (n & 31) & 1) return 0;
    if (classId == 0x8c870733) {
        Rel *made = &gMade[gMadeCount % kMaxMade];
        InitRel(made, 700 + gMadeCount++);
        made->refs = 1;
        gTies[made->id].clear();
        *out = made;
        return 0x12;
    }
    auto *array = static_cast<cTSFamilyRelationshipArray *>(::operator new(sizeof(cTSFamilyRelationshipArray)));
    ConstructSide(gSide, *gCtx, array);
    array->mRefCount = 1;
    *out = array;
    return 0x12;
}

// The allocator service, the director and the persist-system cache point at the fakes for the scope of a test.
struct WorldGlobals {
    difftest::Context &ctx;
    void *saved[3];
    int32_t savedGuard[2];
    bool builtAllocator = false;
    uint32_t allocatorService[8];
    explicit WorldGlobals(difftest::Context &c) : ctx(c)
    {
        static constexpr uint32_t kCells[3] = {0x10faf6c, 0x10c95d4, 0x10c9390};
        for (int i = 0; i < 3; ++i) saved[i] = *ctx.image.At<void *>(kCells[i]);
        savedGuard[0] = *ctx.image.At<int32_t>(0x10c95d8);
        savedGuard[1] = *ctx.image.At<int32_t>(0x10c93f4);
        // The allocator service is an object with the game's own service vtable over the game's allocator, so the original's
        // frees and ours meet the blocks the fakes took from it.
        auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
        const auto *bytes = reinterpret_cast<const uint8_t *>(ga);
        bool zero = true;
        for (size_t i = 0; i < sizeof(GeneralAllocator); ++i) zero = zero && bytes[i] == 0;
        if (zero) {
            ga->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
            builtAllocator = true;
        }
        std::memset(allocatorService, 0, sizeof(allocatorService));
        const void *serviceVtable = ctx.image.At<void>(0xda3cd4);
        std::memcpy(allocatorService, &serviceVtable, sizeof(serviceVtable));
        *ctx.image.At<void *>(0x10c95d4) = allocatorService;
        *ctx.image.At<int32_t>(0x10c95d8) = INT32_MIN;
        gDirectorVT[12] = reinterpret_cast<void *>(&DirectorFramework2);
        *ctx.image.At<void *>(0x10c9390) = gDirectorVT;
        *ctx.image.At<int32_t>(0x10c93f4) = INT32_MIN;
        *ctx.image.At<void *>(0x10faf6c) = &gPersistObj;
    }
    ~WorldGlobals()
    {
        static constexpr uint32_t kCells[3] = {0x10faf6c, 0x10c95d4, 0x10c9390};
        for (int i = 0; i < 3; ++i) *ctx.image.At<void *>(kCells[i]) = saved[i];
        *ctx.image.At<int32_t>(0x10c95d8) = savedGuard[0];
        *ctx.image.At<int32_t>(0x10c93f4) = savedGuard[1];
        if (builtAllocator) {
            auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
            difftest_alloc::ShutdownAndRelease(ga);
        }
    }
};

void ResetWorld(difftest::Context &ctx)
{
    gCtx = &ctx;
    ResetFakes();
    ResetTieFakes();
    gS = Script{};
    gCtxVT[0] = reinterpret_cast<void *>(&CtxQuery);
    gCtxVT[2] = reinterpret_cast<void *>(&FakeRelease);
    gCtxVT[12] = reinterpret_cast<void *>(&CtxHeld);
    gCtxVT[14] = reinterpret_cast<void *>(&CtxGroup);
    gNbrVT[2] = reinterpret_cast<void *>(&FakeRelease);
    gNbrVT[25] = reinterpret_cast<void *>(&NbrAddPending);
    gNbrVT[28] = reinterpret_cast<void *>(&NbrIsKnown);
    gNbrVT[29] = reinterpret_cast<void *>(&NbrCopyResolved);
    gNbrVT[30] = reinterpret_cast<void *>(&NbrCopyAll);
    gNbrVT[32] = reinterpret_cast<void *>(&NbrMapId);
    gSegVT[2] = reinterpret_cast<void *>(&FakeRelease);
    gPersistVT2[5] = reinterpret_cast<void *>(&PersistGetResource);
    gPersistVT2[16] = reinterpret_cast<void *>(&PersistSave2);
    gFrameworkVT[3] = reinterpret_cast<void *>(&FrameworkGetClassObject);
    gCtxObj = {gCtxVT, kCtxId, 1};
    gNbrObj = {gNbrVT, kNbrId, 1};
    gSegObj = {gSegVT, kSegId, 1};
    gPersistObj = {gPersistVT2, kPersistId, 1};
    gFrameworkObj = {gFrameworkVT, kFrameworkId, 1};
}

// Sims 0..range-1, each present with the given chance, with random ties (spouse, mother, father: 0..1 each; siblings and
// children: 0..2) to sims in range + 2 (so some ties lead nowhere).
void BuildSims(difftest::Context &ctx, Array &arr, std::mt19937 &rng, unsigned range, unsigned percent)
{
    arr.Build(ctx, rng, 0, gSide);
    for (unsigned sim = 0; sim < range; ++sim) {
        if (rng() % 100 >= percent) continue;
        Rel *rel = &gRels[sim];
        auto &ties = gTies[rel->id];
        for (int type = 0; type <= 4; ++type) {
            const unsigned count = type <= 2 ? rng() % 2 : rng() % 3;
            for (unsigned i = 0; i < count; ++i) ties.emplace_back(type, uint16_t(rng() % (range + 2)));
        }
        arr.Get()->mRelations.try_emplace(uint16_t(sim), FamilyRelationRef(reinterpret_cast<ifaces::cITSFamilyRelationship *>(rel)));
    }
}

// Everything a run can change: the array, every tie list, every fake's count.
void CaptureWorld(Blob &b, cTSFamilyRelationshipArray *a)
{
    Capture(b, a);
    for (const auto &entry : gTies) {
        b.insert(b.end(), {0xaaaa0000u, entry.first});
        for (const auto &tie : entry.second) b.push_back(uint32_t(tie.first) << 16 | tie.second);
    }
    for (unsigned i = 0; i < gMadeCount; ++i) b.insert(b.end(), {gMade[i].id, uint32_t(gMade[i].refs)});
    for (const Fake *f : {&gCtxObj, &gNbrObj, &gSegObj}) b.push_back(uint32_t(f->refs));
    b.push_back(gS.createCalls);
    b.push_back(gS.mapCalls);
}

} // namespace

DIFF_TEST(FamilyRelationshipArray_SimIdVectorDestroy, 0x4f2110)
{
    WorldGlobals globals(ctx);
    for (uint32_t seed = 0; seed < 12; ++seed) {
        const unsigned count = seed % 4 == 0 ? 0 : seed % 4 == 1 ? 1 : 5 + seed;
        const unsigned spare = seed / 4 * 3; // 0, 3, 6 spare elements
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            alignas(8) uint8_t raw[sizeof(SimIdVector)];
            auto *vector = new (raw) SimIdVector;
            vector->reserve(count + spare);
            for (unsigned i = 0; i < count; ++i) vector->push_back(uint16_t(i * 7));
            uint32_t words[3];
            std::memcpy(words, raw, sizeof(words));
            const uint32_t block = words[0];
            const uint32_t bytes = (count + spare) * 2;
            if (side == 0) ctx.Original<void(__thiscall *)(void *)>(0x4f2110)(raw);
            else reinterpret_cast<SimIdVectorTidy *>(raw)->Destroy();
            std::memcpy(words, raw, sizeof(words));
            blobs[side].insert(blobs[side].end(), {words[0], words[1], words[2]});
            // Whether the block went back to the allocator: it is the next block handed out for its size.
            if (bytes) {
                void *probe = RZMemory::Malloc(bytes);
                blobs[side].push_back(uint32_t(uintptr_t(probe)) == block);
                RZMemory::Free(probe);
            }
        }
        Same(ctx, blobs[0], blobs[1], "SimIdVectorDestroy", seed);
    }
}

DIFF_TEST(FamilyRelationshipArray_Create, 0x4f26e0)
{
    WorldGlobals globals(ctx);
    for (uint32_t seed = 0; seed < 3; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            auto *made = side == 0 ? ctx.Original<cTSFamilyRelationshipArray *(__cdecl *)()>(0x4f26e0)()
                                   : cTSFamilyRelationshipArray::Create();
            const auto *raw = reinterpret_cast<const uint8_t *>(made);
            uint32_t words[12];
            std::memcpy(words, raw, sizeof(words));
            blobs[side].push_back(made ? 1u : 0u);
            PushVTableWords(blobs[side], ctx, side, words);
            for (int i = 4; i < 10; ++i) if (i != 5) blobs[side].push_back(words[i]); // +0x14 holds the byte and three pad bytes
            blobs[side].push_back(raw[0x14]);
            const uint8_t *head = reinterpret_cast<const uint8_t *>(words[10]);
            uint32_t links[3];
            std::memcpy(links, head, sizeof(links));
            blobs[side].insert(blobs[side].end(), {links[0] == words[10], links[1] == words[10], links[2] == words[10],
                                                    head[0xc], head[0xd], words[11]});
            CallSlot<uint32_t>(made, 2); // refcount 0: destroys and frees (the side's own Release)
        }
        Same(ctx, blobs[0], blobs[1], "Create", seed);
    }
}

DIFF_TEST(FamilyRelationshipArray_Unrelate, 0x4f3950)
{
    WorldGlobals globals(ctx);
    for (uint32_t seed = 0; seed < 400; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            ResetWorld(ctx);
            std::mt19937 rng(seed);
            Array arr;
            BuildSims(ctx, arr, rng, 4, 60);
            const uint16_t sim = uint16_t(rng() % 5), other = uint16_t(rng() % 5);
            gS.createFail = rng() % 4 == 0 ? rng() & 3 : 0;
            gEv.clear();
            if (side == 0) ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(0x4f3950)(arr.raw, sim, other);
            else CallSlot<void>(arr.raw, 8, uint32_t(sim), uint32_t(other));
            blobs[side] = gEv;
            CaptureWorld(blobs[side], arr.Get());
            arr.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Unrelate", seed);
    }
}

DIFF_TEST(FamilyRelationshipArray_Relate, 0x4f2280)
{
    WorldGlobals globals(ctx);
    const int32_t roles[] = {0, 1, 2, 3, 4, 5, -1, 0x7fffffff};
    for (uint32_t seed = 0; seed < 1200; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            ResetWorld(ctx);
            std::mt19937 rng(seed);
            Array arr;
            BuildSims(ctx, arr, rng, 5, 65);
            const uint16_t sim = uint16_t(rng() % 5), other = uint16_t(rng() % 5);
            const int32_t role = roles[seed % 8];
            gS.createFail = rng() % 5 == 0 ? rng() & 7 : 0;
            gEv.clear();
            const uint8_t r = side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, int32_t)>(0x4f2280)(arr.raw, sim, other, role)
                                        : CallSlot<uint8_t>(arr.raw, 9, uint32_t(sim), uint32_t(other), role);
            blobs[side] = gEv;
            blobs[side].push_back(r);
            CaptureWorld(blobs[side], arr.Get());
            arr.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Relate", seed);
    }
}

namespace {
// The container-face tests share their frame: the world, the script, the call on each side.
using FaceFn = uint8_t(__thiscall *)(void *, void *);
void RunFace(difftest::Context &ctx, uint32_t rva, const char *name, unsigned seeds,
             void (*configure)(uint32_t seed, std::mt19937 &rng), int slot)
{
    WorldGlobals globals(ctx);
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            ResetWorld(ctx);
            std::mt19937 rng(seed);
            Array arr;
            BuildSims(ctx, arr, rng, 6, 70);
            gS.group = rng();
            gS.saveResult = rng();
            configure(seed, rng);
            void *context = seed % 13 == 12 ? nullptr : &gCtxObj;
            gEv.clear();
            void *face = arr.Face(kContainerFace);
            const uint8_t r = side == 0 ? ctx.Original<FaceFn>(rva)(face, context) : CallSlot<uint8_t>(face, slot, context);
            blobs[side] = gEv;
            blobs[side].push_back(r);
            CaptureWorld(blobs[side], arr.Get());
            arr.Destroy();
            if (gS.loaded && gS.resourceCalls == 0) // never asked for: free it here
                CallSlot<uint32_t>(gS.loaded, 2);
        }
        Same(ctx, blobs[0], blobs[1], name, seed);
    }
}
std::vector<uint32_t> RandomIds(std::mt19937 &rng, unsigned maxCount, unsigned range)
{
    std::vector<uint32_t> ids;
    const unsigned count = rng() % (maxCount + 1);
    for (unsigned i = 0; i < count; ++i) ids.push_back(rng() % 9 == 0 ? 0x10000 + rng() % range : rng() % range);
    return ids;
}
} // namespace

DIFF_TEST(FamilyRelationshipArray_CollectReferenced, 0x4f27a0)
{
    RunFace(ctx, 0x4f27a0, "CollectReferenced", 500,
            [](uint32_t seed, std::mt19937 &rng) {
                gS.qiFail = seed % 7 == 1;
                gS.resolved = RandomIds(rng, 4, 8);
                gS.known = rng();
            }, 3);
}

DIFF_TEST(FamilyRelationshipArray_Export, 0x4f3ff0)
{
    RunFace(ctx, 0x4f3ff0, "Export", 400,
            [](uint32_t seed, std::mt19937 &rng) {
                gS.qiFail = seed % 7 == 1;
                gS.heldFail = seed % 5 == 2;
                gS.all = RandomIds(rng, 6, 8);
            }, 4);
}

DIFF_TEST(FamilyRelationshipArray_Import, 0x4f3300)
{
    // The array resource Import loads: built by the original constructor with a few relationships, handed out by the
    // persist fake (and freed by Import's counted holder).
    RunFace(ctx, 0x4f3300, "Import", 500,
            [](uint32_t seed, std::mt19937 &rng) {
                gS.qiFail = seed % 7 == 1;
                gS.heldFail = seed % 5 == 2;
                gS.all = RandomIds(rng, 6, 8);
                gS.mapFail = rng() % 3 == 0 ? rng() : 0;
                gS.mapOffset = rng() % 3 == 0 ? 0 : rng() % 4;
                const unsigned pattern = seed % 4; // instance 1 / only instance 0 / both / neither
                gS.hit[0] = pattern == 0 || pattern == 2;
                gS.hit[1] = pattern == 1 || pattern == 2;
                if (!gS.hit[0] && !gS.hit[1]) return;
                auto *loaded = static_cast<cTSFamilyRelationshipArray *>(::operator new(sizeof(cTSFamilyRelationshipArray)));
                ConstructSide(gSide, *gCtx, loaded);
                loaded->mRefCount = 1;
                for (unsigned sim = 0; sim < 6; ++sim) {
                    if (rng() % 3 == 0) continue;
                    Rel *rel = &gRels[8 + sim];
                    auto &ties = gTies[rel->id];
                    for (int type = 0; type <= 4; ++type) {
                        const unsigned count = type <= 2 ? rng() % 2 : rng() % 3;
                        for (unsigned i = 0; i < count; ++i) ties.emplace_back(type, uint16_t(rng() % 8));
                    }
                    loaded->mRelations.try_emplace(uint16_t(sim), FamilyRelationRef(reinterpret_cast<ifaces::cITSFamilyRelationship *>(rel)));
                }
                gS.loaded = loaded;
            }, 5);
}

// ===== The remaining slots of the four vtables, each side through its own object's vtable =====
// The tests above call QueryInterface, Release, slots 3, 4, 6-9, 13, 14, 16, 17, the container face's 3-5 and the
// serializable face's 3-5 through our vtable. This one covers the rest: AddRef (folded 0x146a40), IsDirty (0x16db30),
// slots 10 (0x6c6b0), 11 (0x1474a0), 12 (0x149cf0) and 15 (0x3a4f40) of the primary face; slots 0-2 of each secondary face
// (the adjustor thunks: QueryInterface 0x4f2f63 / 0x4f2f6b / 0x4f2f73, AddRef 0x37b3c3 / 0x473592 / 0x47359a, Release
// 0x4f37c6 / 0x4f37ce / 0x4f37d6); and the whole persist face (GetKey 0x147790, SetKey 0x47d420, GetType 0x149cf0,
// GetGroup 0x149d00, GetInstance 0x148ba0, SetType 0x1eec40, SetGroup 0x47d410, SetInstance 0x1f5270, GetRefCount
// thunk 0x4f2dfc). Same seeded state on both sides; compared: the result, what an out-parameter received, the object's
// fields after the call and its four vtable words (the game's on the original side, ours on our side).
namespace {
struct SlotCall {
    uint32_t face;
    int slot;
};
constexpr SlotCall kSlotCalls[] = {
    {0, 1},   {0, 5},   {0, 10},  {0, 11},  {0, 12},  {0, 15},  {4, 0},   {4, 1},   {4, 2},   {8, 0},   {8, 1},   {8, 2},
    {8, 3},   {8, 4},   {8, 5},   {8, 6},   {8, 7},   {8, 8},   {8, 9},   {8, 10},  {8, 11},  {0xc, 0}, {0xc, 1}, {0xc, 2},
};
constexpr uint32_t kQueryIds[] = {1, 0x8c87073f, 0x456b8f1d, 0x8db5bf48, 0xe4fda3d4, 0, 0x8c870743};

void CaptureFields(Blob &b, difftest::Context &ctx, int side, cTSFamilyRelationshipArray *a)
{
    b.insert(b.end(), {0xf1e1d000u, a->mRefCount, a->mDirty, a->mKey.type, a->mKey.group, a->mKey.instance, a->mKey.resource,
                       uint32_t(a->mRelations.size())});
    uint32_t words[4];
    std::memcpy(words, a, sizeof(words));
    PushVTableWords(b, ctx, side, words);
}
} // namespace

DIFF_TEST(FamilyRelationshipArray_Slots, 0x4f2f63)
{
    for (uint32_t seed = 0; seed < 40; ++seed) {
        for (const SlotCall &c : kSlotCalls) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                gSide = side;
                ResetFakes();
                std::mt19937 rng(seed);
                Array arr;
                arr.Build(ctx, rng, rng() % 4, side);
                auto *a = arr.Get();
                a->mRefCount = 2 + rng() % 5; // at least 2: Release only counts down here (its freeing is tested above)
                a->mDirty = uint8_t(rng());
                a->mKey = {rng(), rng(), rng(), rng()};
                const uint32_t arg = rng();
                const cGZPersistResourceKey newKey = {rng(), rng(), rng(), rng()};
                const uint8_t *face = arr.Face(c.face);
                Blob &b = blobs[side];
                b.push_back(c.face << 8 | uint32_t(c.slot));
                if (c.slot == 0) { // QueryInterface through the face
                    void *out = reinterpret_cast<void *>(0x5678);
                    const uint8_t r = CallSlot<uint8_t>(face, 0, kQueryIds[arg % 7], &out);
                    b.insert(b.end(), {r, uint32_t(static_cast<uint8_t *>(out) - arr.raw)});
                } else if (c.slot == 1 || c.slot == 2) { // AddRef / Release
                    b.push_back(CallSlot<uint32_t>(face, c.slot));
                } else if (c.face == 0 && c.slot == 5) {
                    b.push_back(CallSlot<uint8_t>(face, 5));
                } else if (c.face == 0 && (c.slot == 10 || c.slot == 12)) {
                    b.push_back(CallSlot<uint32_t>(face, c.slot));
                } else if (c.face == 0 && c.slot == 11) {
                    b.push_back(CallSlot<uint8_t>(face, 11, arg, arg ^ 1, arg ^ 2));
                } else if (c.face == 0 && c.slot == 15) {
                    void *r = CallSlot<void *>(face, 15);
                    b.push_back(uint32_t(static_cast<uint8_t *>(r) - arr.raw));
                } else if (c.slot == 3) { // GetKey: the key by value (hidden pointer, returned)
                    cGZPersistResourceKey out = {1, 2, 3, 4};
                    void *r = CallSlot<void *>(face, 3, static_cast<void *>(&out));
                    b.insert(b.end(), {r == &out, out.type, out.group, out.instance, out.resource});
                } else if (c.slot == 4) {
                    b.push_back(CallSlot<uint8_t>(face, 4, &newKey));
                } else if (c.slot >= 5 && c.slot <= 7) {
                    b.push_back(CallSlot<uint32_t>(face, c.slot));
                } else if (c.slot >= 8 && c.slot <= 10) {
                    b.push_back(CallSlot<uint8_t>(face, c.slot, arg));
                } else { // persist face slot 11: GetRefCount
                    b.push_back(CallSlot<uint32_t>(face, 11));
                }
                CaptureFields(b, ctx, side, a);
                arr.Destroy();
            }
            Same(ctx, blobs[0], blobs[1], "Slots", seed << 16 | c.face << 8 | uint32_t(c.slot));
        }
    }
}

// ===== The adapters: our functions behind the old entry points, on an object built by the game's constructor =====
// What runs when the game calls an old entry point directly or holds an object with its own vtables (the constructor hook
// off). The object on both sides is the original's (game vtables, so the virtual calls inside stay the game's); the
// original side calls the RVA, ours the adapter the hook installs. One seeded scenario per adapter that has no test of
// its own above (Construct and GetGZCLSID have): the faces' this adjustment and the qualified (non-virtual) forwarding.
// Slots whose originals nothing reaches while the constructor hook is in have no adapter (src/hook/tables).
namespace {
void *gTrapVT[18];
// Stands for a slot nothing may call: logs, then fails the run loudly (its stack arguments are not popped).
uint32_t __fastcall Trap(void *, void *)
{
    Ev({0x7a7a7a7au});
    return 0;
}
} // namespace

DIFF_TEST(FamilyRelationshipArray_Adapters, 0x4f38a0)
{
    using A = cTSFamilyRelationshipArrayAdapters;
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = 0; // both objects are the original's
            ResetFakes();
            std::mt19937 rng(seed);
            Array arr;
            arr.Build(ctx, rng, rng() % 8, 0);
            auto *self = arr.Get();
            self->mRefCount = 5; // Release (last) only counts down: the object lives on the stack
            // The game's vtables with a trap in every slot an adapter forwards to: an adapter that dispatched through the
            // object's vtable (in game: back into its own hook) would log it. Slot 1 (AddRef, which QueryInterface calls
            // through the vtable on both sides) stays the game's.
            std::memcpy(gTrapVT, ctx.image.At<void>(cTSFamilyRelationshipArray::kOriginalVTable), sizeof(gTrapVT));
            for (int slot : {2, 4, 6, 17}) gTrapVT[slot] = reinterpret_cast<void *>(&Trap);
            *reinterpret_cast<void **>(arr.raw) = gTrapVT;
            const uint16_t key = uint16_t(rng() % 9);
            const uint32_t iid = kQueryIds[rng() % 7];
            void *out = nullptr;
            gEv.clear();
            Blob &b = blobs[side];
            // The adapters left are the hooks the byte matches go through (matchcheck ties a slot to its RVA through its
            // adapter); the other slots' adapters were dropped (lane GJ): their originals are trapped instead.
            if (side == 0) {
                b.push_back(ctx.Original<uint8_t(__thiscall *)(void *, uint32_t)>(0x4f38a0)(self, uint32_t(key ^ 2)));
                ctx.Original<void(__thiscall *)(void *)>(0x4f2e80)(self);
                b.push_back(ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0x4f2f00)(self, iid, &out));
            } else {
                b.push_back(A::RemoveRelationship(self, nullptr, uint16_t(key ^ 2)));
                A::SetDirty(self, nullptr);
                b.push_back(A::QueryInterface(self, nullptr, iid, &out));
            }
            b.push_back(out ? uint32_t(static_cast<uint8_t *>(out) - arr.raw) : 0xffffffffu);
            b.push_back(side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *)>(0x4f3d90)(self) : A::Clear(self, nullptr));
            b.insert(b.end(), gEv.begin(), gEv.end());
            Capture(b, self);
            b.push_back(side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x4f3770)(self) : A::Release(self, nullptr));
            b.push_back(self->mRefCount);
            arr.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Adapters", seed);
    }
}
