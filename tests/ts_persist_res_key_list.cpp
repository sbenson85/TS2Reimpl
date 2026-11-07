// Differential tests for cTSPersistResKeyList (src/ts/cTSPersistResKeyList.*): a saved list of persist resource keys.
//
// Standalone shape (docs/standalone-shape.md): the original side's list is built by the original factory 0x1f4dc0 (game vtables)
// and the code is called at its RVA; ours is built by our Create (the factory hook's entry) and called through its own vtable
// slot, as the game's callers reach it. The vtable words are the only bytes that differ by design (PushVTableWords).
// PersistResKeyList_Slots covers the slots no other test calls; PersistResKeyList_Adapters the RVA hooks' entry points on a list
// carrying the game's vtables.
//
// A world is a list built the side's way, its key vector (allocator-service memory) filled from a seed. Fakes: copies of the
// object's own vtables with one slot patched where the code under test calls through them (AddRef, the persist face's GetKey),
// a stream with scripted reads / recorded writes whose QueryInterface hands out a record face and, through it, a key mapper, the
// service object and persist system Commit talks to, and the allocator service for the key vector. A run compares the result,
// the object's words and key list (capacity too: growth is the library's policy), and the event log (every fake call with its
// arguments, key contents instead of addresses).
#include "game_allocator.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "gz/cGZDBSegmentPackedFile.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "ts/cTSPersistResKeyList.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <vector>

namespace {

using Blob = std::vector<uint32_t>;
using List = cTSPersistResKeyList;

enum : uint32_t {
    evAddRef = 1, evStreamQuery, evRecordRelease, evMapperRelease, evRecordQuery, evReadWord, evReadVersion, evWriteWord, evError,
    evMapWrite, evMapRead, evAlloc, evFree, evServiceQuery, evSave, evFindSegment, evFlush, evPersistRelease, evSegmentRelease,
    evGetKey, evThrew,
};

Blob gEv;
void Ev(std::initializer_list<uint32_t> words) { gEv.insert(gEv.end(), words); }
std::mt19937 gRng;

// ---- the object under test ----
using reskeylist::kPersistFace;
using reskeylist::kSerialFace;

// A side's list: the original factory's (game vtables) or ours (the factory hook's entry: our vtables).
List *MakeSide(difftest::Context &ctx, int side)
{
    return side == 0 ? ctx.Original<List *(__cdecl *)()>(0x1f4dc0)() : List::Create();
}

// Slot `slot` of the vtable at `face` (the object's own), called with `this` = face. Byte returns are compared raw (al).
template <class R, class... A> R CallSlot(const void *face, int slot, A... args)
{
    using Fn = R(__thiscall *)(const void *, A...);
    return (*static_cast<Fn *const *>(face))[slot](face, args...);
}

// Our three vtables, from a list built by our Create.
struct OurVTables {
    const void *face[3];
};
OurVTables OursFromCreate()
{
    List *o = List::Create();
    OurVTables v;
    std::memcpy(v.face, o, sizeof(v.face));
    delete o;
    return v;
}
constexpr uint32_t kOriginalVTables[3] = {List::kOriginalVTable, List::kOriginalPersistVTable, List::kOriginalSerialVTable};
// The three vtable words of a side's object, as one flag each: the game's on the original side, ours on our side (and ours are
// not the game's).
void PushVTableWords(Blob &b, difftest::Context &ctx, int side, const uint32_t *words)
{
    const OurVTables ours = OursFromCreate();
    for (int i = 0; i < 3; ++i) {
        const uint32_t game = uint32_t(uintptr_t(ctx.image.At<void>(kOriginalVTables[i])));
        const uint32_t want = side == 0 ? game : uint32_t(uintptr_t(ours.face[i]));
        b.push_back(words[i] == want && uint32_t(uintptr_t(ours.face[i])) != game);
    }
}

struct World {
    List *obj = nullptr;
    int side = 0;
    List *Get() { return obj; }
    uint8_t *Face(uint32_t offset) { return reinterpret_cast<uint8_t *>(obj) + offset; }
    void Build(difftest::Context &ctx, int s, uint32_t seed, unsigned maxKeys, unsigned extraCapacity)
    {
        side = s;
        obj = MakeSide(ctx, side);
        std::mt19937 r(seed * 7919u + 13);
        const unsigned n = maxKeys ? r() % (maxKeys + 1) : 0;
        auto &keys = obj->mKeys;
        if (extraCapacity) keys.reserve(n + extraCapacity);
        for (unsigned i = 0; i < n; ++i) {
            PersistResKey k;
            k.type = r() % 3 ? r() : 0xac506764;
            k.group = r() % 3 ? r() : 0;
            k.instance = r();
            k.instanceHigh = r() % 4 ? r() % 5 : r();
            keys.push_back(k);
        }
        // Spare capacity holds nonzero words (fresh heap is often zero): Read's grow within capacity value-initialises
        // (0x1f4ed0's uninitialised-value-construct-n), and a version-0/1 key never reads its high word (DL).
        for (size_t i = keys.size(); i < keys.capacity(); ++i) keys.data()[i] = {r() | 1, r() | 1, r() | 1, r() | 1};
        obj->mRefCount = r() % 4;
        obj->mKey = {r(), r(), r(), r()};
    }
    // Frees the keys and the block without running either side's destructor (tested on its own).
    void Destroy()
    {
        obj->mKeys.~PersistResKeyVector();
        ::operator delete(obj, sizeof(List));
        obj = nullptr;
    }
};

// Every word but the vtable words: reference count, keys, own key.
void CaptureData(Blob &b, List *o)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(o);
    b.push_back(w[3]); // reference count
    const auto &keys = o->mKeys;
    b.push_back(uint32_t(keys.size()));
    b.push_back(uint32_t(keys.capacity()));
    for (const auto &k : keys) b.insert(b.end(), {k.type, k.group, k.instance, k.instanceHigh});
    b.insert(b.end(), {w[7], w[8], w[9], w[10]}); // the object's own key
}

