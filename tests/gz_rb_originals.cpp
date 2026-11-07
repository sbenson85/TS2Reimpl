// Differential tests for small graphics originals (lane RB, milestone O-c):
// - the current device's enumeration 0x18b470 (src/gz/GraphicServices.cpp): a fake framework service answers a fake graphic
//   system (slot 21 current device index, slot 24 device by index, some indices answering no device); each fake device's
//   slot 16 records its object and answers a value distinct per device. Compared: the result, the cache global, every call.
// - the shader programs manager's ReleaseLinker 0x176e40 (src/gz/cShaderProgramsDX.cpp), run on raw manager memory whose
//   six linker cells hold recording fakes or null. Compared: every Release with its object and the whole manager memory.
// - the multisample clamp 0x18b7d0 (src/gz/cDeviceDX.cpp) against a fake enumeration answering scripted level counts per
//   type. Compared: the type and quality written and every query with its arguments.
#include "difftest.h"

#include "gz/GraphicServices.h"
#include "gz/cDeviceDX.h"
#include "gz/cShaderProgramsDX.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

struct Call {
    int slot;
    const void *self;
    uint32_t a;
};
std::vector<Call> gCalls;
int32_t gCurrentIndex = 0;

struct FakeObject {
    void *vtable;
    uint32_t pad[3];
};
void *gServiceTable[8];
void *gSystemTable[32];
void *gDeviceTable[32];
FakeObject gService{}, gSystem{}, gDevices[4]{};

uint32_t __fastcall RecordRelease(void *self, void *)
{
    gCalls.push_back({2, self, 0});
    return 0;
}
uint32_t __fastcall RecordQuery(void *self, void *, uint32_t a, uint32_t, void **out)
{
    gCalls.push_back({5, self, a});
    *out = &gSystem;
    return 0;
}
int32_t __fastcall AnswerIndex(void *self, void *)
{
    gCalls.push_back({21, self, 0});
    return gCurrentIndex;
}
void *__fastcall AnswerDevice(void *self, void *, uint32_t index)
{
    gCalls.push_back({24, self, index});
    return index % 5 == 4 ? nullptr : &gDevices[index & 3]; // some indices have no device
}
void *__fastcall AnswerEnum(void *self, void *)
{
    gCalls.push_back({16, self, 0});
    return reinterpret_cast<void *>(0x7100000u + uint32_t(static_cast<FakeObject *>(self) - gDevices) * 0x40u);
}

void SetupFakes()
{
    std::memset(gServiceTable, 0, sizeof(gServiceTable));
    std::memset(gSystemTable, 0, sizeof(gSystemTable));
    std::memset(gDeviceTable, 0, sizeof(gDeviceTable));
    gServiceTable[5] = reinterpret_cast<void *>(&RecordQuery);
    gSystemTable[2] = reinterpret_cast<void *>(&RecordRelease);
    gSystemTable[21] = reinterpret_cast<void *>(&AnswerIndex);
    gSystemTable[24] = reinterpret_cast<void *>(&AnswerDevice);
    gDeviceTable[16] = reinterpret_cast<void *>(&AnswerEnum);
    gService.vtable = gServiceTable;
    gSystem.vtable = gSystemTable;
    for (FakeObject &d : gDevices) d.vtable = gDeviceTable;
}

bool SameCalls(const std::vector<Call> &a, const std::vector<Call> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].slot != b[i].slot || a[i].self != b[i].self || a[i].a != b[i].a) return false;
    return true;
}

// ---- ReleaseLinker ----
struct Linker {
    void *vtable;
    uint32_t id;
};
void *gLinkerTable[8];
Linker gLinkers[6];
uint32_t __stdcall LinkerRelease(Linker *self)
{
    gCalls.push_back({2, self, self->id});
    return 7;
}

} // namespace

DIFF_TEST(GraphicServices_CurrentDeviceEnum, 0x18b470)
{
    constexpr uint32_t kCache = 0x10ca87c;
    using Fn = void *(__cdecl *)();
    SetupFakes();
    const Fn orig = ctx.Original<Fn>(0x18b470);
    const Fn ours = &nGZGraphic4::GetCurrentDeviceEnum;
    void *cacheStates[] = {nullptr, &gSystem};
    const int32_t indices[] = {-1, -7, int32_t(0x80000000), 0, 1, 2, 3, 4, 9, 14, 0x7fffffff};
    for (void *cachedIn : cacheStates)
        for (int32_t index : indices) {
            gCurrentIndex = index;
            *ctx.image.At<void *>(0x10c9378) = &gService;
            *ctx.image.At<void *>(kCache) = cachedIn;
            gCalls.clear();
            void *a = orig();
            void *cacheA = *ctx.image.At<void *>(kCache);
            const std::vector<Call> callsA = gCalls;
            *ctx.image.At<void *>(kCache) = cachedIn;
            gCalls.clear();
            void *b = ours();
            void *cacheB = *ctx.image.At<void *>(kCache);
            ++ctx.cases;
            if (a != b || cacheA != cacheB || !SameCalls(callsA, gCalls))
                ctx.Fail("device enum cache=%p index=%d: original %p (cache %p, %zu calls), ours %p (cache %p, %zu calls)",
                         cachedIn, index, a, cacheA, callsA.size(), b, cacheB, gCalls.size());
        }
    *ctx.image.At<void *>(kCache) = nullptr;
    *ctx.image.At<void *>(0x10c9378) = nullptr;
}

