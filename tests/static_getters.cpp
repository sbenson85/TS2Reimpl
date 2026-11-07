// Differential tests for the director singleton chain: 0x1ae18d (framework getter), 0x14491b (the
// function-local static), 0x144897 (director constructor), 0x1adffa (base constructor), 0x1aa569
// (the id set) and 0x1aa5b3 (its list sentinel).
//
// Each test runs the original and the reimplementation on the same memory (the scratch buffer or
// the game's static at 0x10c9390), then compares the bytes. Heap pointers differ between runs
// (each run allocates its own nodes and buckets), so the id set is compared through a canonical
// form: the sentinel is self-linked, the buckets all point at it, and the bucket count is 16.
// The framework service is a fake whose vtable slot 23 counts its calls and returns a sentinel.
//
// The director's guard (0x10c93f4) and the thread's TLS epoch are shared state. The original
// run goes first in each test, so the guard always holds the original's epoch when the
// reimplementation's fast path reads the image's TLS copy (the footer updates that copy).
#include "difftest.h"
#include "test_helpers.h"
#include "game/game.h"
#include "rz/RZCOMDllDirectorShape.h"
#include "rz/StaticGetters.h"
#include "rz/Singleton_20ddfb.h"

#include <intrin.h>
#include <windows.h>

#include <cstring>

extern "C" __declspec(thread) int _Init_thread_epoch; // our CRT's statics epoch (our getters' fast path)