void Capture(Blob &b, difftest::Context &ctx, int side, List *o)
{
    PushVTableWords(b, ctx, side, reinterpret_cast<const uint32_t *>(o));
    CaptureData(b, o);
}

void Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    if (a == b) return;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    ctx.Fail("%s seed %u: differ at word %u (sizes %u/%u): %08x vs %08x", what, seed, unsigned(i), unsigned(a.size()), unsigned(b.size()),
             i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0);
}

// ---- the allocator service the key vector uses (cache 0x10c95d4, guard 0x10c95d8) and the general allocator behind it ----
struct AllocService {
    void **vt;
} gAlloc;
void *gAllocVT[8];
void *__fastcall AllocAlloc(AllocService *, void *, uint32_t bytes)
{
    Ev({evAlloc, bytes});
    return RZMemory::Malloc(bytes);
}
void __fastcall AllocFree(AllocService *, void *, void *block)
{
    Ev({evFree});
    if (block) RZMemory::Free(block);
}

// ---- the service object (global 0x10c9378) and the persist system, segment and persist face it leads to ----
struct Obj {
    void **vt;
};
void *gServiceVT[8], *gPersistVT[40], *gSegmentVT[16], *gFaceVT[8];
Obj gService{gServiceVT}, gPersist{gPersistVT}, gSegment{gSegmentVT};
struct Config {
    uint32_t saveResult = 1, flushResult = 1, findResult = 1;
    bool findSetsOut = true;
    int throwAt = 0; // 1: SaveResource throws, 2: the flush throws
    uint8_t *object = nullptr;
} gCfg;

uint32_t __fastcall ServiceQuery(Obj *, void *, uint32_t id1, uint32_t id2, void **out)
{
    Ev({evServiceQuery, id1, id2});
    *out = &gPersist;
    return 1;
}
uint32_t __fastcall PersistRelease(Obj *, void *)
{
    Ev({evPersistRelease});
    return 0;
}
uint32_t __fastcall PersistSave(Obj *, void *, void *face, void *segment)
{
    Ev({evSave, face ? uint32_t(static_cast<uint8_t *>(face) - gCfg.object) : 0xffffffffu, segment == &gSegment ? 1u : segment ? 2u : 0u});
    if (gCfg.throwAt == 1) throw 7;
    return (0x5a5a00u * (gRng() % 4)) | gCfg.saveResult;
}
uint32_t __fastcall PersistFind(Obj *, void *, const uint32_t *key, uint32_t id, void **out)
{
    Ev({evFindSegment, key[0], key[1], key[2], key[3], id});
    if (gCfg.findResult || gCfg.findSetsOut) *out = &gSegment;
    return (0x3300u * (gRng() % 3)) | gCfg.findResult;
}
uint32_t __fastcall SegmentRelease(Obj *, void *)
{
    Ev({evSegmentRelease});
    return 0;
}
uint32_t __fastcall SegmentFlush(Obj *, void *)
{
    Ev({evFlush});
    if (gCfg.throwAt == 2) throw 8;
    return (0x7700u * (gRng() % 3)) | gCfg.flushResult;
}
// The persist face's GetKey (slot 3, ret 4): copies the object's key (face + 0x18) to the out pointer and returns it.
uint32_t *__fastcall FaceGetKey(uint8_t *face, void *, uint32_t *out)
{
    Ev({evGetKey});
    std::memcpy(out, face + 0x18, 16);
    return out;
}
uint32_t __fastcall FakeAddRef(List *self, void *)
{
    Ev({evAddRef});
    return ++self->mRefCount;
}

