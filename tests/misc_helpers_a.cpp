// Differential tests for the helpers in src/rz/MiscHelpersA.h.
//
// cRZStd::Construct (0x1bf920) reads one global byte (RVA 0xff246c); both sides see the same value
// and the test sweeps it. The vtable stubs and the constructor of the unidentified class are
// compared on the whole object, with the bytes around it filled with a pattern.
//
// The service helpers (0x18de70, 0xb41750) use the service object in game global 0x10c9378, which
// the tests point at a fake whose slot 5 hands out a fake held object. Every fake vtable slot
// records its call (object, arguments), so the call sequences are compared along with the results.
// A null service is not tested: the original dereferences the held object and crashes.
//
// TreeEraser::EraseSubtree (0x1fc790) is run on random trees built in the MSVC node layout, with
// fake values whose slot 2 records their ids. The nodes are freed by the original and by ours
// through the same sized operator delete; the frees can't be observed from the harness, so the
// test compares the release order, the walk and the return value.
#include "difftest.h"
#include "rz/MiscHelpersA.h"

#include <windows.h>

#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace {

using CtorCRZStd = void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
using EraseFn = void *(__thiscall *)(void *, void *, TreeNodeMapped *);

// Fills an object with a pattern, so the bytes around what a constructor writes are compared too.
template <typename T> void FillPattern(T &object, uint32_t seed)
{
    uint8_t *p = reinterpret_cast<uint8_t *>(&object);
    for (size_t i = 0; i < sizeof(object); ++i) p[i] = static_cast<uint8_t>(seed * 31u + i * 7u + 0x5a);
}

// ---- Service fakes ----------------------------------------------------------------------------

struct Call {
    int slot;
    const void *self;
    uint32_t a, b, c;
    const void *wrote;
};

std::vector<Call> gCalls;
uint32_t gHeldResult = 0; // what slot 30 returns (0 is a valid answer: the default is used)

struct FakeObject {
    void *vtable;
    uint32_t pad;
};

void *gServiceVTable[8];
void *gHeldVTable[32];
FakeObject gService{}, gHeld{};

// Fakes are __thiscall: `this` in ecx, the stack arguments popped by the callee. A __fastcall
// function with a dummy edx takes the same registers and stack layout.
uint32_t __fastcall RecordQuery(void *self, void * /*edx*/, uint32_t first, uint32_t second, void **out)
{
    gCalls.push_back({5, self, first, second, 0, &gHeld});
    *out = &gHeld;
    return 0;
}

uint32_t __fastcall RecordRelease(void *self, void * /*edx*/)
{
    gCalls.push_back({2, self, 0, 0, 0, nullptr});
    return 0;
}

uint32_t __fastcall RecordSlot7(void *self, void * /*edx*/, uint32_t a, uint32_t b, uint32_t c)
{
    gCalls.push_back({7, self, a, b, c, nullptr});
    return 0;
}

uint32_t __fastcall RecordSlot30(void *self, void * /*edx*/, uint32_t arg)
{
    gCalls.push_back({30, self, arg, 0, 0, nullptr});
    return gHeldResult;
}

void SetupFakes()
{
    std::memset(gServiceVTable, 0, sizeof(gServiceVTable));
    std::memset(gHeldVTable, 0, sizeof(gHeldVTable));
    gServiceVTable[5] = reinterpret_cast<void *>(&RecordQuery);
    gHeldVTable[2] = reinterpret_cast<void *>(&RecordRelease);
    gHeldVTable[7] = reinterpret_cast<void *>(&RecordSlot7);
    gHeldVTable[30] = reinterpret_cast<void *>(&RecordSlot30);
    gService.vtable = gServiceVTable;
    gHeld.vtable = gHeldVTable;
}

bool SameCalls(const std::vector<Call> &a, const std::vector<Call> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const Call &x = a[i], &y = b[i];
        if (x.slot != y.slot || x.self != y.self || x.a != y.a || x.b != y.b || x.c != y.c || x.wrote != y.wrote)
            return false;
    }
    return true;
}

void SetService(difftest::Context &ctx, void *service)
{
    *ctx.image.At<void *>(0x10c9378) = service;
}

// ---- Tree helpers -----------------------------------------------------------------------------

struct TreeValue {
    void *vtable;
    uint32_t id;
};

std::vector<uint32_t> gReleased; // ids released through slot 2, in order
TreeValue gValues[512];
void *gValueVTable[4];
TreeNodeMapped gNil{}; // the sentinel: isNil set, its links are never followed

uint32_t __fastcall RecordTreeRelease(void *self, void * /*edx*/)
{
    gReleased.push_back(static_cast<TreeValue *>(self)->id);
    return 0;
}

