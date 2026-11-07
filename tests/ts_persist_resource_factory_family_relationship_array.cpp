// Differential tests for cTSPersistResourceFactoryFamilyRelationshipArray (src/ts/cTSPersistResourceFactoryFamilyRelationshipArray.*).
//
// Standalone shape (docs/standalone-shape.md): the original side's factory is built by the original creator 0x4f2740 (game
// vtable) and the code is called at its RVA; ours is built by our Create (the creator hook's entry) and called through its own
// vtable slot. The vtable word is the only word that differs by design. The arrays the factory creates are the original
// constructor's on the original side and our class on ours: their four vtable words are compared the same way, every other word
// as is. Fakes: a resource whose QueryInterface hands out an array fake (ReadFrom / WriteTo slots 13 / 14 logged), and a record
// whose key gives the type and whose stream reader reads zeros (an empty array). A run compares the result, the out-parameter,
// the created object's words, the frees and the event log.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "ts/cTSFamilyRelationshipArray.h"
#include "ts/cTSPersistResourceFactoryFamilyRelationshipArray.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <vector>

namespace {

using Blob = std::vector<uint32_t>;
using Factory = cTSPersistResourceFactoryFamilyRelationshipArray;

enum : uint32_t {
    evResQuery = 1, evResRelease, evArrAddRef, evArrRelease, evReadFrom, evWriteTo, evRecKey, evRecQuery, evStreamRead,
    evStreamRelease, evFakeAddRef, evCreateByType,
};

Blob gEv;
void Ev(std::initializer_list<uint32_t> words) { gEv.insert(gEv.end(), words); }
std::mt19937 gRng;

void Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    if (a == b) return;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    ctx.Fail("%s seed %u: differ at word %u (sizes %u/%u): %08x vs %08x", what, seed, unsigned(i), unsigned(a.size()), unsigned(b.size()),
             i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0);
}

// Slot `slot` of the vtable at `face` (the object's own), called with `this` = face. Byte returns are compared raw (al).
template <class R, class... A> R CallSlot(const void *face, int slot, A... args)
{
    using Fn = R(__thiscall *)(const void *, A...);
    return (*static_cast<Fn *const *>(face))[slot](face, args...);
}

// ---- the factory, the side's way ----
Factory *MakeSide(difftest::Context &ctx, int side)
{
    return side == 0 ? ctx.Original<Factory *(__cdecl *)()>(0x4f2740)() : Factory::Create();
}

const void *OurFactoryVTable()
{
    Factory f;
    const void *vt;
    std::memcpy(&vt, &f, sizeof(vt));
    return vt;
}

// The factory's vtable word as one flag: the game's on the original side, ours on our side (and ours is not the game's).
void PushFactoryVTable(Blob &b, difftest::Context &ctx, int side, const void *object)
{
    uint32_t word;
    std::memcpy(&word, object, sizeof(word));
    const uint32_t game = uint32_t(uintptr_t(ctx.image.At<void>(Factory::kOriginalVTable)));
    const uint32_t ours = uint32_t(uintptr_t(OurFactoryVTable()));
    b.push_back(word == (side == 0 ? game : ours) && ours != game);
}

// The four vtable words of a created array, the same way.
void PushArrayVTables(Blob &b, difftest::Context &ctx, int side, const uint32_t *words)
{
    alignas(8) uint8_t raw[sizeof(cTSFamilyRelationshipArray)];
    auto *a = cTSFamilyRelationshipArrayAdapters::Construct(raw, nullptr);
    uint32_t ours[4];
    std::memcpy(ours, raw, sizeof(ours));
    a->~cTSFamilyRelationshipArray();
    const uint32_t games[4] = {cTSFamilyRelationshipArray::kOriginalVTable, cTSFamilyRelationshipArray::kOriginalContainerVTable,
                               cTSFamilyRelationshipArray::kOriginalPersistVTable, cTSFamilyRelationshipArray::kOriginalSerialVTable};
    for (int i = 0; i < 4; ++i) {
        const uint32_t game = uint32_t(uintptr_t(ctx.image.At<void>(games[i])));
        b.push_back(words[i] == (side == 0 ? game : ours[i]) && ours[i] != game);
    }
}