DIFF_TEST(cShaderProgramsManagerDX_ReleaseLinker, 0x176e40)
{
    using Fn = void(__thiscall *)(void *, uint32_t);
    const Fn orig = ctx.Original<Fn>(0x176e40);
    std::memset(gLinkerTable, 0, sizeof(gLinkerTable));
    gLinkerTable[2] = reinterpret_cast<void *>(&LinkerRelease);
    for (uint32_t i = 0; i < 6; ++i) gLinkers[i] = Linker{gLinkerTable, 0x100 + i};
    alignas(8) uint8_t memA[0x58], memB[0x58];
    std::mt19937 g(0x176e40);
    for (unsigned round = 0; round < 400; ++round) {
        uint8_t seed[0x58];
        for (uint8_t &b : seed) b = uint8_t(g());
        for (uint32_t i = 0; i < 6; ++i) {
            Linker *l = g() % 3 ? &gLinkers[g() % 6] : nullptr;
            std::memcpy(seed + 4 + i * 4, &l, 4);
        }
        const uint32_t index = g() % 6;
        std::memcpy(memA, seed, sizeof(seed));
        gCalls.clear();
        orig(memA, index);
        const std::vector<Call> callsA = gCalls;
        std::memcpy(memB, seed, sizeof(seed));
        gCalls.clear();
        reinterpret_cast<nGZGraphic4::cShaderProgramsManagerDX *>(memB)->ReleaseLinker(index);
        ++ctx.cases;
        if (!SameCalls(callsA, gCalls) || std::memcmp(memA, memB, sizeof(memA)) != 0)
            ctx.Fail("release linker round %u index %u: original %zu calls, ours %zu calls, memory %s", round, index,
                     callsA.size(), gCalls.size(), std::memcmp(memA, memB, sizeof(memA)) ? "differs" : "same");
    }
}

// ---- ClampMultisample ----
// A fake enumeration whose slot 18 answers a scripted level count per type (-1: none) and records each query.
namespace {
int32_t gLevels[64];
void *gEnumTable[32];
FakeObject gEnum{};
int32_t __fastcall AnswerLevels(void *self, void *, uint32_t type, void *displayFormat, void *color)
{
    gCalls.push_back({18, self, type});
    gCalls.push_back({-1, displayFormat, uint32_t(reinterpret_cast<uintptr_t>(color))});
    return gLevels[type & 63];
}
} // namespace

DIFF_TEST(nGZGraphic4_ClampMultisample, 0x18b7d0)
{
    using Fn = void(__cdecl *)(uint32_t *, uint32_t *, void *, void *, void *);
    const Fn orig = ctx.Original<Fn>(0x18b7d0);
    std::memset(gEnumTable, 0, sizeof(gEnumTable));
    gEnumTable[18] = reinterpret_cast<void *>(&AnswerLevels);
    gEnum.vtable = gEnumTable;
    void *const display = reinterpret_cast<void *>(0x5100040), *const color = reinterpret_cast<void *>(0x5200080);
    std::mt19937 g(0x18b7d0);
    // Types from 0 to beyond the cap (no negative ones: those step down through every int without levels).
    for (unsigned round = 0; round < 3000; ++round) {
        const unsigned noneShare = g() % 4; // 0: every type has levels .. 3: most have none
        for (int32_t &l : gLevels) l = (g() % 4) < noneShare ? -1 : int32_t(g() % 9) - (g() % 8 == 0 ? 3 : 0);
        const uint32_t type = round < 64 ? round % 32 : g() % 40;
        const uint32_t quality = g() % 5 == 0 ? uint32_t(int32_t(g() % 7) - 3) : g() % 12;
        uint32_t typeA = type, qualityA = quality, typeB = type, qualityB = quality;
        gCalls.clear();
        orig(&typeA, &qualityA, &gEnum, display, color);
        const std::vector<Call> callsA = gCalls;
        gCalls.clear();
        nGZGraphic4::ClampMultisample(&typeB, &qualityB, reinterpret_cast<nGZGraphic4::cIDeviceEnum *>(&gEnum),
                                      static_cast<nGZGraphic4::cIPixelFormat *>(display),
                                      static_cast<nGZGraphic4::cIPixelFormat *>(color));
        ++ctx.cases;
        if (typeA != typeB || qualityA != qualityB || !SameCalls(callsA, gCalls))
            ctx.Fail("clamp type %u quality %d: original %u/%d (%zu calls), ours %u/%d (%zu calls)", type, int32_t(quality),
                     typeA, int32_t(qualityA), callsA.size(), typeB, int32_t(qualityB), gCalls.size());
    }
}
