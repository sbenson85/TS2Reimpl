// Differential test of std::vector<cMotiveInc>::_Emplace_reallocate (0x50da10), the person's motive-increment vector's
// reallocating insert (Prim1D appends through it when the vector is full).
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "simantics/PrimPerson.h"

#include <cstring>
#include <memory>
#include <random>
#include <vector>
#include <windows.h>

namespace {

constexpr uint32_t kMotiveRealloc = 0x50da10;

// A vector whose elements hold random words everywhere, including the vtable and the padding word the copy
// constructor never writes, so a copied vtable or padding shows up as a difference.
struct RawMotiveVector {
    MotiveIncVector v{};
    RawMotiveVector(size_t size, size_t capacity, std::mt19937 &rng)
    {
        if (capacity) {
            v.mFirst = std::allocator<MotiveInc>().allocate(capacity);
            v.mLast = v.mFirst + size;
            v.mEnd = v.mFirst + capacity;
            for (size_t i = 0; i < capacity; ++i) {
                uint32_t words[4];
                for (uint32_t &w : words) w = rng();
                std::memcpy(&v.mFirst[i], words, sizeof(words));
            }
        }
    }
    size_t Size() const { return static_cast<size_t>(v.mLast - v.mFirst); }
    size_t Capacity() const { return static_cast<size_t>(v.mEnd - v.mFirst); }
    void Release()
    {
        if (v.mFirst) std::allocator<MotiveInc>().deallocate(v.mFirst, Capacity());
        v = {};
    }
};

// The constructed state of an element: the vtable, the id word and the two floats' bits (not the padding).
struct Fields {
    const void *vtable;
    uint16_t motive;
    uint32_t rate, target;
    bool operator==(const Fields &o) const { return vtable == o.vtable && motive == o.motive && rate == o.rate && target == o.target; }
};

Fields Of(const MotiveInc &m)
{
    Fields f{m.mVTable, m.mMotive, 0, 0};
    std::memcpy(&f.rate, &m.mRate, 4);
    std::memcpy(&f.target, &m.mTarget, 4);
    return f;
}

} // namespace

DIFF_TEST(PrimPerson_MotiveEmplaceReallocate, kMotiveRealloc)
{
    auto original = ctx.Original<MotiveInc *(__thiscall *)(MotiveIncVector *, MotiveInc *, const MotiveInc *)>(kMotiveRealloc);
    const void *vtable = GameAddress<void>(0xdced28);
    std::mt19937 rng(kMotiveRealloc);
    uint32_t aliased = 0, atEnd = 0, inside = 0, empty = 0;
    for (int i = 0; i < 800; ++i) {
        size_t size = i % 13 == 0 ? 0 : rng() % 40;
        if (i % 47 == 0) size = 300 + rng() % 200; // blocks of 4 KB and more: the aligned big-block allocation
        size_t capacity = size == 0 && i % 2 == 0 ? 0 : size + (i % 4 == 0 ? 0 : rng() % 6);
        uint32_t seed = rng();
        std::mt19937 ra(seed), rb(seed);
        RawMotiveVector a(size, capacity, ra), b(size, capacity, rb);
        size_t at = i % 3 == 0 ? size : (size ? rng() % (size + 1) : 0);
        MotiveInc value;
        uint32_t words[4] = {rng(), rng(), rng(), rng()};
        std::memcpy(&value, words, sizeof(words));
        const MotiveInc *valueA = &value, *valueB = &value;
        if (i % 7 == 0 && size) { // the value lives inside the vector
            size_t k = rng() % size;
            valueA = a.v.mFirst + k;
            valueB = b.v.mFirst + k;
            value = *valueA;
            ++aliased;
        }
        if (!capacity) ++empty;
        else if (at == size) ++atEnd;
        else ++inside;

        // What both must produce: the old elements and the new one, each with a fresh vtable.
        std::vector<Fields> want;
        for (size_t k = 0; k < size; ++k) want.push_back(Of(a.v.mFirst[k]));
        want.insert(want.begin() + at, Of(value));
        for (Fields &f : want) f.vtable = vtable;

        MotiveInc *oldA = a.v.mFirst, *oldB = b.v.mFirst;
        bool smallBlock = capacity * sizeof(MotiveInc) < 4000;
        difftest::FreeRecorder recA;
        MotiveInc *ia = original(&a.v, a.v.mFirst + at, valueA);
        recA.Stop();
        bool freedA = oldA && smallBlock ? recA.Freed(oldA) : true;
        difftest::FreeRecorder recB;
        MotiveInc *ib = MotiveEmplaceReallocate(&b.v, nullptr, b.v.mFirst + at, valueB);
        recB.Stop();
        bool freedB = oldB && smallBlock ? recB.Freed(oldB) : true;
        ++ctx.cases;

        bool ok = a.Size() == size + 1 && b.Size() == size + 1 && a.Capacity() == b.Capacity();
        if (!ok) ctx.Fail("size %zu/%zu capacity %zu/%zu (old %zu/%zu)", a.Size(), b.Size(), a.Capacity(), b.Capacity(), size, capacity);
        else {
            for (size_t k = 0; k <= size; ++k) {
                if (!(Of(a.v.mFirst[k]) == want[k]) || !(Of(b.v.mFirst[k]) == want[k])) {
                    ctx.Fail("element %zu differs (size %zu, inserted at %zu)", k, size, at);
                    break;
                }
            }
            if (ia != a.v.mFirst + at || ib != b.v.mFirst + at) ctx.Fail("returned element %td/%td, want %zu", ia - a.v.mFirst, ib - b.v.mFirst, at);
        }
        if (!freedA || !freedB) ctx.Fail("old block still allocated: %d/%d", !freedA, !freedB);
        a.Release();
        b.Release();
    }
    if (aliased < 50 || atEnd < 100 || inside < 100 || empty < 20)
        ctx.Fail("coverage: aliased %u atEnd %u inside %u empty %u", aliased, atEnd, inside, empty);
}