// The face offset of an interface id the array answers (QueryInterface 0x4f2f00).
int ArrayFace(uint32_t riid)
{
    switch (riid) {
    case 1:
    case cTSFamilyRelationshipArray::kInterfaceId: return 0;
    case cTSFamilyRelationshipArray::kContainerId: return 4;
    case cTSFamilyRelationshipArray::kPersistResourceId: return 8;
    case cTSFamilyRelationshipArray::kSerializableId: return 0xc;
    default: return -1;
    }
}

// A created array (out-parameter of the given face) captured, then released through its own slot 2 until it is freed.
void CaptureAndReleaseArray(Blob &b, difftest::Context &ctx, int side, void *out, uint32_t riid)
{
    const int faceOffset = ArrayFace(riid);
    b.push_back(uint32_t(faceOffset));
    if (faceOffset < 0) return;
    const uint8_t *base = static_cast<uint8_t *>(out) - faceOffset;
    uint32_t words[12];
    std::memcpy(words, base, sizeof(words));
    PushArrayVTables(b, ctx, side, words);
    b.insert(b.end(), {words[4], base[0x14], words[6], words[7], words[8], words[9], words[11]}); // count, dirty, key, map size
    difftest::FreeRecorder rec;
    uint32_t left = 1;
    for (int i = 0; i < 4 && left; ++i) {
        left = CallSlot<uint32_t>(out, 2);
        b.push_back(left);
    }
    rec.Stop();
    b.push_back(uint32_t(rec.Count()));
}

// ---- the resource, the array fake it answers with, the record and its stream ----
struct Obj {
    void **vt;
};
void *gResVT[4], *gArrVT[18], *gRecVT[8], *gStreamVT[24];
Obj gRes{gResVT}, gArr{gArrVT}, gRec{gRecVT}, gStream{gStreamVT};
struct Config {
    uint32_t resAnswer = 1;     // the resource's QueryInterface byte (upper bits noise)
    bool resSetsOut = true;     // whether it writes the out-parameter
    uint32_t rwAnswer = 1;      // ReadFrom / WriteTo
    uint32_t recordType = cTSFamilyRelationshipArray::kClassId;
    uint32_t streamAnswer = 1;  // the record's QueryInterface for the stream reader
} gCfg;
uint32_t gRecordKey[4];

uint32_t __fastcall ResQuery(Obj *, void *, uint32_t id, void **out)
{
    Ev({evResQuery, id});
    if (gCfg.resSetsOut) *out = &gArr;
    return (0x4400u * (gRng() % 3)) | gCfg.resAnswer;
}
uint32_t __fastcall ResRelease(Obj *, void *)
{
    Ev({evResRelease});
    return 0;
}
uint32_t __fastcall ArrAddRef(Obj *, void *)
{
    Ev({evArrAddRef});
    return 2;
}
uint32_t __fastcall ArrRelease(Obj *, void *)
{
    Ev({evArrRelease});
    return 1;
}
uint32_t __fastcall ArrReadFrom(Obj *, void *, void *record)
{
    Ev({evReadFrom, record == &gRec});
    return (0x1100u * (gRng() % 3)) | gCfg.rwAnswer;
}
uint32_t __fastcall ArrWriteTo(Obj *, void *, void *record)
{
    Ev({evWriteTo, record == &gRec});
    return (0x2200u * (gRng() % 3)) | gCfg.rwAnswer;
}
void *__fastcall RecKey(Obj *, void *)
{
    Ev({evRecKey});
    gRecordKey[0] = gCfg.recordType;
    gRecordKey[1] = 0x1234;
    gRecordKey[2] = 0x5678;
    gRecordKey[3] = 0;
    return gRecordKey;
}
uint32_t __fastcall RecQuery(Obj *, void *, uint32_t id, void **out)
{
    Ev({evRecQuery, id});
    if (!gCfg.streamAnswer) return 0;
    *out = &gStream;
    return 1;
}
uint8_t __fastcall StreamRead32(Obj *, void *, uint32_t *out)
{
    Ev({evStreamRead});
    *out = 0; // version 0, then count 0: an empty array
    return 1;
}
uint32_t __fastcall StreamRelease(Obj *, void *)
{
    Ev({evStreamRelease});
    return 0;
}