// ---- the stream, its record face and the key mapper ----
struct Stream {
    void **vt;
    bool reading = true;
    uint8_t queryAnswer = 1;
    bool mapperPresent = true;
    uint32_t errorByte = 0;
    uint32_t ordinal = 0;
    uint32_t failFrom = 0; // reads fail (leaving the output alone) from this ordinal on, some of the time
    std::vector<uint32_t> script;
    size_t next = 0;
    uint32_t mapSalt = 0;
} gStream;
void *gStreamVT[24];
Obj gRecord, gMapper;
void *gRecordVT[24], *gMapperVT[16];

uint32_t __fastcall StreamQuery(Stream *s, void *, uint32_t id, void **out)
{
    Ev({evStreamQuery, id});
    const uint32_t junk = 0x1200u * (gRng() % 3);
    if (!s->queryAnswer) return junk;
    *out = &gRecord;
    return junk | s->queryAnswer;
}
uint32_t NextWord()
{
    return gStream.next < gStream.script.size() ? gStream.script[gStream.next++] : uint32_t(gRng());
}
bool ReadFails()
{
    const uint32_t n = gStream.ordinal++;
    return gStream.failFrom && n >= gStream.failFrom && gRng() % 3 == 0;
}
uint32_t __fastcall StreamRead(Stream *, void *, uint32_t *out)
{
    if (ReadFails()) {
        Ev({evReadWord, 0xffffffffu, 0});
        return 0x4400;
    }
    const uint32_t v = NextWord();
    Ev({evReadWord, gStream.ordinal, v});
    *out = v;
    return 0x9900 | 1;
}
uint32_t __fastcall StreamReadVersion(Stream *, void *, uint32_t *out)
{
    const uint32_t n = gStream.ordinal++;
    const uint32_t v = NextWord();
    Ev({evReadVersion, n, v});
    *out = v;
    return 1;
}
uint32_t __fastcall StreamWrite(Stream *, void *, uint32_t v)
{
    Ev({evWriteWord, v});
    return 1;
}
uint32_t __fastcall StreamError(Stream *s, void *)
{
    Ev({evError});
    return s->errorByte;
}
uint32_t __fastcall RecordRelease(Obj *, void *)
{
    Ev({evRecordRelease});
    return 0;
}
uint32_t __fastcall RecordQuery(Obj *, void *, uint32_t id, void **out)
{
    Ev({evRecordQuery, id});
    if (!gStream.mapperPresent) return 0;
    *out = &gMapper;
    return 1;
}
uint32_t __fastcall MapperRelease(Obj *, void *)
{
    Ev({evMapperRelease});
    return 0;
}
// Slot 10 (write): out = a function of the key; sometimes only the first two words are written (the rest must be the zeros the
// caller preset). Slot 11 (read): the same, and the answer says whether the caller takes the result.
void Map(const uint32_t *key, uint32_t *out, uint32_t salt)
{
    out[0] = key[0] * 3 + salt;
    out[1] = key[1] ^ 0x5555;
    if (salt % 2) return;
    out[2] = key[2] + 11;
    out[3] = ~key[3];
}
uint32_t __fastcall MapperWrite(Obj *, void *, const uint32_t *key, uint32_t *out)
{
    Ev({evMapWrite, key[0], key[1], key[2], key[3], out[0], out[1], out[2], out[3]});
    Map(key, out, gStream.mapSalt);
    return 0x300u * (gRng() % 3);
}
uint32_t __fastcall MapperRead(Obj *, void *, const uint32_t *key, uint32_t *out)
{
    Ev({evMapRead, key[0], key[1], key[2], key[3], out[0], out[1], out[2], out[3]});
    Map(key, out, gStream.mapSalt + 1);
    return (0x300u * (gRng() % 3)) | (gRng() % 3 != 0);
}

