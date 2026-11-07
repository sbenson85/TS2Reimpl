// Differential tests for the graphic system's five cached manager getters (lane OW, src/gz/GraphicServices.cpp: 0x18acd0,
// 0x18d120, 0x18dcf0, 0x18dd70, 0x18ddf0) and the Draw2D service's (lane WN, 0x18c000; its answer is the fake system's slot 13). A fake framework service answers slot 5 (recording the ids) with a fake graphic
// system whose slots 16-20 each answer an object of their own (or null) and whose slot 2 (Release) records; compared: the
// return value, the cache global, and every call with its arguments. The service or graphic system missing crashes the
// original (the answer is used unchecked), so those states are not run. The release of the temporary raising a C++
// exception (swallowed by the original) or an SEH one (propagated) is run too.
#include "difftest.h"

#include "gz/GraphicServices.h"

#include <windows.h>

#include <cstring>
#include <vector>

namespace {

struct Call {
    int slot;
    const void *self;
    uint32_t a, b;
};
std::vector<Call> gCalls;
void *gGraphicSystemResult = nullptr;
void *gManagerAnswers[8]; // slots 13..20

uint32_t __fastcall RecordRelease(void *self, void *)
{
    gCalls.push_back({2, self, 0, 0});
    return 0;
}
uint32_t __fastcall RecordQuery(void *self, void *, uint32_t a, uint32_t b, void **out)
{
    gCalls.push_back({5, self, a, b});
    *out = gGraphicSystemResult;
    return 0;
}
template <int Slot> void *__fastcall AnswerManager(void *self, void *)
{
    gCalls.push_back({Slot, self, 0, 0});
    return gManagerAnswers[Slot - 13];
}

struct FakeObject {
    void *vtable;
    uint32_t pad;
};
void *gServiceTable[8];
void *gSystemTable[32];
FakeObject gService{}, gSystem{}, gAnswers[8]{}, gCachedObject{};

void SetupFakes()
{
    std::memset(gServiceTable, 0, sizeof(gServiceTable));
    std::memset(gSystemTable, 0, sizeof(gSystemTable));
    gServiceTable[5] = reinterpret_cast<void *>(&RecordQuery);
    gSystemTable[2] = reinterpret_cast<void *>(&RecordRelease);
    gSystemTable[13] = reinterpret_cast<void *>(&AnswerManager<13>);
    gSystemTable[16] = reinterpret_cast<void *>(&AnswerManager<16>);
    gSystemTable[17] = reinterpret_cast<void *>(&AnswerManager<17>);
    gSystemTable[18] = reinterpret_cast<void *>(&AnswerManager<18>);
    gSystemTable[19] = reinterpret_cast<void *>(&AnswerManager<19>);
    gSystemTable[20] = reinterpret_cast<void *>(&AnswerManager<20>);
    gService.vtable = gServiceTable;
    gSystem.vtable = gSystemTable;
}

constexpr uint32_t kCaches[] = {0x10ca830, 0x10ca8fc, 0x10ca990, 0x10caa24, 0x10caa28, 0x10caa3c};

void SetGlobals(difftest::Context &ctx, uint32_t cacheRva, void *cached)
{
    for (uint32_t rva : kCaches) *ctx.image.At<void *>(rva) = nullptr;
    *ctx.image.At<void *>(cacheRva) = cached;
    *ctx.image.At<void *>(0x10c9378) = &gService;
}

bool SameCalls(const std::vector<Call> &a, const std::vector<Call> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].slot != b[i].slot || a[i].self != b[i].self || a[i].a != b[i].a || a[i].b != b[i].b) return false;
    return true;
}

using Getter = void *(__cdecl *)();
Getter gOrig, gOurs;
void CallOriginal() { gOrig(); }
void CallOurs() { gOurs(); }
using Thunk = void (*)();
unsigned long EscapedCode(Thunk fn)
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