namespace {

// Fake framework service: vtable slot 23 counts its calls and returns a sentinel.
int gSlotCalls = 0;
void *const kFrameworkSentinel = reinterpret_cast<void *>(0x5a5a1234);

void *__fastcall RecordSlot23(void *, void *)
{
    ++gSlotCalls;
    return kFrameworkSentinel;
}

void *gFakeVTable[32];

struct FakeService {
    void *vtable;
    uint32_t pad;
};

FakeService gFramework{};

// Installs the fake service in game global 0x10c9378 (the service GetServiceObject returns).
void SetupFramework(difftest::Context &ctx)
{
    std::memset(gFakeVTable, 0, sizeof(gFakeVTable));
    gFakeVTable[23] = reinterpret_cast<void *>(&RecordSlot23);
    gFramework.vtable = gFakeVTable;
    gSlotCalls = 0;
    *ctx.image.At<void *>(0x10c9378) = &gFramework;
}

// Scratch memory for the constructors: the object at offset 0, canary bytes after it.
constexpr size_t kScratchSize = 0x90;
constexpr size_t kObjectSize = 0x64;
alignas(16) uint8_t gScratch[kScratchSize];

void FillScratch()
{
    std::memset(gScratch, 0xCD, sizeof(gScratch));
}

// The id set (0x20 bytes): exact bytes except the four heap words, which are compared by shape.
struct SetCanon {
    uint8_t fixed[0x20];
    bool headSelfLinked;
    uint32_t headSize;
    uint32_t bucketBytes;
    bool bucketsAreHead;
    bool endIsLast;
    bool hasBuckets;
};

SetCanon CanonSet(const uint8_t *set)
{
    SetCanon c{};
    std::memcpy(c.fixed, set, sizeof(c.fixed));
    void *head = nullptr;
    void **first = nullptr, **last = nullptr, **end = nullptr;
    std::memcpy(&head, set + 4, sizeof(head));
    std::memcpy(&first, set + 0xc, sizeof(first));
    std::memcpy(&last, set + 0x10, sizeof(last));
    std::memcpy(&end, set + 0x14, sizeof(end));
    std::memset(c.fixed + 4, 0, 4);
    std::memset(c.fixed + 0xc, 0, 12);
    std::memcpy(&c.headSize, set + 8, sizeof(uint32_t));
    if (head) {
        auto **node = static_cast<void **>(head);
        c.headSelfLinked = node[0] == head && node[1] == head;
    }
    c.hasBuckets = first != nullptr;
    c.bucketBytes = first ? static_cast<uint32_t>(reinterpret_cast<uint8_t *>(last) - reinterpret_cast<uint8_t *>(first)) : 0;
    c.endIsLast = end == last;
    c.bucketsAreHead = true;
    if (first)
        for (uint32_t i = 0; i < c.bucketBytes / sizeof(void *); ++i)
            if (first[i] != head) c.bucketsAreHead = false;
    return c;
}

bool SameSet(const SetCanon &a, const SetCanon &b)
{
    return std::memcmp(a.fixed, b.fixed, sizeof(a.fixed)) == 0 && a.headSelfLinked == b.headSelfLinked &&
           a.headSize == b.headSize && a.bucketBytes == b.bucketBytes && a.bucketsAreHead == b.bucketsAreHead &&
           a.endIsLast == b.endIsLast && a.hasBuckets == b.hasBuckets;
}

// Lane EO: the base and root directors are our classes (rz/RZCOMDllDirectorShape.h); our side is built by our
// constructors (the 0x1adffa / 0x144897 hooks' adapters) and carries our vtables. The vtable words at +0 and +4 are the
// only bytes normalised: the game's and ours of the same class map to one code, anything else stays raw.
uint32_t gVTableWords[8]; // our base +0, +4; our root +0, +4; the game's base +0, +4; root +0, +4

void LearnVTables(difftest::Context &ctx);

void NormaliseVTables(uint8_t *bytes)
{
    for (int face = 0; face < 2; ++face) {
        uint32_t word;
        std::memcpy(&word, bytes + 4 * face, 4);
        for (int k = 0; k < 8; ++k)
            if (word == gVTableWords[k] && word != 0) {
                uint32_t code = 0xc0de0000u + (k % 4); // ours and the game's of one class and face: one code
                std::memcpy(bytes + 4 * face, &code, 4);
                break;
            }
    }
}

// The whole director: bytes before the id set exact (vtable words normalised), the set canonical.
struct DirectorCanon {
    uint8_t head[0x44];
    SetCanon set;
};

DirectorCanon CanonDirector(const uint8_t *object)
{
    DirectorCanon c{};
    std::memcpy(c.head, object, sizeof(c.head));
    NormaliseVTables(c.head);
    c.set = CanonSet(object + 0x44);
    return c;
}

bool SameDirector(const DirectorCanon &a, const DirectorCanon &b)
{
    return std::memcmp(a.head, b.head, sizeof(a.head)) == 0 && SameSet(a.set, b.set);
}

bool CanaryIntact()
{
    for (size_t i = kObjectSize; i < kScratchSize; ++i)
        if (gScratch[i] != 0xCD) return false;
    return true;
}

// The original's id-set constructor takes two stack arguments (ret 8). Their values are used by
// nothing; the original reads the first one's dword, so it gets a valid pointer.
using ListCtorOriginal = void *(__thiscall *)(void *, uint32_t);
using DirectorCtorOriginal = void *(__thiscall *)(void *);
using GetterOriginal = void *(__cdecl *)();

// The game's director static and its guard.
uint8_t *DirectorBytes(difftest::Context &ctx)
{
    return ctx.image.At<uint8_t>(0x10c9390);
}

void ResetDirector(difftest::Context &ctx)
{
    std::memset(DirectorBytes(ctx), 0xCD, kObjectSize);
    *ctx.image.At<int>(0x10c93f4) = 0;
    gSlotCalls = 0;
}

// Overwrites the framework field of an already-built director, to check the getter leaves an
// existing object alone.
void *const kMarker = reinterpret_cast<void *>(0x6b6b0030);

} // namespace

// The list part (head at +4, size at +8) of a list sentinel: whether the head is self-linked, and
// the size. The node's own bytes past the links are not initialised by either constructor.
struct ListShape {
    bool selfLinked;
    uint32_t size;
    void *head;
};