void SetFakes()
{
    gResVT[0] = reinterpret_cast<void *>(&ResQuery);
    gResVT[2] = reinterpret_cast<void *>(&ResRelease);
    gArrVT[1] = reinterpret_cast<void *>(&ArrAddRef);
    gArrVT[2] = reinterpret_cast<void *>(&ArrRelease);
    gArrVT[13] = reinterpret_cast<void *>(&ArrReadFrom);
    gArrVT[14] = reinterpret_cast<void *>(&ArrWriteTo);
    gRecVT[0] = reinterpret_cast<void *>(&RecQuery);
    gRecVT[3] = reinterpret_cast<void *>(&RecKey);
    gStreamVT[2] = reinterpret_cast<void *>(&StreamRelease);
    gStreamVT[9] = reinterpret_cast<void *>(&StreamRead32);
}

// A copy of the factory's own vtable with a recording AddRef (QueryInterface calls slot 1 through the vtable).
uint32_t __fastcall FakeAddRef(Factory *self, void *)
{
    Ev({evFakeAddRef});
    return ++self->mRefCount;
}

// Slot 4 recorded on its way through a copy of the factory's own vtable (slot 3 calls it through the vtable), then forwarded to
// the side's own slot 4.
void **gOwnFactoryVT;
uint32_t __fastcall RecordingCreateByType(Factory *self, void *, uint32_t type, uint32_t riid, void **out, uint32_t a4, uint32_t a5)
{
    Ev({evCreateByType, type, riid, a4, a5});
    using Fn = uint32_t(__thiscall *)(Factory *, uint32_t, uint32_t, void **, uint32_t, uint32_t);
    return reinterpret_cast<Fn>(gOwnFactoryVT[4])(self, type, riid, out, a4, a5);
}

constexpr uint32_t kRiids[] = {cTSFamilyRelationshipArray::kInterfaceId, 1, cTSFamilyRelationshipArray::kPersistResourceId,
                               cTSFamilyRelationshipArray::kContainerId, cTSFamilyRelationshipArray::kSerializableId, 0,
                               0x8c870743, 0xa56b8f17};
constexpr uint32_t kTypes[] = {cTSFamilyRelationshipArray::kClassId, 0x8c870742, 0x8c870733, 0};

} // namespace

// The creator, then the side's own Release (slot 2) at reference counts 0..3: at 1 or below it frees the factory through
// slot 7.
DIFF_TEST(PersistResourceFactoryFamRel_Create, 0x4f2740)
{
    for (uint32_t seed = 0; seed < 4; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            Factory *f = MakeSide(ctx, side);
            Blob &b = blobs[side];
            b.push_back(f ? 1u : 0u);
            if (!f) continue;
            b.push_back(uint32_t(HeapSize(GetProcessHeap(), 0, f) >= sizeof(Factory)));
            PushFactoryVTable(b, ctx, side, f);
            b.push_back(f->mRefCount);
            f->mRefCount = seed;
            difftest::FreeRecorder rec;
            const uint32_t r = CallSlot<uint32_t>(f, 2);
            rec.Stop();
            const bool freed = rec.Freed(f);
            b.insert(b.end(), {r, uint32_t(freed), uint32_t(rec.Count())});
            if (!freed) {
                b.push_back(f->mRefCount);
                ::operator delete(f, sizeof(Factory));
            }
        }
        Same(ctx, blobs[0], blobs[1], "Create", seed);
    }
}

// Slot 7: the original's 0x4f2170 on the original's factory, the compiler's scalar deleting destructor on ours.
DIFF_TEST(PersistResourceFactoryFamRel_DeletingDestructor, 0x4f2170)
{
    for (uint32_t flags = 0; flags < 4; ++flags) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            Factory *f = MakeSide(ctx, side);
            f->mRefCount = 7 + flags;
            difftest::FreeRecorder rec;
            void *r = side == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x4f2170)(f, flags) : CallSlot<void *>(f, 7, flags);
            rec.Stop();
            const bool freed = rec.Freed(f);
            Blob &b = blobs[side];
            b.insert(b.end(), {uint32_t(r == f), uint32_t(freed), uint32_t(rec.Count())});
            if (!freed) {
                PushFactoryVTable(b, ctx, side, f);
                b.push_back(f->mRefCount);
                ::operator delete(f, sizeof(Factory));
            }
        }
        Same(ctx, blobs[0], blobs[1], "DeletingDestructor", flags);
    }
}

