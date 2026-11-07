// Differential tests for cInputEditFilter (lane MC, src/gz/cInputEditFilter.h), on objects laid out as the game's
// builder 0xcaaed0 leaves them (the game's vtables 0xeb5e4c / 0xeb5e60, a count), the same on both sides.
//
// InputEditFilter_Run: QueryInterface 0xcabbc0 (ids 1, 0xc6ae7085 and others; the AddRef through the object's own
// vtable, folded 0x1dab20), Filter 0xcaa120 (message types 0..8, keys Escape, the back quote and others; the window a
// recording fake whose GetParent (slot 11) gives another, whose DoMessage (slot 3) logs the message by content) and
// the deleting destructor 0xcaa030 (flags 0 and 1; reached in game through the thunk 0xca9fd9): the result, the
// pointer handed out, the object's words, whether the block was freed (difftest::FreeRecorder), the event log and the counts compared.
#include "code_patch.h"
#include "difftest.h"
#include "free_scope.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cInputEditFilter.h"

#include <new>
#include <random>

namespace input_edit_filter_test {

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using Filter = cInputEditFilter;

constexpr uint32_t kTag = 0x7e000000;
enum : unsigned { kWindow = 1, kParent };
constexpr uint32_t kIids[] = {1, Filter::kIid, 0xc6ae7086, 0x1a356083};
constexpr uint32_t kKeys[] = {0x1b, 0x60, 0x0d, 0x61, 0x1b00};

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

uint32_t DoMessage(Obj *, uint32_t *a)
{
    const auto *m = reinterpret_cast<const Filter::Message *>(uintptr_t(a[0]));
    Ev(0xd0);
    Ev(m->type);
    Ev(m->key);
    return Draw(2);
}

uint32_t *NewObject()
{
    auto *mem = static_cast<uint32_t *>(::operator new(Filter::kSize));
    mem[0] = uint32_t(uintptr_t(GameAddress<void>(Filter::kVTable)));
    mem[1] = uint32_t(uintptr_t(GameAddress<void>(Filter::kRefVTable)));
    mem[2] = 1 + Draw(3);
    svcfx::AddRange(mem, Filter::kSize, kTag);
    return mem;
}

} // namespace input_edit_filter_test

using namespace input_edit_filter_test;

DIFF_TEST(InputEditFilter_Run, 0xcaa120)
{
    // Also reached: 0xcabbc0 (QueryInterface), 0xcaa030 (the deleting destructor).
    svcfx::Globals globals;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    for (uint32_t seed = 1; seed <= 6000; ++seed) {
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            svcfx::ResetAll();
            F(kWindow).Set(11, 0, &F(kParent));
            F(kParent).Set(3, "p", 0, &DoMessage);
            gRng.seed(seed);
            uint32_t *mem = NewObject();
            auto *filter = reinterpret_cast<Filter *>(mem);
            Blob &b = results[side];
            const unsigned op = Draw(3);
            if (op == 0) {
                void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
                const uint32_t iid = kIids[Draw(4)];
                using Fn = bool(__thiscall *)(void *, uint32_t, void **);
                const bool r = side ? filter->QueryInterface(iid, &out) : ctx.Original<Fn>(0xcabbc0)(mem, iid, &out);
                b.push_back(r);
                b.push_back(svcfx::NormOut(svcfx::P(out), 0x5a5a5a5a));
            } else if (op == 1) {
                Filter::Message m{Draw(9), kKeys[Draw(5)]};
                auto *window = reinterpret_cast<ifaces::cIGZWin *>(&F(kWindow));
                using Fn = bool(__thiscall *)(void *, void *, void *);
                const bool r = side ? filter->Filter(window, &m) : ctx.Original<Fn>(0xcaa120)(mem, window, &m);
                b.push_back(r);
                b.push_back(m.type);
                b.push_back(m.key);
            } else {
                const uint32_t flags = Draw(2);
                using Fn = void *(__thiscall *)(void *, uint32_t);
                difftest::BeginFrees();
                void *r = side ? static_cast<void *>(filter->DeletingDestructor(flags))
                               : ctx.Original<Fn>(0xcaa030)(mem, flags);
                b.push_back(r == mem);
                const bool live = !difftest::FreedSinceBegin(mem);
                b.push_back(live);
                if (!live) mem = nullptr;
            }
            if (mem) {
                svcfx::DumpWords(b, mem, 12, "pdd"); // the vtable, then data
                ::operator delete(mem);
            }
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            svcfx::CaptureFakes(b);
            svcfx::gRanges.clear();
        }
        if (!svcfx::Same(ctx, results[0], results[1], "InputEditFilter_Run", seed)) break;
    }
}
