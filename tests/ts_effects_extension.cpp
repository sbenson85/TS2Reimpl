// cTSEffectsExtension's constructor/destructor and object shape, compared to the image.
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "shape.h"

#include "game/game.h"
#include "hook/hooks.h"
#include "ts/cTSEffectsExtension.h"

#include "rz/cRZSerializableStream.h"

#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cstring>
#include <new>
#include <array>
#include <string>

using nGZSceneGraph::cTSEffectsExtension;
using nGZSceneGraph::TSEffectRecord;

namespace {

using CtorFn = void(__thiscall *)(void *, uint32_t);
using DtorFn = void(__thiscall *)(void *);
using DeletingDtorFn = void *(__thiscall *)(void *, uint32_t);

struct NodeLookupCall {
    const char *name = nullptr;
    void *node = nullptr;
    int limit = 0;
    unsigned calls = 0;
} gNodeLookup;
struct DispatchLog {
    char order[8]{};
    unsigned orderSize = 0, findCalls = 0, createCalls = 0, removeCalls = 0;
    const char *first = nullptr, *second = nullptr;
    uint32_t keyId = 0, keyB = 0, flags = 0;
    void *scope = nullptr, *keyA = nullptr;
} *gDispatchLog = nullptr;
void DispatchStep(char step)
{
    if (gDispatchLog && gDispatchLog->orderSize < sizeof(gDispatchLog->order))
        gDispatchLog->order[gDispatchLog->orderSize++] = step;
}
void *gDispatchFindResult = nullptr, *gDispatchCreateResult = nullptr;
uint32_t gDispatchRemoveResult = 0;
struct WriterLog {
    char op[32]{};
    uint32_t value[32]{};
    const void *pointer[32]{};
    const char *text[32]{};
    unsigned size = 0;
} *gWriterLog = nullptr;
void WriterAdd(char op, uint32_t value = 0, const void *pointer = nullptr, const char *text = nullptr)
{
    if (!gWriterLog || gWriterLog->size >= 32) return;
    const unsigned i = gWriterLog->size++;
    gWriterLog->op[i] = op; gWriterLog->value[i] = value;
    gWriterLog->pointer[i] = pointer; gWriterLog->text[i] = text;
}
uint32_t __fastcall WriterVersion(void *, void *, uint32_t value) { WriterAdd('V', value); return 1; }
uint32_t __fastcall WriterUint(void *, void *, uint32_t value) { WriterAdd('U', value); return 1; }
uint32_t __fastcall WriterString(void *, void *, const void *object)
{
    const auto *text = reinterpret_cast<const std::string *>(static_cast<const uint8_t *>(object) + 4);
    WriterAdd('S', 0, object, text->c_str());
    return 1;
}
uint32_t __fastcall WriterByte(void *, void *, int8_t value) { WriterAdd('B', static_cast<uint8_t>(value)); return 1; }
void *gNodeLookupResult = nullptr;
struct DisplayNameCall { void *owner = nullptr; unsigned calls = 0; } gDisplayNameCall;
const char *gDisplayNameResult = nullptr;

struct FakeRef {
    void **vtable;
    uint32_t refs, releases, setCalls, setValue;
    uint8_t active;
    uint32_t calls7, value7, calls8, value8, calls13, args13[3], calls14, args14[2];
    uint32_t calls3, value3;
    uint32_t calls1, calls6;
    uint8_t ready;
};
uint32_t __fastcall FakeRelease(void *object, void *)
{
    auto *fake = static_cast<FakeRef *>(object);
    ++fake->releases;
    return --fake->refs;
}
uint32_t __fastcall FakeAddRef(void *object, void *)
{
    auto *fake = static_cast<FakeRef *>(object);
    ++fake->calls1;
    return ++fake->refs;
}
void __fastcall FakeSetActive(void *object, void *, uint32_t active)
{
    DispatchStep('4');
    auto *fake = static_cast<FakeRef *>(object);
    ++fake->setCalls;
    fake->setValue = active;
}
uint8_t __fastcall FakeIsActive(void *object, void *) { return static_cast<FakeRef *>(object)->active; }
uint8_t __fastcall FakeIsReady(void *object, void *)
{ auto *fake = static_cast<FakeRef *>(object); ++fake->calls6; return fake->ready; }
void __fastcall FakeSlot7(void *object, void *, uint32_t value)
{ auto *f = static_cast<FakeRef *>(object); ++f->calls7; f->value7 = value; }
void __fastcall FakeSlot8(void *object, void *, uint32_t value)
{ auto *f = static_cast<FakeRef *>(object); ++f->calls8; f->value8 = value; }
void __fastcall FakeSlot3(void *object, void *, uint32_t value)
{ DispatchStep('3'); auto *f = static_cast<FakeRef *>(object); ++f->calls3; f->value3 = value; }
void __fastcall FakeSlot13(void *object, void *, uint32_t a, uint32_t b, uint32_t c)
{ auto *f = static_cast<FakeRef *>(object); ++f->calls13; f->args13[0] = a; f->args13[1] = b; f->args13[2] = c; }
void __fastcall FakeSlot14(void *object, void *, uint32_t a, uint32_t b)
{ auto *f = static_cast<FakeRef *>(object); ++f->calls14; f->args14[0] = a; f->args14[1] = b; }
const uint16_t *gImageNameA = nullptr, *gImageNameB = nullptr, *gOursNameA = nullptr, *gOursNameB = nullptr;
void *gImageSourceA = nullptr, *gImageSourceB = nullptr, *gOursSourceA = nullptr, *gOursSourceB = nullptr;
const uint16_t *__fastcall FakeName(void *object, void *)
{
    if (object == gImageSourceA) return gImageNameA;
    if (object == gImageSourceB) return gImageNameB;
    if (object == gOursSourceA) return gOursNameA;
    if (object == gOursSourceB) return gOursNameB;
    return nullptr;
}
void *__fastcall AllocateVectorBlock(void *, void *, uint32_t size) { return ::operator new(size); }
void __fastcall FreeVectorBlock(void *, void *, void *p, uint32_t) { ::operator delete(p); }
struct VectorAllocatorScope {
    void **cache;
    int *guard;
    void *savedCache;
    int savedGuard;
    alignas(4) uint8_t object[16]{};
    void *vtable[8]{};
    explicit VectorAllocatorScope(difftest::Context &ctx)
        : cache(ctx.image.At<void *>(0x10c95d4)), guard(ctx.image.At<int>(0x10c95d8)),
          savedCache(*cache), savedGuard(*guard)
    {
        vtable[3] = shape::CodeOf(&AllocateVectorBlock);
        vtable[4] = shape::CodeOf(&FreeVectorBlock);
        *reinterpret_cast<void ***>(object) = vtable;
        *cache = object;
        *guard = INT32_MIN;
    }
    ~VectorAllocatorScope() { *cache = savedCache; *guard = savedGuard; }
};

void *__cdecl StandFindNodeByName(const char *name, void *node, int limit)
{
    gNodeLookup.name = name;
    gNodeLookup.node = node;
    gNodeLookup.limit = limit;
    ++gNodeLookup.calls;
    return gNodeLookupResult;
}
void *__fastcall ExtensionFind(void *, void *, const char *first, const char *second, uint32_t id)
{
    DispatchStep('F');
    ++gDispatchLog->findCalls;
    gDispatchLog->first = first; gDispatchLog->second = second; gDispatchLog->keyId = id;
    return gDispatchFindResult;
}
// Slot 2: (kind, name, nodeName, discriminator, flag). Logged as keyB = kind, keyA = name, scope = nodeName.
void *__fastcall ExtensionCreate(void *, void *, uint32_t kind, const char *name, const char *nodeName,
                                 uint32_t discriminator, uint32_t flag)
{
    DispatchStep('C');
    ++gDispatchLog->createCalls;
    gDispatchLog->keyB = kind; gDispatchLog->keyA = const_cast<char *>(name);
    gDispatchLog->scope = const_cast<char *>(nodeName);
    gDispatchLog->keyId = discriminator;
    gDispatchLog->flags = flag & 0xff;
    return gDispatchCreateResult;
}
uint32_t __fastcall ExtensionRemoveByKey(void *, void *, const char *first, const char *second, uint32_t id)
{
    DispatchStep('R');
    ++gDispatchLog->removeCalls;
    gDispatchLog->first = first; gDispatchLog->second = second; gDispatchLog->keyId = id;
    return gDispatchRemoveResult;
}
const char *__cdecl StandObjectDisplayName(void *owner)
{
    gDisplayNameCall.owner = owner;
    ++gDisplayNameCall.calls;
    return gDisplayNameResult;
}

struct ShapeResult {
    uint32_t unknown0, count, mask, buckets, bucketEnd, bucketCapacityEnd, bucketCount;
    uint32_t state40, state44, textLength;
    bool firstBucketHeadIsSentinel, firstBucketTailIsSentinel;
    bool sentinelSelfLinked;
    bool allBucketsEmpty;
};

ShapeResult Inspect(void *object)
{
    auto *const registry = reinterpret_cast<nGZSceneGraph::TSEffectsRegistry *>(static_cast<uint8_t *>(object) + 0x20);
    auto *const sentinel = static_cast<TSEffectRecord *>(registry->sentinel);
    auto *const buckets = static_cast<uint32_t *>(registry->buckets);
    bool empty = true;
    for (uint32_t i = 0; i < 8; ++i)
        empty = empty && buckets[2 * i] == reinterpret_cast<uint32_t>(sentinel) &&
                buckets[2 * i + 1] == reinterpret_cast<uint32_t>(sentinel);
    return {registry->unknown0, registry->count, registry->bucketMask,
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(registry->buckets)),
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(registry->bucketEnd)),
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(registry->bucketCapacityEnd)), registry->bucketCount,
            *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(object) + 0x40),
            *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(object) + 0x44),
            static_cast<uint32_t>(reinterpret_cast<cTSEffectsExtension *>(object)->mText.mString.size()),
            buckets[0] == reinterpret_cast<uint32_t>(sentinel), buckets[1] == reinterpret_cast<uint32_t>(sentinel),
            sentinel->next == sentinel && sentinel->previous == sentinel,
            empty};
}

void UseOriginalEffectsVTable(difftest::Context &ctx, void *face)
{
    *reinterpret_cast<void ***>(face) = ctx.image.At<void *>(cTSEffectsExtension::kEffectsVTable);
}

bool SameShape(const ShapeResult &a, const ShapeResult &b)
{
    return a.unknown0 == b.unknown0 && a.count == b.count && a.mask == b.mask &&
           a.bucketCount == b.bucketCount && a.state40 == b.state40 && a.state44 == b.state44 &&
           a.textLength == b.textLength && a.firstBucketHeadIsSentinel == b.firstBucketHeadIsSentinel &&
           a.firstBucketTailIsSentinel == b.firstBucketTailIsSentinel && a.sentinelSelfLinked == b.sentinelSelfLinked && a.allBucketsEmpty == b.allBucketsEmpty &&
           a.bucketEnd - a.buckets == b.bucketEnd - b.buckets &&
           a.bucketCapacityEnd - a.buckets == b.bucketCapacityEnd - b.buckets;
}

} // namespace

DIFF_TEST(EffectsExtension_ConstructDestruct, 0xcc50c0)
{
    static_assert(sizeof(cTSEffectsExtension) == 0x70);
    static_assert(offsetof(cTSEffectsExtension, mRegistry) == 0x20);
    static_assert(offsetof(cTSEffectsExtension, mText) == 0x48);
    for (unsigned round = 0; round < 24; ++round) {
        auto *image = static_cast<uint8_t *>(::operator new(cTSEffectsExtension::kSize));
        auto *ours = static_cast<uint8_t *>(::operator new(cTSEffectsExtension::kSize));
        std::memset(image, 0xa5, cTSEffectsExtension::kSize);
        std::memset(ours, 0xa5, cTSEffectsExtension::kSize);
        ctx.Original<CtorFn>(0xcc50c0)(image, 1);
        auto *const object = ::new (ours) cTSEffectsExtension();

        const ShapeResult a = Inspect(image), b = Inspect(ours);
        ++ctx.cases;
        if (!SameShape(a, b))
            ctx.Fail("round %u: ctor shape differs: image %08x/%u/%u/%u/%u/%u/%u/%u/%u/%u self=%d empty=%d ours %08x/%u/%u/%u/%u/%u/%u/%u/%u/%u self=%d empty=%d", round,
                     a.unknown0, a.count, a.mask, a.bucketEnd-a.buckets, a.bucketCapacityEnd-a.buckets, a.bucketCount, a.state40, a.state44, a.textLength, a.sentinelSelfLinked, a.allBucketsEmpty,
                     b.unknown0, b.count, b.mask, b.bucketEnd-b.buckets, b.bucketCapacityEnd-b.buckets, b.bucketCount, b.state40, b.state44, b.textLength, b.sentinelSelfLinked, b.allBucketsEmpty);
        ++ctx.cases;
        if (*reinterpret_cast<void **>(image + 0x18) != GameAddress<void>(cTSEffectsExtension::kEffectsVTable))
            ctx.Fail("round %u: image effects face has unexpected vtable", round);
        ++ctx.cases;
        if (*reinterpret_cast<void **>(ours + 0x18) == GameAddress<void>(cTSEffectsExtension::kEffectsVTable))
            ctx.Fail("round %u: our effects face kept the game's vtable", round);
        // Every vtable word of the object (cExtension, referent, effects face, virtual base) is set on both
        // sides, and ours are our own tables, not the image's.
        for (const size_t at : {size_t{0}, size_t{0xc}, size_t{0x18}, size_t{0x6c}}) {
            const void *const imageWord = *reinterpret_cast<void **>(image + at);
            const void *const ourWord = *reinterpret_cast<void **>(ours + at);
            ++ctx.cases;
            if (!imageWord || !ourWord || imageWord == ourWord)
                ctx.Fail("round %u: vtable word at +0x%x: image %p, ours %p", round,
                         static_cast<unsigned>(at), imageWord, ourWord);
        }

        struct DestroyResult { bool sentinelFreed, bucketsFreed; size_t count; size_t sizes[4]; } destroyed[2]{};
        void *imageSentinel = reinterpret_cast<void *>(a.buckets ?
            *reinterpret_cast<uint32_t **>(image + 0x24) : nullptr);
        void *imageBuckets = *reinterpret_cast<void **>(image + 0x2c);
        void *ourSentinel = reinterpret_cast<void *>(*reinterpret_cast<uint32_t **>(ours + 0x24));
        void *ourBuckets = *reinterpret_cast<void **>(ours + 0x2c);
        {
            difftest::FreeRecorder frees;
            ctx.Original<DtorFn>(0xcc53d0)(image);
            frees.Stop();
            destroyed[0].sentinelFreed = frees.Freed(imageSentinel);
            destroyed[0].bucketsFreed = frees.Freed(imageBuckets);
            destroyed[0].count = frees.SortedSizes(destroyed[0].sizes, 4);
        }
        {
            difftest::FreeRecorder frees;
            object->~cTSEffectsExtension();
            frees.Stop();
            destroyed[1].sentinelFreed = frees.Freed(ourSentinel);
            destroyed[1].bucketsFreed = frees.Freed(ourBuckets);
            destroyed[1].count = frees.SortedSizes(destroyed[1].sizes, 4);
        }
        ++ctx.cases;
        if (std::memcmp(&destroyed[0], &destroyed[1], sizeof(destroyed[0])) != 0 ||
            !destroyed[0].sentinelFreed || !destroyed[0].bucketsFreed || destroyed[0].count != 2 ||
            destroyed[0].sizes[0] != 0x40 || destroyed[0].sizes[1] != 0x58)
            ctx.Fail("round %u: destructor free sets differ (sentinel %d/%d, buckets %d/%d, sizes %u)", round,
                     destroyed[0].sentinelFreed, destroyed[1].sentinelFreed,
                     destroyed[0].bucketsFreed, destroyed[1].bucketsFreed, static_cast<unsigned>(destroyed[0].count));
        ::operator delete(image, cTSEffectsExtension::kSize);
        ::operator delete(ours, cTSEffectsExtension::kSize);
    }
}

// 0xcc5460 checks the low byte's bit zero and returns the original object address.
DIFF_TEST(EffectsExtension_DeletingDestructorFlags, 0xcc5460)
{
    for (uint32_t flags : {0u, 0x100u, 1u}) {
        auto *image = static_cast<uint8_t *>(::operator new(cTSEffectsExtension::kSize));
        auto *ours = static_cast<uint8_t *>(::operator new(cTSEffectsExtension::kSize));
        std::memset(image, 0xa5, cTSEffectsExtension::kSize);
        std::memset(ours, 0xa5, cTSEffectsExtension::kSize);
        ctx.Original<CtorFn>(0xcc50c0)(image, 1);
        auto *object = ::new (ours) cTSEffectsExtension();
        bool freed[2]{};
        size_t sizes[2][8]{};
        size_t counts[2]{};
        void *results[2]{};
        for (int side = 0; side != 2; ++side) {
            difftest::FreeRecorder recorder;
            results[side] = side == 0 ? ctx.Original<DeletingDtorFn>(0xcc5460)(image, flags)
                                      : object->DeletingDestructor(flags);
            recorder.Stop();
            freed[side] = recorder.Freed(side == 0 ? image : ours);
            counts[side] = recorder.SortedSizes(sizes[side], 8);
        }
        ++ctx.cases;
        if (results[0] != image || results[1] != ours || freed[0] != freed[1] || counts[0] != counts[1] ||
            std::memcmp(sizes[0], sizes[1], counts[0] * sizeof(size_t)) != 0)
            ctx.Fail("flags %08x: returned %p / %p, object freed %d / %d, free-size count %u / %u", flags,
                     results[0], results[1], freed[0], freed[1], static_cast<unsigned>(counts[0]), static_cast<unsigned>(counts[1]));
        if (!freed[0]) {
            ::operator delete(image, cTSEffectsExtension::kSize);
        }
        if (!freed[1]) {
            ::operator delete(ours, cTSEffectsExtension::kSize);
        }
    }
}

