// Differential tests for cGZPersistBinaryReader's object-level slots: ReadRoot (slot 3, 0x9bfd10), ReadObject (slot 24,
// 0x9c0630), ReadStringRecord (slot 26, 0x9c0490) and the deleting destructor (slot 29, 0x9bfc20).
//
// Every collaborator is an svcfx fake (tests/ts_services_fixture.h): the root and its persist resource, the context and
// its group face, the stream (slot 9 reads words from a script; slot 0 answers the serial record face), the service in
// game global 0x10c9378 that hands out the resource manager, the manager (slot 11 known, 9 register, 10 unregister,
// 3 look up: answers by the key's contents, which it logs), and the director framework that creates the listed
// objects. Each object's Read (slot 3) logs the reader's state at that moment: its stream, delegate, group, and the
// back ring entry's two vectors, which are otherwise destroyed before the call returns. The reader's ring is a real
// std::deque (the game's layout) on both sides, optionally holding older entries; after the call the ring's words, the
// reader's fields, the log, every fake's reference count and the call's heap balance (blocks, bytes) are compared.
#include "ts_services_fixture.h"
#include "free_recorder.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZPersistBinaryReader.h"
#include "rz/cRZString.h"

#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <vector>

namespace {

using Reader = cGZPersistBinaryReader;
using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Norm;
using svcfx::Obj;

enum : unsigned {
    kRoot = 0,
    kResource = 1,
    kContext = 2,
    kGroupFace = 3,
    kStream = 4,
    kRecord = 5,
    kService = 6,
    kManager = 7,
    kFound = 8, // 8..11: resources the manager hands out
    kDirector = 12,
    kCreated = 13, // 13..16: objects the director creates
    kOldStream = 17,
    kOldRecord = 18,
    kHolder = 19, // a word ReadStringRecord overwrites without releasing
};

constexpr uint32_t kServiceCell = 0x10c9378;
constexpr uint32_t kReaderTag = 0x7e000000;
constexpr uint32_t kOutTag = 0x7d000000;
constexpr uint32_t evState = 0x70;
constexpr uint32_t evKey = 0x71;

alignas(16) unsigned char gReaderStorage[sizeof(Reader)];
Reader *gReader = nullptr;

// ---- the stream's word script ----
std::vector<uint32_t> gWords;
size_t gPos = 0;
uint32_t StreamWord(Obj *, uint32_t *args)
{
    auto *dst = reinterpret_cast<uint32_t *>(uintptr_t(args[0]));
    const bool ok = gPos < gWords.size();
    *dst = ok ? gWords[gPos++] : 0; // the fake writes every byte the real read would
    return ok;
}

// ---- the world one case runs in ----
struct World {
    bool rootHasResource = true;
    uint32_t key[4] = {};
    bool context = false;
    bool contextAnswers = false;
    uint32_t faceGroup = 0;
    uint32_t remap = 0;        // 0 none, 1 flip the group's low bit, 2 clear the type, 3 swap group/instance
    bool recordAnswers = true;
    bool known = false;
    uint32_t holdGroups[3] = {};
    uint32_t holdMask = 0;     // instance bits that make a lookup fail
    unsigned oldEntries = 0;
    uint32_t oldGroup = 0;     // what +0x24 holds before the call
};
World gWorld;

uint32_t ResourceKey(Obj *, uint32_t *args)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(args[0])), gWorld.key, 16);
    return 0;
}

uint32_t ContextQuery(Obj *, uint32_t *args)
{
    if (args[0] != Reader::kGroupContextIid || !gWorld.contextAnswers) return 0;
    *reinterpret_cast<void **>(uintptr_t(args[1])) = &F(kGroupFace);
    ++F(kGroupFace).refs;
    return 1;
}

uint32_t GroupRemap(Obj *, uint32_t *args)
{
    const auto *in = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(args[1]));
    Ev(evKey);
    for (int i = 0; i < 4; ++i) Ev(in[i]), Ev(out[i]);
    switch (gWorld.remap) {
    case 1: out[1] ^= 1; break;
    case 2: if (in[2] & 1) out[0] = 0; break;
    case 3: std::swap(out[1], out[2]); break;
    default: break;
    }
    return 0;
}

uint32_t StreamQuery(Obj *, uint32_t *args)
{
    // The out pointer is the new back entry's record slot (heap): logged as whether it is that slot.
    svcfx::gEv.back() = args[1] == svcfx::P(gReader->Ring().back().mRecord.Out()) ? 0x0e0e0e0eu : 0xbad0bad0u;
    if (args[0] != Reader::kSerialRecordIid || !gWorld.recordAnswers) return 0;
    *reinterpret_cast<void **>(uintptr_t(args[1])) = &F(kRecord);
    ++F(kRecord).refs;
    return 1;
}