void SetFakes()
{
    gStreamVT[0] = reinterpret_cast<void *>(&StreamQuery);
    gStreamVT[8] = reinterpret_cast<void *>(&StreamReadVersion);
    gStreamVT[18] = reinterpret_cast<void *>(&StreamError);
    gStream.vt = gStreamVT;
    gRecordVT[2] = reinterpret_cast<void *>(&RecordRelease);
    gRecordVT[15] = reinterpret_cast<void *>(&RecordQuery);
    gRecord.vt = gRecordVT;
    gMapperVT[2] = reinterpret_cast<void *>(&MapperRelease);
    gMapperVT[10] = reinterpret_cast<void *>(&MapperWrite);
    gMapperVT[11] = reinterpret_cast<void *>(&MapperRead);
    gMapper.vt = gMapperVT;
    gServiceVT[5] = reinterpret_cast<void *>(&ServiceQuery);
    gPersistVT[2] = reinterpret_cast<void *>(&PersistRelease);
    gPersistVT[16] = reinterpret_cast<void *>(&PersistSave);
    gPersistVT[34] = reinterpret_cast<void *>(&PersistFind);
    gSegmentVT[2] = reinterpret_cast<void *>(&SegmentRelease);
    gSegmentVT[10] = reinterpret_cast<void *>(&SegmentFlush);
    gFaceVT[3] = reinterpret_cast<void *>(&FaceGetKey);
}
void SetStream(bool reading)
{
    gStream.reading = reading;
    gStreamVT[9] = reading ? reinterpret_cast<void *>(&StreamRead) : reinterpret_cast<void *>(&StreamWrite);
    gStream.ordinal = 0;
    gStream.next = 0;
}

struct Globals {
    difftest::Context &ctx;
    uint32_t savedAlloc, savedGuard, savedService;
    bool builtAllocator = false;
    explicit Globals(difftest::Context &c) : ctx(c)
    {
        savedAlloc = *ctx.image.At<uint32_t>(0x10c95d4);
        savedGuard = *ctx.image.At<uint32_t>(0x10c95d8);
        savedService = *ctx.image.At<uint32_t>(0x10c9378);
        auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
        const auto *bytes = reinterpret_cast<const uint8_t *>(ga);
        bool zero = true;
        for (size_t i = 0; i < sizeof(GeneralAllocator); ++i) zero = zero && bytes[i] == 0;
        if (zero) ga->Construct(nullptr, 0, 0, 0, nullptr, nullptr), builtAllocator = true;
        gAllocVT[3] = reinterpret_cast<void *>(&AllocAlloc);
        gAllocVT[4] = reinterpret_cast<void *>(&AllocFree);
        gAlloc.vt = gAllocVT;
        *ctx.image.At<void *>(0x10c95d4) = &gAlloc;
        *ctx.image.At<int32_t>(0x10c95d8) = INT32_MIN;
        *ctx.image.At<void *>(0x10c9378) = &gService;
        SetFakes();
    }
    ~Globals()
    {
        *ctx.image.At<uint32_t>(0x10c95d4) = savedAlloc;
        *ctx.image.At<uint32_t>(0x10c95d8) = savedGuard;
        *ctx.image.At<uint32_t>(0x10c9378) = savedService;
        if (builtAllocator) {
            auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
            difftest_alloc::ShutdownAndRelease(ga);
        }
    }
};

constexpr uint32_t kSeeds = 160;

// The stream script of a Read: a marker stream (version and count) or a bare count, then key words. Counts reach the resize
// paths: shrink, equal, grow within the capacity, grow past it.
void BuildScript(Stream &s, uint32_t seed, size_t size, size_t capacity)
{
    std::mt19937 r(seed * 31u + 5);
    const size_t picks[] = {0, 1, size ? size - 1 : 0, size, size + 1, capacity, capacity + 1, capacity + 5, 9};
    const uint32_t count = uint32_t(picks[r() % (sizeof(picks) / sizeof(picks[0]))]);
    const uint32_t versions[] = {0, 1, 2, 3, 2, 0x80000000u, 0xffffffffu, 1, 2};
    s.script.clear();
    if (seed % 4 != 0) {
        s.script.push_back(0xdeadbeef);
        s.script.push_back(versions[r() % (sizeof(versions) / sizeof(versions[0]))]);
    }
    s.script.push_back(count);
    for (uint32_t i = 0; i < count * 4 + 4; ++i) s.script.push_back(r() % 5 ? uint32_t(r()) : r() % 4);
}

} // namespace