DIFF_TEST(EffectsExtension_GetDisplayName, 0xcc5e80)
{
    using Fn = uint8_t(__thiscall *)(void *);
    shape::CodePatch displayName(0xae7900u, shape::CodeOf(&StandObjectDisplayName));
    cTSEffectsExtension image, ours;
    auto *imageFace = reinterpret_cast<uint8_t *>(&image) + 0x18;
    auto *oursFace = reinterpret_cast<uint8_t *>(&ours) + 0x18;
    auto **oursVtable = *reinterpret_cast<void ***>(oursFace);
    const char *const named = "Lot 42";
    struct Case { void *owner; const char *display; const char *expected; } cases[] = {
        {reinterpret_cast<void *>(0x12345678u), named, named},
        {nullptr, nullptr, "node 0x00000000"},
        {reinterpret_cast<void *>(0x87654321u), nullptr, "node 0x87654321"},
    };
    for (const Case &c : cases) {
        image.mOwner = ours.mOwner = static_cast<nGZSceneGraph::cIObjectGraphNode *>(c.owner);
        gDisplayNameResult = c.display;
        gDisplayNameCall = {};
        const uint8_t imageResult = ctx.Original<Fn>(0xcc5e80)(imageFace);
        const DisplayNameCall imageCall = gDisplayNameCall;
        gDisplayNameCall = {};
        const uint8_t oursResult = reinterpret_cast<Fn>(oursVtable[0])(oursFace);
        const DisplayNameCall oursCall = gDisplayNameCall;
        ++ctx.cases;
        if (imageResult != oursResult || imageResult != 1 || image.mText.mString != ours.mText.mString ||
            image.mText.mString != c.expected || imageCall.calls != 1 || oursCall.calls != 1 ||
            imageCall.owner != c.owner || oursCall.owner != c.owner)
            ctx.Fail("owner %p: result/text/helper differs (%u/%u, '%s'/'%s')", c.owner,
                     imageResult, oursResult, image.mText.mString.c_str(), ours.mText.mString.c_str());
    }
}

DIFF_TEST(EffectsExtension_DispatchBasic, 0xcc5ef0)
{
    using Fn = uint8_t(__thiscall *)(void *, uint32_t, const char *, const char *, uint32_t, uint32_t, uint32_t);
    cTSEffectsExtension extension;
    void *const face = reinterpret_cast<uint8_t *>(&extension) + 0x18;
    auto **const ownVtable = *reinterpret_cast<void ***>(face);
    void *const ownSlot1 = ownVtable[1];
    void *classVtable[23]{};
    classVtable[1] = ownSlot1;
    classVtable[2] = shape::CodeOf(&ExtensionCreate);
    classVtable[5] = shape::CodeOf(&ExtensionRemoveByKey);
    classVtable[6] = shape::CodeOf(&ExtensionFind);
    void *instanceVtable[5]{};
    instanceVtable[3] = shape::CodeOf(&FakeSlot3);
    instanceVtable[4] = shape::CodeOf(&FakeSetActive);
    FakeRef fake{}; fake.vtable = instanceVtable;
    const char first[] = "effect-key";
    const char second[] = "texture-key";
    void *const keyA = const_cast<char *>(first);
    const uint32_t keyB = 0x5a;
    void *const scope = const_cast<char *>(second);
    const uint32_t keyId = 0xa1b2c3d4, flags = 0x40;
    struct Case { uint32_t operation; void *findResult; void *createResult; uint32_t removeResult; const char *order; uint32_t answer; } cases[] = {
        {0, &fake, nullptr, 0, "F3", 1}, {0, nullptr, nullptr, 0, "FC", 0},
        {0, nullptr, &fake, 0, "FC3", 1}, {1, nullptr, nullptr, 0, "F", 0},
        {1, &fake, nullptr, 0, "F4", 1}, {2, nullptr, &fake, 0, "FC3", 1},
        {3, &fake, nullptr, 0x89abcdef, "F4R", 0xef}, {3, nullptr, nullptr, 0, "F", 0},
        {7, &fake, &fake, 0, "", 0}, {5, &fake, &fake, 0, "", 0},
    };
    *reinterpret_cast<void ***>(face) = classVtable;
    for (const Case &c : cases) {
        gDispatchFindResult = c.findResult;
        gDispatchCreateResult = c.createResult;
        gDispatchRemoveResult = c.removeResult;
        DispatchLog imageLog{}, oursLog{};
        fake.calls3 = fake.value3 = fake.setCalls = fake.setValue = 0;
        gDispatchLog = &imageLog;
        const uint32_t imageResult = ctx.Original<Fn>(0xcc5ef0)(face, c.operation, first, second, keyB, keyId, flags);
        const uint32_t image3 = fake.value3, image4 = fake.setValue;
        const uint32_t image3Calls = fake.calls3, image4Calls = fake.setCalls;
        fake.calls3 = fake.value3 = fake.setCalls = fake.setValue = 0;
        gDispatchLog = &oursLog;
        const uint32_t oursResult = reinterpret_cast<Fn>(classVtable[1])(face, c.operation, first, second,
                                                                          keyB, keyId, flags);
        const uint32_t ours3 = fake.value3, ours4 = fake.setValue;
        const uint32_t ours3Calls = fake.calls3, ours4Calls = fake.setCalls;
        ++ctx.cases;
        const bool commonCallsMatch = imageLog.orderSize == oursLog.orderSize &&
            std::memcmp(imageLog.order, oursLog.order, imageLog.orderSize) == 0 &&
            imageLog.findCalls == oursLog.findCalls && imageLog.createCalls == oursLog.createCalls &&
            imageLog.removeCalls == oursLog.removeCalls && imageLog.first == oursLog.first &&
            imageLog.second == oursLog.second && imageLog.keyId == oursLog.keyId &&
            imageLog.scope == oursLog.scope && imageLog.keyA == oursLog.keyA &&
            imageLog.keyB == oursLog.keyB && imageLog.flags == oursLog.flags;
        if (imageResult != oursResult || imageResult != c.answer || !commonCallsMatch ||
            std::string(imageLog.order, imageLog.orderSize) != c.order ||
            imageLog.findCalls != (c.operation <= 3 ? 1u : 0u) ||
            imageLog.createCalls != ((c.operation == 0 && !c.findResult) || (c.operation == 2 && !c.findResult) ? 1u : 0u) ||
            imageLog.removeCalls != (c.operation == 3 && c.findResult ? 1u : 0u) ||
            (imageLog.findCalls && (imageLog.first != first || imageLog.second != second || imageLog.keyId != keyId)) ||
            (imageLog.createCalls && (imageLog.scope != scope || imageLog.keyA != keyA || imageLog.keyB != keyB ||
                                      imageLog.flags != (c.operation == 2 ? 0u : flags))) ||
            image3Calls != ours3Calls || image3 != ours3 || image4Calls != ours4Calls || image4 != ours4 ||
            image3Calls != (c.operation == 0 && (c.findResult || c.createResult) || c.operation == 2 && c.createResult ? 1u : 0u) ||
            image4Calls != ((c.operation == 1 || c.operation == 3) && c.findResult ? 1u : 0u) ||
            (image3Calls && image3 != (c.operation == 2 ? 1u : 0u)) ||
            (image4Calls && image4 != (c.operation == 3 ? 1u : 0u)))
            ctx.Fail("operation %u: order %.*s/%.*s return %08x/%08x, find/create/remove %u/%u/%u",
                     c.operation, imageLog.orderSize, imageLog.order, oursLog.orderSize, oursLog.order,
                     imageResult, oursResult, imageLog.findCalls, imageLog.createCalls, imageLog.removeCalls);
    }
    gDispatchLog = nullptr;
    *reinterpret_cast<void ***>(face) = ownVtable;
}

DIFF_TEST(EffectsExtension_WriteRecords, 0xcc69e0)
{
    using Fn = void(__thiscall *)(void *, void *);
    cTSEffectsExtension image, ours;
    auto *imageFace = reinterpret_cast<uint8_t *>(&image) + 0x18;
    auto *oursFace = reinterpret_cast<uint8_t *>(&ours) + 0x18;
    auto **imageOwnVtable = *reinterpret_cast<void ***>(imageFace);
    auto **oursVtable = *reinterpret_cast<void ***>(oursFace);
    void **const originalVtable = ctx.image.At<void *>(cTSEffectsExtension::kEffectsVTable);
    void *writerVtable[16]{};
    writerVtable[5] = shape::CodeOf(&WriterByte);
    writerVtable[7] = shape::CodeOf(&WriterVersion);
    writerVtable[9] = shape::CodeOf(&WriterUint);
    writerVtable[15] = shape::CodeOf(&WriterString);
    void *writer[2] = {writerVtable, nullptr};
    void *instanceVtable[7]{};
    instanceVtable[5] = shape::CodeOf(&FakeIsActive);
    instanceVtable[6] = shape::CodeOf(&FakeIsReady);
    FakeRef imageInstance{}; imageInstance.vtable = instanceVtable; imageInstance.active = 1;
    FakeRef oursInstance{}; oursInstance.vtable = instanceVtable; oursInstance.active = 1;
    auto run = [&](unsigned records) {
        nGZSceneGraph::TSEffectRecord imageRecords[2]{}, oursRecords[2]{};
        auto prepare = [&](cTSEffectsExtension &extension, nGZSceneGraph::TSEffectRecord *nodes, void *instance) {
            auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension.mRegistry.sentinel);
            if (!records) sentinel->next = sentinel->previous = sentinel;
            else {
                nodes[0].firstText = "a bound effect";
                nodes[0].secondText = "its texture";
                nodes[0].hashValue = 0x12345678;
                nodes[0].kind = 0;
                nodes[0].flag = 0x7a;
                nodes[0].instance = instance;
                nodes[0].next = records == 2 ? &nodes[1] : sentinel;
                nodes[0].previous = sentinel;
                sentinel->next = &nodes[0];
                sentinel->previous = records == 2 ? &nodes[1] : &nodes[0];
                if (records == 2) {
                    nodes[1].firstText = "excluded effect";
                    nodes[1].secondText = "excluded texture";
                    nodes[1].kind = 1;
                    nodes[1].next = sentinel; nodes[1].previous = &nodes[0];
                }
            }
            extension.mRegistry.count = records;
        };
        prepare(image, imageRecords, &imageInstance);
        prepare(ours, oursRecords, &oursInstance);
        WriterLog imageLog{}, oursLog{};
        gWriterLog = &imageLog;
        *reinterpret_cast<void ***>(imageFace) = originalVtable;
        ctx.Original<Fn>(0xcc69e0)(imageFace, writer);
        *reinterpret_cast<void ***>(imageFace) = imageOwnVtable;
        gWriterLog = &oursLog;
        reinterpret_cast<Fn>(oursVtable[19])(oursFace, writer);
        ++ctx.cases;
        const char *expectedOps = records ? "VUSSUUB" : "VU";
        const unsigned expectedSize = records ? 7u : 2u;
        bool equal = imageLog.size == oursLog.size && imageLog.size == expectedSize &&
                     std::memcmp(imageLog.op, expectedOps, expectedSize) == 0 &&
                     std::memcmp(imageLog.op, oursLog.op, expectedSize) == 0;
        for (unsigned i = 0; i < expectedSize && equal; ++i) {
            equal = imageLog.value[i] == oursLog.value[i];
            if (i == 0) equal = equal && imageLog.value[i] == 2;
            else if (i == 1) equal = equal && imageLog.value[i] == (records ? 1u : 0u);
            if (records && i == 2) equal = equal && std::strcmp(imageLog.text[i], "a bound effect") == 0 &&
                                                      std::strcmp(oursLog.text[i], "a bound effect") == 0;
            if (records && i == 3) equal = equal && std::strcmp(imageLog.text[i], "its texture") == 0 &&
                                                      std::strcmp(oursLog.text[i], "its texture") == 0;
        }
        if (records) equal = equal && imageLog.value[4] == 0x12345678 && imageLog.value[5] == 0 &&
                                      imageLog.value[6] == 0x7a && imageLog.pointer[2] == &imageRecords[0].firstTextVTable &&
                                      imageLog.pointer[3] == &imageRecords[0].nameProviderVTable &&
                                      oursLog.pointer[2] == &oursRecords[0].firstTextVTable &&
                                      oursLog.pointer[3] == &oursRecords[0].nameProviderVTable;
        if (!equal || image.mRegistry.count != ours.mRegistry.count || image.mRegistry.count != records)
            ctx.Fail("%u records: writer sequence/result differed (%u/%u)", records, imageLog.size, oursLog.size);
        for (auto *extension : {&image, &ours}) {
            auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension->mRegistry.sentinel);
            sentinel->next = sentinel->previous = sentinel;
            extension->mRegistry.count = 0;
        }
    };
    run(0);
    run(2);
    gWriterLog = nullptr;
}

DIFF_TEST(EffectsExtension_BindEffects, 0xcc6550)
{
    using Fn = void(__thiscall *)(void *);
    shape::CodePatch findNode(0xae7490u, shape::CodeOf(&StandFindNodeByName));
    cTSEffectsExtension image, ours;
    auto *imageFace = reinterpret_cast<uint8_t *>(&image) + 0x18;
    auto *oursFace = reinterpret_cast<uint8_t *>(&ours) + 0x18;
    auto **oursVtable = *reinterpret_cast<void ***>(oursFace);
    image.mOwner = ours.mOwner = reinterpret_cast<nGZSceneGraph::cIObjectGraphNode *>(0x12345678u);
    void *instanceVtable[15]{};
    instanceVtable[3] = shape::CodeOf(&FakeSlot3);
    instanceVtable[14] = shape::CodeOf(&FakeSlot14);
    FakeRef imageFake{}; imageFake.vtable = instanceVtable;
    FakeRef oursFake{}; oursFake.vtable = instanceVtable;
    nGZSceneGraph::TSEffectRecord imageRecords[2]{}, oursRecords[2]{};
    auto prepare = [](cTSEffectsExtension &extension, nGZSceneGraph::TSEffectRecord *records, void *instance) {
        auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension.mRegistry.sentinel);
        records[0].secondText = "unused";
        records[1].secondText = "sparkle";
        records[0].instance = nullptr;
        records[1].instance = instance;
        records[0].next = &records[1]; records[0].previous = sentinel;
        records[1].next = sentinel; records[1].previous = &records[0];
        sentinel->next = &records[0]; sentinel->previous = &records[1];
        extension.mRegistry.count = 2;
    };
    prepare(image, imageRecords, &imageFake);
    prepare(ours, oursRecords, &oursFake);
    void *const node = reinterpret_cast<void *>(0x87654321u);
    void *answers[] = {nullptr, node};
    for (void *answer : answers) {
        image.mUnknown40 = ours.mUnknown40 = 0;
        gNodeLookupResult = answer;
        gNodeLookup = {};
        ctx.Original<Fn>(0xcc6550)(imageFace);
        const NodeLookupCall imageLookup = gNodeLookup;
        gNodeLookup = {};
        reinterpret_cast<Fn>(oursVtable[15])(oursFace);
        const NodeLookupCall oursLookup = gNodeLookup;
        ++ctx.cases;
        if (image.mUnknown40 != ours.mUnknown40 || static_cast<uint8_t>(image.mUnknown40) != 1 ||
            imageFake.calls3 != oursFake.calls3 || imageFake.value3 != oursFake.value3 ||
            imageFake.calls14 != oursFake.calls14 || imageFake.args14[0] != oursFake.args14[0] ||
            imageFake.args14[1] != oursFake.args14[1] || imageLookup.calls != oursLookup.calls ||
            imageLookup.calls != 1 || imageLookup.name == nullptr || std::strcmp(imageLookup.name, "sparkle") != 0 ||
            imageLookup.node != image.mOwner || oursLookup.node != ours.mOwner ||
            imageFake.calls3 != (answer ? 1u : 0u) || imageFake.calls14 != (answer ? 1u : 0u) ||
            (answer && (imageFake.args14[0] != 2 || imageFake.args14[1] != reinterpret_cast<uint32_t>(node) || imageFake.value3 != 1)))
            ctx.Fail("node %p: state %x/%x slot3 %u:%u/%u:%u slot14 %u:%u,%x/%u:%u,%x find %u/%u '%s' %p/%p",
                     answer, image.mUnknown40, ours.mUnknown40, imageFake.calls3, imageFake.value3,
                     oursFake.calls3, oursFake.value3, imageFake.calls14, imageFake.args14[0], imageFake.args14[1],
                     oursFake.calls14, oursFake.args14[0], oursFake.args14[1], imageLookup.calls, oursLookup.calls,
                     imageLookup.name ? imageLookup.name : "<null>", imageLookup.node, oursLookup.node);
        imageFake.calls3 = oursFake.calls3 = imageFake.calls14 = oursFake.calls14 = 0;
        imageFake.value3 = oursFake.value3 = 0;
        imageFake.args14[0] = imageFake.args14[1] = oursFake.args14[0] = oursFake.args14[1] = 0;
    }
    image.mUnknown40 = ours.mUnknown40 = 1;
    gNodeLookup = {};
    ctx.Original<Fn>(0xcc6550)(imageFace);
    const NodeLookupCall gatedImage = gNodeLookup;
    gNodeLookup = {};
    reinterpret_cast<Fn>(oursVtable[15])(oursFace);
    ++ctx.cases;
    if (gNodeLookup.calls != 0 || gatedImage.calls != 0 || image.mUnknown40 != ours.mUnknown40)
        ctx.Fail("already-bound pass was not gated");
    for (auto *extension : {&image, &ours}) {
        auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension->mRegistry.sentinel);
        sentinel->next = sentinel->previous = sentinel;
        extension->mRegistry.count = 0;
    }
}

// 0xcc5b50's null/empty/locator shortcuts and its one original helper call.
DIFF_TEST(EffectsExtension_FindEffectNode, 0xcc5b50)
{
    shape::CodePatch findNode(0xae7490u, shape::CodeOf(&StandFindNodeByName));
    cTSEffectsExtension extension;
    const char locator[] = "LoCaToR";
    const char empty[] = "";
    const char ordinary[] = "spark";
    auto *fallback = reinterpret_cast<void *>(0x12345678u);
    auto *found = reinterpret_cast<void *>(0x87654321u);
    gNodeLookupResult = found;
    struct Case { const char *name; bool callsHelper; } cases[] = {
        {nullptr, false}, {empty, false}, {locator, false}, {ordinary, true},
    };
    for (const Case &c : cases) {
        gNodeLookup = {};
        void *imageResult = ctx.Original<void *(__cdecl *)(const char *, void *)>(0xcc5b50)(c.name, fallback);
        const NodeLookupCall imageCall = gNodeLookup;
        gNodeLookup = {};
        void *ourResult = nGZSceneGraph::cTSEffectsExtensionAdapters::FindEffectNode(c.name, fallback);
        const NodeLookupCall ourCall = gNodeLookup;
        ++ctx.cases;
        if (imageResult != ourResult || imageResult != (c.callsHelper ? found : fallback) ||
            imageCall.calls != (c.callsHelper ? 1u : 0u) || ourCall.calls != imageCall.calls ||
            (c.callsHelper && (imageCall.name != c.name || imageCall.node != fallback || imageCall.limit != 0xf ||
                               ourCall.name != imageCall.name || ourCall.node != imageCall.node || ourCall.limit != imageCall.limit)))
            ctx.Fail("name %s: result/helper call mismatch (image %p/%u, ours %p/%u)",
                     c.name ? c.name : "<null>", imageResult, imageCall.calls, ourResult, ourCall.calls);
    }
}