ListShape ShapeOf(const uint8_t *list)
{
    ListShape s{};
    std::memcpy(&s.head, list, sizeof(s.head));
    std::memcpy(&s.size, list + 4, sizeof(s.size));
    if (s.head) {
        auto **node = static_cast<void **>(s.head);
        s.selfLinked = node[0] == s.head && node[1] == s.head;
    }
    return s;
}

DIFF_TEST(StaticGetters_ListNodeConstruct, 0x1aa5b3)
{
    auto orig = ctx.Original<ListCtorOriginal>(0x1aa5b3);
    uint8_t *list = gScratch + 4;
    auto *ours = reinterpret_cast<HashNodeList *>(list);

    FillScratch();
    void *a = orig(list, 0x12345678); // the stack argument is ignored
    ListShape shapeA = ShapeOf(list);
    ++ctx.cases;
    if (a != list) ctx.Fail("list ctor returned %p, expected %p", a, list);
    if (!CanaryIntact()) ctx.Fail("original list ctor wrote past its 8 bytes");

    FillScratch();
    void *b = ours->Construct(0x12345678);
    ListShape shapeB = ShapeOf(list);
    ++ctx.cases;
    if (b != list) ctx.Fail("ours returned %p, expected %p", b, list);
    if (shapeA.selfLinked != shapeB.selfLinked || shapeA.size != shapeB.size)
        ctx.Fail("list ctor: original linked=%d size=%u, ours linked=%d size=%u", shapeA.selfLinked, shapeA.size,
                 shapeB.selfLinked, shapeB.size);
    if (!shapeB.selfLinked || shapeB.size != 0) ctx.Fail("list ctor: ours is not an empty self-linked sentinel");
    if (!CanaryIntact()) ctx.Fail("list ctor wrote past its 8 bytes");
}

DIFF_TEST(StaticGetters_DllDirectorConstruct, 0x1adffa)
{
    LearnVTables(ctx);
    auto orig = ctx.Original<DirectorCtorOriginal>(0x1adffa);

    FillScratch();
    void *a = orig(gScratch);
    DirectorCanon canonA = CanonDirector(gScratch);
    ++ctx.cases;
    if (a != gScratch) ctx.Fail("base ctor returned %p, expected %p", a, gScratch);
    if (!CanaryIntact()) ctx.Fail("original base ctor wrote past its object");

    FillScratch();
    void *b = rz::cRZCOMDllDirectorAdapters::Construct(gScratch, nullptr); // 0x1adffa: our constructor
    DirectorCanon canonB = CanonDirector(gScratch);
    ++ctx.cases;
    if (b != gScratch) ctx.Fail("ours returned %p, expected %p", b, gScratch);
    if (!SameDirector(canonA, canonB)) ctx.Fail("base ctor: original and ours differ");
    if (!CanaryIntact()) ctx.Fail("base ctor wrote past its object");
}

DIFF_TEST(StaticGetters_DirectorConstruct, 0x144897)
{
    LearnVTables(ctx);
    SetupFramework(ctx);
    auto orig = ctx.Original<DirectorCtorOriginal>(0x144897);

    FillScratch();
    void *a = orig(gScratch);
    DirectorCanon canonA = CanonDirector(gScratch);
    void *frameworkA = *reinterpret_cast<void **>(gScratch + 0x30);
    int callsA = gSlotCalls;
    ++ctx.cases;
    if (a != gScratch) ctx.Fail("director ctor returned %p, expected %p", a, gScratch);
    if (frameworkA != kFrameworkSentinel || callsA != 1)
        ctx.Fail("original director ctor: framework %p after %d slot-23 calls", frameworkA, callsA);

    SetupFramework(ctx);
    FillScratch();
    void *b = rz::cGZCOMDirectorAdapters::Construct(gScratch, nullptr); // 0x144897: our constructor
    DirectorCanon canonB = CanonDirector(gScratch);
    void *frameworkB = *reinterpret_cast<void **>(gScratch + 0x30);
    int callsB = gSlotCalls;
    ++ctx.cases;
    if (b != gScratch) ctx.Fail("ours returned %p, expected %p", b, gScratch);
    if (frameworkB != frameworkA || callsB != callsA)
        ctx.Fail("ours: framework %p after %d slot-23 calls", frameworkB, callsB);
    if (!SameDirector(canonA, canonB)) ctx.Fail("director ctor: original and ours differ");
    if (!CanaryIntact()) ctx.Fail("director ctor wrote past its object");
}


