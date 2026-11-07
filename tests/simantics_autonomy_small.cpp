// Differential tests for the small helpers of the FindBestAction search: cEdithObjectTestSim::SetPerson (0x4510b0, slot 4) and the
// action-string flag (SetActionStringShown 0x4529b0, WasActionStringShown 0x452690).
#include "difftest.h"
#include "game/game.h"
#include "simantics/AutonomyHelpers.h"
#include "simantics/EdithObjectTestSim.h"

#include <cstdint>
#include <cstring>
#include <random>

DIFF_TEST(Autonomy_TestSim_SetPerson, 0x4510b0)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, void *, uint32_t, uint32_t)>(0x4510b0);
    std::mt19937 r(41);
    for (int i = 0; i < 20000; ++i) {
        uint8_t a[sizeof(cEdithObjectTestSim) + 8], b[sizeof(cEdithObjectTestSim) + 8];
        for (size_t k = 0; k < sizeof(a); ++k) a[k] = b[k] = static_cast<uint8_t>(r());
        void *const person = reinterpret_cast<void *>(static_cast<uintptr_t>(r() | 1));
        const uint32_t flagA = (r() % 3 == 0) ? r() : r() % 2, flagB = (r() % 3 == 0) ? r() : r() % 2;
        const uint32_t expected = original(a, person, flagA, flagB) & 0xff;
        const uint32_t actual = reinterpret_cast<cEdithObjectTestSim *>(b)->SetPerson(person, flagA, flagB);
        ++ctx.cases;
        if (expected != actual || std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("case %d: return %u/%u, object %s", i, expected, actual, std::memcmp(a, b, sizeof(a)) ? "differs" : "same");
    }
}

DIFF_TEST(Autonomy_ActionStringFlag, 0x4529b0)
{
    auto originalSet = ctx.Original<void(__cdecl *)(uint32_t)>(0x4529b0);
    auto originalGet = ctx.Original<uint8_t(__cdecl *)()>(0x452690);
    uint8_t *const flag = ctx.image.At<uint8_t>(0x10da96c);
    const uint8_t saved = *flag;
    std::mt19937 r(43);
    for (int i = 0; i < 4000; ++i) {
        const uint32_t value = (r() % 2) ? r() : (r() % 2);
        const uint8_t before = static_cast<uint8_t>(r());
        *flag = before;
        const uint8_t seenOriginal = originalGet();
        const uint8_t seenOurs = WasActionStringShown();
        originalSet(value);
        const uint8_t afterOriginal = *flag;
        *flag = before;
        SetActionStringShown(value);
        ++ctx.cases;
        if (seenOriginal != seenOurs || afterOriginal != *flag) ctx.Fail("case %d: get %u/%u, set %u/%u", i, seenOriginal, seenOurs, afterOriginal, *flag);
    }
    *flag = saved;
}