DIFF_TEST(PersistResKeyList_GetGZCLSID, 0x1f4e40)
{
    Globals globals(ctx);
    Blob blobs[2];
    for (int side = 0; side < 2; ++side) {
        World w;
        w.Build(ctx, side, 1, 3, 0);
        void *serial = w.Face(kSerialFace);
        const uint32_t r = side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x1f4e40)(serial) : CallSlot<uint32_t>(serial, 5);
        blobs[side].push_back(r);
        Capture(blobs[side], ctx, side, w.Get());
        w.Destroy();
    }
    Same(ctx, blobs[0], blobs[1], "GetGZCLSID", 0);
}

// The factory, then the side's own Release (slot 2) at reference counts 0..3: at 1 or below it destroys and frees the list
// through slot 8.
DIFF_TEST(PersistResKeyList_Create, 0x1f4dc0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 4; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            List *o = MakeSide(ctx, side);
            Blob &b = blobs[side];
            b.push_back(o ? 1u : 0u);
            if (!o) continue;
            b.push_back(uint32_t(HeapSize(GetProcessHeap(), 0, o) >= sizeof(List)));
            const uint32_t *w = reinterpret_cast<const uint32_t *>(o);
            PushVTableWords(b, ctx, side, w);
            for (int i = 3; i < 11; ++i) b.push_back(w[i]);
            o->mRefCount = seed;
            difftest::FreeRecorder rec;
            const uint32_t r = CallSlot<uint32_t>(o, 2);
            rec.Stop();
            const bool freed = rec.Freed(o);
            b.insert(b.end(), {r, uint32_t(freed)});
            if (!freed) {
                b.push_back(o->mRefCount);
                ::operator delete(o, sizeof(List));
            }
        }
        Same(ctx, blobs[0], blobs[1], "Create", seed);
    }
}

DIFF_TEST(PersistResKeyList_QueryInterface, 0x1f4e60)
{
    Globals globals(ctx);
    const uint32_t ids[] = {0x6cd04176, 0x6cd04175, 0x6cd04177, 1, 0, 2, 0x456b8f1d, 0x456b8f1c, 0x456b8f1e, 0xe4fda3d4, 0xe4fda3d3,
                            0xe4fda3d5, 0xffffffff, 0x80000000, 0xac506764};
    for (uint32_t seed = 0; seed < 4; ++seed) {
        for (uint32_t id : ids) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                World w;
                w.Build(ctx, side, seed, 3, 0);
                // A copy of the object's own vtable with a recording AddRef.
                void **own = *reinterpret_cast<void ***>(w.Get());
                void *vt[9];
                std::memcpy(vt, own, sizeof(vt));
                vt[1] = reinterpret_cast<void *>(&FakeAddRef);
                *reinterpret_cast<void ***>(w.Get()) = vt;
                gEv.clear();
                void *out = reinterpret_cast<void *>(0xdeadbeef);
                uint8_t r;
                if (side == 0) r = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, void **)>(0x1f4e60)(w.Get(), id, &out));
                else r = CallSlot<uint8_t>(w.Get(), 0, id, &out);
                *reinterpret_cast<void ***>(w.Get()) = own;
                blobs[side].push_back(r);
                blobs[side].push_back(out == reinterpret_cast<void *>(0xdeadbeef) ? 0xdeadbeefu
                                                                                    : uint32_t(static_cast<uint8_t *>(out) - w.Face(0)));
                blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
                Capture(blobs[side], ctx, side, w.Get());
                w.Destroy();
            }
            Same(ctx, blobs[0], blobs[1], "QueryInterface", id);
        }
    }
}

