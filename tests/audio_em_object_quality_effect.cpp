// cEMObjectQualityEffect's two slots (src/audio/cEMObjectQualityEffect.h), lane WO: original vs ours.
//
// Both sides run on the same block of curves' addresses: mode 0 calls 0x240418 and 0x2410fe, mode 1 ours. The object's first
// 0x14 bytes are never read here, and the curves are stood in for (0xc0a6a1 answers from the rng, huge and NaN included; 0xc0a86a
// logs the text it is given), as are the base's slot 3 0x240684 and the cached singleton 0x20ddfb. Compared: results, the
// log of every call in order, the toggle.
//
// Covered RVAs: 0x240418 (Init), 0x2410fe (Apply).
#include "audio/cEMObjectQualityEffect.h"
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZString.h"
#include "shape.h"

#include <bit>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace {

using Blob = std::vector<uint32_t>;
Blob gLog;
std::mt19937 *gR = nullptr;
cEMObjectQualityEffect *gObject = nullptr;
bool gBaseOk = false;

enum : uint32_t {
    evBase = 0xe1000000,
    evParse = 0xe2000000,
    evEvaluate = 0xe3000000,
    evKey = 0xe4000000,
    evRegistry = 0xe5000000,
    evFind = 0xe6000000,
    evRef = 0xe7000000,
    evSet = 0xe8000000,
};

uint32_t CurveOffset(const void *curve) { return uint32_t(static_cast<const uint8_t *>(curve) - reinterpret_cast<uint8_t *>(gObject)); }

bool __fastcall StandBaseInit(void *self, void *)
{
    gLog.push_back(evBase);
    gLog.push_back(self == gObject);
    return gBaseOk;
}
bool __fastcall StandParse(void *curve, void *, const cRZString *text)
{
    gLog.push_back(evParse);
    gLog.push_back(CurveOffset(curve));
    for (const char c : text->mString) gLog.push_back(uint8_t(c));
    return (*gR)() % 2 != 0;
}
double __fastcall StandEvaluate(void *curve, void *, float x)
{
    gLog.push_back(evEvaluate);
    gLog.push_back(CurveOffset(curve));
    gLog.push_back(std::bit_cast<uint32_t>(x));
    std::mt19937 &r = *gR;
    switch (r() % 8) {
    case 0: return std::numeric_limits<double>::quiet_NaN();
    case 1: return 5e9 * (r() % 2 ? 1.0 : -1.0);  // past 32 bits
    case 2: return 1e30 * (r() % 2 ? 1.0 : -1.0); // past 64 bits
    case 3: return -double(r() % 2000) / 8.0;
    default: return double(r() % 40000) / 8.0;
    }
}

struct Fake {
    void **vt;
    Fake *next; // the registry of the singleton
};
void *gSingletonVT[32], *gRegistryVT[32], *gFoundVT[4], *gEventVT[16];
Fake gFound{gFoundVT, nullptr}, gRegistry{gRegistryVT, nullptr}, gSingleton{gSingletonVT, &gRegistry}, gEvent{gEventVT, nullptr};

void *__cdecl StandSingleton() { return &gSingleton; }
void *__fastcall SingletonRegistry(Fake *f, void *)
{
    gLog.push_back(evRegistry);
    return f->next;
}
void *__fastcall RegistryFind(Fake *, void *, uint32_t key)
{
    gLog.push_back(evFind);
    gLog.push_back(key);
    return (*gR)() % 4 ? &gFound : nullptr;
}
uint32_t __fastcall FoundAddRef(Fake *, void *)
{
    gLog.push_back(evRef | 1);
    return (*gR)();
}
uint32_t __fastcall FoundRelease(Fake *, void *)
{
    gLog.push_back(evRef | 2);
    return (*gR)();
}
uint32_t __fastcall EventKey(Fake *, void *, uint32_t id)
{
    gLog.push_back(evKey);
    gLog.push_back(id);
    return (*gR)();
}
void __fastcall EventSet(Fake *, void *, uint32_t id, uint32_t value)
{
    gLog.push_back(evSet);
    gLog.push_back(id);
    gLog.push_back(value);
}

const bool gFilled = [] {
    gSingletonVT[26] = reinterpret_cast<void *>(&SingletonRegistry);
    gRegistryVT[29] = reinterpret_cast<void *>(&RegistryFind);
    gFoundVT[1] = reinterpret_cast<void *>(&FoundAddRef);
    gFoundVT[2] = reinterpret_cast<void *>(&FoundRelease);
    gEventVT[11] = reinterpret_cast<void *>(&EventKey);
    gEventVT[12] = reinterpret_cast<void *>(&EventSet);
    return true;
}();

} // namespace

DIFF_TEST(EMObjectQualityEffect_Slots, 0x240418)
{
    using InitFn = bool(__thiscall *)(void *);
    using ApplyFn = bool(__thiscall *)(void *, void *, void *);
    const InitFn originalInit = ctx.Original<InitFn>(0x240418);
    const ApplyFn originalApply = ctx.Original<ApplyFn>(0x2410fe);
    shape::CodePatch base{0x240684u, shape::CodeOf(&StandBaseInit)};
    shape::CodePatch parse{0xc0a86au, shape::CodeOf(&StandParse)};
    shape::CodePatch evaluate{0xc0a6a1u, shape::CodeOf(&StandEvaluate)};
    shape::StandIn singleton{0x20ddfbu, &GetCachedSingleton, &StandSingleton};
    alignas(8) static uint8_t block[sizeof(cEMObjectQualityEffect)];
    gObject = reinterpret_cast<cEMObjectQualityEffect *>(block);
    uint32_t *toggle = GameAddress<uint32_t>(cEMObjectQualityEffect::kQualityToggle);
    const uint32_t savedToggle = *toggle;
    for (uint32_t seed = 0; seed < 400; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            gLog.clear();
            std::mt19937 r(seed * 15485863u + 1);
            gR = &r;
            static const uint32_t kToggles[] = {0, 1, 7, 0xffffffffu};
            *toggle = kToggles[r() % 4];
            for (int step = 0; step < 12; ++step) {
                b.push_back(0x0b000000u | uint32_t(step));
                if (r() % 3 == 0) {
                    gBaseOk = r() % 4 != 0;
                    const bool ok = mode ? gObject->Init() : originalInit(gObject);
                    b.push_back(uint32_t(ok));
                } else {
                    const bool ok = mode ? gObject->Apply(&gEvent, nullptr) : originalApply(gObject, &gEvent, nullptr);
                    b.push_back(uint32_t(ok));
                }
                b.insert(b.end(), gLog.begin(), gLog.end());
                gLog.clear();
                b.push_back(*toggle);
            }
            gR = nullptr;
        }
        ++ctx.cases;
        if (blobs[0] != blobs[1]) {
            size_t i = 0;
            while (i < blobs[0].size() && i < blobs[1].size() && blobs[0][i] == blobs[1][i]) ++i;
            ctx.Fail("EMObjectQualityEffect_Slots seed %u: differ at word %u: %08x vs %08x", seed, uint32_t(i),
                     i < blobs[0].size() ? blobs[0][i] : 0, i < blobs[1].size() ? blobs[1][i] : 0);
            break;
        }
    }
    *toggle = savedToggle;
}
