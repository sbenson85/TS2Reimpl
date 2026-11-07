// Differential tests for cRZKeyboard (lane KQ, src/gz/cRZKeyboard.h) and the shared import helper (src/game/import.h).
//
// Keyboard_Run: a keyboard built by the original constructor (0x9cff2f, game vtable 0xe61c38) against one built by ours,
// each driven through its own vtable by the same random operations: Reset 0x9cff68, IsKeyDownWithModifiers 0x9cffd7,
// IsKeyDown 0x9cffcc, IsKeyDownNow 0x9cfff9 (through AsyncKeyDown 0x9d0006), IsModifierKey 0x9cff4b, GetModifiers
// 0x9cff8d, IsKeyToggled 0x9d002e, KeyDown 0x9d0048 and KeyUp 0x9d006a, on the boundary keys 0 and 0xff, the modifier
// keys 0x10..0x12 and their neighbours 0xf / 0x13, the buttons 1 / 2 and random keys. The Win32 calls go through the
// game's import slots (GetSystemMetrics 0xd9a4a4, GetAsyncKeyState 0xd9a548, GetKeyState 0xd9a5a4), pointed at logging
// fakes for both sides. Compared after every operation: the result (al for the bool slots), the key bytes, the order
// list, the vtable word (game / ours) and the fakes' log. The game's operator new / delete (0x824632 / 0x824624, the
// list's head and nodes) are ours, so the original's list is freed by our destructor.
//
// ImportFunction_Slot: ImportFunction<Fn, rva>() reads the slot when called (a slot changed between calls is followed), and
// both the original AsyncKeyDown 0x9d0006 and ours call whatever the slot holds.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "game/import.h"
#include "gz/cRZKeyboard.h"

#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace keyboard_test {

using svcfx::Blob;
using svcfx::Ev;

constexpr uint32_t kIatGetSystemMetrics = 0xd9a4a4, kIatGetAsyncKeyState = 0xd9a548, kIatGetKeyState = 0xd9a5a4;
constexpr uint32_t kGameVTable = 0xe61c38;

// An import slot pointed at a fake for the scope.
struct IatSlot {
    void **slot;
    void *saved;
    IatSlot(uint32_t rva, void *fake) : slot(GameAddress<void *>(rva)), saved(*slot) { *slot = fake; }
    ~IatSlot() { *slot = saved; }
    IatSlot(const IatSlot &) = delete;
    IatSlot &operator=(const IatSlot &) = delete;
};

// The fakes' answers: GetSystemMetrics(SM_SWAPBUTTON) from gSwap, the key states from a mix of the key and gSalt.
uint32_t gSwap = 0, gSalt = 0;
int __stdcall FakeGetSystemMetrics(int index)
{
    Ev(0x51), Ev(uint32_t(index));
    return int(gSwap);
}
int16_t __stdcall FakeGetAsyncKeyState(int key)
{
    Ev(0x52), Ev(uint32_t(key));
    return int16_t((uint32_t(key) * 2654435761u ^ gSalt) >> 7);
}
int16_t __stdcall FakeAllDown(int key)
{
    Ev(0x54), Ev(uint32_t(key));
    return int16_t(0x8000);
}
int16_t __stdcall FakeGetKeyState(int key)
{
    Ev(0x53), Ev(uint32_t(key));
    return int16_t((uint32_t(key) * 40503u ^ gSalt) >> 3);
}

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

template <class R, class... A> R Slot(void *object, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(object))[slot]);
    return fn(object, a...);
}

uint32_t PickKey(std::mt19937 &rng)
{
    static const uint32_t kKeys[] = {0, 0xff, 0x10, 0x11, 0x12, 0xf, 0x13, 1, 2, 0x41, 0x80, 0xfe};
    return rng() % 3 ? kKeys[rng() % (sizeof(kKeys) / sizeof(kKeys[0]))] : rng() % 0x100;
}

void Dump(Blob &b, const cRZKeyboard &k, bool ours)
{
    const bool game = *reinterpret_cast<void *const *>(&k) == GameAddress<void>(kGameVTable);
    b.push_back(ours ? !game : game);
    for (uint32_t i = 0; i < cRZKeyboard::kKeyCount; i += 4) {
        uint32_t w;
        std::memcpy(&w, &k.mDown[i], 4);
        b.push_back(w);
    }
    b.push_back(uint32_t(k.mDownOrder.size()));
    for (uint32_t key : k.mDownOrder) b.push_back(key);
}