void CheckGetter(difftest::Context &ctx, const char *name, uint32_t rva, Getter ours, uint32_t cacheRva, int slot)
{
    SetupFakes();
    gOrig = ctx.Original<Getter>(rva);
    gOurs = ours;
    for (int i = 0; i < 8; ++i) gManagerAnswers[i] = &gAnswers[i];
    void *cacheStates[] = {nullptr, &gCachedObject};
    void *answers[] = {nullptr, &gAnswers[slot - 13]};
    for (void *cachedIn : cacheStates)
        for (void *answer : answers) {
            gManagerAnswers[slot - 13] = answer;
            gGraphicSystemResult = &gSystem;
            SetGlobals(ctx, cacheRva, cachedIn);
            gCalls.clear();
            void *a = gOrig();
            void *cacheA = *ctx.image.At<void *>(cacheRva);
            const std::vector<Call> callsA = gCalls;
            SetGlobals(ctx, cacheRva, cachedIn);
            gCalls.clear();
            void *b = gOurs();
            void *cacheB = *ctx.image.At<void *>(cacheRva);
            ++ctx.cases;
            if (a != b || cacheA != cacheB || !SameCalls(callsA, gCalls))
                ctx.Fail("%s cache=%p answer=%p: original %p (cache %p, %zu calls), ours %p (cache %p, %zu calls)", name, cachedIn,
                         answer, a, cacheA, callsA.size(), b, cacheB, gCalls.size());
        }
    // The release of the held graphic system raising.
    uint32_t (__fastcall *releases[])(void *, void *) = {&RaisingRelease, &ThrowingRelease};
    for (auto release : releases) {
        gSystemTable[2] = reinterpret_cast<void *>(release);
        gManagerAnswers[slot - 13] = &gAnswers[slot - 13];
        gGraphicSystemResult = &gSystem;
        SetGlobals(ctx, cacheRva, nullptr);
        const unsigned long ea = EscapedCode(&CallOriginal);
        void *cacheA = *ctx.image.At<void *>(cacheRva);
        SetGlobals(ctx, cacheRva, nullptr);
        const unsigned long eb = EscapedCode(&CallOurs);
        void *cacheB = *ctx.image.At<void *>(cacheRva);
        ++ctx.cases;
        if (ea != eb || cacheA != cacheB)
            ctx.Fail("%s release raising: original escaped 0x%08lx cache %p, ours 0x%08lx cache %p", name, ea, cacheA, eb, cacheB);
    }
    SetGlobals(ctx, cacheRva, nullptr);
    *ctx.image.At<void *>(0x10c9378) = nullptr;
}

} // namespace

DIFF_TEST(GraphicServices_PixelFormatsManager, 0x18acd0)
{
    CheckGetter(ctx, "pixel formats", 0x18acd0, &nGZGraphic4::GetPixelFormatsManagerCached, 0x10ca830, 16);
}
DIFF_TEST(GraphicServices_Draw2D, 0x18c000)
{
    CheckGetter(ctx, "draw 2d", 0x18c000, &nGZGraphic4::GetDraw2DCached, 0x10ca8fc, 13);
}
DIFF_TEST(GraphicServices_ImageManip, 0x18d120)
{
    CheckGetter(ctx, "image manip", 0x18d120, &nGZGraphic4::GetImageManipCached, 0x10ca990, 20);
}
DIFF_TEST(GraphicServices_VertexFormatsManager, 0x18dcf0)
{
    CheckGetter(ctx, "vertex formats", 0x18dcf0, &nGZGraphic4::GetVertexFormatsManagerCached, 0x10caa24, 17);
}
DIFF_TEST(GraphicServices_VertexDeclarationsManager, 0x18dd70)
{
    CheckGetter(ctx, "vertex declarations", 0x18dd70, &nGZGraphic4::GetVertexDeclarationsManagerCached, 0x10caa28, 18);
}
DIFF_TEST(GraphicServices_ShaderProgramsManager, 0x18ddf0)
{
    CheckGetter(ctx, "shader programs", 0x18ddf0, &nGZGraphic4::GetShaderProgramsManagerCached, 0x10caa3c, 19);
}