DIFF_TEST(PrimPerson_MotiveReallocateExactly, 0x50f4d0)
{
    auto original = ctx.Original<void(__thiscall *)(MotiveIncVector *, const uint32_t *)>(0x50f4d0);
    const void *vtable = GameAddress<void>(0xdced28);
    std::mt19937 rng(0x50f4d0);
    uint32_t grown = 0, fromEmpty = 0, big = 0;
    for (int i = 0; i < 400; ++i) {
        size_t size = i % 17 == 0 ? 0 : rng() % 50;
        size_t capacity = size == 0 && i % 2 == 0 ? 0 : size + (i % 3 == 0 ? 0 : rng() % 6);
        uint32_t seed = rng();
        std::mt19937 ra(seed), rb(seed);
        RawMotiveVector a(size, capacity, ra), b(size, capacity, rb);
        // The caller (cTSPerson::vf198) asks for more than the capacity; also the same and a smaller one (at least the size).
        uint32_t want = static_cast<uint32_t>(capacity + 1 + rng() % 40);
        if (i % 11 == 0) want = static_cast<uint32_t>(size + rng() % (capacity - size + 1));
        if (i % 37 == 0) want += 300, ++big; // 4 KB and more: the aligned big-block allocation
        if (!capacity) ++fromEmpty;
        if (want > capacity) ++grown;
        std::vector<Fields> expected;
        for (size_t k = 0; k < size; ++k) expected.push_back(Of(a.v.mFirst[k]));
        for (Fields &f : expected) f.vtable = vtable;
        uint32_t wantA = want, wantB = want;
        MotiveInc *oldA = a.v.mFirst, *oldB = b.v.mFirst;
        bool smallBlock = capacity * sizeof(MotiveInc) < 4000;
        difftest::FreeRecorder recA;
        original(&a.v, &wantA);
        recA.Stop();
        bool freedA = oldA && smallBlock ? recA.Freed(oldA) : true;
        difftest::FreeRecorder recB;
        MotiveReallocateExactly(&b.v, nullptr, &wantB);
        recB.Stop();
        bool freedB = oldB && smallBlock ? recB.Freed(oldB) : true;
        ++ctx.cases;
        if (a.Size() != size || b.Size() != size || a.Capacity() != want || b.Capacity() != want)
            ctx.Fail("size %zu/%zu capacity %zu/%zu, want %zu and %u", a.Size(), b.Size(), a.Capacity(), b.Capacity(), size, want);
        else
            for (size_t k = 0; k < size; ++k)
                if (!(Of(a.v.mFirst[k]) == expected[k]) || !(Of(b.v.mFirst[k]) == expected[k])) {
                    ctx.Fail("element %zu differs (size %zu)", k, size);
                    break;
                }
        if (want && (!a.v.mFirst || !b.v.mFirst)) ctx.Fail("no block for %u elements", want);
        if (!freedA || !freedB) ctx.Fail("old block still allocated: %d/%d", !freedA, !freedB);
        a.Release();
        b.Release();
    }
    if (grown < 200 || fromEmpty < 10 || big < 5) ctx.Fail("coverage: grown %u fromEmpty %u big %u", grown, fromEmpty, big);
}

