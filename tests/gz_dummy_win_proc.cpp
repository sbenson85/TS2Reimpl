// cGZDummyWinProc (src/gz/cGZLegacyHandlers4.size.cpp, lane MK, foundations F6 part 5): the generic window's window procedure
// (vtable 0xe49270, 8 bytes: the vtable and a count). Each slot's original runs by its RVA on an object whose vtable word is
// the game's, against ours on the same object (the compiler's vtable); the director's AddRef / RemoveRef are the logging
// fakes of a replaced static director (tests/gz_com_support.h). The deletes are seen on both sides: the game's delete
// (0x824624) is stood in for by a logging one, and ours is a subclass whose class operator delete logs. Slots 0 (0x67546f) and
// 3 (0x1474b0) are shared bodies the game's vtable points at: ours is checked against them too, but they are not hooked.
#include "gz_com_support.h"

#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZLegacyHandlers.h"

#include <random>
#include <vector>

namespace {


std::vector<uint32_t> gEvents; // 1 director AddRef, 2 director RemoveRef, 3 freed

uint32_t __fastcall DirectorAddRef(void *, void *)
{
    gEvents.push_back(1);
    return 1;
}
uint32_t __fastcall DirectorRemoveRef(void *, void *)
{
    gEvents.push_back(2);
    return 1;
}
void __cdecl LoggingDelete(void *p, size_t)
{
    gEvents.push_back(3);
    ::operator delete(p);
}

// Ours, with a class operator delete that logs (the deleting destructor frees through it).
struct Probe : cGZDummyWinProc {
    ~Probe() override {}
    static void operator delete(void *p)
    {
        gEvents.push_back(3);
        ::operator delete(p);
    }
};
static_assert(sizeof(Probe) == cGZDummyWinProc::kSize, "the object is the vtable and a count");

// An object of the side: both sides are ours in memory, the original's with the game's vtable.
Probe *Make(int side, uint32_t count)
{
    auto *p = new Probe;
    p->mRefCount = count;
    if (side == 0) *reinterpret_cast<const void **>(p) = GameAddress<void>(0xe49270);
    return p;
}

struct Fakes {
    FakeStaticDirector director;
    shape::CodePatch gameDelete;
    explicit Fakes(difftest::Context &ctx) : director(ctx), gameDelete(0x824624, shape::CodeOf(&LoggingDelete))
    {
        gDirB.table[1] = reinterpret_cast<void *>(&DirectorAddRef);
        gDirB.table[10] = reinterpret_cast<void *>(&DirectorRemoveRef);
    }
};

const uint32_t kCounts[] = {0, 1, 2, 3, 7, 0xfffffffe};

void EndSide(std::vector<uint32_t> &b)
{
    b.push_back(uint32_t(gEvents.size()));
    b.insert(b.end(), gEvents.begin(), gEvents.end());
    gEvents.clear();
}

} // namespace

DIFF_TEST(GZDummyWinProc_AddRef, 0x85da25)
{
    // 0x85da25: the director counted, the object's count incremented and returned.
    Fakes fakes(ctx);
    using Fn = uint32_t(__thiscall *)(void *);
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        std::vector<uint32_t> results[2];
        for (int side = 0; side < 2; ++side) {
            Probe *p = Make(side, kCounts[seed % (sizeof(kCounts) / 4)]);
            const uint32_t r = side ? p->cGZDummyWinProc::AddRef() : ctx.Original<Fn>(0x85da25)(p);
            results[side].push_back(r);
            results[side].push_back(p->mRefCount);
            EndSide(results[side]);
            ::operator delete(p);
        }
        if (!svcfx::Same(ctx, results[0], results[1], "GZDummyWinProc_AddRef", seed)) break;
    }
}

DIFF_TEST(GZDummyWinProc_Release, 0x86de9e)
{
    // 0x86de9e: the director's count taken off; a count above one is decremented and returned, else the object is deleted
    // (through its deleting destructor) and 0 returned. Also reached: 0x85d9e6 (the original's deleting destructor).
    Fakes fakes(ctx);
    using Fn = uint32_t(__thiscall *)(void *);
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        std::vector<uint32_t> results[2];
        for (int side = 0; side < 2; ++side) {
            Probe *p = Make(side, kCounts[seed % (sizeof(kCounts) / 4)]);
            const uint32_t before = p->mRefCount;
            const uint32_t r = side ? p->cGZDummyWinProc::Release() : ctx.Original<Fn>(0x86de9e)(p);
            results[side].push_back(r);
            if (before > 1) { // still there
                results[side].push_back(p->mRefCount);
                ::operator delete(p);
            }
            EndSide(results[side]);
        }
        if (!svcfx::Same(ctx, results[0], results[1], "GZDummyWinProc_Release", seed)) break;
    }
}

