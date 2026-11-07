// Differential tests for the current device getter 0x18ae40 and the pixel format for a colour type 0x18a7f0 (lane OZ,
// src/gz/GraphicServices.cpp). Fakes as in gz_graphic_services.cpp: a framework service whose slot 5 answers a fake graphic
// system (slot 2 Release, slot 21 current device index, slot 24 device by index; the answer is read by callers from eax),
// and a fake formats manager whose slot 6 (GetARGB) records its five arguments. Compared: return value, the cache global,
// every call with its arguments. A missing service or graphic system crashes the original (used unchecked), not run.
#include "difftest.h"

#include "gz/GraphicServices.h"

#include <windows.h>

#include <cstring>
#include <vector>

namespace {

struct Call {
    int slot;
    const void *self;
    uint32_t a, b, c, d, e;
};
std::vector<Call> gCalls;
int32_t gCurrentIndex = 0;

struct FakeObject {
    void *vtable;
    uint32_t pad;
};
void *gServiceTable[8];
void *gSystemTable[32];
void *gFormatsTable[16];
FakeObject gService{}, gSystem{}, gFormats{}, gCachedSystem{};

uint32_t __fastcall RecordRelease(void *self, void *)
{
    gCalls.push_back({2, self, 0, 0, 0, 0, 0});
    return 0;
}
uint32_t __fastcall RecordQuery(void *self, void *, uint32_t a, uint32_t b, void **out)
{
    gCalls.push_back({5, self, a, b, 0, 0, 0});
    *out = &gSystem;
    return 0;
}
int32_t __fastcall AnswerIndex(void *self, void *)
{
    gCalls.push_back({21, self, 0, 0, 0, 0, 0});
    return gCurrentIndex;
}
void *__fastcall AnswerDevice(void *self, void *, uint32_t index)
{
    gCalls.push_back({24, self, index, 0, 0, 0, 0});
    return reinterpret_cast<void *>(0x5000000u + index * 0x10u); // distinct per index, never dereferenced
}
void *__fastcall AnswerFormat(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    gCalls.push_back({6, self, a, b, c, d, e});
    return reinterpret_cast<void *>(0x6000000u + a * 0x1000u + b * 0x100u + c * 0x10u + d + e * 0x10000u);
}

void SetupFakes()
{
    std::memset(gServiceTable, 0, sizeof(gServiceTable));
    std::memset(gSystemTable, 0, sizeof(gSystemTable));
    std::memset(gFormatsTable, 0, sizeof(gFormatsTable));
    gServiceTable[5] = reinterpret_cast<void *>(&RecordQuery);
    gSystemTable[2] = reinterpret_cast<void *>(&RecordRelease);
    gSystemTable[21] = reinterpret_cast<void *>(&AnswerIndex);
    gSystemTable[24] = reinterpret_cast<void *>(&AnswerDevice);
    gFormatsTable[6] = reinterpret_cast<void *>(&AnswerFormat);
    gService.vtable = gServiceTable;
    gSystem.vtable = gSystemTable;
    gCachedSystem.vtable = gSystemTable;
    gFormats.vtable = gFormatsTable;
}

bool SameCalls(const std::vector<Call> &a, const std::vector<Call> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::memcmp(&a[i], &b[i], sizeof(Call)) != 0) return false;
    return true;
}

using DeviceGetter = void *(__cdecl *)();
DeviceGetter gOrig, gOurs;
void CallOriginal() { gOrig(); }
void CallOurs() { gOurs(); }
unsigned long EscapedCode(void (*fn)())
{
    __try {
        fn();
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
uint32_t __fastcall RaisingRelease(void *, void *)
{
    RaiseException(0xE0001234, 0, 0, nullptr);
    return 0;
}
uint32_t __fastcall ThrowingRelease(void *, void *) { throw 42; }

} // namespace

DIFF_TEST(GraphicServices_CurrentDevice, 0x18ae40)
{
    constexpr uint32_t kCache = 0x10ca87c;
    SetupFakes();
    gOrig = ctx.Original<DeviceGetter>(0x18ae40);
    gOurs = reinterpret_cast<DeviceGetter>(&nGZGraphic4::GetCurrentDeviceCached);
    void *cacheStates[] = {nullptr, &gCachedSystem};
    const int32_t indices[] = {-1, -2, int32_t(0x80000000), 0, 1, 5, 0x7fffffff};
    for (void *cachedIn : cacheStates)
        for (int32_t index : indices) {
            gCurrentIndex = index;
            *ctx.image.At<void *>(0x10c9378) = &gService;
            *ctx.image.At<void *>(kCache) = cachedIn;
            gCalls.clear();
            void *a = gOrig();
            void *cacheA = *ctx.image.At<void *>(kCache);
            const std::vector<Call> callsA = gCalls;
            *ctx.image.At<void *>(kCache) = cachedIn;
            gCalls.clear();
            void *b = gOurs();
            void *cacheB = *ctx.image.At<void *>(kCache);
            ++ctx.cases;
            if (a != b || cacheA != cacheB || !SameCalls(callsA, gCalls))
                ctx.Fail("current device cache=%p index=%d: original %p (cache %p, %zu calls), ours %p (cache %p, %zu calls)", cachedIn,
                         index, a, cacheA, callsA.size(), b, cacheB, gCalls.size());
        }
    // The release of the held graphic system raising (cache empty): the same exception escapes and the cache holds the system.
    uint32_t (__fastcall *releases[])(void *, void *) = {&RaisingRelease, &ThrowingRelease};
    for (auto release : releases) {
        gSystemTable[2] = reinterpret_cast<void *>(release);
        gCurrentIndex = 3;
        *ctx.image.At<void *>(kCache) = nullptr;
        const unsigned long ea = EscapedCode(&CallOriginal);
        void *cacheA = *ctx.image.At<void *>(kCache);
        *ctx.image.At<void *>(kCache) = nullptr;
        const unsigned long eb = EscapedCode(&CallOurs);
        void *cacheB = *ctx.image.At<void *>(kCache);
        ++ctx.cases;
        if (ea != eb || cacheA != cacheB)
            ctx.Fail("current device release raising: original escaped 0x%08lx cache %p, ours 0x%08lx cache %p", ea, cacheA, eb, cacheB);
    }
    *ctx.image.At<void *>(kCache) = nullptr;
    *ctx.image.At<void *>(0x10c9378) = nullptr;
}

DIFF_TEST(GraphicServices_PixelFormatForColorType, 0x18a7f0)
{
    constexpr uint32_t kCache = 0x10ca830;
    using Fn = void *(__cdecl *)(uint32_t);
    SetupFakes();
    const Fn orig = ctx.Original<Fn>(0x18a7f0);
    const Fn ours = &nGZGraphic4::GetPixelFormatForColorType;
    *ctx.image.At<void *>(kCache) = &gFormats;
    const uint32_t types[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0x80000000u, 0xfffffffeu, 0xffffffffu};
    for (uint32_t type : types) {
        gCalls.clear();
        void *a = orig(type);
        const std::vector<Call> callsA = gCalls;
        gCalls.clear();
        void *b = ours(type);
        ++ctx.cases;
        if (a != b || !SameCalls(callsA, gCalls))
            ctx.Fail("pixel format type %u: original %p (%zu calls), ours %p (%zu calls)", type, a, callsA.size(), b, gCalls.size());
    }
    *ctx.image.At<void *>(kCache) = nullptr;
}