// The class face's slot 3 collects ids whose provider returns the requested UTF-16 name.
DIFF_TEST(EffectsExtension_CollectNamed, 0xcc5b90)
{
    using Fn = bool(__thiscall *)(void *, const uint16_t *, std::vector<uint16_t> *);
    VectorAllocatorScope vectorAllocator(ctx);
    cTSEffectsExtension image, ours;
    auto *imageFace = reinterpret_cast<uint8_t *>(&image) + 0x18;
    auto *oursFace = reinterpret_cast<uint8_t *>(&ours) + 0x18;
    auto **oursVtable = *reinterpret_cast<void ***>(oursFace);
    void *providerVtable[6]{};
    providerVtable[5] = shape::CodeOf(&FakeName);
    const uint16_t alpha[] = {'A', 'l', 'p', 'h', 'a', 0};
    const uint16_t beta[] = {'B', 'e', 't', 'a', 0};
    gImageNameA = gOursNameA = alpha;
    gImageNameB = gOursNameB = beta;
    nGZSceneGraph::TSEffectRecord imageRecords[2]{}, oursRecords[2]{};
    auto prepare = [&](cTSEffectsExtension &extension, nGZSceneGraph::TSEffectRecord *records,
                      unsigned count) {
        auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension.mRegistry.sentinel);
        sentinel->next = count ? &records[0] : sentinel;
        sentinel->previous = count ? &records[count - 1] : sentinel;
        for (unsigned i = 0; i < count; ++i) {
            auto *record = &records[i];
            record->next = i + 1 == count ? sentinel : &records[i + 1];
            record->previous = i == 0 ? sentinel : &records[i - 1];
            record->hashValue = 0x120 + i;
            record->nameProviderVTable = providerVtable;
        }
        extension.mRegistry.count = count;
    };
    for (unsigned count : {0u, 1u, 2u}) {
        prepare(image, imageRecords, count);
        prepare(ours, oursRecords, count);
        gImageSourceA = &imageRecords[0].nameProviderVTable;
        gImageSourceB = &imageRecords[1].nameProviderVTable;
        gOursSourceA = &oursRecords[0].nameProviderVTable;
        gOursSourceB = &oursRecords[1].nameProviderVTable;
        for (const uint16_t *query : {alpha, beta}) {
            std::vector<uint16_t> imageOut, oursOut;
            ctx.Mark("CollectNamed original");
            const bool imageResult = ctx.Original<Fn>(0xcc5b90)(imageFace, query, &imageOut);
            ctx.Mark("CollectNamed ours");
            const bool oursResult = reinterpret_cast<Fn>(oursVtable[3])(oursFace, query, &oursOut);
            ++ctx.cases;
            if (imageResult != oursResult || imageOut != oursOut || imageResult != !imageOut.empty())
                ctx.Fail("count %u query %u: result/output mismatch (%u/%u, sizes %u/%u)", count,
                         static_cast<unsigned>(query == alpha), imageResult, oursResult,
                         static_cast<unsigned>(imageOut.size()), static_cast<unsigned>(oursOut.size()));
        }
    }
    for (auto *extension : {&image, &ours}) {
        auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension->mRegistry.sentinel);
        sentinel->next = sentinel->previous = sentinel;
        extension->mRegistry.count = 0;
    }
}

// 0xcc4d10 compares the two strings and discriminator in a selected bucket and writes its insertion result.
DIFF_TEST(EffectsExtension_FindRecord, 0xcc4d10)
{
    using FindFn = nGZSceneGraph::TSEffectFindResult *(__thiscall *)(nGZSceneGraph::TSEffectsRegistry *,
        nGZSceneGraph::TSEffectFindResult *, const nGZSceneGraph::TSEffectFindKey *, uint32_t);
    nGZSceneGraph::TSEffectRecord sentinel{};
    nGZSceneGraph::TSEffectRecord records[2]{};
    std::array<nGZSceneGraph::TSEffectRecord *, 16> buckets{};
    nGZSceneGraph::TSEffectsRegistry registry{};
    registry.sentinel = &sentinel;
    registry.buckets = buckets.data();
    registry.bucketMask = 7;
    registry.bucketCount = 8;
    sentinel.next = sentinel.previous = &sentinel;
    for (size_t i = 0; i < buckets.size(); i += 2) buckets[i] = buckets[i + 1] = &sentinel;
    records[0].firstText = "red";
    records[0].secondText = "one";
    records[0].hashValue = 0x11;
    records[1].firstText = "blue";
    records[1].secondText = "two";
    records[1].hashValue = 0x22;

    struct Query { const char *first; const char *second; uint32_t discriminator; };
    for (unsigned count = 0; count <= 2; ++count) {
        sentinel.next = count ? &records[0] : &sentinel;
        sentinel.previous = count ? &records[count - 1] : &sentinel;
        records[0].previous = &sentinel;
        records[0].next = count == 2 ? &records[1] : &sentinel;
        records[1].previous = count == 2 ? &records[0] : &sentinel;
        records[1].next = &sentinel;
        buckets[6] = count ? &records[0] : &sentinel;
        buckets[7] = count ? &records[count - 1] : &sentinel;
        Query queries[] = {{"red", "one", 0x11}, {"blue", "two", 0x22}, {"red", "wrong", 0x11},
                           {"red", "one", 0x33}, {"wrong", "one", 0x11}};
        for (const Query &query : queries) {
            nGZSceneGraph::TSEffectFindKey key{};
            key.firstText = query.first;
            key.secondText = query.second;
            key.discriminator = query.discriminator;
            nGZSceneGraph::TSEffectFindResult original{}, ours{};
            auto *originalReturn = ctx.Original<FindFn>(0xcc4d10)(&registry, &original, &key, 3);
            auto *ourReturn = reinterpret_cast<cTSEffectsExtension *>(reinterpret_cast<uint8_t *>(&registry) - 0x20)
                                  ->FindRecord(&ours, &key, 3);
            ++ctx.cases;
            if (originalReturn != &original || ourReturn != &ours || std::memcmp(&original, &ours, sizeof(ours)) != 0)
                ctx.Fail("count %u key (%s,%s,%x): output (%p,%p) / (%p,%p)", count, query.first,
                         query.second, query.discriminator, original.next, original.found, ours.next, ours.found);
        }
    }
}

DIFF_TEST(EffectsExtension_FindInstance, 0xcc5a00)
{
    using Fn = void *(__thiscall *)(void *, const char *, const char *, uint32_t);
    using HashBytesFn = uint32_t(__cdecl *)(const char *, uint32_t);
    using HashIntFn = uint32_t(__cdecl *)(const uint32_t *);
    cTSEffectsExtension image, ours;
    auto *imageFace = reinterpret_cast<uint8_t *>(&image) + 0x18;
    auto *oursFace = reinterpret_cast<uint8_t *>(&ours) + 0x18;
    auto **oursVtable = *reinterpret_cast<void ***>(oursFace);
    nGZSceneGraph::TSEffectRecord imageRecord{}, oursRecord{};
    auto prepare = [&](cTSEffectsExtension &extension, nGZSceneGraph::TSEffectRecord &record,
                       const char *first, const char *second, uint32_t discriminator, void *instance) {
        auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension.mRegistry.sentinel);
        auto **buckets = static_cast<nGZSceneGraph::TSEffectRecord **>(extension.mRegistry.buckets);
        sentinel->next = sentinel->previous = &record;
        record.next = record.previous = sentinel;
        record.firstText = first;
        record.secondText = second;
        record.hashValue = discriminator;
        record.instance = instance;
        using HashBytesFn = uint32_t(__cdecl *)(const char *, uint32_t);
        using HashIntFn = uint32_t(__cdecl *)(const uint32_t *);
        const uint32_t bucket = (ctx.Original<HashBytesFn>(0x4fea40)(first, static_cast<uint32_t>(record.firstText.size())) |
                                 ctx.Original<HashBytesFn>(0x4fea40)(second, static_cast<uint32_t>(record.secondText.size())) |
                                 ctx.Original<HashIntFn>(0x141a14)(&record.hashValue)) & extension.mRegistry.bucketMask;
        for (size_t i = 0; i < 16; i += 2) buckets[i] = buckets[i + 1] = sentinel;
        buckets[bucket * 2] = buckets[bucket * 2 + 1] = &record;
        extension.mRegistry.count = 1;
    };
    struct Key { const char *first; const char *second; uint32_t id; } keys[] = {
        {"red", nullptr, 0x15}, {"blue", "texture", 0x112}, {"red", "texture", 0x112},
    };
    for (const Key &key : keys) {
        void *imageMarker = reinterpret_cast<void *>(0x11112222u);
        void *oursMarker = reinterpret_cast<void *>(0x33334444u);
        prepare(image, imageRecord, key.first, key.second ? key.second : "", key.id, imageMarker);
        prepare(ours, oursRecord, key.first, key.second ? key.second : "", key.id, oursMarker);
        struct Query { const char *first; const char *second; uint32_t id; bool found; } queries[] = {
            {key.first, key.second, key.id, true},
            {key.first, key.second, key.id + 8, false},
            {"missing", key.second, key.id, false},
        };
        for (const Query &query : queries) {
            void *imageResult = ctx.Original<Fn>(0xcc5a00)(imageFace, query.first, query.second, query.id);
            void *oursResult = reinterpret_cast<Fn>(oursVtable[6])(oursFace, query.first, query.second, query.id);
            ++ctx.cases;
            void *expectedImage = query.found ? imageMarker : nullptr;
            void *expectedOurs = query.found ? oursMarker : nullptr;
            if (imageResult != expectedImage || oursResult != expectedOurs)
                ctx.Fail("key (%s,%s,%x): instance lookup got %p/%p, expected %p/%p", query.first,
                         query.second ? query.second : "<null>", query.id, imageResult, oursResult,
                         expectedImage, expectedOurs);
        }
    }
    for (auto *extension : {&image, &ours}) {
        auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension->mRegistry.sentinel);
        sentinel->next = sentinel->previous = sentinel;
        extension->mRegistry.count = 0;
    }
}

DIFF_TEST(EffectsExtension_RemoveByKey, 0xcc6620)
{
    using Fn = uint32_t(__thiscall *)(void *, const char *, const char *, uint32_t);
    shape::CodePatch gameDelete(0x824624u, shape::CodeOf(+[](void *p, size_t) { ::operator delete(p); }));
    cTSEffectsExtension image, ours;
    auto *imageFace = reinterpret_cast<uint8_t *>(&image) + 0x18;
    auto *oursFace = reinterpret_cast<uint8_t *>(&ours) + 0x18;
    auto **oursVtable = *reinterpret_cast<void ***>(oursFace);
    void *instanceVtable[6]{};
    instanceVtable[2] = shape::CodeOf(&FakeRelease);
    instanceVtable[4] = shape::CodeOf(&FakeSetActive);
    instanceVtable[5] = shape::CodeOf(&FakeIsActive);
    FakeRef imageFake{}; imageFake.vtable = instanceVtable;
    FakeRef oursFake{}; oursFake.vtable = instanceVtable;
    using HashBytesFn = uint32_t(__cdecl *)(const char *, uint32_t);
    using HashIntFn = uint32_t(__cdecl *)(const uint32_t *);
    struct Case { bool record; bool active; uint32_t queryId; } cases[] = {
        {false, false, 0x23}, {true, false, 0x23}, {true, true, 0x23}, {true, true, 0x2b},
    };
    for (const Case &c : cases) {
        auto prepare = [&](cTSEffectsExtension &extension, FakeRef &fake) {
            auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension.mRegistry.sentinel);
            auto **buckets = static_cast<nGZSceneGraph::TSEffectRecord **>(extension.mRegistry.buckets);
            for (size_t i = 0; i < 16; i += 2) buckets[i] = buckets[i + 1] = sentinel;
            sentinel->next = sentinel->previous = sentinel;
            extension.mRegistry.count = 0;
            fake.active = c.active;
            fake.refs = 4; fake.releases = fake.setCalls = fake.setValue = 0;
            if (!c.record) return static_cast<nGZSceneGraph::TSEffectRecord *>(nullptr);
            auto *record = ::new (::operator new(sizeof(nGZSceneGraph::TSEffectRecord))) nGZSceneGraph::TSEffectRecord{};
            record->firstText = "long effect identifier for key removal";
            record->secondText = "long texture identifier for key removal";
            record->hashValue = 0x23;
            record->instance = &fake;
            const uint32_t bucket = (ctx.Original<HashBytesFn>(0x4fea40)(record->firstText.data(),
                                         static_cast<uint32_t>(record->firstText.size())) |
                                     ctx.Original<HashBytesFn>(0x4fea40)(record->secondText.data(),
                                         static_cast<uint32_t>(record->secondText.size())) |
                                     ctx.Original<HashIntFn>(0x141a14)(&record->hashValue)) & extension.mRegistry.bucketMask;
            sentinel->next = sentinel->previous = record;
            record->next = record->previous = sentinel;
            buckets[2 * bucket] = buckets[2 * bucket + 1] = record;
            extension.mRegistry.count = 1;
            return record;
        };
        auto *imageRecord = prepare(image, imageFake);
        auto *oursRecord = prepare(ours, oursFake);
        uint32_t returns[2]{};
        size_t freeCounts[2]{};
        bool recordFreed[2]{};
        for (int side = 0; side != 2; ++side) {
            auto *record = side == 0 ? imageRecord : oursRecord;
            difftest::FreeRecorder frees;
            if (side == 0) returns[0] = ctx.Original<Fn>(0xcc6620)(imageFace,
                    "long effect identifier for key removal", "long texture identifier for key removal", c.queryId);
            else returns[1] = reinterpret_cast<Fn>(oursVtable[5])(oursFace,
                    "long effect identifier for key removal", "long texture identifier for key removal", c.queryId);
            frees.Stop();
            freeCounts[side] = frees.Count();
            recordFreed[side] = record && frees.Freed(record);
        }
        ++ctx.cases;
        const bool found = c.record && c.queryId == 0x23;
        if (returns[0] != returns[1] || static_cast<uint8_t>(returns[0]) != 0 ||
            image.mRegistry.count != ours.mRegistry.count || image.mRegistry.count != (found ? 0u : (c.record ? 1u : 0u)) ||
            freeCounts[0] != freeCounts[1] || recordFreed[0] != recordFreed[1] || recordFreed[0] != found ||
            imageFake.releases != oursFake.releases || imageFake.setCalls != oursFake.setCalls ||
            imageFake.setValue != oursFake.setValue || imageFake.releases != (found ? 1u : 0u) ||
            imageFake.setCalls != (found && c.active ? 1u : 0u) ||
            (found && c.active && imageFake.setValue != 1))
            ctx.Fail("record %u active %u id %x: return %08x/%08x count %u/%u frees %u/%u release %u/%u set %u/%u",
                     c.record, c.active, c.queryId, returns[0], returns[1], image.mRegistry.count,
                     ours.mRegistry.count, static_cast<unsigned>(freeCounts[0]), static_cast<unsigned>(freeCounts[1]),
                     imageFake.releases, oursFake.releases, imageFake.setCalls, oursFake.setCalls);
        for (auto *extension : {&image, &ours}) {
            auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension->mRegistry.sentinel);
            auto **buckets = static_cast<nGZSceneGraph::TSEffectRecord **>(extension->mRegistry.buckets);
            sentinel->next = sentinel->previous = sentinel;
            for (size_t i = 0; i < 16; i += 2) buckets[i] = buckets[i + 1] = sentinel;
            extension->mRegistry.count = 0;
        }
        if (c.record && !found) { delete imageRecord; delete oursRecord; }
    }
}

