// Differential tests for cGZPersistBinaryWriter's object-level slots: WriteRoot (slot 3, 0x9c0d50) and the deleting
// destructor (slot 31, 0x9bfc50).
//
// Every collaborator is an svcfx fake (tests/ts_services_fixture.h): the context and its group face (slot 5 the group,
// slot 3 maps a key, logging both), the director framework that creates the write context, the write context (slot 3
// begin, slot 29 the stream, 33/34 objects, 36/37 dependent resource entries), the entries (slot 7 the resource,
// slot 10 after the write), the resources (slot 3 fills a key), the objects (slot 4 hands out a counted info and a
// block whose +4 is the class id; slot 5 writes, logging the writer's ring at that moment) and the stream (slot 9 logs
// each word as data, slot 3 flushes). The writer's ring is a real std::deque (the game's layout) on both sides,
// optionally holding older elements; after the call the ring's words, the group field, the log, every fake's reference
// count and the call's heap balance are compared.
#include "ts_services_fixture.h"
#include "free_recorder.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZPersistBinaryWriter.h"

#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <vector>

namespace {

using Writer = cGZPersistBinaryWriter;
using Ref = cGZPersistBinaryReader::Ref;
using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Norm;
using svcfx::Obj;

enum : unsigned {
    kRoot = 0,
    kContext = 1,
    kGroupFace = 2,
    kStream = 3,
    kDirector = 4,
    kCreated = 5,   // the write context the director creates
    kEntry = 6,     // 6..8: dependent resource entries
    kResource = 9,  // 9..11: their resources
    kObject = 12,   // 12..15: objects to write
    kInfo = 16,     // 16..17: counted infos the objects hand out
    kOld = 18,      // an older element already on the ring
    kOldStream = 19,
};

constexpr uint32_t kWriterTag = 0x7e000000;
constexpr uint32_t kBlockTag = 0x7b000000;
constexpr uint32_t kGroupContextIid = 0xad095a01;
constexpr uint32_t evKey = 0x71;
constexpr uint32_t evState = 0x70;

alignas(16) unsigned char gWriterStorage[sizeof(Writer)];

struct World {
    bool context = true;
    bool contextAnswers = true;
    uint32_t faceGroup = 0;
    uint32_t remap = 0; // 0 none, 1 flip the group's low bit, 2 clear the type, 3 swap instance words
    uint32_t resources = 0;
    uint32_t objects = 0;
    uint32_t keys[3][4] = {};
    uint32_t classIds[4] = {};
    uint32_t infoMask = 0; // bit i: object i hands out a null info
    unsigned oldElements = 0;
    uint32_t oldGroup = 0;
};
World gWorld;
alignas(16) uint32_t gBlocks[4][2];

uint32_t ContextQuery(Obj *, uint32_t *args)
{
    if (args[0] != kGroupContextIid || !gWorld.contextAnswers) return 0;
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
    case 2: out[0] = 0; break;
    case 3: std::swap(out[2], out[3]); break;
    default: break;
    }
    return 0;
}

uint32_t DirectorCreate(Obj *, uint32_t *args)
{
    *reinterpret_cast<void **>(uintptr_t(args[2])) = &F(kCreated);
    ++F(kCreated).refs;
    return 1;
}

uint32_t ContextEntry(Obj *, uint32_t *args) { return svcfx::P(&F(kEntry + args[0] % 3)); }
uint32_t ContextObject(Obj *, uint32_t *args) { return svcfx::P(&F(kObject + args[0] % 4)); }

uint32_t ResourceKey(Obj *self, uint32_t *args)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(args[0])), gWorld.keys[(self->id - 200 - kResource) % 3], 16);
    return 0;
}

uint32_t ObjectInfo(Obj *self, uint32_t *args)
{
    const unsigned k = (self->id - 200 - kObject) % 4;
    auto *out = reinterpret_cast<void **>(uintptr_t(args[0]));
    if (gWorld.infoMask >> k & 1) {
        *out = nullptr;
    } else {
        *out = &F(kInfo + k % 2);
        ++F(kInfo + k % 2).refs;
    }
    return svcfx::P(gBlocks[k]);
}

