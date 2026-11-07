// Differential tests for cHTMLElement's hit test (slot 7) and the rectangle test it calls (src/ui/cHTMLElement.h).
#include "difftest.h"
#include "free_recorder.h"
#include "ui/cHTMLElement.h"

#include <new>

#include <cstring>
#include <random>

namespace {

// Coordinates near the edges (each edge, one either side) plus extremes and random values.
int32_t Coordinate(std::mt19937 &rng, int32_t lo, int32_t hi)
{
    switch (rng() % 8) {
    case 0: return lo - 1;
    case 1: return lo;
    case 2: return hi - 1;
    case 3: return hi;
    case 4: return hi + 1;
    case 5: return static_cast<int32_t>(rng() % 2 ? 0x7fffffff : 0x80000000);
    case 6: return static_cast<int32_t>(rng());
    default: return lo + static_cast<int32_t>(rng() % 64) - 16;
    }
}

void RandomRect(std::mt19937 &rng, int32_t r[4])
{
    for (int i = 0; i < 4; ++i) r[i] = static_cast<int32_t>(rng() % 200) - 50;
    if (rng() % 8 == 0) r[rng() % 4] = static_cast<int32_t>(rng()); // anything, including inverted rectangles
}

int gAccepts = 0;
uint32_t gAcceptsEax = 0;
void *gAcceptsSelf = nullptr;

uint32_t __fastcall FakeAcceptsHits(void *self, void * /*edx*/)
{
    ++gAccepts;
    gAcceptsSelf = self;
    return gAcceptsEax; // the caller tests al only
}

void *gVTable[11] = {};

} // namespace

// The deleting destructor on a 0x1c-byte heap block per side, flags with bit 0 clear or set (and other bits): the
// result, the frees (FreeRecorder) and a surviving block's bytes (the vtable word the destructor stores included) are
// compared. A freed block's vtable store is not observable (the heap reuses the first bytes).
DIFF_TEST(HTMLElement_DeletingDestructor, 0x97e660)
{
    std::mt19937 rng(0x48470004);
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x97e660);
    for (int i = 0; i < 4000; ++i) {
        uint32_t flags = i % 4 == 0 ? 0 : i % 4 == 1 ? 1 : rng();
        uint32_t words[7];
        for (auto &w : words) w = rng();
        uint32_t *blocks[2];
        for (auto &b : blocks) {
            b = static_cast<uint32_t *>(::operator new(0x1c));
            std::memcpy(b, words, sizeof words);
        }
        void *results[2];
        bool freed[2];
        size_t frees[2];
        for (int side = 0; side < 2; ++side) {
            difftest::FreeRecorder rec;
            results[side] = side == 0 ? orig(blocks[side], flags)
                                      : reinterpret_cast<cHTMLElement *>(blocks[side])->DeletingDestructor(flags);
            rec.Stop();
            freed[side] = rec.Freed(blocks[side]);
            frees[side] = rec.Count();
        }
        ++ctx.cases;
        bool same = results[0] == blocks[0] && results[1] == blocks[1] && freed[0] == freed[1] && frees[0] == frees[1];
        if (same && !freed[0]) same = std::memcmp(blocks[0], blocks[1], 0x1c) == 0;
        if (!same) ctx.Fail("flags 0x%x: original freed %d (%zu frees), ours freed %d (%zu frees)", flags, freed[0], frees[0], freed[1], frees[1]);
        for (int side = 0; side < 2; ++side)
            if (!freed[side]) ::operator delete(blocks[side]);
    }
}

DIFF_TEST(HTMLRect_Contains, 0x5bbc56)
{
    std::mt19937 rng(0x48470001);
    auto orig = ctx.Original<uint32_t(__thiscall *)(const void *, int32_t, int32_t)>(0x5bbc56);
    for (int i = 0; i < 200000; ++i) {
        HTMLRect rect;
        int32_t r[4];
        RandomRect(rng, r);
        std::memcpy(&rect, r, sizeof r);
        int32_t x = Coordinate(rng, r[0], r[2]);
        int32_t y = Coordinate(rng, r[1], r[3]);
        uint8_t a = static_cast<uint8_t>(orig(&rect, x, y));
        uint8_t b = rect.Contains(x, y);
        ++ctx.cases;
        if (a != b) ctx.Fail("rect (%d,%d,%d,%d) point (%d,%d): original %u, ours %u", r[0], r[1], r[2], r[3], x, y, a, b);
    }
}

DIFF_TEST(HTMLElement_HitTest, 0x9892c0)
{
    std::mt19937 rng(0x48470002);
    gVTable[6] = reinterpret_cast<void *>(&FakeAcceptsHits);
    auto orig = ctx.Original<int32_t(__thiscall *)(void *, void **, int32_t, int32_t)>(0x9892c0);
    constexpr uintptr_t kUntouched = 0x5a5a5a5a;
    for (int i = 0; i < 100000; ++i) {
        int32_t r[4];
        RandomRect(rng, r);
        int32_t x = Coordinate(rng, r[0], r[2]);
        int32_t y = Coordinate(rng, r[1], r[3]);
        uint32_t acceptsEax = rng() % 3 == 0 ? 0 : (rng() % 2 ? 1 : (rng() & 0xffffff00u) | (rng() % 2 ? 0x80 : 0));
        uint32_t fill = rng();

        uint32_t objects[2][8];
        void *outs[2];
        int32_t results[2];
        int accepts[2];
        bool selves[2];
        for (int side = 0; side < 2; ++side) {
            uint32_t *o = objects[side];
            for (int k = 0; k < 8; ++k) o[k] = fill + k;
            o[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gVTable));
            std::memcpy(&o[3], r, sizeof r);
            outs[side] = reinterpret_cast<void *>(kUntouched);
            gAccepts = 0;
            gAcceptsSelf = nullptr;
            gAcceptsEax = acceptsEax;
            if (side == 0)
                results[side] = orig(o, &outs[side], x, y);
            else
                results[side] = reinterpret_cast<cHTMLElement *>(o)->HitTest(reinterpret_cast<cHTMLElement **>(&outs[side]), x, y);
            accepts[side] = gAccepts;
            selves[side] = gAcceptsSelf == o;
        }
        auto code = [&](int side) -> uintptr_t {
            uintptr_t v = reinterpret_cast<uintptr_t>(outs[side]);
            if (v == kUntouched) return 0;
            if (outs[side] == objects[side]) return 1;
            return v;
        };
        ++ctx.cases;
        if (results[0] != results[1] || code(0) != code(1) || accepts[0] != accepts[1] || selves[0] != selves[1] ||
            std::memcmp(objects[0], objects[1], sizeof objects[0]) != 0)
            ctx.Fail("rect (%d,%d,%d,%d) point (%d,%d) accepts 0x%x: original %d out %u calls %d; ours %d out %u calls %d", r[0],
                     r[1], r[2], r[3], x, y, acceptsEax, results[0], static_cast<unsigned>(code(0)), accepts[0], results[1],
                     static_cast<unsigned>(code(1)), accepts[1]);
    }
}