DIFF_TEST(EffectsExtension_ProcessUnused, 0xcc5c20)
{
    using Fn = void(__thiscall *)(void *, uint32_t); // flags are consumed by the original as one low byte
    shape::CodePatch gameDelete(0x824624u, shape::CodeOf(+[](void *p, size_t) { ::operator delete(p); }));
    cTSEffectsExtension image, ours;
    auto *imageFace = reinterpret_cast<uint8_t *>(&image) + 0x18;
    auto *oursFace = reinterpret_cast<uint8_t *>(&ours) + 0x18;
    auto **oursVtable = *reinterpret_cast<void ***>(oursFace);
    void *instanceVtable[7]{};
    instanceVtable[1] = shape::CodeOf(&FakeAddRef);
    instanceVtable[2] = shape::CodeOf(&FakeRelease);
    instanceVtable[5] = shape::CodeOf(&FakeIsActive);
    instanceVtable[6] = shape::CodeOf(&FakeIsReady);
    FakeRef imageFake{}; imageFake.vtable = instanceVtable;
    FakeRef oursFake{}; oursFake.vtable = instanceVtable;
    using HashBytesFn = uint32_t(__cdecl *)(const char *, uint32_t);
    using HashIntFn = uint32_t(__cdecl *)(const uint32_t *);
    struct Case { uint32_t kind; bool hasInstance; uint8_t active; uint8_t ready; uint32_t refs; uint32_t flag; } cases[] = {
        {1, false, 0, 0, 2, 0}, {0, false, 0, 0, 2, 0},
        {0, true, 0, 0, 2, 0}, {0, true, 1, 0, 2, 0},
        {0, true, 1, 1, 4, 0x100}, {0, true, 1, 1, 4, 0x101}, {0, true, 1, 1, 3, 1},
    };
    for (const Case &c : cases) {
        auto prepare = [&](cTSEffectsExtension &extension, FakeRef &fake) {
            auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension.mRegistry.sentinel);
            auto **buckets = static_cast<nGZSceneGraph::TSEffectRecord **>(extension.mRegistry.buckets);
            for (size_t i = 0; i < 16; i += 2) buckets[i] = buckets[i + 1] = sentinel;
            auto *record = ::new (::operator new(sizeof(nGZSceneGraph::TSEffectRecord))) nGZSceneGraph::TSEffectRecord{};
            record->firstText = "unbound effect with long identifier";
            record->secondText = "unbound texture with long identifier";
            record->hashValue = 0x35;
            record->kind = c.kind;
            record->instance = c.hasInstance ? &fake : nullptr;
            record->next = record->previous = sentinel;
            sentinel->next = sentinel->previous = record;
            extension.mRegistry.count = 1;
            const uint32_t bucket = (ctx.Original<HashBytesFn>(0x4fea40)(record->firstText.data(),
                                         static_cast<uint32_t>(record->firstText.size())) |
                                     ctx.Original<HashBytesFn>(0x4fea40)(record->secondText.data(),
                                         static_cast<uint32_t>(record->secondText.size())) |
                                     ctx.Original<HashIntFn>(0x141a14)(&record->hashValue)) & extension.mRegistry.bucketMask;
            buckets[2 * bucket] = buckets[2 * bucket + 1] = record;
            fake.refs = c.refs; fake.active = c.active; fake.ready = c.ready;
            fake.releases = fake.calls1 = fake.calls6 = 0;
            return record;
        };
        auto *imageRecord = prepare(image, imageFake);
        auto *oursRecord = prepare(ours, oursFake);
        size_t freeCounts[2]{};
        bool recordFreed[2]{};
        for (int side = 0; side != 2; ++side) {
            auto *record = side == 0 ? imageRecord : oursRecord;
            difftest::FreeRecorder frees;
            if (side == 0) ctx.Original<Fn>(0xcc5c20)(imageFace, c.flag);
            else reinterpret_cast<Fn>(oursVtable[9])(oursFace, c.flag);
            frees.Stop();
            freeCounts[side] = frees.Count();
            recordFreed[side] = frees.Freed(record);
        }
        ++ctx.cases;
        const uint8_t lowFlag = static_cast<uint8_t>(c.flag);
        const bool acted = c.kind != 1 && c.hasInstance && (!c.active || (lowFlag != 0 && c.ready));
        const bool removed = c.kind != 1 && (!c.hasInstance || (acted && c.refs < 3));
        const uint32_t expectedRefs = !c.hasInstance ? c.refs : acted ? c.refs - (c.refs < 3 ? 1u : 0u) : c.refs;
        if (image.mRegistry.count != ours.mRegistry.count || image.mRegistry.count != (removed ? 0u : 1u) ||
            freeCounts[0] != freeCounts[1] || recordFreed[0] != recordFreed[1] || recordFreed[0] != removed ||
            imageFake.calls1 != oursFake.calls1 || imageFake.releases != oursFake.releases ||
            imageFake.calls6 != oursFake.calls6 || imageFake.refs != oursFake.refs ||
            imageFake.calls1 != (acted ? 1u : 0u) || imageFake.calls6 != (c.active && lowFlag ? 1u : 0u) ||
            imageFake.releases != (acted ? (c.refs < 3 ? 2u : 1u) : 0u) || imageFake.refs != expectedRefs)
            ctx.Fail("kind %u instance %u active %u ready %u refs %u flag %x: count %u/%u frees %u/%u calls %u/%u release %u/%u refs %u/%u",
                     c.kind, c.hasInstance, c.active, c.ready, c.refs, c.flag, image.mRegistry.count,
                     ours.mRegistry.count, static_cast<unsigned>(freeCounts[0]), static_cast<unsigned>(freeCounts[1]),
                     imageFake.calls1, oursFake.calls1, imageFake.releases, oursFake.releases, imageFake.refs, oursFake.refs);
        for (auto *extension : {&image, &ours}) {
            auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension->mRegistry.sentinel);
            auto **buckets = static_cast<nGZSceneGraph::TSEffectRecord **>(extension->mRegistry.buckets);
            sentinel->next = sentinel->previous = sentinel;
            for (size_t i = 0; i < 16; i += 2) buckets[i] = buckets[i + 1] = sentinel;
            extension->mRegistry.count = 0;
        }
        if (!recordFreed[0]) delete imageRecord;
        if (!recordFreed[1]) delete oursRecord;
    }
}

// 0xcc4fd0 unlinks one record, releases its instance, destroys both strings and frees 0x58 bytes.
DIFF_TEST(EffectsExtension_RemoveRecord, 0xcc4fd0)
{
    using RemoveFn = nGZSceneGraph::TSEffectRecord **(__thiscall *)(nGZSceneGraph::TSEffectsRegistry *,
        nGZSceneGraph::TSEffectRecord **, nGZSceneGraph::TSEffectRecord *);
    shape::CodePatch gameNew(0x824632u, shape::CodeOf(+[](size_t n) -> void * {
        return ::operator new(n);
    }));
    shape::CodePatch gameDelete(0x824624u, shape::CodeOf(+[](void *p, size_t) { ::operator delete(p); }));
    cTSEffectsExtension extension;
    auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension.mRegistry.sentinel);
    auto **buckets = static_cast<nGZSceneGraph::TSEffectRecord **>(extension.mRegistry.buckets);
    void *refVtable[5]{};
    refVtable[2] = shape::CodeOf(&FakeRelease);
    FakeRef ref{refVtable, 2, 0, 0, 0, 0};
    struct Result { bool objectFreed; bool objectSizeFreed; size_t freeCount; size_t frees[8]; uint32_t releases; uint32_t refs;
                    uint32_t registryCount; bool listEmpty; bool bucketEmpty; nGZSceneGraph::TSEffectRecord *next; } result[2]{};
    for (int side = 0; side != 2; ++side) {
        sentinel->next = sentinel->previous = sentinel;
        for (size_t i = 0; i < 16; i += 2) buckets[i] = buckets[i + 1] = sentinel;
        extension.mRegistry.count = 1;
        auto *record = ::new (::operator new(sizeof(nGZSceneGraph::TSEffectRecord))) nGZSceneGraph::TSEffectRecord{};
        record->firstText = "a long effect identifier beyond small string storage";
        record->secondText = "a different long texture identifier beyond small string storage";
        record->hashValue = 0x23;
        record->instance = &ref;
        const uint32_t bucket = (static_cast<uint32_t>(std::hash<std::string>{}(record->firstText)) |
                                 static_cast<uint32_t>(std::hash<std::string>{}(record->secondText)) | record->hashValue) &
                                extension.mRegistry.bucketMask;
        sentinel->next = sentinel->previous = record;
        record->next = record->previous = sentinel;
        buckets[2 * bucket] = buckets[2 * bucket + 1] = record;
        ref.refs = 2;
        ref.releases = 0;
        nGZSceneGraph::TSEffectRecord *next = nullptr;
        {
            difftest::FreeRecorder frees;
            if (side == 0)
                ctx.Original<RemoveFn>(0xcc4fd0)(&extension.mRegistry, &next, record);
            else
                extension.RemoveRecord(&next, record);
            frees.Stop();
            result[side].objectFreed = frees.Freed(record);
            result[side].freeCount = frees.SortedSizes(result[side].frees, 8);
            for (size_t i = 0; i < result[side].freeCount; ++i)
                result[side].objectSizeFreed |= result[side].frees[i] == sizeof(nGZSceneGraph::TSEffectRecord);
        }
        result[side].releases = ref.releases;
        result[side].refs = ref.refs;
        result[side].registryCount = extension.mRegistry.count;
        result[side].listEmpty = sentinel->next == sentinel && sentinel->previous == sentinel;
        result[side].bucketEmpty = buckets[2 * bucket] == sentinel && buckets[2 * bucket + 1] == sentinel;
        result[side].next = next;
    }
    ++ctx.cases;
    if (std::memcmp(&result[0], &result[1], sizeof(Result)) != 0 || !result[0].objectFreed || !result[0].objectSizeFreed ||
        result[0].freeCount == 0 || result[0].releases != 1 || result[0].refs != 1 || result[0].registryCount != 0 ||
        !result[0].listEmpty || !result[0].bucketEmpty || result[0].next != sentinel)
        ctx.Fail("RemoveRecord differences: freed %d/%d (%u/%u blocks), release %u/%u, count %u/%u, list %d/%d, bucket %d/%d",
                 result[0].objectFreed, result[1].objectFreed, static_cast<unsigned>(result[0].freeCount),
                 static_cast<unsigned>(result[1].freeCount), result[0].releases, result[1].releases,
                 result[0].registryCount, result[1].registryCount, result[0].listEmpty, result[1].listEmpty,
                 result[0].bucketEmpty, result[1].bucketEmpty);

    // Removing the tail from a two-record bucket updates only its end pointer; the head survives.
    Result tailResult[2]{};
    for (int side = 0; side != 2; ++side) {
        sentinel->next = sentinel->previous = sentinel;
        for (size_t i = 0; i < 16; i += 2) buckets[i] = buckets[i + 1] = sentinel;
        extension.mRegistry.count = 2;
        auto *head = ::new (::operator new(sizeof(nGZSceneGraph::TSEffectRecord))) nGZSceneGraph::TSEffectRecord{};
        auto *tail = ::new (::operator new(sizeof(nGZSceneGraph::TSEffectRecord))) nGZSceneGraph::TSEffectRecord{};
        for (auto *r : {head, tail}) {
            r->firstText = "same long effect identifier beyond small string storage";
            r->secondText = "same long texture identifier beyond small string storage";
            r->hashValue = 0x23;
            r->instance = &ref;
        }
        const uint32_t bucket = (static_cast<uint32_t>(std::hash<std::string>{}(head->firstText)) |
                                 static_cast<uint32_t>(std::hash<std::string>{}(head->secondText)) | head->hashValue) &
                                extension.mRegistry.bucketMask;
        sentinel->next = head;
        sentinel->previous = tail;
        head->previous = sentinel;
        head->next = tail;
        tail->previous = head;
        tail->next = sentinel;
        buckets[2 * bucket] = head;
        buckets[2 * bucket + 1] = tail;
        ref.refs = 3;
        ref.releases = 0;
        nGZSceneGraph::TSEffectRecord *next = nullptr;
        {
            difftest::FreeRecorder frees;
            if (side == 0)
                ctx.Original<RemoveFn>(0xcc4fd0)(&extension.mRegistry, &next, tail);
            else
                extension.RemoveRecord(&next, tail);
            frees.Stop();
            tailResult[side].objectFreed = frees.Freed(tail);
            tailResult[side].freeCount = frees.SortedSizes(tailResult[side].frees, 8);
            for (size_t i = 0; i < tailResult[side].freeCount; ++i)
                tailResult[side].objectSizeFreed |= tailResult[side].frees[i] == sizeof(nGZSceneGraph::TSEffectRecord);
        }
        tailResult[side].releases = ref.releases;
        tailResult[side].refs = ref.refs;
        tailResult[side].registryCount = extension.mRegistry.count;
        tailResult[side].listEmpty = sentinel->next == head && sentinel->previous == head && head->next == sentinel && head->previous == sentinel;
        tailResult[side].bucketEmpty = buckets[2 * bucket] == head && buckets[2 * bucket + 1] == head;
        tailResult[side].next = next;
        head->firstText.~basic_string();
        head->secondText.~basic_string();
        ::operator delete(head, sizeof(nGZSceneGraph::TSEffectRecord));
        extension.mRegistry.count = 0;
        sentinel->next = sentinel->previous = sentinel;
        buckets[2 * bucket] = buckets[2 * bucket + 1] = sentinel;
    }
    ++ctx.cases;
    if (std::memcmp(&tailResult[0], &tailResult[1], sizeof(Result)) != 0 || !tailResult[0].objectFreed ||
        !tailResult[0].objectSizeFreed || tailResult[0].releases != 1 || tailResult[0].refs != 2 ||
        tailResult[0].registryCount != 1 || !tailResult[0].listEmpty || !tailResult[0].bucketEmpty ||
        tailResult[0].next != sentinel)
        ctx.Fail("RemoveRecord tail case differs: freed %d/%d, count %u/%u, list %d/%d, bucket %d/%d",
                 tailResult[0].objectFreed, tailResult[1].objectFreed, tailResult[0].registryCount,
                 tailResult[1].registryCount, tailResult[0].listEmpty, tailResult[1].listEmpty,
                 tailResult[0].bucketEmpty, tailResult[1].bucketEmpty);
}

// 0xcc65d0 finds a record by instance, calls that instance's slot 4 with one, then removes the record.
DIFF_TEST(EffectsExtension_RemoveByInstance, 0xcc65d0)
{
    using RemoveByInstanceFn = bool(__thiscall *)(void *, void *);
    shape::CodePatch gameNew(0x824632u, shape::CodeOf(+[](size_t n) -> void * { return ::operator new(n); }));
    shape::CodePatch gameDelete(0x824624u, shape::CodeOf(+[](void *p, size_t) { ::operator delete(p); }));
    cTSEffectsExtension extension;
    auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension.mRegistry.sentinel);
    auto **buckets = static_cast<nGZSceneGraph::TSEffectRecord **>(extension.mRegistry.buckets);
    void *refVtable[5]{};
    refVtable[2] = shape::CodeOf(&FakeRelease);
    refVtable[4] = shape::CodeOf(&FakeSetActive);
    FakeRef target{refVtable, 2, 0, 0, 0, 0}, other{refVtable, 2, 0, 0, 0, 0};
    struct Result { bool answer; uint32_t releases, refs, setCalls, setValue, count; bool recordFreed; bool empty; } got[2]{};
    bool answers[2][3]{};
    for (int side = 0; side != 2; ++side) {
        for (unsigned scenario = 0; scenario != 3; ++scenario) {
            sentinel->next = sentinel->previous = sentinel;
            for (size_t i = 0; i < 16; i += 2) buckets[i] = buckets[i + 1] = sentinel;
            extension.mRegistry.count = 0;
            auto *record = static_cast<nGZSceneGraph::TSEffectRecord *>(nullptr);
            if (scenario != 0) {
                record = ::new (::operator new(sizeof(nGZSceneGraph::TSEffectRecord))) nGZSceneGraph::TSEffectRecord{};
                record->firstText = "spark";
                record->secondText = "texture";
                record->hashValue = 7;
                record->instance = scenario == 1 ? &other : &target;
                const uint32_t bucket = (static_cast<uint32_t>(std::hash<std::string>{}(record->firstText)) |
                                         static_cast<uint32_t>(std::hash<std::string>{}(record->secondText)) | record->hashValue) &
                                        extension.mRegistry.bucketMask;
                buckets[2 * bucket] = buckets[2 * bucket + 1] = record;
                sentinel->next = sentinel->previous = record;
                record->next = record->previous = sentinel;
                extension.mRegistry.count = 1;
            }
            target.refs = other.refs = 2;
            target.releases = other.releases = 0;
            target.setCalls = other.setCalls = 0;
            target.setValue = other.setValue = 0;
            const void *imageFace = reinterpret_cast<uint8_t *>(&extension) + 0x18;
            const void *requested = scenario == 1 ? static_cast<void *>(&target) : static_cast<void *>(&target);
            {
                difftest::FreeRecorder frees;
                bool answer = side == 0
                    ? ctx.Original<RemoveByInstanceFn>(0xcc65d0)(const_cast<void *>(imageFace), const_cast<void *>(requested))
                    : extension.RemoveByInstance(const_cast<void *>(requested));
                answers[side][scenario] = answer;
                got[side].answer |= answer;
                frees.Stop();
                got[side].recordFreed |= record && frees.Freed(record);
            }
            FakeRef &selected = scenario == 1 ? other : target;
            got[side].releases += selected.releases;
            got[side].refs += selected.refs;
            got[side].setCalls += selected.setCalls;
            got[side].setValue += selected.setValue;
            got[side].count = extension.mRegistry.count;
            got[side].empty = sentinel->next == sentinel && sentinel->previous == sentinel;
            if (record && sentinel->next == record) {
                record->firstText.~basic_string();
                record->secondText.~basic_string();
                ::operator delete(record, sizeof(nGZSceneGraph::TSEffectRecord));
                sentinel->next = sentinel->previous = sentinel;
                extension.mRegistry.count = 0;
            }
        }
    }
    ++ctx.cases;
    if (std::memcmp(&got[0], &got[1], sizeof(Result)) != 0 ||
        std::memcmp(answers[0], answers[1], sizeof(answers[0])) != 0 || answers[0][0] || answers[0][1] || !answers[0][2] ||
        got[0].releases != 1 || got[0].setCalls != 1 || got[0].setValue != 1 || got[0].count != 0 ||
        !got[0].recordFreed || !got[0].empty)
        ctx.Fail("RemoveByInstance image/ours results differ: result %u/%u, release %u/%u, set %u/%u, count %u/%u",
                 got[0].answer, got[1].answer, got[0].releases, got[1].releases,
                 got[0].setCalls, got[1].setCalls, got[0].count, got[1].count);
}

// Slot 17 reports whether any stored instance answers true to its slot 5.
DIFF_TEST(EffectsExtension_HasActiveEffect, 0xcc5e40)
{
    shape::CodePatch gameNew(0x824632u, shape::CodeOf(+[](size_t n) -> void * { return ::operator new(n); }));
    shape::CodePatch gameDelete(0x824624u, shape::CodeOf(+[](void *p, size_t) { ::operator delete(p); }));
    cTSEffectsExtension extension;
    auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension.mRegistry.sentinel);
    auto **buckets = static_cast<nGZSceneGraph::TSEffectRecord **>(extension.mRegistry.buckets);
    void *refVtable[6]{};
    refVtable[5] = shape::CodeOf(&FakeIsActive);
    FakeRef fake{refVtable, 1, 0, 0, 0, 0};
    nGZSceneGraph::TSEffectRecord record{};
    void *face = reinterpret_cast<uint8_t *>(&extension) + 0x18;
    auto **ownVtable = *reinterpret_cast<void ***>(face);
    using Fn = uint8_t(__thiscall *)(void *);
    for (int scenario = 0; scenario != 4; ++scenario) {
        sentinel->next = sentinel->previous = sentinel;
        for (size_t i = 0; i < 16; i += 2) buckets[i] = buckets[i + 1] = sentinel;
        extension.mRegistry.count = 0;
        fake.active = scenario == 3 ? 1 : 0;
        if (scenario != 0) {
            record.instance = scenario == 1 ? nullptr : &fake;
            record.next = record.previous = sentinel;
            sentinel->next = sentinel->previous = &record;
            extension.mRegistry.count = 1;
        }
        const uint8_t image = ctx.Original<Fn>(0xcc5e40)(face);
        const uint8_t ours = reinterpret_cast<Fn>(ownVtable[17])(face);
        ++ctx.cases;
        const uint8_t expected = scenario == 3 ? 1 : 0;
        if (image != ours || image != expected)
            ctx.Fail("scenario %d: active query returned %u / %u, expected %u", scenario, image, ours, expected);
    }
    sentinel->next = sentinel->previous = sentinel;
    extension.mRegistry.count = 0;
}