// Slot 4: a new array for the FAMT type (the original constructor's on the original side, our class on ours), asked for riid.
DIFF_TEST(PersistResourceFactoryFamRel_CreateInstanceByType, 0x4f25d0)
{
    uint32_t n = 0;
    for (uint32_t type : kTypes) {
        for (uint32_t riid : kRiids) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                Factory *f = MakeSide(ctx, side);
                f->mRefCount = 3;
                void *out = reinterpret_cast<void *>(0x5678);
                difftest::FreeRecorder rec;
                const uint8_t r = side == 0
                                      ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, void **, uint32_t, uint32_t)>(0x4f25d0)(
                                            f, type, riid, &out, n, ~n)
                                      : CallSlot<uint8_t>(f, 4, type, riid, &out, n, ~n);
                rec.Stop();
                Blob &b = blobs[side];
                b.insert(b.end(), {r, uint32_t(out == reinterpret_cast<void *>(0x5678)), uint32_t(rec.Count()), f->mRefCount});
                PushFactoryVTable(b, ctx, side, f);
                if (r) CaptureAndReleaseArray(b, ctx, side, out, riid);
                ::operator delete(f, sizeof(Factory));
            }
            Same(ctx, blobs[0], blobs[1], "CreateInstanceByType", n++);
        }
    }
}

namespace {
void RunReadWrite(difftest::Context &ctx, uint32_t rva, int slot, const char *what)
{
    SetFakes();
    for (uint32_t seed = 0; seed < 24; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            Factory *f = MakeSide(ctx, side);
            f->mRefCount = 2;
            gCfg = Config{};
            gCfg.resAnswer = seed % 3 == 0 ? 0 : seed % 3 == 1 ? 1 : 0x81;
            // A failed query may or may not write the out-parameter; a successful one always does (the original dereferences it).
            gCfg.resSetsOut = gCfg.resAnswer == 0 ? (seed / 3) % 2 != 0 : true;
            gCfg.rwAnswer = (seed / 4) % 3 == 2 ? 0x80 : (seed / 4) % 3; // 0x80: a byte that is neither 0 nor 1 goes out as is
            gRng.seed(seed);
            gEv.clear();
            const uint8_t r = side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, void *, void *)>(rva)(f, &gRes, &gRec)
                                        : CallSlot<uint8_t>(f, slot, static_cast<void *>(&gRes), static_cast<void *>(&gRec));
            Blob &b = blobs[side];
            b.push_back(r);
            b.insert(b.end(), gEv.begin(), gEv.end());
            b.push_back(f->mRefCount);
            PushFactoryVTable(b, ctx, side, f);
            ::operator delete(f, sizeof(Factory));
        }
        Same(ctx, blobs[0], blobs[1], what, seed);
    }
}
} // namespace

DIFF_TEST(PersistResourceFactoryFamRel_Read, 0x4f3280)
{
    RunReadWrite(ctx, 0x4f3280, 5, "Read");
}

DIFF_TEST(PersistResourceFactoryFamRel_Write, 0x4f3f70)
{
    RunReadWrite(ctx, 0x4f3f70, 6, "Write");
}