uint32_t ServiceGet(Obj *, uint32_t *args)
{
    *reinterpret_cast<void **>(uintptr_t(args[2])) = &F(kManager);
    ++F(kManager).refs;
    return 1;
}

uint32_t ManagerLookup(Obj *, uint32_t *args)
{
    const auto *k = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
    Ev(evKey);
    for (int i = 0; i < 4; ++i) Ev(k[i]);
    bool held = false;
    for (uint32_t g : gWorld.holdGroups) held = held || g == k[1];
    if (k[2] & gWorld.holdMask) held = false;
    auto *out = reinterpret_cast<void **>(uintptr_t(args[2]));
    if (!held) {
        *out = nullptr;
        return 0;
    }
    Obj &found = F(kFound + (k[2] + k[0]) % 4);
    *out = &found;
    ++found.refs;
    return 1;
}

uint32_t DirectorCreate(Obj *, uint32_t *args)
{
    auto *out = reinterpret_cast<void **>(uintptr_t(args[2]));
    if (args[0] % 7 == 0) {
        *out = nullptr;
        return 0;
    }
    Obj &made = F(kCreated + args[0] % 4);
    *out = &made;
    ++made.refs;
    return 1;
}

void LogRefs(const std::vector<Reader::Ref> &v)
{
    Ev(uint32_t(v.size()));
    for (const Reader::Ref &r : v) Ev(Norm(svcfx::P(r.Get())));
}

// An object's Read: logs the reader's state while ReadRoot runs.
uint32_t ObjectRead(Obj *, uint32_t *args)
{
    Reader *r = reinterpret_cast<Reader *>(uintptr_t(args[0]));
    Ev(evState);
    Ev(Norm(svcfx::P(r->mStream)));
    Ev(Norm(svcfx::P(r->mDelegate)));
    Ev(r->mGroup);
    Ev(r->mCount);
    if (r->mCount != 0) {
        const Reader::Entry &back = r->Ring().back();
        LogRefs(back.mResources);
        LogRefs(back.mObjects);
        Ev(Norm(svcfx::P(back.mStream.Get())));
        Ev(Norm(svcfx::P(back.mRecord.Get())));
    }
    return 1;
}

void BuildFakes()
{
    svcfx::ResetAll();
    F(kRoot).Set(8, 0, gWorld.rootHasResource ? svcfx::P(&F(kResource)) : 0u);
    F(kRoot).Set(3, "p", 0, &ObjectRead);
    F(kResource).Set(3, "p", 0, &ResourceKey);
    F(kContext).Set(0, "dp", 0, &ContextQuery);
    F(kGroupFace).Set(5, 0, gWorld.faceGroup);
    F(kGroupFace).Data(6, 1);
    F(kGroupFace).Set(6, "d");
    F(kGroupFace).Set(4, "pp", 0, &GroupRemap);
    F(kStream).Set(9, "p", 0, &StreamWord);
    F(kStream).Set(0, "dp", 0, &StreamQuery);
    F(kService).Set(5, "--p", 0, &ServiceGet);
    F(kService).Data(5, 3);
    F(kManager).Set(11, "p", gWorld.known ? 1u : 0u);
    F(kManager).Set(9, "pp");
    F(kManager).Set(10, "p");
    F(kManager).Set(3, "p-p--", 0, &ManagerLookup);
    F(kManager).Data(3, 2);
    F(kDirector).Set(3, "--p", 0, &DirectorCreate);
    F(kDirector).Data(3, 3);
    for (unsigned i = 0; i < 4; ++i) F(kCreated + i).Set(3, "p", 0, &ObjectRead);
    svcfx::AddRange(gReaderStorage, sizeof(gReaderStorage), kReaderTag);
}