DIFF_TEST(PrimPerson_MotiveInsertN, 0x524d30)
{
    auto original = ctx.Original<MotiveInc **(__thiscall *)(MotiveIncVector *, MotiveInc **, MotiveInc *, uint32_t, const MotiveInc *)>(0x524d30);
    std::mt19937 rng(0x524d30);
    uint32_t none = 0, oneAtBack = 0, grown = 0, spill = 0, assign = 0, aliased = 0;
    for (int i = 0; i < 1500; ++i) {
        size_t size = i % 13 == 0 ? 0 : rng() % 30;
        size_t capacity = size == 0 && i % 2 == 0 ? 0 : size + rng() % 12;
        uint32_t seed = rng();
        std::mt19937 ra(seed), rb(seed);
        RawMotiveVector a(size, capacity, ra), b(size, capacity, rb);
        size_t at = i % 4 == 0 ? size : (size ? rng() % (size + 1) : 0);
        uint32_t count = i % 19 == 0 ? 0 : (i % 5 == 0 ? 1 : rng() % 16);
        if (i % 97 == 0) count = 300 + rng() % 100; // 4 KB and more
        MotiveInc value;
        uint32_t words[4] = {rng(), rng(), rng(), rng()};
        std::memcpy(&value, words, sizeof(words));
        const MotiveInc *valueA = &value, *valueB = &value;
        if (i % 6 == 0 && size) { // the value lives inside the vector (the library copies it first)
            size_t k = rng() % size;
            valueA = a.v.mFirst + k;
            valueB = b.v.mFirst + k;
            value = *valueA;
            ++aliased;
        }
        const bool grows = count > capacity - size;
        if (!count) ++none;
        else if (grows) ++grown;
        else if (count == 1 && at == size) ++oneAtBack;
        else if (count > size - at) ++spill;
        else ++assign;

        // The id and floats every element must end with.
        std::vector<Fields> want;
        for (size_t k = 0; k < size; ++k) want.push_back(Of(a.v.mFirst[k]));
        want.insert(want.begin() + at, count, Of(value));

        MotiveInc *outA = nullptr, *outB = nullptr;
        MotiveInc **ra2 = original(&a.v, &outA, a.v.mFirst + at, count, valueA);
        MotiveInc **rb2 = MotiveInsertN(&b.v, nullptr, &outB, b.v.mFirst + at, count, valueB);
        ++ctx.cases;
        if (ra2 != &outA || rb2 != &outB) ctx.Fail("returned %p/%p, want the out pointer", static_cast<void *>(ra2), static_cast<void *>(rb2));
        if (a.Size() != size + count || b.Size() != size + count || a.Capacity() != b.Capacity())
            ctx.Fail("size %zu/%zu capacity %zu/%zu (old %zu/%zu, count %u)", a.Size(), b.Size(), a.Capacity(), b.Capacity(), size, capacity, count);
        else {
            if (outA != a.v.mFirst + at || outB != b.v.mFirst + at) ctx.Fail("out %td/%td, want %zu", outA - a.v.mFirst, outB - b.v.mFirst, at);
            for (size_t k = 0; k < want.size(); ++k) {
                Fields fa = Of(a.v.mFirst[k]), fb = Of(b.v.mFirst[k]);
                // Without reallocation the whole element must match, padding included (neither side writes it,
                // and an assigned element keeps its vtable); in a new block only the constructed fields.
                bool same = grows ? fa == fb : std::memcmp(&a.v.mFirst[k], &b.v.mFirst[k], sizeof(MotiveInc)) == 0;
                fa.vtable = fb.vtable = want[k].vtable = nullptr;
                if (!same || !(fa == want[k]) || !(fb == want[k])) {
                    ctx.Fail("element %zu differs (size %zu, capacity %zu, %u at %zu)", k, size, capacity, count, at);
                    break;
                }
            }
            // The unused capacity beyond the end is untouched without reallocation.
            if (!grows && std::memcmp(a.v.mLast, b.v.mLast, (a.Capacity() - a.Size()) * sizeof(MotiveInc)) != 0)
                ctx.Fail("spare capacity differs (size %zu, %u at %zu)", size, count, at);
        }
        a.Release();
        b.Release();
    }
    if (none < 30 || oneAtBack < 30 || grown < 200 || spill < 100 || assign < 100 || aliased < 100)
        ctx.Fail("coverage: none %u oneAtBack %u grown %u spill %u assign %u aliased %u", none, oneAtBack, grown, spill, assign, aliased);
}
