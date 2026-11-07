#include "difftest.h"
#include "ts/cTSStringSet.h"
#include "ts/cTSResourceFactoryForIGZSerializable.h"
#include "game/game.h"
#include "game/vtable.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <random>

namespace {
// The string-set object between guard bytes, filled with the same random bytes on both sides before construction.
struct Guarded { uint8_t before[16]; alignas(4) uint8_t object[0x7c]; uint8_t after[16]; };
Guarded Seeded(uint32_t seed)
{
    Guarded value;
    std::mt19937 random(seed);
    for (auto &byte : value.before) byte = uint8_t(random());
    for (auto &byte : value.object) byte = uint8_t(random());
    for (auto &byte : value.after) byte = uint8_t(random());
    return value;
}
constexpr uint32_t kSentinel = 0x99887766;
}

// QueryInterface: the ids the switch distinguishes (1, the interface, the class id, the +4 face, neighbours of each).
// The object is built by the original constructor on both sides, so only the answer, the output and the reference
// count are compared (map head pointers differ between heaps and are not part of the answer).
DIFF_TEST(StringSet_QueryInterface, 0x1ee280) {
    const uint32_t ids[] = {1, 0xfb4cb0de, 0xfb4cbaab, 0x456b8f1d, 0, 2, 0x456b8f1c, 0x456b8f1e, 0xfb4cb0df,
        0xfb4cbaaa, 0xfb4cbaac, 0xffffffff};
    for (uint32_t i = 0; i < 48; ++i) {
        const auto seed = Seeded(0x51c0 + i);
        std::array<uint32_t, 3> summary[2];
        for (int side = 0; side < 2; ++side) {
            Guarded work = seed;
            auto *set = reinterpret_cast<cTSStringSet *>(work.object);
            ctx.Original<cTSStringSet *(__thiscall *)(cTSStringSet *)>(0x1eace0)(set);
            set->mRefCount = 1 + i % 3;
            void *out = reinterpret_cast<void *>(uintptr_t(kSentinel));
            const bool returned = side ? set->QueryInterface(ids[i % 12], &out) :
                ctx.Original<bool(__thiscall *)(cTSStringSet *, uint32_t, void **)>(0x1ee280)(set, ids[i % 12], &out);
            summary[side] = {uint32_t(returned),
                uint32_t(uintptr_t(out) == kSentinel ? 0xdeadbeef : uintptr_t(out) - uintptr_t(set)), set->mRefCount};
            ctx.Original<void(__thiscall *)(cTSStringSet *)>(0x1eae50)(set);
        }
        if (summary[0] != summary[1]) ctx.Fail("QueryInterface(0x%x) differs: returned/out/refcount", ids[i % 12]);
        ++ctx.cases;
    }
}

// The string-set factory creator (cdecl): a new 8-byte factory with the factory vtable and a zero count.
DIFF_TEST(StringSetFactory_Create, 0x1eba40) {
    for (uint32_t i = 0; i < 8; ++i) {
        std::array<uint32_t, 2> summary[2];
        for (int side = 0; side < 2; ++side) {
            auto *factory = side ? reinterpret_cast<uint32_t *>(cTSResourceFactoryStringSet::Create()) :
                ctx.Original<uint32_t *(__cdecl *)()>(0x1eba40)();
            if (!factory) ctx.Fail("factory creator returned null");
            summary[side] = {uint32_t(factory[0] == uint32_t(uintptr_t(GameAddress(cTSResourceFactoryStringSet::kVTable)))), factory[1]};
            ::operator delete(factory);
        }
        if (summary[0] != summary[1]) ctx.Fail("string-set factory creator differs");
        ++ctx.cases;
    }
}

// The generic persist factory creator (cdecl, registered for the factory class id 0xaba91d51).
DIFF_TEST(GenericFactory_Create, 0x1f6740) {
    for (uint32_t i = 0; i < 8; ++i) {
        std::array<uint32_t, 2> summary[2];
        for (int side = 0; side < 2; ++side) {
            auto *factory = side ? reinterpret_cast<uint32_t *>(cTSResourceFactoryForIGZSerializable::Create()) :
                ctx.Original<uint32_t *(__cdecl *)()>(0x1f6740)();
            if (!factory) ctx.Fail("factory creator returned null");
            summary[side] = {uint32_t(factory[0] == uint32_t(uintptr_t(GameAddress(cTSResourceFactoryForIGZSerializable::kVTable)))), factory[1]};
            ::operator delete(factory);
        }
        if (summary[0] != summary[1]) ctx.Fail("generic factory creator differs");
        ++ctx.cases;
    }
}

// Destruct on a constructed set: the vtables go back and the two map heads are freed (with the map destructors),
// the same number of frees on both sides. The stale map pointers differ, so only the vtables and the frees compare.
DIFF_TEST(StringSet_Destruct, 0x1eae50) {
    for (uint32_t i = 0; i < 8; ++i) {
        const auto seed = Seeded(0x7e57 + i);
        std::array<uint32_t, 3> summary[2];
        for (int side = 0; side < 2; ++side) {
            Guarded work = seed;
            auto *set = reinterpret_cast<cTSStringSet *>(work.object);
            ctx.Original<cTSStringSet *(__thiscall *)(cTSStringSet *)>(0x1eace0)(set);
            difftest::FreeRecorder frees;
            if (side) set->Destruct();
            else ctx.Original<void(__thiscall *)(cTSStringSet *)>(0x1eae50)(set);
            frees.Stop();
            summary[side] = {frees.Count(), uint32_t(uintptr_t(set->mVTable) == uintptr_t(GameAddress(cTSStringSet::kVTable))),
                uint32_t(uintptr_t(set->mPersist.mVTable) == uintptr_t(GameAddress(cTSStringSet::kPersistVTable)))};
        }
        if (summary[0] != summary[1]) ctx.Fail("Destruct: frees or vtables differ");
        ++ctx.cases;
    }
}