// Shared body 0x3730d0 reads the low byte at object offset 0x40; it is deliberately not hooked.
DIFF_TEST(EffectsExtension_GetStateByte, 0x3730d0)
{
    cTSEffectsExtension extension;
    void *face = reinterpret_cast<uint8_t *>(&extension) + 0x18;
    using Fn = uint8_t(__thiscall *)(void *);
    const uint32_t values[] = {0, 0x100, 0x12345678, 0xff, 0xffffff80};
    for (uint32_t value : values) {
        extension.mUnknown40 = value;
        const uint8_t image = ctx.Original<Fn>(0x3730d0)(face);
        const uint8_t ours = extension.vf16();
        ++ctx.cases;
        if (image != ours || ours != static_cast<uint8_t>(value))
            ctx.Fail("state %08x: shared getter returned %02x / %02x", value, image, ours);
    }
}

// Slots 10 through 13 update the low bytes or forward their exact argument lists to each nonnull record instance.
DIFF_TEST(EffectsExtension_BroadcastSettings, 0xcc67e0)
{
    cTSEffectsExtension extension;
    void *face = reinterpret_cast<uint8_t *>(&extension) + 0x18;
    auto **ownVtable = *reinterpret_cast<void ***>(face);
    void **fakeVtable = static_cast<void **>(::operator new[](15 * sizeof(void *)));
    std::memset(fakeVtable, 0, 15 * sizeof(void *));
    fakeVtable[7] = shape::CodeOf(&FakeSlot7);
    fakeVtable[8] = shape::CodeOf(&FakeSlot8);
    fakeVtable[13] = shape::CodeOf(&FakeSlot13);
    fakeVtable[14] = shape::CodeOf(&FakeSlot14);
    FakeRef a{}, b{};
    a.vtable = b.vtable = fakeVtable;
    nGZSceneGraph::TSEffectRecord records[3]{};
    auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension.mRegistry.sentinel);
    auto **buckets = static_cast<nGZSceneGraph::TSEffectRecord **>(extension.mRegistry.buckets);
    records[0].instance = &a;
    records[1].instance = nullptr;
    records[2].instance = &b;
    sentinel->next = &records[0]; sentinel->previous = &records[2];
    records[0].previous = sentinel; records[0].next = &records[1];
    records[1].previous = &records[0]; records[1].next = &records[2];
    records[2].previous = &records[1]; records[2].next = sentinel;
    extension.mRegistry.count = 3;
    struct Side { FakeRef a, b; uint32_t state; } got[2]{};
    for (int side = 0; side != 2; ++side) {
        a = FakeRef{}; b = FakeRef{}; a.vtable = b.vtable = fakeVtable;
        extension.mUnknown40 = 0;
        using Setter = void(__thiscall *)(void *, uint32_t);
        for (uint32_t value : {0x100u, 1u, 1u, 0x100u}) {
            if (side == 0) ctx.Original<Setter>(0xcc67e0)(face, value);
            else reinterpret_cast<Setter>(ownVtable[10])(face, value);
        }
        for (uint32_t value : {0x100u, 1u, 1u, 0x100u}) {
            if (side == 0) ctx.Original<Setter>(0xcc6820)(face, value);
            else reinterpret_cast<Setter>(ownVtable[11])(face, value);
        }
        using Three = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
        using Two = void(__thiscall *)(void *, uint32_t, uint32_t);
        if (side == 0) {
            ctx.Original<Three>(0xcc6870)(face, 0x10203040, 0x50607080, 0x90a0b0c0);
            ctx.Original<Two>(0xcc68c0)(face, 0x0badcafe, 0x76543210);
        } else {
            reinterpret_cast<Three>(ownVtable[12])(face, 0x10203040, 0x50607080, 0x90a0b0c0);
            reinterpret_cast<Two>(ownVtable[13])(face, 0x0badcafe, 0x76543210);
        }
        got[side] = {a, b, *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(&extension) + 0x40)};
    }
    ++ctx.cases;
    if (std::memcmp(&got[0], &got[1], sizeof(got[0])) != 0 || got[0].a.calls8 != 2 || got[0].b.calls8 != 2 ||
        got[0].a.value8 != 0x100 || got[0].a.calls7 != 2 || got[0].a.value7 != 0x100 ||
        got[0].a.calls13 != 1 || got[0].a.args13[0] != 0x10203040 || got[0].a.args13[1] != 0x50607080 ||
        got[0].a.args13[2] != 0x90a0b0c0 || got[0].a.calls14 != 1 || got[0].a.args14[0] != 0x0badcafe ||
        got[0].a.args14[1] != 0x76543210 || got[0].b.calls13 != 1 || got[0].b.calls14 != 1 ||
        got[0].state != 0)
        ctx.Fail("settings broadcast differs: slot7 %u/%u, slot8 %u/%u, slot13 %u/%u, slot14 %u/%u, state %08x/%08x",
                 got[0].a.calls7, got[0].b.calls7, got[0].a.calls8, got[0].b.calls8,
                 got[0].a.calls13, got[0].b.calls13, got[0].a.calls14, got[0].b.calls14, got[0].state, got[1].state);
    sentinel->next = sentinel->previous = sentinel;
    extension.mRegistry.count = 0;
    ::operator delete[](fakeVtable);
}

// Slot 8 filters active records by kind, updates slot 4, clears/releases their instance and removes them.
DIFF_TEST(EffectsExtension_RemoveActive, 0xcc6760)
{
    shape::CodePatch gameNew(0x824632u, shape::CodeOf(+[](size_t n) -> void * { return ::operator new(n); }));
    shape::CodePatch gameDelete(0x824624u, shape::CodeOf(+[](void *p, size_t) { ::operator delete(p); }));
    cTSEffectsExtension extension;
    auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension.mRegistry.sentinel);
    auto **buckets = static_cast<nGZSceneGraph::TSEffectRecord **>(extension.mRegistry.buckets);
    void *refVtable[6]{};
    refVtable[2] = shape::CodeOf(&FakeRelease);
    refVtable[4] = shape::CodeOf(&FakeSetActive);
    refVtable[5] = shape::CodeOf(&FakeIsActive);
    FakeRef fake{refVtable, 2, 0, 0, 0, 1};
    struct Result { uint32_t count, releases, refs, setCalls, setValue, bucketIndex, staleBucket; bool freed, empty, bucketEmpty; size_t freeCount; size_t sizes[8]; } got[2][4]{};
    const uint32_t kinds[] = {2, 1, 1, 1};
    const uint8_t active[] = {1, 0, 1, 1};
    const uint8_t flags[] = {0, 0, 0, 1};
    for (int side = 0; side != 2; ++side) {
        for (unsigned scenario = 0; scenario != 4; ++scenario) {
            sentinel->next = sentinel->previous = sentinel;
            for (size_t i = 0; i < 16; i += 2) buckets[i] = buckets[i + 1] = sentinel;
            extension.mRegistry.count = 1;
            auto *record = ::new (::operator new(sizeof(nGZSceneGraph::TSEffectRecord))) nGZSceneGraph::TSEffectRecord{};
            record->firstText = "active effect";
            record->secondText = "active texture";
            record->hashValue = 5;
            record->kind = kinds[scenario];
            record->flag = flags[scenario];
            record->instance = &fake;
            using HashBytesFn = uint32_t(__cdecl *)(const char *, uint32_t);
            using HashIntFn = uint32_t(__cdecl *)(const uint32_t *);
            const uint32_t bucket = (ctx.Original<HashBytesFn>(0x4fea40)(record->firstText.data(),
                                        static_cast<uint32_t>(record->firstText.size())) |
                                     ctx.Original<HashBytesFn>(0x4fea40)(record->secondText.data(),
                                        static_cast<uint32_t>(record->secondText.size())) |
                                     ctx.Original<HashIntFn>(0x141a14)(&record->hashValue)) & extension.mRegistry.bucketMask;
            got[side][scenario].bucketIndex = bucket + 1;
            buckets[2 * bucket] = buckets[2 * bucket + 1] = record;
            sentinel->next = sentinel->previous = record;
            record->next = record->previous = sentinel;
            fake.active = active[scenario];
            fake.refs = 2; fake.releases = 0; fake.setCalls = 0; fake.setValue = 0;
            {
                difftest::FreeRecorder frees;
                if (side == 0)
                    ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xcc6760)(
                        reinterpret_cast<uint8_t *>(&extension) + 0x18, 1);
                else
                    extension.vf8(1);
                frees.Stop();
                got[side][scenario].freed = frees.Freed(record);
                got[side][scenario].freeCount = frees.SortedSizes(got[side][scenario].sizes, 8);
            }
            got[side][scenario].count = extension.mRegistry.count;
            got[side][scenario].releases = fake.releases;
            got[side][scenario].refs = fake.refs;
            got[side][scenario].setCalls = fake.setCalls;
            got[side][scenario].setValue = fake.setValue;
            got[side][scenario].empty = sentinel->next == sentinel && sentinel->previous == sentinel;
            got[side][scenario].bucketEmpty = buckets[2 * bucket] == sentinel && buckets[2 * bucket + 1] == sentinel;
            for (uint32_t i = 0; i < 8; ++i)
                if (buckets[2 * i] == record || buckets[2 * i + 1] == record) got[side][scenario].staleBucket = i + 1;
            if (!got[side][scenario].freed) {
                record->firstText.~basic_string();
                record->secondText.~basic_string();
                ::operator delete(record, sizeof(nGZSceneGraph::TSEffectRecord));
                sentinel->next = sentinel->previous = sentinel;
                buckets[2 * bucket] = buckets[2 * bucket + 1] = sentinel;
                extension.mRegistry.count = 0;
            }
        }
    }
    ++ctx.cases;
    if (std::memcmp(got[0], got[1], sizeof(got[0])) != 0 || got[0][0].freed || got[0][1].freed || !got[0][2].freed ||
        got[0][3].freed || got[0][0].setCalls || got[0][1].setCalls || got[0][2].setCalls != 1 ||
        got[0][2].setValue != 1 || got[0][2].releases != 1 || got[0][2].refs != 1 || got[0][2].count != 0 ||
        !got[0][2].empty || !got[0][2].bucketEmpty)
        ctx.Fail("RemoveActive filter or destruction differs: freed %d/%d/%d/%d, release %u, set %u, count %u, empty %d, bucket %d, stale %u/%u, expected %u/%u, sizes %u",
                 got[0][0].freed, got[0][1].freed, got[0][2].freed, got[0][3].freed,
                 got[0][2].releases, got[0][2].setCalls, got[0][2].count, got[0][2].empty,
                 got[0][2].bucketEmpty, got[0][2].staleBucket, got[1][2].staleBucket,
                 got[0][2].bucketIndex, got[1][2].bucketIndex,
                 static_cast<unsigned>(got[0][2].freeCount));
    sentinel->next = sentinel->previous = sentinel;
    extension.mRegistry.count = 0;
}

// Slot 7 treats its first argument as a low-byte flag: zero filters active records, nonzero delegates to slot 8.
DIFF_TEST(EffectsExtension_FilterActive, 0xcc6900)
{
    shape::CodePatch gameNew(0x824632u, shape::CodeOf(+[](size_t n) -> void * { return ::operator new(n); }));
    shape::CodePatch gameDelete(0x824624u, shape::CodeOf(+[](void *p, size_t) { ::operator delete(p); }));
    cTSEffectsExtension extension;
    auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension.mRegistry.sentinel);
    auto **buckets = static_cast<nGZSceneGraph::TSEffectRecord **>(extension.mRegistry.buckets);
    void *refVtable[6]{};
    refVtable[2] = shape::CodeOf(&FakeRelease);
    refVtable[4] = shape::CodeOf(&FakeSetActive);
    refVtable[5] = shape::CodeOf(&FakeIsActive);
    FakeRef fake{refVtable, 2, 0, 0, 0, 1};
    void *face = reinterpret_cast<uint8_t *>(&extension) + 0x18;
    auto **ownVtable = *reinterpret_cast<void ***>(face);
    void **const originalVtable = ctx.image.At<void *>(cTSEffectsExtension::kEffectsVTable);
    using RawFilter = void(__thiscall *)(void *, uint32_t, int32_t);
    struct Result { bool freed; uint32_t setCalls, setValue, releases, refs, count; bool empty; } got[2][6]{};
    const uint32_t flags[] = {0, 0x100, 0, 1, 2, 0x201};
    const int32_t kinds[] = {1, 1, 2, 2, 2, 2};
    for (int side = 0; side != 2; ++side) {
        for (unsigned scenario = 0; scenario != 6; ++scenario) {
            sentinel->next = sentinel->previous = sentinel;
            for (size_t i = 0; i < 16; i += 2) buckets[i] = buckets[i + 1] = sentinel;
            extension.mRegistry.count = 1;
            auto *record = ::new (::operator new(sizeof(nGZSceneGraph::TSEffectRecord))) nGZSceneGraph::TSEffectRecord{};
            record->firstText = "filter effect";
            record->secondText = "filter texture";
            record->hashValue = 0x31;
            record->kind = 2;
            record->instance = &fake;
            using HashBytesFn = uint32_t(__cdecl *)(const char *, uint32_t);
            using HashIntFn = uint32_t(__cdecl *)(const uint32_t *);
            const uint32_t bucket = (ctx.Original<HashBytesFn>(0x4fea40)(record->firstText.data(),
                                        static_cast<uint32_t>(record->firstText.size())) |
                                     ctx.Original<HashBytesFn>(0x4fea40)(record->secondText.data(),
                                        static_cast<uint32_t>(record->secondText.size())) |
                                     ctx.Original<HashIntFn>(0x141a14)(&record->hashValue)) & extension.mRegistry.bucketMask;
            buckets[2 * bucket] = buckets[2 * bucket + 1] = record;
            sentinel->next = sentinel->previous = record;
            record->next = record->previous = sentinel;
            fake.refs = 2; fake.releases = 0; fake.setCalls = 0; fake.setValue = 0;
            {
                difftest::FreeRecorder frees;
                if (side == 0) {
                    *reinterpret_cast<void ***>(face) = originalVtable;
                    ctx.Original<RawFilter>(0xcc6900)(face, flags[scenario], kinds[scenario]);
                    *reinterpret_cast<void ***>(face) = ownVtable;
                } else reinterpret_cast<RawFilter>(ownVtable[7])(face, flags[scenario], kinds[scenario]);
                frees.Stop();
                got[side][scenario].freed = frees.Freed(record);
            }
            got[side][scenario].setCalls = fake.setCalls;
            got[side][scenario].setValue = fake.setValue;
            got[side][scenario].releases = fake.releases;
            got[side][scenario].refs = fake.refs;
            got[side][scenario].count = extension.mRegistry.count;
            got[side][scenario].empty = sentinel->next == sentinel && sentinel->previous == sentinel;
            if (!got[side][scenario].freed) {
                record->firstText.~basic_string(); record->secondText.~basic_string();
                ::operator delete(record, sizeof(nGZSceneGraph::TSEffectRecord));
                sentinel->next = sentinel->previous = sentinel;
                buckets[2 * bucket] = buckets[2 * bucket + 1] = sentinel;
                extension.mRegistry.count = 0;
            }
        }
    }
    ++ctx.cases;
    if (std::memcmp(got[0], got[1], sizeof(got[0])) != 0 || got[0][0].setCalls || got[0][1].setCalls ||
        got[0][2].setCalls != 1 || got[0][2].setValue != 0 || got[0][2].freed || !got[0][3].freed ||
        got[0][3].setValue != 1 || got[0][3].releases != 1 || got[0][3].refs != 1 ||
        got[0][3].count != 0 || !got[0][3].empty ||
        !got[0][4].freed || got[0][4].setValue != 1 || got[0][4].releases != 1 || got[0][4].count != 0 ||
        !got[0][5].freed || got[0][5].setValue != 1 || got[0][5].releases != 1 || got[0][5].count != 0)
        ctx.Fail("FilterActive low-byte/filter/delegate paths differ: callbacks %u/%u/%u/%u, frees %d/%d/%d/%d",
                 got[0][0].setCalls, got[0][1].setCalls, got[0][2].setCalls, got[0][3].setCalls,
                 got[0][0].freed, got[0][1].freed, got[0][2].freed, got[0][3].freed);
    sentinel->next = sentinel->previous = sentinel;
    extension.mRegistry.count = 0;
}