// The thread's TLS copy of the director's epoch: offset 4 of the image's TLS block for its index.
// The fast path reads it; the tests set it to choose the path (high: fast, -1: slow).
int *ImageTlsBlock(difftest::Context &ctx)
{
    uint32_t index = *ctx.image.At<uint32_t>(0x10df814);
    auto **array = reinterpret_cast<int **>(__readfsdword(0x2c));
    return array[index];
}

// Builds the director with the original's constructor (0x144897), which runs no atexit: it is
// the expected state of a first use.
void BuildWithOriginalConstructor(difftest::Context &ctx)
{
    auto ctor = ctx.Original<DirectorCtorOriginal>(0x144897);
    ctor(DirectorBytes(ctx));
}

// The fast path: the guard is not above the thread's epoch, so the getter returns the director
// without touching the guard or building anything. The original's getter is run on the same state.
// The first-use path is in StaticGetters_GetCOMDirectorFirstUse.
DIFF_TEST(StaticGetters_GetCOMDirectorFast, 0x14491b)
{
    SetupFramework(ctx);
    int *tls = ImageTlsBlock(ctx);
    int savedEpoch = tls[1];
    tls[1] = 0x7fffffff;

    ResetDirector(ctx);
    BuildWithOriginalConstructor(ctx);
    DirectorCanon before = CanonDirector(DirectorBytes(ctx));
    *ctx.image.At<int>(0x10c93f4) = 5;
    int callsBefore = gSlotCalls;

    auto orig = ctx.Original<GetterOriginal>(0x14491b);
    void *a = orig();
    bool sameA = SameDirector(before, CanonDirector(DirectorBytes(ctx))) && *ctx.image.At<int>(0x10c93f4) == 5 &&
                 gSlotCalls == callsBefore;
    ++ctx.cases;
    if (a != DirectorBytes(ctx) || !sameA) ctx.Fail("original fast path changed state or returned %p", a);

    void *b = GetCOMDirector();
    bool sameB = SameDirector(before, CanonDirector(DirectorBytes(ctx))) && *ctx.image.At<int>(0x10c93f4) == 5 &&
                 gSlotCalls == callsBefore;
    ++ctx.cases;
    if (b != a || !sameB) ctx.Fail("fast path: ours returned %p, changed state %d", b, !sameB);

    tls[1] = savedEpoch;
}

// The first use: the reimplementation builds the director through the guard protocol. The guard
// is 0 and the thread's epoch is -1, so the slow path runs. The expected state is what the
// original constructor builds. The reimplementation registers its destructor with atexit; the
// original run is not made here (see above).
// The image's atexit (0x824cbe) is patched to record instead of run for the duration of a test
// (testhelp::AtexitRedirect: its CRT is not initialised in the harness). Our registration is
// recorded through gRegisterExitHandler, so nothing is registered with the real exit list.
using testhelp::AtexitRedirect;

std::vector<void (*)()> gOursRegistered;

int RecordOurRegistration(void (*handler)())
{
    gOursRegistered.push_back(handler);
    return 0;
}

// The director after its destructor: the id set's list head is a freed node (its value differs
// between runs), so that word is masked; everything else is exact.
struct DirectorBytesSnap {
    uint8_t bytes[0x64];
};

