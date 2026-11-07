// Differential tests for the TopCallersA group. Fakes carry recording vtables, so the vtable calls
// each side makes are compared (order, arguments, and results), not just the return values.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "rz/TopCallersA.h"

#include <windows.h>

#include <cstring>
#include <random>

namespace {

// --- nTSWorld::cWorldDBMessageData::Release -------------------------------------------------------

// Both blocks are allocated before either side runs, so neither side's free can be reused by the
// other's allocation. A block is judged freed by a FreeRecorder across its own call. The vtable write
// before the free is not observable: the heap overwrites the block's first bytes when it frees it.
void TestWorldDBRelease(difftest::Context &ctx, std::mt19937 &rng)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x5449e0);
    for (int i = 0; i < 20000; ++i) {
        uint32_t refs = rng() % 5;
        auto *a = static_cast<nTSWorld::cWorldDBMessageData *>(::operator new(8));
        auto *b = static_cast<nTSWorld::cWorldDBMessageData *>(::operator new(8));
        a->mVTable = b->mVTable = ctx.image.At(nTSWorld::cWorldDBMessageData::kVTable);
        a->mRefCount = b->mRefCount = refs;

        difftest::FreeRecorder recA;
        uint32_t ra = orig(a);
        recA.Stop();
        bool freedA = recA.Freed(a);
        difftest::FreeRecorder recB;
        uint32_t rb = b->Release();
        recB.Stop();
        bool freedB = recB.Freed(b);
        ++ctx.cases;
        if (ra != rb || freedA != freedB) ctx.Fail("WorldDBRelease refs %u: original %u (freed %d), ours %u (freed %d)", refs, ra, freedA, rb, freedB);
        if (!freedA) {
            if (a->mRefCount != b->mRefCount || a->mVTable != b->mVTable) ctx.Fail("WorldDBRelease refs %u: survivor differs", refs);
            ::operator delete(a);
        }
        if (!freedB) ::operator delete(b);
    }
}

// --- SetWindowEnabled ------------------------------------------------------------------------------

struct Call {
    int slot;
    uint32_t a, b;
};
Call gCalls[8];
int gCallCount = 0;
void *gChild = nullptr;
uint32_t gQueryEax = 0;

void Record(int slot, uint32_t a, uint32_t b)
{
    if (gCallCount < 8) gCalls[gCallCount] = {slot, a, b};
    ++gCallCount;
}

void *__fastcall FakeGetChild(void * /*self*/, void * /*edx*/, uint32_t enabled)
{
    Record(39, enabled, 0);
    return gChild;
}
uint32_t __fastcall FakeQuery(void * /*self*/, void * /*edx*/, uint32_t flags)
{
    Record(82, flags, 0);
    return gQueryEax; // the caller reads only al
}
void __fastcall FakeSet(void * /*self*/, void * /*edx*/, uint32_t flags, uint32_t value)
{
    Record(83, flags, value);
}