// Slot 8: the original's deleting destructor 0x1f4d20 on the original's list, the compiler's scalar deleting destructor on ours.
DIFF_TEST(PersistResKeyList_DeletingDestructor, 0x1f4d20)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            World w;
            w.Build(ctx, side, seed, seed % 7, seed % 3 ? 0 : 4);
            List *h = w.Get();
            // The destructor puts the class's vtables back: the +4 / +8 words start as others (the first is the call's way in).
            reinterpret_cast<uint32_t *>(h)[1] = 0x22220000 + seed;
            reinterpret_cast<uint32_t *>(h)[2] = 0x33330000 + seed;
            const uint32_t flags = seed % 4;
            gEv.clear();
            difftest::FreeRecorder rec;
            List *r = side == 0 ? ctx.Original<List *(__thiscall *)(void *, uint32_t)>(0x1f4d20)(h, flags) : CallSlot<List *>(h, 8, flags);
            rec.Stop();
            const bool freed = rec.Freed(h);
            Blob &b = blobs[side];
            b.push_back(r == h);
            b.push_back(freed);
            b.push_back(uint32_t(rec.Count()));
            b.insert(b.end(), gEv.begin(), gEv.end());
            if (!freed) {
                const uint32_t *words = reinterpret_cast<const uint32_t *>(h);
                PushVTableWords(b, ctx, side, words);
                for (int i = 3; i < 11; ++i) b.push_back(i >= 4 && i < 7 ? (words[i] != 0) : words[i]); // pointers: zero or not
                ::operator delete(h, sizeof(List));
            }
        }
        Same(ctx, blobs[0], blobs[1], "DeletingDestructor", seed);
    }
}

DIFF_TEST(PersistResKeyList_Write, 0x1f5290)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        for (int shape = 0; shape < 4; ++shape) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                World w;
                w.Build(ctx, side, seed, seed % 6 ? 7 : 0, 0);
                SetStream(false);
                gStream.queryAnswer = (shape & 1) ? uint8_t(1 + gRng() % 1) : 0;
                gStream.mapperPresent = (shape & 2) != 0;
                gStream.errorByte = (seed % 5 == 0) ? 1u : (seed % 5 == 1 ? 0x80u : 0u);
                gStream.mapSalt = seed;
                gRng.seed(seed);
                gEv.clear();
                void *serial = w.Face(kSerialFace);
                uint8_t r;
                if (side == 0) r = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x1f5290)(serial, &gStream));
                else r = CallSlot<uint8_t>(serial, 3, static_cast<void *>(&gStream));
                blobs[side].push_back(r);
                blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
                Capture(blobs[side], ctx, side, w.Get());
                w.Destroy();
            }
            Same(ctx, blobs[0], blobs[1], "Write", seed * 4 + uint32_t(shape));
        }
    }
}

DIFF_TEST(PersistResKeyList_Read, 0x1f4ed0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 3; ++seed) {
        for (int shape = 0; shape < 4; ++shape) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                World w;
                w.Build(ctx, side, seed, seed % 5 ? 6 : 0, seed % 3 ? 0 : 3);
                SetStream(true);
                gStream.queryAnswer = (shape & 1) ? 1 : 0;
                gStream.mapperPresent = (shape & 2) != 0;
                gStream.errorByte = (seed % 6 == 0) ? 1u : 0u;
                gStream.mapSalt = seed;
                // The first word, the version and the count fill otherwise uninitialised stack words in the original (ours
                // start at 0), so they never fail; later reads may fail and leave their output alone.
                gStream.failFrom = seed % 2 ? 0 : 4;
                BuildScript(gStream, seed, w.Get()->mKeys.size(), w.Get()->mKeys.capacity());
                gRng.seed(seed);
                gEv.clear();
                void *serial = w.Face(kSerialFace);
                uint8_t r;
                if (side == 0) r = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x1f4ed0)(serial, &gStream));
                else r = CallSlot<uint8_t>(serial, 4, static_cast<void *>(&gStream));
                blobs[side].push_back(r);
                blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
                Capture(blobs[side], ctx, side, w.Get());
                w.Destroy();
            }
            Same(ctx, blobs[0], blobs[1], "Read", seed * 4 + uint32_t(shape));
        }
    }
}