DIFF_TEST(GZDummyWinProc_HandlesMessage, 0x863b3e)
{
    // 0x863b3e: true (al) for the two message types, whatever the other arguments.
    Fakes fakes(ctx);
    using Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    std::mt19937 rng(7);
    for (uint32_t seed = 1; seed <= 400; ++seed) {
        const uint32_t kinds[] = {cGZDummyWinProc::kMessageA, cGZDummyWinProc::kMessageB, uint32_t(rng()),
                                  cGZDummyWinProc::kMessageA + 2, cGZDummyWinProc::kMessageA - 1, 0};
        const uint32_t message = kinds[seed % 6];
        const uint32_t a = rng(), c = rng(), d = rng(), e = rng();
        std::vector<uint32_t> results[2];
        for (int side = 0; side < 2; ++side) {
            Probe *p = Make(side, 1);
            const bool r = side ? p->HandlesMessage(a, message, c, d, e)
                                : (ctx.Original<Fn>(0x863b3e)(p, a, message, c, d, e) & 0xff) != 0;
            results[side].push_back(r);
            // The message in the first argument's place and the third's is not the type.
            const bool other = side ? p->HandlesMessage(message, a, c, d, e)
                                    : (ctx.Original<Fn>(0x863b3e)(p, message, a, c, d, e) & 0xff) != 0;
            results[side].push_back(other);
            EndSide(results[side]);
            ::operator delete(p);
        }
        if (!svcfx::Same(ctx, results[0], results[1], "GZDummyWinProc_HandlesMessage", seed)) break;
    }
}

DIFF_TEST(GZDummyWinProc_Deleting, 0x85d9e6)
{
    // 0x85d9e6 (the deleting destructor, slot 5): with the low flag bit it also frees the object; it returns the object and
    // leaves the vtable word as the class's own.
    Fakes fakes(ctx);
    using Fn = void *(__thiscall *)(void *, uint32_t);
    for (uint32_t seed = 1; seed <= 100; ++seed) {
        std::vector<uint32_t> results[2];
        for (int side = 0; side < 2; ++side) {
            Probe *p = Make(side, kCounts[seed % (sizeof(kCounts) / 4)]);
            const uint32_t flag = seed & 1;
            void *r;
            if (side) {
                using Slot = void *(__thiscall *)(void *, uint32_t);
                r = reinterpret_cast<Slot *>(*reinterpret_cast<void ***>(p))[5](p, flag);
            } else {
                r = ctx.Original<Fn>(0x85d9e6)(p, flag);
            }
            results[side].push_back(r == p);
            if (!flag) {
                results[side].push_back(p->mRefCount);
                ::operator delete(p);
            }
            EndSide(results[side]);
        }
        if (!svcfx::Same(ctx, results[0], results[1], "GZDummyWinProc_Deleting", seed)) break;
    }
}

DIFF_TEST(GZDummyWinProc_Shared, 0x67546f)
{
    // The shared slots the game's vtable points at, not hooked: slot 0 (0x67546f, ids 1 and 0x22e85d8e answer with the object,
    // counted) and slot 3 (0x1474b0, false).
    Fakes fakes(ctx);
    using QueryFn = uint32_t(__thiscall *)(void *, uint32_t, void **);
    using Vf3Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
    const uint32_t ids[] = {1, cGZDummyWinProc::kQueryId, 0, 2, 0x22e85d8d, 0x22e85d8f, 0xffffffff};
    for (uint32_t seed = 1; seed <= 140; ++seed) {
        std::vector<uint32_t> results[2];
        for (int side = 0; side < 2; ++side) {
            Probe *p = Make(side, kCounts[seed % (sizeof(kCounts) / 4)]);
            void *out = nullptr;
            const uint32_t id = ids[seed % 7];
            const bool r = side ? p->QueryInterface(id, &out) : (ctx.Original<QueryFn>(0x67546f)(p, id, &out) & 0xff) != 0;
            results[side].push_back(r);
            results[side].push_back(out == p);
            results[side].push_back(p->mRefCount);
            const bool v = side ? p->vf3_cGZDummyWinProc(seed, 2) : (ctx.Original<Vf3Fn>(0x1474b0)(p, seed, 2) & 0xff) != 0;
            results[side].push_back(v);
            EndSide(results[side]);
            ::operator delete(p);
        }
        if (!svcfx::Same(ctx, results[0], results[1], "GZDummyWinProc_Shared", seed)) break;
    }
}
