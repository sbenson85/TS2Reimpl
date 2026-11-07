// Differential tests of the library vector workers of the person's ActionRecord vectors (src/simantics/PersonVectors.cpp).
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "simantics/PersonVectors.h"

#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <vector>
#include <windows.h>

namespace {

// Interactions that log their AddRef (+id) and Release (-id) calls.
std::vector<int> gEvents;

struct FakeInteraction {
    void **mVTable;
    int mId;
};

uint32_t __fastcall FakeAddRef(FakeInteraction *self, void *)
{
    gEvents.push_back(self->mId);
    return 1;
}

uint32_t __fastcall FakeRelease(FakeInteraction *self, void *)
{
    gEvents.push_back(-self->mId);
    return 1;
}

void *gFakeVTable[3] = {nullptr, reinterpret_cast<void *>(&FakeAddRef), reinterpret_cast<void *>(&FakeRelease)};

// A record with random fields, a text of 0..40 characters (inline and allocated) and maybe an interaction.
void FillRecord(ActionRecord *r, std::mt19937 &rng, std::vector<FakeInteraction> &objects)
{
    new (r) ActionRecord();
    r->mStackObjectId = static_cast<uint16_t>(rng());
    r->mEntryIndex = static_cast<uint16_t>(rng());
    uint32_t bits = rng();
    std::memcpy(&r->mScore, &bits, 4);
    r->mParameterA = rng();
    r->mParameterB = rng();
    r->mParameterC = static_cast<int16_t>(rng());
    r->mFlag12 = static_cast<uint8_t>(rng());
    for (uint16_t &a : r->mArguments) a = static_cast<uint16_t>(rng());
    for (float *f : {&r->mMotiveScore, &r->mDistanceScale, &r->mDistance}) {
        bits = rng();
        std::memcpy(f, &bits, 4);
    }
    const size_t length = rng() % 41;
    for (size_t k = 0; k < length; ++k) r->mName.mText.push_back(static_cast<char>('a' + rng() % 26));
    r->mName.mRefCount = rng(); // a copy starts its own count at zero
    r->mChecked = static_cast<uint8_t>(rng());
    if (rng() % 4) {
        gEvents.clear();
        r->mInteraction.Hold(&objects[rng() % objects.size()]);
    }
}

struct RecordVector {
    ActionRecordVector v{};
    size_t Size() const { return static_cast<size_t>(v.mLast - v.mFirst); }
    size_t Capacity() const { return static_cast<size_t>(v.mEnd - v.mFirst); }
    void Release()
    {
        if (v.mFirst) {
            std::destroy(v.mFirst, v.mLast);
            std::allocator<ActionRecord>().deallocate(v.mFirst, Capacity());
        }
        v = {};
    }
};

// The copied state of a record: every field but the padding.
bool SameRecord(const ActionRecord &a, const ActionRecord &b)
{
    return a.mStackObjectId == b.mStackObjectId && a.mEntryIndex == b.mEntryIndex && std::memcmp(&a.mScore, &b.mScore, 4) == 0 &&
           a.mParameterA == b.mParameterA && a.mParameterB == b.mParameterB && a.mParameterC == b.mParameterC && a.mFlag12 == b.mFlag12 &&
           std::memcmp(a.mArguments, b.mArguments, sizeof(a.mArguments)) == 0 && std::memcmp(&a.mMotiveScore, &b.mMotiveScore, 12) == 0 &&
           a.mName.mVTable == b.mName.mVTable && a.mName.mText == b.mName.mText && a.mName.mRefCount == b.mName.mRefCount &&
           a.mChecked == b.mChecked && a.mInteraction.Get() == b.mInteraction.Get();
}

} // namespace

DIFF_TEST(PersonVectors_ActionRecordsReallocateExactly, 0x50f560)
{
    auto original = ctx.Original<void(__thiscall *)(ActionRecordVector *, const uint32_t *)>(0x50f560);
    std::vector<FakeInteraction> objects(8);
    for (size_t k = 0; k < objects.size(); ++k) objects[k] = {gFakeVTable, static_cast<int>(k + 1)};
    std::mt19937 rng(0x50f560);
    uint32_t grown = 0, fromEmpty = 0, big = 0;
    for (int i = 0; i < 300; ++i) {
        size_t size = i % 17 == 0 ? 0 : rng() % 30;
        size_t capacity = size == 0 && i % 2 == 0 ? 0 : size + (i % 3 == 0 ? 0 : rng() % 6);
        uint32_t want = static_cast<uint32_t>(capacity + 1 + rng() % 40);
        if (i % 11 == 0) want = static_cast<uint32_t>(size + rng() % (capacity - size + 1));
        if (i % 29 == 0) want += 60, ++big; // 4 KB and more: the aligned big-block allocation
        if (!capacity) ++fromEmpty;
        if (want > capacity) ++grown;

        // Both vectors hold equal copies of the same records.
        const uint32_t seed = rng();
        RecordVector a, b;
        for (RecordVector *rv : {&a, &b}) {
            std::mt19937 rr(seed);
            if (capacity) {
                rv->v.mFirst = std::allocator<ActionRecord>().allocate(capacity);
                rv->v.mLast = rv->v.mFirst + size;
                rv->v.mEnd = rv->v.mFirst + capacity;
                for (size_t k = 0; k < size; ++k) FillRecord(rv->v.mFirst + k, rr, objects);
            }
        }
        std::vector<ActionRecord> expected(a.v.mFirst, a.v.mLast); // copies: the text, a zero count, the interaction
        ActionRecord *oldA = a.v.mFirst, *oldB = b.v.mFirst;
        const bool smallBlock = capacity * sizeof(ActionRecord) < 4000;

        uint32_t wantA = want, wantB = want;
        gEvents.clear();
        difftest::FreeRecorder recA;
        original(&a.v, &wantA);
        recA.Stop();
        const std::vector<int> eventsA = gEvents;
        const bool freedA = oldA && smallBlock ? recA.Freed(oldA) : true;
        gEvents.clear();
        difftest::FreeRecorder recB;
        ActionRecordsReallocateExactly(&b.v, nullptr, &wantB);
        recB.Stop();
        const std::vector<int> eventsB = gEvents;
        const bool freedB = oldB && smallBlock ? recB.Freed(oldB) : true;
        ++ctx.cases;

        if (eventsA != eventsB) ctx.Fail("reference calls differ (%zu vs %zu, size %zu)", eventsA.size(), eventsB.size(), size);
        if (a.Size() != size || b.Size() != size || a.Capacity() != want || b.Capacity() != want)
            ctx.Fail("size %zu/%zu capacity %zu/%zu, want %zu and %u", a.Size(), b.Size(), a.Capacity(), b.Capacity(), size, want);
        else
            for (size_t k = 0; k < size; ++k)
                if (!SameRecord(a.v.mFirst[k], expected[k]) || !SameRecord(b.v.mFirst[k], expected[k])) {
                    ctx.Fail("record %zu differs (size %zu)", k, size);
                    break;
                }
        if (want && (!a.v.mFirst || !b.v.mFirst)) ctx.Fail("no block for %u records", want);
        if (!freedA || !freedB) ctx.Fail("old block still allocated: %d/%d", !freedA, !freedB);
        expected.clear();
        a.Release();
        b.Release();
        gEvents.clear();
    }
    if (grown < 150 || fromEmpty < 5 || big < 5) ctx.Fail("coverage: grown %u fromEmpty %u big %u", grown, fromEmpty, big);
}