// Lays the reader out as its constructor leaves it, with `old` entries already on the ring (each with a stream, a record
// and one resource and object), and the reader's stream fields at the back entry as ReadRoot leaves them.
Reader *BuildReader(unsigned old, uint32_t group)
{
    std::memset(gReaderStorage, 0, sizeof(gReaderStorage));
    auto *r = reinterpret_cast<Reader *>(gReaderStorage);
    r->mVTable = GameAddress<void>(Reader::kVTable);
    r->mRefCount = 1;
    new (&r->Ring()) Reader::EntryRing();
    for (unsigned i = 0; i < old; ++i) {
        r->Ring().emplace_back(Reader::Ref(&F(kOldStream)));
        Reader::Entry &e = r->Ring().back();
        *e.mRecord.Out() = &F(kOldRecord);
        ++F(kOldRecord).refs;
        e.mResources.push_back(Reader::Ref(&F(kFound + i % 4)));
        e.mObjects.push_back(Reader::Ref(&F(kCreated + i % 4)));
    }
    if (old != 0) {
        r->mStream = r->Ring().back().mStream.Get();
        r->mDelegate = r->Ring().back().mRecord.Get();
    }
    r->mGroup = group;
    return r;
}

void CaptureReader(Blob &b, Reader *r)
{
    const auto *w = reinterpret_cast<const uint32_t *>(r);
    b.push_back(w[1]);                    // refcount
    b.push_back(w[4]);                    // map size
    b.push_back(w[5]);                    // offset
    b.push_back(w[6]);                    // count
    b.push_back(Norm(w[7]));              // stream
    b.push_back(Norm(w[8]));              // delegate
    b.push_back(w[9]);                    // group
    b.push_back(r->Ring().size());
    for (const Reader::Entry &e : r->Ring()) {
        b.push_back(Norm(svcfx::P(e.mStream.Get())));
        b.push_back(Norm(svcfx::P(e.mRecord.Get())));
        b.push_back(uint32_t(e.mResources.size()));
        b.push_back(uint32_t(e.mObjects.size()));
    }
}

void RandomWorld(std::mt19937 &rng, std::vector<uint32_t> &words)
{
    World w;
    static const uint32_t groups[] = {0x11111111, 0x22222222, Reader::kFallbackGroup, 0x33333333, 0};
    w.rootHasResource = rng() % 20 != 0;
    w.key[0] = uint32_t(rng());
    w.key[1] = groups[rng() % 5];
    w.key[2] = uint32_t(rng());
    w.key[3] = rng() % 2 ? 0 : uint32_t(rng());
    w.context = rng() % 4 != 0;
    w.contextAnswers = rng() % 4 != 0;
    switch (rng() % 4) {
    case 0: w.faceGroup = 0xffffffffu; break;
    case 1: w.faceGroup = 0; break;
    default: w.faceGroup = groups[rng() % 5]; break;
    }
    w.remap = uint32_t(rng() % 4);
    w.recordAnswers = rng() % 5 != 0;
    w.known = rng() % 3 == 0;
    for (uint32_t &g : w.holdGroups) g = rng() % 3 == 0 ? w.key[1] : groups[rng() % 5];
    w.holdMask = rng() % 2 ? 0 : 1u << (rng() % 4);
    w.oldEntries = unsigned(rng() % 3);
    w.oldGroup = uint32_t(rng());
    gWorld = w;

    words.clear();
    const uint32_t version = uint32_t(rng() % 4); // 0: no version word, else version - 1
    const uint32_t deps = uint32_t(rng() % 6);
    if (version != 0) words.push_back(0xffff0000u | (version - 1));
    words.push_back(deps);
    for (uint32_t i = 0; i < deps; ++i) {
        words.push_back(rng() % 4 == 0 ? w.faceGroup : groups[rng() % 5]); // group
        words.push_back(uint32_t(rng()));                                  // instance
        if (version >= 2) words.push_back(uint32_t(rng()));                // instance high (version 1 and up)
        words.push_back(rng() % 5 == 0 ? 0u : uint32_t(rng()));            // type
    }
    const uint32_t objects = uint32_t(rng() % 6);
    words.push_back(objects);
    for (uint32_t i = 0; i < objects; ++i) words.push_back(rng() % 5 == 0 ? 0u : uint32_t(rng() % 50));
    if (rng() % 8 == 0 && !words.empty()) words.resize(rng() % words.size()); // a short stream
}

Blob RunReadRoot(difftest::Context &ctx, bool original, const std::vector<uint32_t> &words)
{
    BuildFakes();
    gWords = words;
    gPos = 0;
    Reader *r = BuildReader(gWorld.oldEntries, gWorld.oldGroup);
    gReader = r;
    void *context = gWorld.context ? &F(kContext) : nullptr;
    difftest::AllocationCounter heap;
    if (original)
        ctx.Original<void(__thiscall *)(void *, void *, void *, void *)>(0x9bfd10)(r, &F(kRoot), &F(kStream), context);
    else
        r->ReadRoot(&F(kRoot), &F(kStream), context);
    heap.Stop();
    Blob b = svcfx::gEv;
    b.push_back(0xa110c);
    b.push_back(uint32_t(heap.Blocks()));
    b.push_back(uint32_t(heap.Bytes()));
    b.push_back(uint32_t(gPos));
    CaptureReader(b, r);
    svcfx::CaptureFakes(b);
    std::destroy_at(&r->Ring());
    return b;
}

} // namespace