void TestSetWindowEnabled(difftest::Context &ctx, std::mt19937 &rng)
{
    using Fn = uint8_t(__cdecl *)(void *, uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(0x93526f);

    void *windowVt[40] = {};
    windowVt[39] = reinterpret_cast<void *>(&FakeGetChild);
    void *childVt[84] = {};
    childVt[82] = reinterpret_cast<void *>(&FakeQuery);
    childVt[83] = reinterpret_cast<void *>(&FakeSet);

    void *child[2] = {childVt, nullptr};
    void *window[2] = {windowVt, nullptr};

    for (int i = 0; i < 20000; ++i) {
        uint32_t id = rng();
        uint32_t enabled = rng();
        if (i % 3 == 0) enabled &= 1; // mostly the usual booleans
        // The query result's low byte equals the enabled byte half the time, so both branches run.
        gQueryEax = (i & 1) ? (rng() & 0xffffff00u) | (enabled & 0xff) : rng();
        gChild = (i % 7 == 0) ? nullptr : child;
        uint32_t queryEax = gQueryEax;

        gCallCount = 0;
        uint8_t ra = orig(window, id, enabled);
        int countA = gCallCount;
        Call logA[8];
        std::memcpy(logA, gCalls, sizeof(logA));

        gCallCount = 0;
        gQueryEax = queryEax;
        uint8_t rb = SetWindowEnabled(window, id, enabled);
        int countB = gCallCount;

        ++ctx.cases;
        if (ra != rb || countA != countB) ctx.Fail("SetWindowEnabled(%u, %u): result %u vs %u, calls %d vs %d", id, enabled, ra, rb, countA, countB);
        for (int k = 0; k < countA && k < 8; ++k) {
            if (logA[k].slot != gCalls[k].slot || logA[k].a != gCalls[k].a || logA[k].b != gCalls[k].b) ctx.Fail("SetWindowEnabled(%u, %u): call %d differs", id, enabled, k);
        }
    }
}

// --- cRZMemPtrIteratorU16::Copy --------------------------------------------------------------------

void TestIteratorCopy(difftest::Context &ctx, std::mt19937 &rng)
{
    using Fn = void *(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0xa70130);
    for (int i = 0; i < 20000; ++i) {
        // The source's own vtable and +4 word are random: the copy must take neither.
        uint32_t src[4] = {rng(), rng(), rng(), rng()};
        auto *a = reinterpret_cast<uint32_t *>(orig(src));
        auto *b = reinterpret_cast<uint32_t *>(reinterpret_cast<cRZMemPtrIteratorU16 *>(src)->Copy());
        ++ctx.cases;
        if (!a || !b) {
            ctx.Fail("IteratorCopy returned null: original %p, ours %p", static_cast<void *>(a), static_cast<void *>(b));
        } else {
            if (std::memcmp(a, b, 16) != 0) ctx.Fail("IteratorCopy %d: copies differ (%08x %08x %08x %08x vs %08x %08x %08x %08x)", i, a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
            ::operator delete(a);
            ::operator delete(b);
        }
    }
}

// --- nGZCommandParser::cBlockCommand::StoreAndQuery ------------------------------------------------

int gValueCalls = 0;
uint32_t gValue = 0;
uint32_t __fastcall FakeValue(void * /*self*/, void * /*edx*/)
{
    ++gValueCalls;
    return gValue; // only the low byte is stored
}

void TestBlockCommand(difftest::Context &ctx, std::mt19937 &rng)
{
    using Fn = void(__thiscall *)(void *self, void *object, uint32_t value);
    auto orig = ctx.Original<Fn>(0xaf7bd0);

    void *objectVt[44] = {};
    objectVt[43] = reinterpret_cast<void *>(&FakeValue);
    void *object[2] = {objectVt, nullptr};

    for (int i = 0; i < 20000; ++i) {
        uint8_t initial[0x40];
        for (auto &byte : initial) byte = static_cast<uint8_t>(rng());
        uint8_t a[0x40], b[0x40];
        std::memcpy(a, initial, sizeof(a));
        std::memcpy(b, initial, sizeof(b));
        uint32_t value = rng();
        gValue = rng();

        gValueCalls = 0;
        orig(a, object, value);
        int callsA = gValueCalls;
        gValueCalls = 0;
        reinterpret_cast<nGZCommandParser::cBlockCommand *>(b)->StoreAndQuery(object, value);
        int callsB = gValueCalls;

        ++ctx.cases;
        if (callsA != callsB) ctx.Fail("BlockCommand: slot 43 called %d vs %d times", callsA, callsB);
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("BlockCommand %d: object bytes differ (flag %02x vs %02x)", i, a[0x3c], b[0x3c]);
    }
}

} // namespace

DIFF_TEST(TopCallersA_All, 0x5449e0)
{
    std::mt19937 rng(15);
    TestWorldDBRelease(ctx, rng);
    TestSetWindowEnabled(ctx, rng);
    TestIteratorCopy(ctx, rng);
    TestBlockCommand(ctx, rng);
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}