DirectorBytesSnap SnapDirector(const uint8_t *object)
{
    DirectorBytesSnap s;
    std::memcpy(s.bytes, object, sizeof(s.bytes));
    NormaliseVTables(s.bytes);
    std::memset(s.bytes + 0x48, 0, 4);
    // The string's buffer union (at +0x14) keeps the upper three bytes of its freed heap pointer
    // after _Tidy_deallocate (which only writes byte 0). Those bytes are heap addresses: masked.
    std::memset(s.bytes + 0x15, 0, 3);
    return s;
}

bool SameSnap(const DirectorBytesSnap &a, const DirectorBytesSnap &b)
{
    return std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}

// Gives an empty director heap memory to free (variant 1): a long string and a vector of two entries.
void PrepareDirector(uint8_t *object, bool withHeap)
{
    if (!withHeap) return;
    auto *d = reinterpret_cast<rz::cRZCOMDllDirector *>(object);
    d->mLibraryPath.value.mString.assign(40, 'x');
    d->mChildren.assign(2, nullptr);
}

void SetWord(uint8_t *at, const void *value)
{
    std::memcpy(at, &value, sizeof(value));
}

// Case 0: the sentinel is its own predecessor; its next pointer is cleared and nothing is freed.
// Case 1: the predecessor is another cell with a null next; that cell's next is cleared.
DIFF_TEST(StaticGetters_FreeListNodes, 0x1adf40)
{
    auto orig = ctx.Original<void(__cdecl *)(void *, void *)>(0x1adf40);
    uint8_t *sentinel = gScratch;
    uint8_t *other = gScratch + 0x20;
    for (int variant = 0; variant < 2; ++variant) {
        uint8_t origAfter[0x40], oursAfter[0x40];
        for (int side = 0; side < 2; ++side) {
            FillScratch();
            if (variant == 0) {
                SetWord(sentinel, sentinel);
                SetWord(sentinel + 4, sentinel);
            } else {
                SetWord(sentinel, nullptr);
                SetWord(sentinel + 4, other);
                SetWord(other, reinterpret_cast<void *>(0x1234));
            }
            if (side == 0)
                orig(sentinel, sentinel);
            else
                FreeListNodes(sentinel, sentinel);
            std::memcpy(side == 0 ? origAfter : oursAfter, gScratch, sizeof(origAfter));
        }
        ++ctx.cases;
        if (std::memcmp(origAfter, oursAfter, sizeof(origAfter)) != 0) ctx.Fail("free list nodes, variant %d", variant);
    }
}

// The list sentinel's destructor: frees the sentinel (no other nodes here, as the list is empty).
// The freed head pointer is left as it was, so only the size and the rest of the cell are compared.
DIFF_TEST(StaticGetters_ListDestruct, 0x1ae301)
{
    auto origCtor = ctx.Original<ListCtorOriginal>(0x1aa5b3);
    auto origDtor = ctx.Original<void(__thiscall *)(void *)>(0x1ae301);
    uint8_t *list = gScratch;
    FillScratch();
    origCtor(list, 0);
    origDtor(list);
    uint32_t sizeA = *reinterpret_cast<uint32_t *>(list + 4);
    void *headA;
    std::memcpy(&headA, list, sizeof(headA));

    FillScratch();
    origCtor(list, 0);
    reinterpret_cast<HashNodeList *>(list)->Destruct();
    uint32_t sizeB = *reinterpret_cast<uint32_t *>(list + 4);
    void *headB;
    std::memcpy(&headB, list, sizeof(headB));
    ++ctx.cases;
    if (sizeA != sizeB || !CanaryIntact() || headA == nullptr || headB == nullptr)
        ctx.Fail("list destruct: sizes %u/%u", sizeA, sizeB);
}