// Slot 14 clears the bound byte and only dispatches slot 9 when it was set.
DIFF_TEST(EffectsExtension_Unbind, 0xcc6990)
{
    using Fn = void(__thiscall *)(void *);
    using HashBytesFn = uint32_t(__cdecl *)(const char *, uint32_t);
    using HashIntFn = uint32_t(__cdecl *)(const uint32_t *);
    shape::CodePatch gameDelete(0x824624u, shape::CodeOf(+[](void *p, size_t) { ::operator delete(p); }));
    cTSEffectsExtension image, ours;
    auto *imageFace = reinterpret_cast<uint8_t *>(&image) + 0x18;
    auto *oursFace = reinterpret_cast<uint8_t *>(&ours) + 0x18;
    auto **imageOwnVtable = *reinterpret_cast<void ***>(imageFace);
    auto **oursVtable = *reinterpret_cast<void ***>(oursFace);
    void **const originalVtable = ctx.image.At<void *>(cTSEffectsExtension::kEffectsVTable);
    auto *imageBound = reinterpret_cast<uint8_t *>(&image) + 0x40;
    auto *oursBound = reinterpret_cast<uint8_t *>(&ours) + 0x40;
    for (uint8_t initial : {uint8_t{0}, uint8_t{1}, uint8_t{0x7f}}) {
        *imageBound = *oursBound = initial;
        *reinterpret_cast<void ***>(imageFace) = originalVtable;
        ctx.Original<Fn>(0xcc6990)(imageFace);
        *reinterpret_cast<void ***>(imageFace) = imageOwnVtable;
        reinterpret_cast<Fn>(oursVtable[14])(oursFace);
        ++ctx.cases;
        if (*imageBound != *oursBound || *oursBound != 0)
            ctx.Fail("initial bound state %u became %u / %u", initial, *imageBound, *oursBound);
    }
    void *instanceVtable[15]{};
    instanceVtable[4] = shape::CodeOf(&FakeSetActive);
    instanceVtable[14] = shape::CodeOf(&FakeSlot14);
    FakeRef imageFake{}; imageFake.vtable = instanceVtable;
    FakeRef oursFake{}; oursFake.vtable = instanceVtable;
    auto prepare = [&](cTSEffectsExtension &extension, void *instance) {
        auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension.mRegistry.sentinel);
        auto **buckets = static_cast<nGZSceneGraph::TSEffectRecord **>(extension.mRegistry.buckets);
        for (size_t i = 0; i < 16; i += 2) buckets[i] = buckets[i + 1] = sentinel;
        auto *record = ::new (::operator new(sizeof(nGZSceneGraph::TSEffectRecord))) nGZSceneGraph::TSEffectRecord{};
        auto *removed = ::new (::operator new(sizeof(nGZSceneGraph::TSEffectRecord))) nGZSceneGraph::TSEffectRecord{};
        for (auto *r : {record, removed}) {
            r->firstText = "bound effect";
            r->secondText = "bound texture";
            r->hashValue = 0x47;
        }
        record->kind = 1; // vf9(0) skips this item, then unbind notifies it.
        record->instance = instance;
        removed->kind = 0; // vf9(0) removes an uninstantiated item before notifications.
        removed->instance = nullptr;
        record->next = removed; record->previous = sentinel;
        removed->next = sentinel; removed->previous = record;
        sentinel->next = record; sentinel->previous = removed;
        const uint32_t bucket = (ctx.Original<HashBytesFn>(0x4fea40)(record->firstText.data(),
                                     static_cast<uint32_t>(record->firstText.size())) |
                                 ctx.Original<HashBytesFn>(0x4fea40)(record->secondText.data(),
                                     static_cast<uint32_t>(record->secondText.size())) |
                                 ctx.Original<HashIntFn>(0x141a14)(&record->hashValue)) & extension.mRegistry.bucketMask;
        buckets[2 * bucket] = record;
        buckets[2 * bucket + 1] = removed;
        extension.mRegistry.count = 2;
        return std::pair<nGZSceneGraph::TSEffectRecord *, nGZSceneGraph::TSEffectRecord *>(record, removed);
    };
    auto imageRecords = prepare(image, &imageFake);
    auto oursRecords = prepare(ours, &oursFake);
    *imageBound = *oursBound = 0;
    *reinterpret_cast<void ***>(imageFace) = originalVtable;
    ctx.Original<Fn>(0xcc6990)(imageFace);
    *reinterpret_cast<void ***>(imageFace) = imageOwnVtable;
    reinterpret_cast<Fn>(oursVtable[14])(oursFace);
    ++ctx.cases;
    if (image.mRegistry.count != 2 || ours.mRegistry.count != 2 || imageFake.setCalls || oursFake.setCalls ||
        imageFake.calls14 || oursFake.calls14 || *imageBound || *oursBound)
        ctx.Fail("unbound state with records should skip both inner processing and notifications");
    *imageBound = *oursBound = 1;
    *reinterpret_cast<void ***>(imageFace) = originalVtable;
    bool imageRemovedFreed;
    {
        difftest::FreeRecorder frees;
        ctx.Original<Fn>(0xcc6990)(imageFace);
        frees.Stop();
        imageRemovedFreed = frees.Freed(imageRecords.second);
    }
    *reinterpret_cast<void ***>(imageFace) = imageOwnVtable;
    bool oursRemovedFreed;
    {
        difftest::FreeRecorder frees;
        reinterpret_cast<Fn>(oursVtable[14])(oursFace);
        frees.Stop();
        oursRemovedFreed = frees.Freed(oursRecords.second);
    }
    ++ctx.cases;
    if (*imageBound != 0 || *oursBound != 0 || imageFake.setCalls != 1 || oursFake.setCalls != 1 ||
        imageFake.setValue != 1 || oursFake.setValue != 1 || imageFake.calls14 != 1 || oursFake.calls14 != 1 ||
        imageFake.args14[0] != 2 || oursFake.args14[0] != 2 || imageFake.args14[1] != 0 || oursFake.args14[1] != 0 ||
        image.mRegistry.count != ours.mRegistry.count || image.mRegistry.count != 1 ||
        !imageRemovedFreed || !oursRemovedFreed || imageRecords.second == imageRecords.first ||
        imageRecords.first->next != static_cast<nGZSceneGraph::TSEffectRecord *>(image.mRegistry.sentinel) ||
        oursRecords.first->next != static_cast<nGZSceneGraph::TSEffectRecord *>(ours.mRegistry.sentinel))
        ctx.Fail("unbind did not process null records then notify each surviving instance");
    for (auto *extension : {&image, &ours}) {
        auto *sentinel = static_cast<nGZSceneGraph::TSEffectRecord *>(extension->mRegistry.sentinel);
        sentinel->next = sentinel->previous = sentinel;
        extension->mRegistry.count = 0;
    }
    delete imageRecords.first;
    delete oursRecords.first;
    *imageBound = *oursBound = 0;
}

// ======================================================================================================================
// Service-backed slots: 1 (case 4), 2, 18, 22, with the framework / common system / effects manager chain faked and the
// original and ours run on identical preconditions. A run's text (records, bucket edges, instances, call order, manager
// requests, node lookups) is compared between the two and then checked against what the original must have done.
// ======================================================================================================================
namespace fx {

constexpr unsigned kPool = 32;
constexpr uintptr_t kOwnerValue = 0xa0001000u, kFoundValue = 0xb0002000u;

FakeRef gPool[kPool];
void *gPoolVtable[16];
unsigned gPoolNext = 0;
std::string gSteps;
bool gManagerAnswers = true;
char gRequested[8][40];
unsigned gRequests = 0, gFrameworkCalls = 0, gCommonReleases = 0;
uint32_t gFrameworkClass = 0, gFrameworkIid = 0;
void *gFrameworkObject[2], *gCommonObject[2], *gManagerObject[2];
void *gFrameworkVtable[8], *gCommonVtable[8], *gManagerVtable[16];
std::vector<std::string> gSprintfLog, gFreedTexts;

uint32_t __fastcall PoolAddRef(void *o, void *) { gSteps += 'a'; auto *f = static_cast<FakeRef *>(o); ++f->calls1; return ++f->refs; }
uint32_t __fastcall PoolRelease(void *o, void *) { gSteps += 'r'; auto *f = static_cast<FakeRef *>(o); ++f->releases; return --f->refs; }
void __fastcall PoolStart(void *o, void *, uint32_t v) { gSteps += '3'; auto *f = static_cast<FakeRef *>(o); ++f->calls3; f->value3 = v; }
void __fastcall PoolStop(void *o, void *, uint32_t v) { gSteps += '4'; auto *f = static_cast<FakeRef *>(o); ++f->setCalls; f->setValue = v; }
uint8_t __fastcall PoolIsActive(void *o, void *) { gSteps += '?'; return static_cast<FakeRef *>(o)->active; }
uint8_t __fastcall PoolIsReady(void *o, void *) { return static_cast<FakeRef *>(o)->ready; }
void __fastcall PoolSlot7(void *o, void *, uint32_t v) { gSteps += '7'; auto *f = static_cast<FakeRef *>(o); ++f->calls7; f->value7 = v; }
void __fastcall PoolSlot8(void *o, void *, uint32_t v) { gSteps += '8'; auto *f = static_cast<FakeRef *>(o); ++f->calls8; f->value8 = v; }
void __fastcall PoolAttach(void *o, void *, uint32_t a, void *b)
{
    gSteps += 'E';
    auto *f = static_cast<FakeRef *>(o);
    ++f->calls14; f->args14[0] = a; f->args14[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(b));
}

uint8_t __fastcall FrameworkGetService(void *, void *, uint32_t cls, uint32_t iid, void **out)
{
    gSteps += 'F'; ++gFrameworkCalls; gFrameworkClass = cls; gFrameworkIid = iid;
    *out = gCommonObject;
    return 1;
}
void *__fastcall CommonEffects(void *, void *) { gSteps += 'C'; return gManagerObject; }
uint32_t __fastcall CommonRelease(void *, void *) { gSteps += 'c'; ++gCommonReleases; return 1; }
uint8_t __fastcall ManagerRequest(void *, void *, const char *name, void **out)
{
    gSteps += 'M';
    if (gRequests < 8) { std::strncpy(gRequested[gRequests], name, 39); gRequested[gRequests][39] = 0; }
    ++gRequests;
    if (!gManagerAnswers || gPoolNext == kPool) return 0;
    FakeRef *f = &gPool[gPoolNext++];
    f->refs = 1;
    *out = f;
    return 1;
}

cRZString *__cdecl FakeSprintf(cRZString *self, const char *fmt, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(buffer, sizeof buffer, _TRUNCATE, fmt, args);
    va_end(args);
    gSprintfLog.push_back(buffer);
    self->mString.assign(buffer);
    return self;
}
void __cdecl GameDeletePlain(void *p, size_t) { ::operator delete(p); }
void __cdecl GameDeleteCapture(void *p, size_t size)
{
    if (p && size) {
        const char *text = static_cast<const char *>(p);
        const char *end = static_cast<const char *>(std::memchr(text, 0, size));
        gFreedTexts.emplace_back(text, end ? static_cast<size_t>(end - text) : size);
    }
    ::operator delete(p);
}
void *__cdecl GameNewPlain(size_t n) { return ::operator new(n); }

void Reset()
{
    gSteps.clear();
    gSteps.reserve(512);
    gManagerAnswers = true;
    std::memset(gRequested, 0, sizeof gRequested);
    gRequests = gFrameworkCalls = gCommonReleases = 0;
    gFrameworkClass = gFrameworkIid = 0;
    gPoolNext = 0;
    gSprintfLog.clear(); gFreedTexts.clear();
    std::memset(gPoolVtable, 0, sizeof gPoolVtable);
    gPoolVtable[1] = shape::CodeOf(&PoolAddRef);
    gPoolVtable[2] = shape::CodeOf(&PoolRelease);
    gPoolVtable[3] = shape::CodeOf(&PoolStart);
    gPoolVtable[4] = shape::CodeOf(&PoolStop);
    gPoolVtable[5] = shape::CodeOf(&PoolIsActive);
    gPoolVtable[6] = shape::CodeOf(&PoolIsReady);
    gPoolVtable[7] = shape::CodeOf(&PoolSlot7);
    gPoolVtable[8] = shape::CodeOf(&PoolSlot8);
    gPoolVtable[14] = shape::CodeOf(&PoolAttach);
    for (FakeRef &p : gPool) { p = FakeRef{}; p.vtable = gPoolVtable; p.active = 1; p.ready = 0; }
    std::memset(gFrameworkVtable, 0, sizeof gFrameworkVtable);
    std::memset(gCommonVtable, 0, sizeof gCommonVtable);
    std::memset(gManagerVtable, 0, sizeof gManagerVtable);
    gFrameworkVtable[5] = shape::CodeOf(&FrameworkGetService);
    gCommonVtable[2] = shape::CodeOf(&CommonRelease);
    gCommonVtable[4] = shape::CodeOf(&CommonEffects);
    gManagerVtable[14] = shape::CodeOf(&ManagerRequest);
    gFrameworkObject[0] = gFrameworkVtable; gFrameworkObject[1] = nullptr;
    gCommonObject[0] = gCommonVtable; gCommonObject[1] = nullptr;
    gManagerObject[0] = gManagerVtable; gManagerObject[1] = nullptr;
    gNodeLookup = {};
    gNodeLookupResult = reinterpret_cast<void *>(kFoundValue);
}

// The fakes installed for a run: the game's allocator, the node lookup and the service object (game global 0x10c9378).
struct Scope {
    shape::CodePatch gameNew;
    shape::CodePatch gameDelete;
    shape::CodePatch findNode;
    void **slot;
    void *saved;
    Scope(difftest::Context &ctx, bool captureFreedText = false)
        : gameNew(0x824632u, shape::CodeOf(&GameNewPlain)),
          gameDelete(0x824624u, captureFreedText ? shape::CodeOf(&GameDeleteCapture) : shape::CodeOf(&GameDeletePlain)),
          findNode(0xae7490u, shape::CodeOf(&StandFindNodeByName)),
          slot(ctx.image.At<void *>(0x10c9378)), saved(*slot)
    {
        Reset();
        *slot = gFrameworkObject;
    }
    ~Scope() { *slot = saved; }
};

int PoolIndex(const void *p)
{
    if (!p) return -1;
    for (unsigned i = 0; i != kPool; ++i)
        if (p == &gPool[i]) return static_cast<int>(i);
    return -2;
}

void ClearRegistry(cTSEffectsExtension &e)
{
    auto *sentinel = static_cast<TSEffectRecord *>(e.mRegistry.sentinel);
    auto **buckets = static_cast<TSEffectRecord **>(e.mRegistry.buckets);
    for (auto *r = sentinel->next; r != sentinel;) {
        auto *next = r->next;
        r->firstText.~basic_string();
        r->secondText.~basic_string();
        ::operator delete(r, sizeof(TSEffectRecord));
        r = next;
    }
    sentinel->next = sentinel->previous = sentinel;
    for (size_t i = 0; i < 16; i += 2) buckets[i] = buckets[i + 1] = sentinel;
    e.mRegistry.count = 0;
}

// An extension whose face can run the original (the image's face vtable on it for the call) or ours.
struct Side {
    cTSEffectsExtension ext;
    void *face;
    void **own;
    void **orig;
    explicit Side(difftest::Context &ctx)
        : face(reinterpret_cast<uint8_t *>(&ext) + 0x18), own(*reinterpret_cast<void ***>(face)),
          orig(ctx.image.At<void *>(cTSEffectsExtension::kEffectsVTable))
    {
        ext.mOwner = reinterpret_cast<nGZSceneGraph::cIObjectGraphNode *>(kOwnerValue);
    }
    ~Side() { ClearRegistry(ext); }
    uint8_t &Byte(unsigned offset) { return *(reinterpret_cast<uint8_t *>(&ext) + offset); }
    template <class Fn, class... A> auto Call(bool original, unsigned slot, A... a)
    {
        struct Restore {
            void *face; void **own;
            ~Restore() { *reinterpret_cast<void ***>(face) = own; }
        } restore{face, own};
        void **vt = original ? orig : own;
        *reinterpret_cast<void ***>(face) = vt;
        return reinterpret_cast<Fn>(vt[slot])(face, a...);
    }
};

unsigned PositionOf(TSEffectRecord *sentinel, TSEffectRecord *r)
{
    if (r == sentinel) return 99;
    unsigned n = 0;
    for (auto *p = sentinel->next; p != sentinel && n < 64; p = p->next, ++n)
        if (p == r) return n;
    return 98;
}

std::string Snapshot(cTSEffectsExtension &e, bool withFlags = true)
{
    std::string s;
    char b[320];
    auto add = [&](const char *fmt, auto... a) { std::snprintf(b, sizeof b, fmt, a...); s += b; };
    auto *sentinel = static_cast<TSEffectRecord *>(e.mRegistry.sentinel);
    auto **buckets = static_cast<TSEffectRecord **>(e.mRegistry.buckets);
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(&e);
    add("count=%u bound=%u s41=%u s42=%u dbg=%u ", e.mRegistry.count, raw[0x40], raw[0x41], raw[0x42], raw[0x43]);
    unsigned n = 0;
    for (auto *r = sentinel->next; r != sentinel && n < 64; r = r->next, ++n)
        add("[%s|%s|%08x inst%d k%u f%u]", r->firstText.c_str(), r->secondText.c_str(), r->hashValue,
            PoolIndex(r->instance), r->kind, withFlags ? static_cast<unsigned>(r->flag) : 0u);
    add(" buckets");
    for (unsigned i = 0; i != 8; ++i)
        add(" %u:%u-%u", i, PositionOf(sentinel, buckets[2 * i]), PositionOf(sentinel, buckets[2 * i + 1]));
    for (unsigned i = 0; i != gPoolNext; ++i) {
        const FakeRef &p = gPool[i];
        add(" {p%u r%u rel%u a%u 3:%u/%u 4:%u/%u 7:%u/%x 8:%u/%x E:%u/%x/%08x}", i, p.refs, p.releases, p.calls1,
            p.calls3, p.value3, p.setCalls, p.setValue, p.calls7, p.value7, p.calls8, p.value8, p.calls14,
            p.args14[0], p.args14[1]);
    }
    add(" steps=%s req=%u:", gSteps.c_str(), gRequests);
    for (unsigned i = 0; i < gRequests && i < 8; ++i) add("%s,", gRequested[i]);
    add(" fw=%u/%08x/%08x cr=%u lookup=%u/%s/%08x/%d", gFrameworkCalls, gFrameworkClass, gFrameworkIid, gCommonReleases,
        gNodeLookup.calls, gNodeLookup.calls ? gNodeLookup.name : "-",
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gNodeLookup.node)), gNodeLookup.limit);
    return s;
}

// ---- a real stream (cRZSerializableStream, the image's code) ----
using StreamCtor = void *(__thiscall *)(void *, uint32_t);
using StreamDtor = void(__thiscall *)(void *);
using PutU32 = uint32_t(__thiscall *)(void *, uint32_t);
using PutText = uint32_t(__thiscall *)(void *, const void *);
using PutByte = uint32_t(__thiscall *)(void *, int8_t);

struct Rec { const char *a, *b; uint32_t disc, kind; uint8_t flag; };

struct Stream {
    difftest::Context &ctx;
    uint8_t *object;
    explicit Stream(difftest::Context &c) : ctx(c), object(static_cast<uint8_t *>(::operator new(0x38)))
    {
        std::memset(object, 0, 0x38);
        ctx.Original<StreamCtor>(0x9cbcf3)(object, 1);
    }
    ~Stream() { ctx.Original<StreamDtor>(0x9cbd43)(object); ::operator delete(object); }
    void *Out() { return object; }
    void *In() { return object + 0x10; }
    void **Vt(void *face) { return *reinterpret_cast<void ***>(face); }
    void U16(uint32_t v) { reinterpret_cast<PutU32>(Vt(Out())[7])(Out(), v); }
    void U32(uint32_t v) { reinterpret_cast<PutU32>(Vt(Out())[9])(Out(), v); }
    void Byte(uint8_t v) { reinterpret_cast<PutByte>(Vt(Out())[5])(Out(), static_cast<int8_t>(v)); }
    void Text(const char *t)
    {
        cRZString s;
        s.mVTable = cRZString::VTable();
        s.mString.assign(t);
        s.mRefCount = 0;
        reinterpret_cast<PutText>(Vt(Out())[15])(Out(), &s);
    }
    uint32_t Size() const { return *reinterpret_cast<const uint32_t *>(object + 0x24); }
    uint32_t Position() const { return *reinterpret_cast<const uint32_t *>(object + 0x2c); }
    std::string Bytes() const
    {
        std::string out;
        const uint8_t *data = *reinterpret_cast<uint8_t *const *>(object + 0x34);
        char b[4];
        for (uint32_t i = 0; i < Size(); ++i) { std::snprintf(b, sizeof b, "%02x", data[i]); out += b; }
        return out;
    }
    void Fill(uint32_t version, uint32_t count, const std::vector<Rec> &records)
    {
        U16(version);
        U32(count);
        for (const Rec &r : records) {
            Text(r.a); Text(r.b); U32(r.disc); U32(r.kind);
            if (version > 1) Byte(r.flag);
        }
    }
};

} // namespace fx