DIFF_TEST(cGZPersistBinaryReader_ReadRoot, 0x9bfd10)
{
    svcfx::Globals globals;
    globals.Set(kServiceCell, &F(kService));
    svcfx::gFrameworkIndex = kDirector;
    std::mt19937 rng(0x9bfd10);
    std::vector<uint32_t> words;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        RandomWorld(rng, words);
        const World saved = gWorld;
        const Blob a = RunReadRoot(ctx, true, words);
        gWorld = saved;
        const Blob b = RunReadRoot(ctx, false, words);
        if (!svcfx::Same(ctx, a, b, "ReadRoot", seed)) break;
    }
}

// ---- ReadObject ----

static uint32_t CreatedQuery(Obj *self, uint32_t *args)
{
    if ((args[0] & 1) == 0) return 0;
    *reinterpret_cast<void **>(uintptr_t(args[1])) = self;
    ++self->refs;
    return 1;
}

DIFF_TEST(cGZPersistBinaryReader_ReadObject, 0x9c0630)
{
    svcfx::Globals globals;
    svcfx::gFrameworkIndex = kDirector;
    std::mt19937 rng(0x9c0630);
    alignas(16) static uint32_t out[4];
    for (uint32_t seed = 0; seed < 600; ++seed) {
        // Class ids are never multiples of 7 here except 0: the original uses a created object without a null test.
        uint32_t classId = rng() % 4 == 0 ? 0u : uint32_t(rng() % 50);
        if (classId % 7 == 0 && classId != 0) ++classId;
        const uint32_t iid = uint32_t(rng());
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            svcfx::ResetAll();
            F(kStream).Set(9, "p", 0, &StreamWord);
            F(kDirector).Set(3, "--p", 0, &DirectorCreate);
            F(kDirector).Data(3, 3);
            for (unsigned i = 0; i < 4; ++i) {
                F(kCreated + i).Set(3, "p");
                F(kCreated + i).Set(0, "-p", 0, &CreatedQuery);
                F(kCreated + i).Data(0, 1);
            }
            svcfx::AddRange(gReaderStorage, sizeof(gReaderStorage), kReaderTag);
            svcfx::AddRange(out, sizeof(out), kOutTag);
            // Always a full script: on a failed read the class id is the original's uninitialised stack word.
            gWords = std::vector<uint32_t>{classId};
            gPos = 0;
            Reader *r = BuildReader(0, 0);
            r->mStream = &F(kStream);
            out[0] = 0x5eed0000u + seed;
            uint32_t result = side == 0
                ? ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, void **)>(0x9c0630)(r, iid, reinterpret_cast<void **>(out))
                : r->ReadObject(iid, reinterpret_cast<void **>(out));
            Blob &b = sides[side];
            b = svcfx::gEv;
            // The result: the out pointer for class id 0, else the created object's Release count.
            b.push_back(classId == 0 ? Norm(result) : result);
            b.push_back(svcfx::NormOut(out[0], 0x5eed0000u + seed));
            svcfx::CaptureFakes(b);
            std::destroy_at(&r->Ring());
        }
        if (!svcfx::Same(ctx, sides[0], sides[1], "ReadObject", seed)) break;
    }
}

// ---- ReadStringRecord ----

alignas(16) static uint32_t gRecord[4];

static uint32_t StreamText(Obj *, uint32_t *args)
{
    auto *text = reinterpret_cast<cRZString *>(uintptr_t(args[0]));
    // The string is new on the heap: logged as whether it is the one the record now holds.
    svcfx::gEv.back() = args[0] == gRecord[0] ? 0x57570000u : 0xbad0bad0u;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "text-%u", unsigned(gPos * 7 + gWords.size()));
    VirtualCall<void(__thiscall *)(void *, const char *), 4>(text, buf); // FromChar
    return 1;
}