// An object's Write: logs the writer's ring while WriteRoot runs.
uint32_t ObjectWrite(Obj *, uint32_t *args)
{
    auto *w = reinterpret_cast<Writer *>(uintptr_t(args[0]));
    Ev(evState);
    Ev(uint32_t(w->Ring().size()));
    if (!w->Ring().empty()) Ev(Norm(svcfx::P(w->Ring().back().Get())));
    Ev(w->mGroup);
    return 1;
}

void BuildFakes()
{
    svcfx::ResetAll();
    F(kContext).Set(0, "dp", 0, &ContextQuery);
    F(kGroupFace).Set(5, 0, gWorld.faceGroup);
    F(kGroupFace).Set(3, "pp", 0, &GroupRemap);
    F(kDirector).Set(3, "--p", 0, &DirectorCreate);
    F(kDirector).Data(3, 3);
    F(kCreated).Set(3, "ppp");
    F(kCreated).Set(29, 0, svcfx::P(&F(kStream)));
    F(kCreated).Set(33, 0, gWorld.objects);
    F(kCreated).Set(34, 1, 0, &ContextObject);
    F(kCreated).Data(34, 1);
    F(kCreated).Set(36, 0, gWorld.resources);
    F(kCreated).Set(37, 1, 0, &ContextEntry);
    F(kCreated).Data(37, 1);
    for (unsigned i = 0; i < 3; ++i) {
        F(kEntry + i).Set(7, 0, svcfx::P(&F(kResource + i)));
        F(kEntry + i).Set(10, 0);
        F(kResource + i).Set(3, "p", 0, &ResourceKey);
    }
    for (unsigned i = 0; i < 4; ++i) {
        F(kObject + i).Set(4, "p", 0, &ObjectInfo);
        F(kObject + i).Set(5, "p", 0, &ObjectWrite);
    }
    F(kStream).Set(9, 1);
    F(kStream).Data(9, 1);
    F(kStream).Set(3, 0);
    F(kOld).Set(29, 0, svcfx::P(&F(kOldStream)));
    svcfx::AddRange(gWriterStorage, sizeof(gWriterStorage), kWriterTag);
    svcfx::AddRange(gBlocks, sizeof(gBlocks), kBlockTag);
}

Writer *BuildWriter(unsigned old, uint32_t group)
{
    std::memset(gWriterStorage, 0, sizeof(gWriterStorage));
    auto *w = reinterpret_cast<Writer *>(gWriterStorage);
    w->mVTable = GameAddress<void>(Writer::kVTable);
    *reinterpret_cast<uint32_t *>(w->mUnknown4) = 1; // reference count
    new (&w->Ring()) Writer::ElementRing();
    for (unsigned i = 0; i < old; ++i) w->Ring().emplace_back(&F(kOld));
    w->mGroup = group;
    return w;
}

void RandomWorld(std::mt19937 &rng)
{
    World w;
    w.context = rng() % 4 != 0;
    w.contextAnswers = rng() % 4 != 0;
    w.faceGroup = rng() % 3 == 0 ? 0xffffffffu : uint32_t(rng());
    w.remap = uint32_t(rng() % 4);
    w.resources = uint32_t(rng() % 5);
    w.objects = uint32_t(rng() % 6);
    for (auto &k : w.keys)
        for (uint32_t &v : k) v = uint32_t(rng());
    for (uint32_t &c : w.classIds) c = uint32_t(rng());
    w.infoMask = uint32_t(rng() % 16);
    w.oldElements = unsigned(rng() % 3);
    w.oldGroup = uint32_t(rng());
    gWorld = w;
    for (unsigned k = 0; k < 4; ++k) {
        gBlocks[k][0] = 0xb10c0000u + k;
        gBlocks[k][1] = w.classIds[k];
    }
}