namespace {

// ---- slot 2 ----
std::string CreateRun(difftest::Context &ctx, bool original, std::vector<int> &rets, int variant)
{
    fx::Scope scope(ctx);
    fx::Side s(ctx);
    using Fn = void *(__thiscall *)(void *, uint32_t, const char *, const char *, uint32_t, uint32_t);
    auto create = [&](uint32_t kind, const char *name, const char *node, uint32_t disc, uint32_t flag) {
        rets.push_back(fx::PoolIndex(s.Call<Fn>(original, 2, kind, name, node, disc, flag)));
    };
    s.Byte(0x41) = 9;
    s.Byte(0x42) = 7;
    if (variant == 1) {
        s.Byte(0x40) = 5;
        create(1, "b", "", 1, 0);
        return fx::Snapshot(s.ext);
    }
    create(3, "spark", "", 0x77, 0x101);              // new: bound byte 0 -> 1 with the first record
    create(3, "spark", "", 0x77, 1);                  // the same key: nothing
    create(4, "spark", "locator", 0x78, 0x100);       // "locator": the owner node, no lookup
    create(5, "glow", "Hand", 0x79, 0);               // a named node
    s.Byte(0x40) = 0;
    create(2, "other", "", 0x7a, 2);                  // records exist: the bound byte stays 0
    s.Byte(0x43) = 1;
    create(2, "dbg", "", 0x7b, 3);                    // the debug byte: the factory is asked for "debugEffect"
    gNodeLookupResult = nullptr;
    create(2, "lost", "Missing", 1, 0);               // no node: nothing, no request
    gNodeLookupResult = reinterpret_cast<void *>(fx::kFoundValue);
    fx::gManagerAnswers = false;
    create(2, "refused", "", 0x7c, 0);                // the manager makes nothing
    return fx::Snapshot(s.ext);
}

bool Contains(const std::string &text, const char *part) { return text.find(part) != std::string::npos; }

} // namespace

DIFF_TEST(EffectsExtension_CreateEffect, 0xcc5480)
{
    for (int variant = 0; variant != 2; ++variant) {
        std::vector<int> imageRets, oursRets;
        const std::string image = CreateRun(ctx, true, imageRets, variant);
        const std::string ours = CreateRun(ctx, false, oursRets, variant);
        ++ctx.cases;
        if (image != ours || imageRets != oursRets) {
            ctx.Fail("CreateEffect variant %d differs:\n  image %s\n  ours  %s", variant, image.c_str(), ours.c_str());
            continue;
        }
        bool ok;
        if (variant == 0)
            ok = imageRets == std::vector<int>{0, -1, 1, 2, 3, 4, -1, -1} && Contains(image, "count=5 bound=0 ") &&
                 Contains(image, "[spark||00000077 inst0 k3 f1]") && Contains(image, "[spark|locator|00000078 inst1 k4 f0]") &&
                 Contains(image, "[glow|Hand|00000079 inst2 k5 f0]") && Contains(image, "[other||0000007a inst3 k2 f2]") &&
                 Contains(image, "[dbg||0000007b inst4 k2 f3]") &&
                 Contains(image, "req=6:spark,spark,glow,other,debugEffect,debugEffect,") &&
                 Contains(image, "lookup=2/Missing/") && Contains(image, " 7:1/7 8:1/9 E:1/2/a0001000}") &&
                 Contains(image, " E:1/2/b0002000}") && Contains(image, "fw=6/8af76dc8/4af76d8c cr=6");
        else
            ok = imageRets == std::vector<int>{0} && Contains(image, "count=1 bound=5 ") &&
                 Contains(image, "[b||00000001 inst0 k1 f0]");
        if (!ok) ctx.Fail("CreateEffect variant %d: the original did not do what the test expects: ...%s", variant, image.c_str() + (image.size() > 330 ? image.size() - 330 : 0));
    }
}

// ---- slot 1, operation 4 ----
DIFF_TEST(EffectsExtension_DispatchStandalone, 0xcc5ef0)
{
    auto run = [&](bool original, std::vector<int> &rets) {
        fx::Scope scope(ctx);
        fx::Side s(ctx);
        using Fn = uint8_t(__thiscall *)(void *, uint32_t, const char *, const char *, uint32_t, uint32_t, uint32_t);
        s.Byte(0x41) = 9;
        s.Byte(0x42) = 7;
        auto go = [&](const char *name, const char *node) {
            rets.push_back(s.Call<Fn>(original, 1, 4u, name, node, 0x11u, 0x22u, 0x33u));
        };
        go("burst", "");
        go("burst2", "Hand");
        gNodeLookupResult = nullptr;
        go("x", "Missing");
        gNodeLookupResult = reinterpret_cast<void *>(fx::kFoundValue);
        fx::gManagerAnswers = false;
        go("refused", "");
        return fx::Snapshot(s.ext);
    };
    std::vector<int> imageRets, oursRets;
    const std::string image = run(true, imageRets), ours = run(false, oursRets);
    ++ctx.cases;
    if (image != ours || imageRets != oursRets)
        ctx.Fail("Dispatch case 4 differs:\n  image %s\n  ours  %s", image.c_str(), ours.c_str());
    else if (imageRets != std::vector<int>{1, 1, 0, 0} || !Contains(image, "count=0 ") ||
             !Contains(image, "req=3:burst,burst2,refused,") || !Contains(image, "ME783r") ||
             !Contains(image, " E:1/2/a0001000 ") && !Contains(image, "E:1/2/a0001000}") ||
             !Contains(image, "E:1/2/b0002000}") || !Contains(image, "{p0 r0 rel1 a0 3:1/0 4:0/0 7:1/7 8:1/9"))
        ctx.Fail("Dispatch case 4: the original did not do what the test expects: %s", image.c_str());
}

// ---- slot 18: what the original reads, and what the write side of the class makes of it ----
namespace {

struct ReadCase {
    const char *label;
    uint32_t version, count;
    std::vector<fx::Rec> records;
    bool answers, ignoreFlag;
    std::vector<const char *> expect;
};

std::string ReadRun(difftest::Context &ctx, bool original, const ReadCase &c)
{
    fx::Scope scope(ctx);
    fx::gManagerAnswers = c.answers;
    fx::Side s(ctx);
    s.Byte(0x41) = 9;
    s.Byte(0x42) = 7;
    fx::Stream stream(ctx);
    stream.Fill(c.version, c.count, c.records);
    s.Call<void(__thiscall *)(void *, void *)>(original, 18, stream.In());
    char b[64];
    std::snprintf(b, sizeof b, " pos=%u/%u", stream.Position(), stream.Size());
    return fx::Snapshot(s.ext, !c.ignoreFlag) + b;
}

} // namespace

DIFF_TEST(EffectsExtension_ReadRecords, 0xcc61c0)
{
    const fx::Rec a{"fx_a", "tex_a", 5, 0, 1}, b{"fx_b", "tex_b", 6, 2, 0x80};
    const std::vector<ReadCase> cases = {
        {"two records", 2, 2, {a, b}, true, false,
         {"count=2 ", "[fx_a|tex_a|00000005 inst0 k0 f1]", "[fx_b|tex_b|00000006 inst1 k2 f128]", "req=2:fx_a,fx_b,",
          " 7:1/7 8:1/9", "fw=1/8af76dc8/4af76d8c cr=1"}},
        {"duplicate", 2, 3, {a, a, b}, true, false, {"count=2 ", "req=2:fx_a,fx_b,", "{p0 r1 rel2 a2 "}},
        {"version 1", 1, 1, {{"fx_a", "tex_a", 5, 3, 0}}, true, true, {"count=1 ", "[fx_a|tex_a|00000005 inst0 k3 f0]"}},
        {"version 0", 0, 2, {a, b}, true, false, {"count=0 ", "req=0:"}},
        {"version 3", 3, 2, {a, b}, true, false, {"count=0 ", "req=0:"}},
        {"count 0", 2, 0, {}, true, false, {"count=0 ", "req=0:"}},
        {"count 1000", 2, 1000, {}, true, false, {"count=0 ", "req=0:"}},
        {"manager refuses", 2, 2, {a, b}, false, false, {"count=0 ", "req=2:fx_a,fx_b,"}},
        {"kind 1", 2, 1, {{"fx_k", "tex_k", 9, 1, 0}}, true, false, {"count=1 ", "[fx_k|tex_k|00000009 inst0 k1 f0]"}},
        // Keys that share two of their three parts: the hash ORs the parts, so these fall in one bucket, where only the
        // comparison tells them apart.
        {"other discriminators", 2, 6,
         {{"fx_a", "tex_a", 1, 0, 0},
          {"fx_a", "tex_a", 2, 0, 0},
          {"fx_a", "tex_a", 3, 0, 0},
          {"fx_a", "tex_a", 4, 0, 0},
          {"fx_a", "tex_a", 5, 0, 0},
          {"fx_a", "tex_a", 6, 0, 0}},
         true, false, {"count=6 "}},
        {"other second texts", 2, 6,
         {{"fx_a", "tex_1", 5, 0, 0},
          {"fx_a", "tex_2", 5, 0, 0},
          {"fx_a", "tex_3", 5, 0, 0},
          {"fx_a", "tex_4", 5, 0, 0},
          {"fx_a", "tex_5", 5, 0, 0},
          {"fx_a", "tex_6", 5, 0, 0}},
         true, false, {"count=6 "}},
    };
    for (const ReadCase &c : cases) {
        const std::string image = ReadRun(ctx, true, c), ours = ReadRun(ctx, false, c);
        ++ctx.cases;
        if (image != ours) {
            ctx.Fail("ReadRecords '%s' differs:\n  image %s\n  ours  %s", c.label, image.c_str(), ours.c_str());
            continue;
        }
        for (const char *part : c.expect)
            if (!Contains(image, part)) {
                ctx.Fail("ReadRecords '%s': the original's result lacks '%s': %s", c.label, part, image.c_str());
                break;
            }
    }
}

// The destructor over a filled registry (the rest of this file clears it first): each record's key strings, which these
// long texts keep on the heap, its instance's reference and its node go with it.
namespace {

struct DestroyedRegistry {
    uint32_t count = 0, releases = 0, frees = 0;
    size_t sizes[24] = {};
    bool operator==(const DestroyedRegistry &o) const
    {
        return count == o.count && releases == o.releases && frees == o.frees &&
               std::equal(std::begin(sizes), std::end(sizes), std::begin(o.sizes));
    }
};

DestroyedRegistry DestroyFilled(difftest::Context &ctx, bool original)
{
    fx::Scope scope(ctx);
    fx::gManagerAnswers = true;
    // Not an fx::Side on the stack: its destructor would clear the registry first.
    alignas(fx::Side) uint8_t storage[sizeof(fx::Side)];
    auto *side = new (storage) fx::Side(ctx);
    const std::vector<fx::Rec> records = {{"a_first_text_too_long_for_the_inline_buffer", "a_second_text_too_long_for_the_inline_buffer", 5, 0, 1},
                                          {"b_first_text_too_long_for_the_inline_buffer", "b_second_text_too_long_for_the_inline_buffer", 6, 2, 0},
                                          {"c_first_text_too_long_for_the_inline_buffer", "c_second_text_too_long_for_the_inline_buffer", 7, 0, 0}};
    DestroyedRegistry result;
    {
        fx::Stream stream(ctx);
        stream.Fill(2, static_cast<uint32_t>(records.size()), records);
        side->Call<void(__thiscall *)(void *, void *)>(false, 18, stream.In());
    }
    result.count = side->ext.mRegistry.count;
    uint32_t releasesBefore = 0;
    for (unsigned i = 0; i != fx::gPoolNext; ++i) releasesBefore += fx::gPool[i].releases;
    {
        difftest::FreeRecorder frees;
        if (original)
            ctx.Original<DtorFn>(0xcc53d0)(&side->ext);
        else
            side->ext.~cTSEffectsExtension();
        frees.Stop();
        result.frees = static_cast<uint32_t>(frees.SortedSizes(result.sizes, std::size(result.sizes)));
    }
    for (unsigned i = 0; i != fx::gPoolNext; ++i) result.releases += fx::gPool[i].releases;
    result.releases -= releasesBefore;
    return result;
}

} // namespace

DIFF_TEST(EffectsExtension_DestructFilled, 0xcc53d0)
{
    const DestroyedRegistry image = DestroyFilled(ctx, true), ours = DestroyFilled(ctx, false);
    ++ctx.cases;
    if (!(image == ours))
        ctx.Fail("filled registry's destruction differs: image %u records, %u releases, %u frees; ours %u, %u, %u", image.count,
                 image.releases, image.frees, ours.count, ours.releases, ours.frees);
    // Three records: their references, and at least two text blocks and a node each beside the table's own blocks.
    ++ctx.cases;
    if (image.count != 3 || image.releases != 3 || image.frees < 3 * 3 + 2)
        ctx.Fail("the original did not free what the test expects: %u records, %u releases, %u frees", image.count, image.releases,
                 image.frees);
}

// Read then write, each by the original or by ours, through real streams: the written bytes are the read ones (without
// the kind 1 record, which the write leaves out).
DIFF_TEST(EffectsExtension_RecordsRoundTrip, 0xcc69e0)
{
    const std::vector<fx::Rec> in = {{"fx_a", "tex_a", 5, 0, 1}, {"fx_skip", "tex_skip", 7, 1, 0}, {"fx_b", "a longer texture name", 6, 2, 0x80}};
    const std::vector<fx::Rec> out = {in[0], in[2]};
    std::string expected;
    {
        fx::Stream reference(ctx);
        reference.Fill(2, 2, out);
        expected = reference.Bytes();
    }
    for (int combo = 0; combo != 4; ++combo) {
        const bool readOriginal = (combo & 1) != 0, writeOriginal = (combo & 2) != 0;
        fx::Scope scope(ctx);
        fx::Side s(ctx);
        fx::Stream source(ctx), sink(ctx);
        source.Fill(2, 3, in);
        s.Call<void(__thiscall *)(void *, void *)>(readOriginal, 18, source.In());
        s.Call<void(__thiscall *)(void *, void *)>(writeOriginal, 19, sink.Out());
        ++ctx.cases;
        const std::string written = sink.Bytes();
        if (written != expected || s.ext.mRegistry.count != 3)
            ctx.Fail("read by %s, written by %s: %u records, bytes %s, expected %s", readOriginal ? "image" : "ours",
                     writeOriginal ? "image" : "ours", s.ext.mRegistry.count, written.c_str(), expected.c_str());
    }
}

// ---- slot 22 ----
DIFF_TEST(EffectsExtension_StatusText, 0xcc5810)
{
    const char *const expected[2] = {
        "Effects status for 'node-name':\n  Effects are unbound.\n  fx_run/tex_run/0x00000002/0x00000005 is started\n"
        "  fx_stop/tex_stop/0x00000000/0x00000010 is stopped\n  fx_none/tex_none/0x00000003/0x7fffffff is not instantiated\n",
        "Effects status for 'node-name':\n  Effects are bound.\n  fx_run/tex_run/0x00000002/0x00000005 is started\n"
        "  fx_stop/tex_stop/0x00000000/0x00000010 is stopped\n  fx_none/tex_none/0x00000003/0x7fffffff is not instantiated\n"};
    for (unsigned bound = 0; bound != 2; ++bound) {
        auto run = [&](bool original, std::string &steps, std::vector<std::string> &sprintfs, std::vector<std::string> &freed,
                       std::string &text) {
            fx::Scope scope(ctx, true);
            shape::CodePatch sprintfPatch(0x1ad6f7u, shape::CodeOf(&fx::FakeSprintf));
            fx::Side s(ctx);
            s.ext.mText.mString = "node-name";
            s.Byte(0x40) = static_cast<uint8_t>(bound);
            fx::gPool[0].active = 1;
            fx::gPool[1].active = 0;
            TSEffectRecord records[3]{};
            auto *sentinel = static_cast<TSEffectRecord *>(s.ext.mRegistry.sentinel);
            const char *names[3][2] = {{"fx_run", "tex_run"}, {"fx_stop", "tex_stop"}, {"fx_none", "tex_none"}};
            const uint32_t kinds[3] = {2, 0, 3}, hashes[3] = {5, 0x10, 0x7fffffff};
            for (unsigned i = 0; i != 3; ++i) {
                records[i].firstText = names[i][0];
                records[i].secondText = names[i][1];
                records[i].kind = kinds[i];
                records[i].hashValue = hashes[i];
                records[i].instance = i == 0 ? &fx::gPool[0] : i == 1 ? &fx::gPool[1] : nullptr;
                records[i].previous = i ? &records[i - 1] : sentinel;
                records[i].next = i == 2 ? sentinel : &records[i + 1];
            }
            sentinel->next = &records[0];
            sentinel->previous = &records[2];
            s.ext.mRegistry.count = 3;
            fx::gSprintfLog.clear();
            fx::gSteps.clear();
            s.Call<void(__thiscall *)(void *)>(original, 22);
            steps = fx::gSteps;
            sprintfs = fx::gSprintfLog;
            freed = fx::gFreedTexts;
            text = original ? std::string() : s.ext.StatusText();
            sentinel->next = sentinel->previous = sentinel;
            s.ext.mRegistry.count = 0;
        };
        std::string imageSteps, oursSteps, imageText, oursText;
        std::vector<std::string> imageSprintf, oursSprintf, imageFreed, oursFreed;
        run(true, imageSteps, imageSprintf, imageFreed, imageText);
        run(false, oursSteps, oursSprintf, oursFreed, oursText);
        ++ctx.cases;
        bool built = false;
        for (const std::string &t : imageFreed) built = built || t == expected[bound];
        const bool sprintfOk = imageSprintf.size() == 4 && imageSprintf[0] == "Effects status for 'node-name':\n" &&
                               imageSprintf[1] == "  fx_run/tex_run/0x00000002/0x00000005" &&
                               imageSprintf[2] == "  fx_stop/tex_stop/0x00000000/0x00000010" &&
                               imageSprintf[3] == "  fx_none/tex_none/0x00000003/0x7fffffff";
        if (imageSteps != "??" || oursSteps != "??" || oursText != expected[bound] || !built || !sprintfOk)
            ctx.Fail("status text (bound %u): steps %s/%s, built %d, sprintf %u, ours '%s'", bound, imageSteps.c_str(),
                     oursSteps.c_str(), built, static_cast<unsigned>(imageSprintf.size()), oursText.c_str());
    }
}