// Builds a random tree of `count` nodes in the MSVC layout. The values come from gValues in
// creation order; about one node in five gets a null value. The shape and the values depend only
// on the generator, so the same seed builds the same tree twice.
TreeNodeMapped *BuildTree(std::mt19937 &gen, int count, int &nextIndex)
{
    if (count <= 0) return &gNil;
    int leftCount = static_cast<int>(gen() % static_cast<uint32_t>(count));
    TreeNodeMapped *left = BuildTree(gen, leftCount, nextIndex);
    TreeNodeMapped *right = BuildTree(gen, count - 1 - leftCount, nextIndex);
    void *raw = ::operator new(TreeNodeMapped::kSize);
    TreeNodeMapped *node = new (raw) TreeNodeMapped{};
    node->mLeft = left;
    node->mRight = right;
    node->mColor = 1;
    node->mIsNil = 0;
    node->mKey = static_cast<uint32_t>(nextIndex);
    bool nullValue = gen() % 5 == 0;
    node->mValue = nullValue ? nullptr : static_cast<void *>(&gValues[nextIndex]);
    ++nextIndex;
    return node;
}

} // namespace

DIFF_TEST(cRZStd_Construct, 0x1bf920)
{
    auto orig = ctx.Original<CtorCRZStd>(0x1bf920);
    uint8_t *flagByte = ctx.image.At<uint8_t>(0xff246c);
    uint8_t savedFlag = *flagByte;
    const uint8_t flagValues[] = {0, 1, 0x80};
    std::mt19937 gen(4242);
    for (uint8_t flag : flagValues) {
        *flagByte = flag;
        for (uint32_t mode = 0; mode < 0x1000; ++mode) {
            for (int round = 0; round < 3; ++round) {
                uint32_t mode32 = round == 0 ? mode : (mode | (gen() & 0xfffff000u));
                uint32_t first = gen(), second = gen();
                uint32_t oldFlags = round == 0 ? 0u : gen();
                cRZStd a{}, b{};
                a.mFlags = b.mFlags = oldFlags;
                void *retA = orig(&a, mode32, first, second);
                cRZStd *retB = b.Construct(mode32, first, second);
                ++ctx.cases;
                if (retA != &a || retB != &b || std::memcmp(&a, &b, sizeof(a)) != 0)
                    ctx.Fail("Construct(flag %u, mode 0x%x, old 0x%x): original flags 0x%x, ours 0x%x", flag, mode32,
                             oldFlags, a.mFlags, b.mFlags);
            }
        }
    }
    *flagByte = savedFlag;
}

DIFF_TEST(cTSAudioAmbienceGlobalPrimitiveBase_Construct, 0x2439f4)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0x2439f4);
    for (uint32_t seed = 0; seed < 8; ++seed) {
        uint8_t a[16], b[16];
        FillPattern(a, seed);
        std::memcpy(b, a, sizeof(a));
        orig(a);
        reinterpret_cast<cTSAudioAmbienceGlobalPrimitiveBase *>(b)->Construct();
        ++ctx.cases;
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("base constructor stub differs (seed %u)", seed);
    }
}

DIFF_TEST(cTSAudioAmbienceGlobalPrimitiveBase_ConstructWithArg, 0x2439de)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x2439de);
    std::mt19937 gen(2439);
    for (uint32_t seed = 0; seed < 500; ++seed) {
        uint32_t arg = seed == 0 ? 0 : gen();
        uint8_t a[16], b[16];
        FillPattern(a, seed);
        std::memcpy(b, a, sizeof(a));
        void *retA = orig(a, arg);
        void *retB = reinterpret_cast<cTSAudioAmbienceGlobalPrimitiveBase *>(b)->ConstructWithArg(arg);
        ++ctx.cases;
        if (retA != a || retB != b || std::memcmp(a, b, sizeof(a)) != 0)
            ctx.Fail("constructor(0x%08x) differs (seed %u)", arg, seed);
    }
}

DIFF_TEST(TreeEraser_EraseSubtree, 0x1fc790)
{
    gValueVTable[2] = reinterpret_cast<void *>(&RecordTreeRelease);
    for (uint32_t i = 0; i < 512; ++i) {
        gValues[i].vtable = gValueVTable;
        gValues[i].id = i;
    }
    gNil.mIsNil = 1;
    auto orig = ctx.Original<EraseFn>(0x1fc790);
    std::mt19937 gen(31337);
    int head = 0; // the head argument is only passed along
    for (int round = 0; round < 3000; ++round) {
        int count = static_cast<int>(gen() % 24);
        uint32_t seed = gen();
        // Two identical trees from the same seed: one for the original, one for ours.
        std::mt19937 genA(seed), genB(seed);
        int nextA = 0, nextB = 0;
        TreeNodeMapped *rootA = BuildTree(genA, count, nextA);
        TreeNodeMapped *rootB = BuildTree(genB, count, nextB);

        TreeEraser eraserA{}, eraserB{};
        gReleased.clear();
        void *retA = orig(&eraserA, &head, rootA);
        std::vector<uint32_t> releasedA = gReleased;
        gReleased.clear();
        void *retB = eraserB.EraseSubtree(&head, rootB);
        ++ctx.cases;
        if (retA != &eraserA || retB != &eraserB)
            ctx.Fail("EraseSubtree (count %d) returned %p / %p, expected this", count, retA, retB);
        if (releasedA != gReleased)
            ctx.Fail("EraseSubtree (count %d): original released %zu values, ours %zu", count, releasedA.size(),
                     gReleased.size());
    }
}