Blob RunWriteRoot(difftest::Context &ctx, bool original)
{
    BuildFakes();
    Writer *w = BuildWriter(gWorld.oldElements, gWorld.oldGroup);
    void *context = gWorld.context ? &F(kContext) : nullptr;
    difftest::AllocationCounter heap;
    if (original)
        ctx.Original<void(__thiscall *)(void *, void *, void *, void *)>(0x9c0d50)(w, &F(kRoot), &F(kStream), context);
    else
        w->WriteRoot(&F(kRoot), &F(kStream), context);
    heap.Stop();
    Blob b = svcfx::gEv;
    b.push_back(0xa110c);
    b.push_back(uint32_t(heap.Blocks()));
    b.push_back(uint32_t(heap.Bytes()));
    const auto *words = reinterpret_cast<const uint32_t *>(w);
    b.push_back(words[1]);                         // reference count
    for (int i = 4; i < 8; ++i) b.push_back(words[i]); // map size, offset, count, group
    for (const Ref &r : w->Ring()) b.push_back(Norm(svcfx::P(r.Get())));
    svcfx::CaptureFakes(b);
    std::destroy_at(&w->Ring());
    return b;
}

} // namespace

DIFF_TEST(cGZPersistBinaryWriter_WriteRoot, 0x9c0d50)
{
    svcfx::Globals globals;
    svcfx::gFrameworkIndex = kDirector;
    std::mt19937 rng(0x9c0d50);
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        RandomWorld(rng);
        const World saved = gWorld;
        const Blob a = RunWriteRoot(ctx, true);
        gWorld = saved;
        const Blob b = RunWriteRoot(ctx, false);
        if (!svcfx::Same(ctx, a, b, "WriteRoot", seed)) break;
    }
}

DIFF_TEST(cGZPersistBinaryWriter_DeletingDestructor, 0x9bfc50)
{
    std::mt19937 rng(0x9bfc50);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        const unsigned elements = unsigned(rng() % 11);
        const uint32_t flags = uint32_t(rng() % 4);
        const unsigned pops = elements ? unsigned(rng() % (elements + 1)) : 0;
        const uint32_t nullMask = uint32_t(rng());
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            svcfx::ResetAll();
            // A writer on the heap (flags & 1 frees it), with a ring whose offset has moved (pops from the front).
            auto *w = static_cast<Writer *>(::operator new(sizeof(Writer)));
            std::memset(static_cast<void *>(w), 0, sizeof(Writer));
            w->mVTable = GameAddress<void>(Writer::kVTable);
            *reinterpret_cast<uint32_t *>(w->mUnknown4) = 1;
            w->mGroup = 0x600d + seed;
            new (&w->Ring()) Writer::ElementRing();
            for (unsigned i = 0; i < elements; ++i)
                w->Ring().emplace_back(nullMask >> i & 1 ? nullptr : static_cast<void *>(&F(kObject + i % 4)));
            for (unsigned i = 0; i < pops; ++i) w->Ring().pop_front();
            svcfx::gEv.reserve(1024); // the log's own growth would free its old buffer inside the recording
            difftest::FreeRecorder frees;
            Writer *result = side == 0 ? ctx.Original<Writer *(__thiscall *)(void *, uint32_t)>(0x9bfc50)(w, flags)
                                       : w->DeletingDestructor(flags);
            frees.Stop();
            Blob &b = sides[side];
            b = svcfx::gEv;
            b.push_back(result == w);
            b.push_back(uint32_t(frees.Count()));
            size_t sizes[64];
            const size_t n = frees.SortedSizes(sizes, 64);
            for (size_t i = 0; i < n; ++i) b.push_back(uint32_t(sizes[i]));
            b.push_back(frees.Freed(w));
            if ((flags & 1) == 0) {
                const auto *words = reinterpret_cast<const uint32_t *>(w);
                b.push_back(words[0] - svcfx::P(GameAddress<void>(Writer::kVTable)));
                for (int i = 1; i < 8; ++i) b.push_back(words[i]); // the ring's words are all cleared
                ::operator delete(w);
            }
            svcfx::CaptureFakes(b);
        }
        if (!svcfx::Same(ctx, sides[0], sides[1], "DeletingDestructor", seed)) break;
    }
}