// ======================================================================================================================
// Registry removal and filtering on many records: built through the class's own CreateEffect (the same insert on both
// sides), then taken apart by the original or by ours, every step compared (the bucket ranges included).
// ======================================================================================================================
namespace {

using CreateFn = void *(__thiscall *)(void *, uint32_t, const char *, const char *, uint32_t, uint32_t);

// Builds `count` records: kind/flag/active from the tables (cycled), names "r00".., discriminator i.
void BuildMany(fx::Side &s, unsigned count, const uint32_t *kinds, const uint32_t *flags, const uint8_t *active, unsigned period)
{
    for (unsigned i = 0; i != count; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, "r%02u", i);
        s.Call<CreateFn>(false, 2, kinds[i % period], name, "", 0x100u + i, flags[i % period]);
        if (active) fx::gPool[i].active = active[i % period];
    }
}

} // namespace

// 0xcc4fd0 on a registry with many records: the head, the tail and the middle of each bucket's range, and a bucket's only
// record, each removed in four orders. The test counts which of those cases it took.
DIFF_TEST(EffectsExtension_RemoveRecordBuckets, 0xcc4fd0)
{
    using RemoveFn = TSEffectRecord **(__thiscall *)(void *, TSEffectRecord **, TSEffectRecord *);
    const uint32_t kinds[1] = {2}, flags[1] = {0};
    unsigned taken[4] = {};
    for (int plan = 0; plan != 4; ++plan) {
        std::string text[2];
        for (int side = 0; side != 2; ++side) {
            const bool original = side == 0;
            fx::Scope scope(ctx);
            fx::Side s(ctx);
            BuildMany(s, 24, kinds, flags, nullptr, 1);
            auto *sentinel = static_cast<TSEffectRecord *>(s.ext.mRegistry.sentinel);
            auto **buckets = static_cast<TSEffectRecord **>(s.ext.mRegistry.buckets);
            text[side] = fx::Snapshot(s.ext);
            for (unsigned step = 0; s.ext.mRegistry.count != 0; ++step) {
                const unsigned n = s.ext.mRegistry.count;
                const unsigned pick = plan == 0 ? 0 : plan == 1 ? n - 1 : plan == 2 ? n / 2 : (step % 2 ? n - 1 : 0);
                auto *record = sentinel->next;
                for (unsigned i = 0; i != pick; ++i) record = record->next;
                const uint32_t bucket = (static_cast<uint32_t>(std::hash<std::string>{}(record->firstText)) |
                                         static_cast<uint32_t>(std::hash<std::string>{}(record->secondText)) | record->hashValue) &
                                        s.ext.mRegistry.bucketMask;
                auto **range = buckets + bucket * 2;
                if (original) {
                    if (range[0] == record && range[1] == record) ++taken[0];
                    else if (range[0] == record) ++taken[1];
                    else if (range[1] == record) ++taken[2];
                    else ++taken[3];
                }
                TSEffectRecord *next = nullptr;
                if (original) ctx.Original<RemoveFn>(0xcc4fd0)(&s.ext.mRegistry, &next, record);
                else s.ext.RemoveRecord(&next, record);
                char b[32];
                std::snprintf(b, sizeof b, " next@%u;", fx::PositionOf(sentinel, next));
                text[side] += b;
                text[side] += fx::Snapshot(s.ext);
            }
        }
        ++ctx.cases;
        if (text[0] != text[1]) ctx.Fail("RemoveRecord on many records (order %d) differs:\n  image %s\n  ours  %s", plan, text[0].c_str(), text[1].c_str());
    }
    for (unsigned t : taken)
        if (t == 0) ctx.Fail("RemoveRecord on many records did not take every case (only %u, head %u, tail %u, middle %u)", taken[0], taken[1], taken[2], taken[3]);
}

// Slots 7 and 8 over records of several kinds and flags, with active and inactive instances.
DIFF_TEST(EffectsExtension_ActiveFilters, 0xcc6900)
{
    const uint32_t kinds[8] = {1, 2, 2, 3, 1, 2, 0, 3};
    const uint32_t flags[8] = {0, 0, 1, 0, 0, 1, 0, 1};
    const uint8_t active[8] = {1, 1, 1, 0, 1, 1, 1, 1};
    struct Case { int slot; uint32_t flag; int32_t kind; unsigned sets, remaining; };
    // slot 7 with flag 0: stops active flag-0 records of the kind (kind 0: all kinds); with flag != 0: slot 8's removal.
    // slot 8: removes (stop + release + remove) active flag-0 records of the kind (kind 0: all kinds).
    const Case cases[] = {
        {7, 0, 0, 4, 8}, {7, 0, 2, 1, 8}, {7, 0, 1, 2, 8}, {7, 0, 3, 0, 8}, {7, 0, 9, 0, 8}, {7, 0, -1, 0, 8},
        {7, 1, 0, 4, 4}, {7, 0x100, 2, 1, 8}, {7, 1, 1, 2, 6},
        {8, 0, 0, 4, 4}, {8, 0, 2, 1, 7}, {8, 0, 1, 2, 6}, {8, 0, 3, 0, 8}, {8, 0, 9, 0, 8},
    };
    for (const Case &c : cases) {
        std::string text[2];
        unsigned sets[2], remaining[2];
        for (int side = 0; side != 2; ++side) {
            const bool original = side == 0;
            fx::Scope scope(ctx);
            fx::Side s(ctx);
            BuildMany(s, 8, kinds, flags, active, 8);
            fx::gSteps.clear();
            if (c.slot == 7) s.Call<void(__thiscall *)(void *, uint32_t, int32_t)>(original, 7, c.flag, c.kind);
            else s.Call<void(__thiscall *)(void *, uint32_t)>(original, 8, static_cast<uint32_t>(c.kind));
            text[side] = fx::Snapshot(s.ext);
            sets[side] = 0;
            for (unsigned i = 0; i != fx::gPoolNext; ++i) sets[side] += fx::gPool[i].setCalls;
            remaining[side] = s.ext.mRegistry.count;
        }
        ++ctx.cases;
        if (text[0] != text[1] || sets[0] != c.sets || remaining[0] != c.remaining)
            ctx.Fail("slot %d (%u, %d): stops %u (expected %u), records %u (expected %u)\n  image %s\n  ours  %s", c.slot, c.flag,
                     c.kind, sets[0], c.sets, remaining[0], c.remaining, text[0].c_str(), text[1].c_str());
    }
}

// ======================================================================================================================
// What the class inherits: its interface lookup (0xcc6170, `this` the persistable face) and the cExtension virtuals its
// vtable reaches through thunks (the image's thunks against ours).
// ======================================================================================================================
DIFF_TEST(EffectsExtension_QueryInterface, 0xcc6170)
{
    using QueryFn = uint8_t(__thiscall *)(void *, uint32_t, void **);
    const uint32_t iids[] = {cTSEffectsExtension::kEffectsIid, 0x00000001u, 0x00000002u, 0x0bb263d5u, 0xdeadbeefu, 0u,
                             0x22ee8b7bu, 0x6bc1d4d4u, 0x1d2e3f40u, 0xffffffffu, 0x2c3cb6bcu, 0x4a6b4b9fu};
    unsigned accepted = 0, refused = 0;
    for (const uint32_t iid : iids) {
        struct Result { uint8_t ok; int32_t offset; uint32_t words[8]; } got[2]{};
        for (int side = 0; side != 2; ++side) {
            cTSEffectsExtension ext;
            auto *object = reinterpret_cast<uint8_t *>(&ext);
            void *out = nullptr;
            got[side].ok = side == 0 ? ctx.Original<QueryFn>(0xcc6170)(object + 0xc, iid, &out)
                                     : static_cast<uint8_t>(ext.QueryInterface(iid, &out));
            got[side].offset = out ? static_cast<int32_t>(static_cast<uint8_t *>(out) - object) : -1;
            std::memcpy(got[side].words, object, sizeof got[side].words);
            got[side].words[0] = 0; // the vtable word differs by side's object only through the original's vtable choice
        }
        ++ctx.cases;
        // ours and the image's results agree, counted references included
        if (std::memcmp(&got[0], &got[1], sizeof(Result)) != 0)
            ctx.Fail("QueryInterface(%08x): image %u at %d, ours %u at %d (words %08x %08x %08x / %08x %08x %08x)", iid,
                     got[0].ok, got[0].offset, got[1].ok, got[1].offset, got[0].words[1], got[0].words[2], got[0].words[3],
                     got[1].words[1], got[1].words[2], got[1].words[3]);
        (got[0].ok ? accepted : refused) += 1;
    }
    if (accepted < 2 || refused == 0)
        ctx.Fail("QueryInterface: the iids took %u accepted and %u refused answers; the test needs both sides", accepted, refused);
}

DIFF_TEST(EffectsExtension_InheritedVirtuals, 0xcc5b30)
{
    using ThisFn = void *(__thiscall *)(void *);
    using BoolFn = uint8_t(__thiscall *)(void *);
    using KindFn = uint8_t(__thiscall *)(void *, uint32_t);
    using NameFn = const char *(__thiscall *)(void *);
    using TextFn = void(__thiscall *)(void *, void *);
    using SetFn = void(__thiscall *)(void *, uint32_t);
    cTSEffectsExtension ext;
    auto *object = reinterpret_cast<uint8_t *>(&ext);
    void *face = object + 0x6c;
    void **const oursVt = *reinterpret_cast<void ***>(face);
    void **const imageVt = ctx.image.At<void *>(cTSEffectsExtension::kEffectsFaceVTable);
    auto slot = [&](bool original, unsigned i) { return (original ? imageVt : oursVt)[i]; };
    ++ctx.cases;
    if (reinterpret_cast<ThisFn>(slot(true, 15))(face) != nullptr || reinterpret_cast<ThisFn>(slot(false, 15))(face) != nullptr)
        ctx.Fail("Clone: not null");
    ++ctx.cases;
    if (reinterpret_cast<BoolFn>(slot(true, 24))(face) != reinterpret_cast<BoolFn>(slot(false, 24))(face) ||
        reinterpret_cast<BoolFn>(slot(true, 24))(face) != 0)
        ctx.Fail("IsPersistent differs or is not false");
    unsigned yes = 0;
    for (const uint32_t iid : {cTSEffectsExtension::kEffectsIid, 0x00000001u, 0x0bb263d5u, 0xdeadbeefu, 0u, 0xffffffffu, 0x22ee8b7bu, 0x6bc1d4d4u, 0x2c3cb6bcu}) {
        ++ctx.cases;
        const uint8_t a = reinterpret_cast<KindFn>(slot(true, 23))(face, iid), b = reinterpret_cast<KindFn>(slot(false, 23))(face, iid);
        yes += a != 0;
        if (a != b) ctx.Fail("IsKindOf(%08x): image %u, ours %u", iid, a, b);
    }
    if (yes == 0) ctx.Fail("IsKindOf: no iid answered yes, the test took only the 'no' side");
    ++ctx.cases;
    const char *imageName = reinterpret_cast<NameFn>(slot(true, 25))(face), *oursName = reinterpret_cast<NameFn>(slot(false, 25))(face);
    if (!imageName || !oursName || std::strcmp(imageName, oursName) != 0 || std::strlen(imageName) == 0)
        ctx.Fail("GetTypeName: image '%s', ours '%s'", imageName ? imageName : "(null)", oursName ? oursName : "(null)");
    ++ctx.cases;
    std::string texts[2];
    for (int side = 0; side != 2; ++side) {
        cRZString text;
        text.mVTable = cRZString::VTable();
        text.mString = "not empty";
        text.mRefCount = 0;
        reinterpret_cast<TextFn>(slot(side == 0, 26))(face, &text);
        texts[side] = text.mString;
    }
    if (texts[0] != texts[1] || !texts[0].empty()) ctx.Fail("GetDescription: image '%s', ours '%s'", texts[0].c_str(), texts[1].c_str());
    // the shared setters of the persistable face's slots 20 and 21 (object bytes +0x43 and +0x44)
    for (const unsigned which : {20u, 21u}) {
        for (const uint32_t value : {0x155u, 0u, 0x12345678u}) {
            uint8_t memory[2][8];
            for (int side = 0; side != 2; ++side) {
                cTSEffectsExtension e;
                auto *o = reinterpret_cast<uint8_t *>(&e);
                std::memset(o + 0x40, 0xcc, 8);
                void *f = o + 0x18;
                void **vt = side == 0 ? ctx.image.At<void *>(cTSEffectsExtension::kEffectsVTable) : *reinterpret_cast<void ***>(f);
                reinterpret_cast<SetFn>(vt[which])(f, value);
                std::memcpy(memory[side], o + 0x40, 8);
            }
            ++ctx.cases;
            if (std::memcmp(memory[0], memory[1], 8) != 0)
                ctx.Fail("slot %u(%x): image wrote %02x%02x%02x%02x%02x%02x%02x%02x, ours %02x%02x%02x%02x%02x%02x%02x%02x", which,
                         value, memory[0][0], memory[0][1], memory[0][2], memory[0][3], memory[0][4], memory[0][5], memory[0][6],
                         memory[0][7], memory[1][0], memory[1][1], memory[1][2], memory[1][3], memory[1][4], memory[1][5],
                         memory[1][6], memory[1][7]);
        }
    }
}

// Every slot of the cIExtension face (the +0x6c vtable: vtordisp thunks into cExtension's bodies and the class's own
// overrides) and of the referent face (+0xc) through the interface, on the image's object (built by the original
// constructor) and ours. The thunks exist only in our vtables once the constructor is ours. Not called: the event
// slots (OnEvent, OnQuery, Notify/AskListeners, Connect/Disconnect, GetEventIds, GetEventAttribute, GetEventCount) walk
// the game's event registry, which the harness does not initialise (the image's heap is corrupted by them), and
// SetOwner/Set/GetAttribute (the original binds the effects to the owner node: FindEffectNode on a null owner).
// Original thunks this covers: 0xcc6160 QueryInterface, 0xcc65bf Release, 0xcc5e73 IsActive, 0xcc57d7
// GetPersistReferent, 0xcc5ed6 GetNextConst, 0xcc68a7 SetNext, 0xcc695e IsKindOf, 0xcc5b25 GetDescription.
DIFF_TEST(EffectsExtension_InterfaceSlots, 0xcc57cc)
{
    using nGZSceneGraph::cIExtension;
    auto run = [&](bool original) {
        std::string out;
        char b[160];
        auto add = [&](const char *fmt, auto... a) { std::snprintf(b, sizeof b, fmt, a...); out += b; };
        uint8_t *object;
        cTSEffectsExtension *ours = nullptr;
        if (original) {
            object = static_cast<uint8_t *>(::operator new(cTSEffectsExtension::kSize));
            std::memset(object, 0, cTSEffectsExtension::kSize);
            ctx.Original<CtorFn>(0xcc50c0)(object, 1);
        } else {
            ours = new cTSEffectsExtension();
            object = reinterpret_cast<uint8_t *>(ours);
        }
        *reinterpret_cast<uint32_t *>(object + 0x10) = 0;
        auto *e = reinterpret_cast<cIExtension *>(object + 0x6c);
        auto *referent = reinterpret_cast<cIGZPersistSerializableReferent *>(object + 0xc);
        auto count = [&] { return *reinterpret_cast<uint32_t *>(object + 8); };
        add("count %08x;", count());
        add("addref %u;", e->AddRef());
        add("addref %u;", e->AddRef());
        add("release %u;", e->Release());
        add("count %08x;", count());
        for (const uint32_t iid : {0x1u, 0x0bb263d5u, 0xfb06dba0u, 0x5b0aaa74u, 0xbb11a53eu, 0x3b253dc7u, 0xfb859324u, 0xdeadbeefu, 0u}) {
            void *face = nullptr;
            const bool ok = e->QueryInterface(iid, &face);
            add("qi %08x %d at %d count %08x;", iid, ok, face ? static_cast<int>(static_cast<uint8_t *>(face) - object) : -1, count());
            while (count() > 0x1 && face) { e->Release(); face = nullptr; }
        }
        add("active %u;", e->IsActive());
        add("clone %p;", static_cast<void *>(e->Clone()));
        add("persist %d;", static_cast<int>(reinterpret_cast<uint8_t *>(e->GetPersistReferent()) - object));
        add("owner %p %p;", static_cast<void *>(e->GetOwnerConst()), static_cast<void *>(e->GetOwner()));
        add("next %p %p;", static_cast<void *>(e->GetNextConst()), static_cast<void *>(e->GetNext()));
        e->SetNext(nullptr);
        for (const uint32_t iid : {0u, 1u, cTSEffectsExtension::kEffectsIid, 0xfb06dba0u, 0xdeadbeefu})
            add("kind %08x %d;", iid, e->IsKindOf(iid));
        add("persistent %d type %s;", e->IsPersistent(), e->GetTypeName());
        {
        cRZString text;
        text.mVTable = cRZString::VTable();
        text.mString = "not empty";
        text.mRefCount = 0;
        e->GetDescription(reinterpret_cast<cIGZString *>(&text));
        add("description '%s';", text.mString.c_str());
        }
        {
        const uint32_t referentAdd = referent->AddRef();
        const uint32_t referentRelease = referent->Release();
        add("ref addref %u release %u owner %u;", referentAdd, referentRelease, referent->GetOwnerInfo());
        cPersistTypeInfo info =referent->GetTypeInfo();
        add("typeinfo %s %u %u;", info.mName ? reinterpret_cast<cRZString *>(info.mName)->mString.c_str() : "-", info.mWord4, info.mVersion);
        // info.mName->Release() omitted
        }
        add("count %08x;", count());
        if (original) {
            ctx.Original<DtorFn>(0xcc53d0)(object);
            ::operator delete(object, cTSEffectsExtension::kSize);
        } else {
            ours->~cTSEffectsExtension();
            ::operator delete(ours);
        }
        return out;
    };
    const std::string image = run(true), ours = run(false);
    ++ctx.cases;
    if (image != ours) ctx.Fail("interface slots differ:\n  image %s\n  ours  %s", image.c_str(), ours.c_str());
}