DIFF_TEST(QueryServiceValue, 0x18de70)
{
    SetupFakes();
    auto orig = ctx.Original<uint32_t(__cdecl *)(uint32_t)>(0x18de70);
    std::mt19937 gen(555);
    const uint32_t results[] = {0, 0x12345678, 0xdeadbeef};
    for (uint32_t result : results) {
        for (int i = 0; i < 300; ++i) {
            uint32_t arg = i == 0 ? 0 : gen();
            SetService(ctx, &gService);
            gHeldResult = result;
            gCalls.clear();
            uint32_t a = orig(arg);
            std::vector<Call> callsA = gCalls;

            SetService(ctx, &gService);
            gHeldResult = result;
            gCalls.clear();
            void *b = QueryServiceValue(arg);
            ++ctx.cases;
            if (a != reinterpret_cast<uintptr_t>(b) || !SameCalls(callsA, gCalls))
                ctx.Fail("QueryServiceValue(0x%08x) result 0x%x: original 0x%x (%zu calls), ours %p (%zu calls)", arg,
                         result, a, callsA.size(), b, gCalls.size());
        }
    }
    SetService(ctx, nullptr);
    gHeldResult = 0;
}

namespace {

// What cRZStd's slot 2 saw: the object's fields and every argument the format can read (the
// original also leaves the code on the stack after the value; the format "%s" takes one).
struct LogLine {
    uint32_t flags, first, second;
    const void *self;
    const char *category;
    uint32_t number;
    const char *format;
    void *value;
};
std::vector<LogLine> gLogLines;

void __cdecl RecordLogLine(cRZStd *self, const char *category, uint32_t number, const char *format, void *value)
{
    gLogLines.push_back({self->mFlags, self->mFirst, self->mSecond, nullptr, category, number, format, value});
}

// Slot 2 of cRZStd's vtable (0xda3e6c) points at RecordLogLine while alive. The real slot formats
// the text and writes it out through the file and debugger imports (0x1bff41), which the harness traps.
struct LogSlotPatch {
    void **slot;
    void *saved;
    explicit LogSlotPatch(difftest::Context &ctx) : slot(ctx.image.At<void *>(cRZStd::kVTable + 8))
    {
        DWORD old;
        VirtualProtect(slot, 4, PAGE_READWRITE, &old);
        saved = *slot;
        *slot = reinterpret_cast<void *>(&RecordLogLine);
    }
    ~LogSlotPatch() { *slot = saved; }
};

bool SameLines(const std::vector<LogLine> &a, const std::vector<LogLine> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::memcmp(&a[i], &b[i], sizeof(LogLine)) != 0) return false;
    return true;
}

} // namespace

DIFF_TEST(LogD3DError, 0x18def0)
{
    SetupFakes();
    LogSlotPatch patch(ctx);
    auto orig = ctx.Original<void(__cdecl *)(uint32_t, uint32_t, uint32_t)>(0x18def0);
    std::mt19937 gen(18);
    const uint32_t results[] = {0, 0x12345678};
    for (uint32_t result : results) {
        for (int i = 0; i < 300; ++i) {
            uint32_t code = i == 0 ? 0 : gen(), first = gen(), second = gen();
            SetService(ctx, &gService);
            gHeldResult = result;
            gCalls.clear();
            gLogLines.clear();
            orig(code, first, second);
            std::vector<Call> callsA = gCalls;
            std::vector<LogLine> linesA = gLogLines;

            SetService(ctx, &gService);
            gHeldResult = result;
            gCalls.clear();
            gLogLines.clear();
            LogD3DError(code, first, second);
            ++ctx.cases;
            if (linesA.size() != 1 || !SameCalls(callsA, gCalls) || !SameLines(linesA, gLogLines))
                ctx.Fail("LogD3DError(0x%08x, 0x%08x, 0x%08x): original %zu lines (%zu calls), ours %zu (%zu calls)", code,
                         first, second, linesA.size(), callsA.size(), gLogLines.size(), gCalls.size());
        }
    }
    SetService(ctx, nullptr);
    gHeldResult = 0;
}

DIFF_TEST(ServiceCallWithHeld, 0xb41750)
{
    SetupFakes();
    auto orig = ctx.Original<void(__cdecl *)(uint32_t, uint32_t)>(0xb41750);
    std::mt19937 gen(777);
    for (int i = 0; i < 300; ++i) {
        uint32_t arg1 = i == 0 ? 0 : gen(), arg2 = i == 0 ? 0 : gen();
        SetService(ctx, &gService);
        gCalls.clear();
        orig(arg1, arg2);
        std::vector<Call> callsA = gCalls;

        SetService(ctx, &gService);
        gCalls.clear();
        ServiceCallWithHeld(arg1, arg2);
        ++ctx.cases;
        if (!SameCalls(callsA, gCalls))
            ctx.Fail("ServiceCallWithHeld(0x%08x, 0x%08x): original %zu calls, ours %zu", arg1, arg2, callsA.size(),
                     gCalls.size());
    }
    SetService(ctx, nullptr);
}