DIFF_TEST(PersistResKeyList_Commit, 0x1f5130)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            World w;
            w.Build(ctx, side, seed, 3, 0);
            // A copy of the persist face's own vtable with a recording GetKey.
            void **&persistWord = *reinterpret_cast<void ***>(w.Face(kPersistFace));
            void **own = persistWord;
            void *vt[12];
            std::memcpy(vt, own, sizeof(vt));
            vt[3] = reinterpret_cast<void *>(&FaceGetKey);
            persistWord = vt;
            gCfg.object = w.Face(0);
            gCfg.saveResult = seed % 4 == 0 ? 0 : seed % 4 == 1 ? 2 : 1;
            gCfg.flushResult = (seed / 4) % 3;
            gCfg.findResult = (seed / 12) % 2;
            gCfg.findSetsOut = (seed / 24) % 2 == 0;
            gCfg.throwAt = seed % 17 == 0 ? 1 : seed % 19 == 0 ? 2 : 0;
            void *segment = (seed / 3) % 2 ? static_cast<void *>(&gSegment) : nullptr;
            gRng.seed(seed);
            gEv.clear();
            uint8_t r = 0xee;
            bool threw = false;
            try {
                if (side == 0) r = uint8_t(ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x1f5130)(w.Get(), segment));
                else r = CallSlot<uint8_t>(w.Get(), 4, segment);
            } catch (int) {
                threw = true;
            }
            persistWord = own;
            blobs[side].push_back(r);
            blobs[side].push_back(threw);
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            Capture(blobs[side], ctx, side, w.Get());
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Commit", seed);
    }
}

// ===== Every other slot through the object's own vtable =====
// The slots no test above calls: the main face's AddRef 0x15e520 (library), Release 0x1f5100, Clear 0x1f5280,
// GetPersistResource 0x1c04e0, GetKeys / GetKeys2 0x147780; the +4 / +8 faces' QueryInterface / AddRef / Release thunks
// (0x1f4eb7 / 0x1f4daf / 0x1f5117, 0x1f4ebf / 0x1f4db7 / 0x1f511f); and the whole persist face (GetKey 0x1f4e50, SetKey 0x1f5260,
// GetType 0x148ba0, GetGroup 0x147d00, GetInstance 0x15e550, SetType 0x1f5270, SetGroup 0x1f5240, SetInstance 0x1f5250,
// GetRefCount 0x46390). Same seeded state on both sides; compared: the result, what an out-parameter received, the object's
// words after the call and its three vtable words (the game's on the original side, ours on our side).
namespace {
struct SlotCall {
    uint32_t face;
    int slot;
};
constexpr SlotCall kSlotCalls[] = {
    {0, 1}, {0, 2}, {0, 3}, {0, 5}, {0, 6}, {0, 7}, {4, 0}, {4, 1}, {4, 2}, {4, 3},  {4, 4},
    {4, 5}, {4, 6}, {4, 7}, {4, 8}, {4, 9}, {4, 10}, {4, 11}, {8, 0}, {8, 1}, {8, 2},
};
constexpr uint32_t kQueryIds[] = {1, 0x6cd04176, 0x456b8f1d, 0xe4fda3d4, 0, 0xac506764, 0xa56b8f17};
} // namespace

DIFF_TEST(PersistResKeyList_Slots, 0x1f4eb7)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 40; ++seed) {
        for (const SlotCall &c : kSlotCalls) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                World w;
                w.Build(ctx, side, seed, seed % 5, seed % 3 ? 0 : 2);
                List *o = w.Get();
                std::mt19937 rng(seed);
                o->mRefCount = 2 + rng() % 5; // at least 2: Release only counts down here (its freeing is tested in _Create)
                o->mKey = {rng(), rng(), rng(), rng()};
                const uint32_t arg = rng();
                const cGZPersistResourceKey newKey = {rng(), rng(), rng(), rng()};
                const uint8_t *face = w.Face(c.face);
                const uint8_t *base = w.Face(0);
                Blob &b = blobs[side];
                b.push_back(c.face << 8 | uint32_t(c.slot));
                if (c.slot == 0) { // QueryInterface through the face
                    void *out = reinterpret_cast<void *>(0x5678);
                    const uint8_t r = CallSlot<uint8_t>(face, 0, kQueryIds[arg % 7], &out);
                    b.insert(b.end(), {r, out == reinterpret_cast<void *>(0x5678) ? 0xdeadbeefu : uint32_t(static_cast<uint8_t *>(out) - base)});
                } else if (c.slot == 1 || c.slot == 2) { // AddRef / Release
                    b.push_back(CallSlot<uint32_t>(face, c.slot));
                } else if (c.face == 0 && c.slot == 3) { // Clear (no result: the original leaves begin in eax)
                    CallSlot<void>(face, 3);
                } else if (c.face == 0) { // GetPersistResource, GetKeys, GetKeys2: pointers into the object
                    void *r = CallSlot<void *>(face, c.slot);
                    b.push_back(uint32_t(static_cast<uint8_t *>(r) - base));
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
                Capture(b, ctx, side, o);
                w.Destroy();
            }
            Same(ctx, blobs[0], blobs[1], "Slots", seed << 16 | c.face << 8 | uint32_t(c.slot));
        }
    }
}