void RunOp(Blob &b, void *k, std::mt19937 &rng)
{
    const uint32_t key = PickKey(rng);
    switch (rng() % 12) {
    case 0: if (rng() % 4 == 0) Slot<void>(k, 0); break;
    case 1: b.push_back(Slot<uint32_t>(k, 1, key, rng() % 9) & 0xff); break;
    case 2: b.push_back(Slot<uint32_t>(k, 2, key) & 0xff); break;
    case 3:
        gSwap = rng() % 3 == 0 ? 0x10000u * (rng() % 3) + (rng() % 2) : 0;
        b.push_back(Slot<uint32_t>(k, 3, key) & 0xff);
        break;
    case 4: b.push_back(Slot<uint32_t>(k, 4, key) & 0xff); break;
    case 5: b.push_back(Slot<uint32_t>(k, 5)); break;
    case 6: b.push_back(Slot<uint32_t>(k, 6, key) & 0xff); break;
    case 7: case 8: case 9: Slot<void>(k, 7, key); break;
    default: Slot<void>(k, 8, key); break;
    }
}

} // namespace keyboard_test

using namespace keyboard_test;

DIFF_TEST(Keyboard_Run, 0x9cff2f)
{
    svcfx::gEv.clear(); // events an earlier test left behind are not this test's
    // Also reached (see the file comment): 0x9cff68, 0x9cffd7, 0x9cffcc, 0x9cfff9, 0x9d0006, 0x9cff4b, 0x9cff8d, 0x9d002e,
    // 0x9d0048, 0x9d006a.
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    IatSlot metrics(kIatGetSystemMetrics, reinterpret_cast<void *>(&FakeGetSystemMetrics));
    IatSlot async(kIatGetAsyncKeyState, reinterpret_cast<void *>(&FakeGetAsyncKeyState));
    IatSlot state(kIatGetKeyState, reinterpret_cast<void *>(&FakeGetKeyState));
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed);
            gSalt = rng();
            void *memory = ::operator new(cRZKeyboard::kSize);
            std::memset(memory, 0xc5, cRZKeyboard::kSize);
            if (side) ::new (memory) cRZKeyboard;
            else ctx.Original<void *(__thiscall *)(void *)>(0x9cff2f)(memory);
            auto *k = static_cast<cRZKeyboard *>(memory);
            const unsigned ops = 1 + rng() % 60;
            for (unsigned i = 0; i < ops; ++i) {
                Blob b;
                RunOp(b, memory, rng);
                Dump(b, *k, side == 1);
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                results[side].push_back(std::move(b));
            }
            k->~cRZKeyboard();
            ::operator delete(memory);
        }
        bool same = true;
        for (size_t i = 0; i < results[0].size() && same; ++i)
            same = svcfx::Same(ctx, results[0][i], results[1][i], "Keyboard_Run", seed);
        if (!same) break;
    }
}

DIFF_TEST(ImportFunction_Slot, 0x9d0006)
{
    svcfx::gEv.clear(); // events an earlier test left behind are not this test's
    using AsyncFn = int16_t(__stdcall *)(int);
    auto original = ctx.Original<bool(__cdecl *)(uint32_t)>(0x9d0006);
    IatSlot metrics(kIatGetSystemMetrics, reinterpret_cast<void *>(&FakeGetSystemMetrics));
    for (int round = 0; round < 2; ++round) {
        // Round 0 the logging fake, round 1 a fake answering every key as down.
        IatSlot async(kIatGetAsyncKeyState, round ? reinterpret_cast<void *>(&FakeAllDown)
                                                  : reinterpret_cast<void *>(&FakeGetAsyncKeyState));
        ++ctx.cases;
        if (ImportFunction<AsyncFn, kIatGetAsyncKeyState>() != *GameAddress<AsyncFn>(kIatGetAsyncKeyState))
            ctx.Fail("ImportFunction: not the slot's word");
        for (uint32_t key = 0; key < 0x100; ++key) {
            gSwap = key & 1;
            Blob a, b;
            a.push_back(original(key) & 0xff);
            a.insert(a.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            b.push_back(cRZKeyboard::AsyncKeyDown(key) & 0xff);
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            if (b.size() < 2) ctx.Fail("ImportFunction: our call did not reach the patched slot (key %u)", key);
            if (!svcfx::Same(ctx, a, b, "ImportFunction_Slot", key)) return;
        }
    }
}