// The director's destructor (0x1ae048), on a director built by the original constructor. Variant 1
// adds a long string and a vector entry block so the destructor frees them too.
DIFF_TEST(StaticGetters_DirectorDestruct, 0x1ae048)
{
    LearnVTables(ctx);
    SetupFramework(ctx);
    auto origCtor = ctx.Original<DirectorCtorOriginal>(0x144897);
    auto origDtor = ctx.Original<void(__thiscall *)(void *)>(0x1ae048);
    for (int variant = 0; variant < 2; ++variant) {
        FillScratch();
        origCtor(gScratch);
        PrepareDirector(gScratch, variant == 1);
        origDtor(gScratch);
        DirectorBytesSnap a = SnapDirector(gScratch);
        bool canaryA = CanaryIntact();

        // The adapter (0x1ae048's hook) on a director built by the original.
        FillScratch();
        origCtor(gScratch);
        PrepareDirector(gScratch, variant == 1);
        rz::cRZCOMDllDirectorAdapters::Destruct(reinterpret_cast<rz::cRZCOMDllDirector *>(gScratch), nullptr);
        DirectorBytesSnap b = SnapDirector(gScratch);
        ++ctx.cases;
        if (!SameSnap(a, b) || !canaryA || !CanaryIntact()) ctx.Fail("director destructor (adapter), variant %d", variant);

        // Ours: our root director (our constructor), destroyed through our vtable's slot 16 with flags 0.
            SetupFramework(ctx);
        FillScratch();
        rz::cGZCOMDirectorAdapters::Construct(gScratch, nullptr);
        PrepareDirector(gScratch, variant == 1);
        using Deleting = void *(__thiscall *)(void *, uint32_t);
        void *r = (*reinterpret_cast<Deleting *const *>(gScratch))[16](gScratch, 0);
        DirectorBytesSnap c = SnapDirector(gScratch);
        ++ctx.cases;
        if (r != gScratch || !SameSnap(a, c) || !CanaryIntact()) ctx.Fail("director destructor (our slot 16), variant %d", variant);
    }
}