// ===== The adapters: our functions behind the old entry points, on a list built by the game's factory =====
// What runs when the game calls an old entry point directly or holds a list with its own vtables (the factory hook off). The
// object on both sides is the original's; the original side calls the RVA, ours the adapter the hook installs. The game's
// vtables are copied with a trap in every slot an adapter forwards to, so an adapter that dispatched through the object's
// vtable (in game: back into its own hook) logs it. AddRef (QueryInterface's call) and the persist face's GetKey (Commit's)
// stay the game's.
namespace {
void *gTrapVT[9];
void *gTrapSerialVT[6];
uint32_t __fastcall Trap(void *, void *)
{
    Ev({0x7a7a7a7au});
    return 0;
}
} // namespace

DIFF_TEST(PersistResKeyList_Adapters, 0x1f4e40)
{
    using A = cTSPersistResKeyListAdapters;
    Globals globals(ctx);
    const uint32_t ids[] = {0x6cd04176, 1, 0x456b8f1d, 0xe4fda3d4, 0, 0xac506764};
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            World w;
            w.Build(ctx, 0, seed, 5, seed % 3 ? 0 : 2); // both lists are the original's
            List *self = w.Get();
            self->mRefCount = 5;
            std::memcpy(gTrapVT, ctx.image.At<void>(List::kOriginalVTable), sizeof(gTrapVT));
            for (int slot : {0, 4, 8}) gTrapVT[slot] = reinterpret_cast<void *>(&Trap);
            std::memcpy(gTrapSerialVT, ctx.image.At<void>(List::kOriginalSerialVTable), sizeof(gTrapSerialVT));
            for (int slot : {3, 4, 5}) gTrapSerialVT[slot] = reinterpret_cast<void *>(&Trap);
            *reinterpret_cast<void **>(w.Face(0)) = gTrapVT;
            *reinterpret_cast<void **>(w.Face(kSerialFace)) = gTrapSerialVT;
            std::mt19937 rng(seed);
            const uint32_t id = ids[rng() % 6];
            void *out = nullptr;
            void *serial = w.Face(kSerialFace);
            auto *serialFace = reinterpret_cast<cIGZSerializable *>(serial);
            gEv.clear();
            Blob &b = blobs[side];
            // Write, Read, Commit and the deleting destructor lost their adapters (lane GP: only the game's vtables reach them,
            // and no list carries those while the Create hook is in; "in_effect": "callers"): the original runs on both sides,
            // keeping the sequence the remaining adapters see; their own tests above compare ours.
            b.push_back(side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0x1f4e60)(self, id, &out)
                                  : A::QueryInterface(self, nullptr, id, &out));
            b.push_back(out ? uint32_t(static_cast<uint8_t *>(out) - w.Face(0)) : 0xffffffffu);
            b.push_back(side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x1f4e40)(serial) : A::GetGZCLSID(serialFace, nullptr));
            SetStream(false);
            gStream.queryAnswer = 1;
            gStream.mapperPresent = seed % 2 != 0;
            gStream.errorByte = 0;
            gStream.mapSalt = seed;
            gRng.seed(seed);
            b.push_back(ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x1f5290)(serial, &gStream));
            SetStream(true);
            gStream.failFrom = 0;
            BuildScript(gStream, seed, self->mKeys.size(), self->mKeys.capacity());
            b.push_back(ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x1f4ed0)(serial, &gStream));
            gCfg = Config{};
            gCfg.object = w.Face(0);
            void *segment = seed % 2 ? static_cast<void *>(&gSegment) : nullptr;
            b.push_back(ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x1f5130)(self, segment));
            b.insert(b.end(), gEv.begin(), gEv.end());
            CaptureData(b, self);
            difftest::FreeRecorder rec;
            List *r = ctx.Original<List *(__thiscall *)(void *, uint32_t)>(0x1f4d20)(self, 1);
            rec.Stop();
            b.insert(b.end(), {uint32_t(r == self), uint32_t(rec.Freed(self))});
        }
        Same(ctx, blobs[0], blobs[1], "Adapters", seed);
    }
}