DIFF_TEST(cGZPersistBinaryReader_ReadStringRecord, 0x9c0490)
{
    std::mt19937 rng(0x9c0490);
    uint32_t *record = gRecord;
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::vector<uint32_t> words;
        for (int i = 0, n = int(rng() % 3); i < n; ++i) words.push_back(uint32_t(rng()));
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            svcfx::ResetAll();
            F(kStream).Set(9, "p", 0, &StreamWord);
            F(kStream).Set(15, "p", 1, &StreamText);
            svcfx::AddRange(gRecord, sizeof(gRecord), kOutTag);
            gWords = words;
            gPos = 0;
            Reader *r = BuildReader(0, 0);
            r->mStream = &F(kStream);
            record[0] = svcfx::P(&F(kHolder)); // overwritten without a release
            record[1] = 0x01010101u;
            record[2] = 0x02020202u;
            record[3] = 0x03030303u;
            difftest::AllocationCounter heap;
            void *result = side == 0 ? ctx.Original<void *(__thiscall *)(void *, void *)>(0x9c0490)(r, record)
                                     : r->ReadStringRecord(record);
            heap.Stop();
            Blob &b = sides[side];
            b = svcfx::gEv;
            b.push_back(Norm(svcfx::P(result)));
            b.push_back(uint32_t(heap.Blocks()));
            b.push_back(uint32_t(heap.Bytes()));
            const auto *text = reinterpret_cast<const cRZString *>(uintptr_t(record[0]));
            b.push_back(text->mRefCount);
            b.push_back(svcfx::P(text->mVTable) - svcfx::P(GameAddress<void>(cRZString::kVTable)));
            for (char ch : text->mString) b.push_back(uint8_t(ch));
            for (int i = 1; i < 4; ++i) b.push_back(record[i]);
            svcfx::CaptureFakes(b);
            VirtualCall<uint32_t(__thiscall *)(void *), 2>(text); // the test's release: frees the string
            std::destroy_at(&r->Ring());
        }
        if (!svcfx::Same(ctx, sides[0], sides[1], "ReadStringRecord", seed)) break;
    }
}

// ---- the deleting destructor ----

DIFF_TEST(cGZPersistBinaryReader_DeletingDestructor, 0x9bfc20)
{
    std::mt19937 rng(0x9bfc20);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        const unsigned entries = unsigned(rng() % 5);
        const uint32_t flags = uint32_t(rng() % 4);
        const unsigned pops = entries ? unsigned(rng() % (entries + 1)) : 0;
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            svcfx::ResetAll();
            // A reader on the heap (flags & 1 frees it), with a ring whose offset has moved (pops from the front).
            auto *r = static_cast<Reader *>(::operator new(sizeof(Reader)));
            std::memset(static_cast<void *>(r), 0, sizeof(Reader));
            r->mVTable = GameAddress<void>(Reader::kVTable);
            r->mRefCount = 1;
            r->mGroup = 0x600d + seed;
            new (&r->Ring()) Reader::EntryRing();
            for (unsigned i = 0; i < entries; ++i) {
                r->Ring().emplace_back(Reader::Ref(&F(kOldStream)));
                Reader::Entry &e = r->Ring().back();
                if (i % 2) {
                    *e.mRecord.Out() = &F(kOldRecord);
                    ++F(kOldRecord).refs;
                }
                for (unsigned k = 0; k < i; ++k) e.mResources.push_back(Reader::Ref(&F(kFound + k % 4)));
                for (unsigned k = 0; k <= i % 3; ++k) e.mObjects.push_back(Reader::Ref(&F(kCreated + (k + i) % 4)));
            }
            for (unsigned i = 0; i < pops; ++i) r->Ring().pop_front();
            svcfx::gEv.reserve(1024); // the log's own growth would free its old buffer inside the recording
            difftest::FreeRecorder frees;
            Reader *result = side == 0 ? ctx.Original<Reader *(__thiscall *)(void *, uint32_t)>(0x9bfc20)(r, flags)
                                       : r->DeletingDestructor(flags);
            frees.Stop();
            Blob &b = sides[side];
            b = svcfx::gEv;
            b.push_back(result == r);
            b.push_back(uint32_t(frees.Count()));
            size_t sizes[64];
            const size_t n = frees.SortedSizes(sizes, 64);
            for (size_t i = 0; i < n; ++i) b.push_back(uint32_t(sizes[i]));
            b.push_back(frees.Freed(r));
            if ((flags & 1) == 0) {
                const auto *w = reinterpret_cast<const uint32_t *>(r);
                b.push_back(w[0] - svcfx::P(GameAddress<void>(Reader::kVTable)));
                for (int i = 1; i < 10; ++i) b.push_back(w[i]); // the ring's words are all cleared
                ::operator delete(r);
            }
            svcfx::CaptureFakes(b);
        }
        if (!svcfx::Same(ctx, sides[0], sides[1], "DeletingDestructor", seed)) break;
    }
}