// ===== The folded slots through the factory's own vtable =====
// QueryInterface 0x1ee250, AddRef 0x148fb0, Release 0x1ee310 (counting down), CreateInstance 0x431e60 (a real array created
// through slot 4 and read through slot 5 from a record whose stream holds an empty array) and GetRefCount 0x147a30.
DIFF_TEST(PersistResourceFactoryFamRel_Slots, 0x431e60)
{
    SetFakes();
    const uint32_t queryIds[] = {1, 0xa56b8f17, 0, 0x456b8f1d, 0xa56b8f16, 0x8c87073f};
    for (uint32_t seed = 0; seed < 64; ++seed) {
        for (int slot : {0, 1, 2, 3, 8}) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                Factory *f = MakeSide(ctx, side);
                std::mt19937 rng(seed);
                f->mRefCount = 2 + rng() % 4;
                gCfg = Config{};
                Blob &b = blobs[side];
                gEv.clear();
                if (slot == 0) {
                    void **own = *reinterpret_cast<void ***>(f);
                    void *vt[9];
                    std::memcpy(vt, own, sizeof(vt));
                    vt[1] = reinterpret_cast<void *>(&FakeAddRef);
                    *reinterpret_cast<void ***>(f) = vt;
                    void *out = reinterpret_cast<void *>(0x5678);
                    const uint8_t r = CallSlot<uint8_t>(f, 0, queryIds[seed % 6], &out);
                    *reinterpret_cast<void ***>(f) = own;
                    b.insert(b.end(), {r, out == f ? 0u : out == reinterpret_cast<void *>(0x5678) ? 1u : 2u});
                } else if (slot == 1 || slot == 2 || slot == 8) {
                    b.push_back(CallSlot<uint32_t>(f, slot));
                } else {
                    gCfg.recordType = kTypes[seed % 4];
                    gCfg.streamAnswer = (seed / 4) % 2;
                    const uint32_t riid = kRiids[(seed / 8) % 8];
                    void *out = reinterpret_cast<void *>(0x5678);
                    gOwnFactoryVT = *reinterpret_cast<void ***>(f);
                    void *vt[9];
                    std::memcpy(vt, gOwnFactoryVT, sizeof(vt));
                    vt[4] = reinterpret_cast<void *>(&RecordingCreateByType);
                    *reinterpret_cast<void ***>(f) = vt;
                    const uint8_t r = CallSlot<uint8_t>(f, 3, static_cast<void *>(&gRec), riid, &out, seed, ~seed);
                    *reinterpret_cast<void ***>(f) = gOwnFactoryVT;
                    b.insert(b.end(), {r, uint32_t(out == reinterpret_cast<void *>(0x5678))});
                    if (out != reinterpret_cast<void *>(0x5678)) CaptureAndReleaseArray(b, ctx, side, out, riid);
                }
                b.insert(b.end(), gEv.begin(), gEv.end());
                b.push_back(f->mRefCount);
                PushFactoryVTable(b, ctx, side, f);
                ::operator delete(f, sizeof(Factory));
            }
            Same(ctx, blobs[0], blobs[1], "Slots", seed << 8 | uint32_t(slot));
        }
    }
}

// ===== The adapters on a factory built by the game's creator, with a trap in every slot they forward to =====
namespace {
void *gTrapVT[9];
uint32_t __fastcall Trap(void *, void *)
{
    Ev({0x7a7a7a7au});
    return 0;
}
} // namespace

DIFF_TEST(PersistResourceFactoryFamRel_Adapters, 0x4f2170)
{
    using A = cTSPersistResourceFactoryFamilyRelationshipArrayAdapters;
    SetFakes();
    for (uint32_t seed = 0; seed < 32; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            Factory *f = MakeSide(ctx, 0); // both factories are the original's
            f->mRefCount = 4;
            std::memcpy(gTrapVT, ctx.image.At<void>(Factory::kOriginalVTable), sizeof(gTrapVT));
            for (int slot : {4, 5, 6, 7}) gTrapVT[slot] = reinterpret_cast<void *>(&Trap);
            *reinterpret_cast<void **>(f) = gTrapVT;
            gCfg = Config{};
            gCfg.resAnswer = seed % 3 != 0;
            gCfg.rwAnswer = seed % 2;
            gRng.seed(seed);
            gEv.clear();
            Blob &b = blobs[side];
            const uint32_t riid = kRiids[seed % 8];
            const uint32_t type = kTypes[(seed / 8) % 4];
            void *out = reinterpret_cast<void *>(0x5678);
            // CreateInstanceByType, Read and Write lost their adapters (lane GP: only the game's vtable reaches them, and no
            // factory carries it while the Create hook is in; "in_effect": "callers"): the original runs on both sides, keeping
            // the sequence the deleting destructor's adapter sees; their own tests above compare ours.
            const uint8_t created = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, void **, uint32_t, uint32_t)>(
                0x4f25d0)(f, type, riid, &out, 0, 0);
            b.push_back(created);
            if (created) CaptureAndReleaseArray(b, ctx, 0, out, riid); // the original constructor's array on both sides
            b.push_back(ctx.Original<uint8_t(__thiscall *)(void *, void *, void *)>(0x4f3280)(f, &gRes, &gRec));
            b.push_back(ctx.Original<uint8_t(__thiscall *)(void *, void *, void *)>(0x4f3f70)(f, &gRes, &gRec));
            b.insert(b.end(), gEv.begin(), gEv.end());
            b.push_back(f->mRefCount);
            difftest::FreeRecorder rec;
            void *r = side == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x4f2170)(f, 1) : A::DeletingDestructor(f, nullptr, 1);
            rec.Stop();
            b.insert(b.end(), {uint32_t(r == f), uint32_t(rec.Freed(f))});
        }
        Same(ctx, blobs[0], blobs[1], "Adapters", seed);
    }
}