// The first use, both sides. The original's getter registers its destructor stub (0xd8e6e0)
// through the image's atexit; ours registers DestroyCOMDirector through gRegisterExitHandler. Each
// handler is run afterwards, and the director it leaves must match.
DIFF_TEST(StaticGetters_GetCOMDirectorFirstUse, 0x14491b)
{
    LearnVTables(ctx);
    SetupFramework(ctx);
    int *tls = ImageTlsBlock(ctx);
    int savedEpoch = tls[1];
    tls[1] = -1; // the guard is 0: the slow path runs for both getters

    auto orig = ctx.Original<GetterOriginal>(0x14491b);
    auto *guard = ctx.image.At<int>(0x10c93f4);

    // The original.
    ResetDirector(ctx);
    AtexitRedirect::Clear();
    void *a;
    {
        AtexitRedirect redirect(ctx);
        a = orig();
    }
    DirectorCanon canonA = CanonDirector(DirectorBytes(ctx));
    int callsA = gSlotCalls;
    int guardA = *guard;
    void (*origHandler)() = testhelp::gAtexitHandler;
    ++ctx.cases;
    if (testhelp::gAtexitCalls != 1) ctx.Fail("first use: the original registered %d exit handlers", testhelp::gAtexitCalls);
    if (origHandler != reinterpret_cast<void (*)()>(GameAddress<void>(0xd8e6e0)))
        ctx.Fail("first use: the original registered %p, expected its stub 0xd8e6e0", origHandler);
    if (a != DirectorBytes(ctx) || callsA != 1) ctx.Fail("first use: original returned %p after %d framework calls", a, callsA);
    if (guardA == 0 || guardA == -1) ctx.Fail("first use: original guard %d", guardA);
    if (origHandler) origHandler();
    DirectorBytesSnap postA = SnapDirector(DirectorBytes(ctx));

    // The reimplementation. The original's footer raised the image's TLS copy, so reset it to -1.
    SetupFramework(ctx);
    tls[1] = -1;
    ResetDirector(ctx);
    gOursRegistered.clear();
    gRegisterExitHandler = &RecordOurRegistration;
    void *b = GetCOMDirector();
    gRegisterExitHandler = &atexit;
    DirectorCanon canonB = CanonDirector(DirectorBytes(ctx));
    int callsB = gSlotCalls;
    int guardB = *guard;
    ++ctx.cases;
    if (gOursRegistered.size() != 1) ctx.Fail("first use: ours registered %zu exit handlers", gOursRegistered.size());
    if (gOursRegistered.size() == 1 && gOursRegistered[0] != &DestroyCOMDirector)
        ctx.Fail("first use: ours registered %p, expected DestroyCOMDirector", gOursRegistered[0]);
    if (b != DirectorBytes(ctx) || callsB != 1) ctx.Fail("first use: ours returned %p after %d framework calls", b, callsB);
    if (!SameDirector(canonA, canonB)) ctx.Fail("first use: the constructed director differs from the original's");
    if (guardB == 0 || guardB == -1) ctx.Fail("first use: ours guard %d", guardB);

    // Run our handler: it must leave the same destroyed state as the original's stub did.
    ++ctx.cases;
    if (gOursRegistered.size() == 1) gOursRegistered[0]();
    DirectorBytesSnap postB = SnapDirector(DirectorBytes(ctx));
    if (!SameSnap(postA, postB)) ctx.Fail("first use: the destroyed directors differ");

    gOursRegistered.clear();
    tls[1] = savedEpoch;
}
DIFF_TEST(StaticGetters_GetDirectorFramework, 0x1ae18d)
{
    SetupFramework(ctx);
    int *tls = ImageTlsBlock(ctx);
    int savedEpoch = tls[1];
    tls[1] = 0x7fffffff; // fast path for both getters: the original reads the image's epoch, ours our CRT's
    const int savedOurEpoch = _Init_thread_epoch;
    _Init_thread_epoch = 0x7fffffff;

    ResetDirector(ctx);
    BuildWithOriginalConstructor(ctx);
    auto orig = ctx.Original<GetterOriginal>(0x1ae18d);
    void *values[] = {nullptr, kMarker, kFrameworkSentinel};
    for (void *value : values) {
        *reinterpret_cast<void **>(DirectorBytes(ctx) + 0x30) = value;
        void *a = orig();
        void *b = GetDirectorFrameworkObject();
        ++ctx.cases;
        if (a != value || b != value) ctx.Fail("framework %p: original returned %p, ours %p", value, a, b);
    }
    tls[1] = savedEpoch;
    _Init_thread_epoch = savedOurEpoch;
}

namespace {
// Our vtables, read from objects built by our constructors on a scratch buffer, and the game's.
void LearnVTables(difftest::Context &ctx)
{
    alignas(8) static uint8_t buf[0x64];
    rz::cRZCOMDllDirectorAdapters::Construct(buf, nullptr);
    std::memcpy(gVTableWords, buf, 8);
    rz::cRZCOMDllDirectorAdapters::Destruct(reinterpret_cast<rz::cRZCOMDllDirector *>(buf), nullptr);
    void **global = ctx.image.At<void *>(0x10c9378);
    void *saved = *global;
    SetupFramework(ctx);
    rz::cGZCOMDirectorAdapters::Construct(buf, nullptr);
    std::memcpy(gVTableWords + 2, buf, 8);
    rz::cRZCOMDllDirectorAdapters::Destruct(reinterpret_cast<rz::cRZCOMDllDirector *>(buf), nullptr);
    *global = saved;
    gSlotCalls = 0;
    const uint32_t game[4] = {0xda265c, 0xda26a4, 0xd9b754, 0xd9b7a0};
    for (int k = 0; k < 4; ++k) gVTableWords[4 + k] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At<void>(game[k])));
    if (gVTableWords[0] == gVTableWords[4] || gVTableWords[2] == gVTableWords[6]) ctx.Fail("our directors carry the game's vtables");
}
} // namespace
