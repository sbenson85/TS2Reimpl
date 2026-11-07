// Differential tests for the two SmallVirtuals members that don't byte-match (register allocation only):
// cBranchNodesIIDFilter::IsIncluded and cRZSocketError::Describe (src/misc/SmallVirtuals.h).
#include "difftest.h"
#include "misc/SmallVirtuals.h"

#include <cstring>
#include <random>

namespace {

// A node whose slot 25 (bool HasInterface(iid)) answers a chosen eax: 0, 1, or garbage above al with 0 or 1 in al.
uint32_t gAnswer = 0;
uint32_t gAskedIid = 0;
const void *gAskedSelf = nullptr;
int gCalls = 0;

uint32_t __fastcall FakeHasInterface(const void *self, void * /*edx*/, uint32_t iid)
{
    ++gCalls;
    gAskedSelf = self;
    gAskedIid = iid;
    return gAnswer;
}

void *gNodeVTable[32] = {};

// The string's slot 31 (cdecl printf-style format): records its arguments and answers a chosen value.
const void *gFormatSelf = nullptr;
const char *gFormat = nullptr;
int32_t gFormatCode = 0;
const char *gFormatText = nullptr;
uint32_t gFormatResult = 0;
int gFormatCalls = 0;

uint32_t __cdecl FakeFormat(const void *self, const char *format, int32_t code, const char *text)
{
    ++gFormatCalls;
    gFormatSelf = self;
    gFormat = format;
    gFormatCode = code;
    gFormatText = text;
    return gFormatResult;
}

void *gStringVTable[40] = {};

} // namespace

// Filter, node and IID random; slot 25's answer 0, 1, or garbage above al: the full eax, the slot's argument and
// receiver and the call count are compared.
DIFF_TEST(BranchNodesIIDFilter_IsIncluded, 0xaeacb0)
{
    std::mt19937 rng(0x48580001);
    gNodeVTable[25] = reinterpret_cast<void *>(&FakeHasInterface);
    auto orig = ctx.Original<uint32_t(__thiscall *)(const void *, void *)>(0xaeacb0);
    for (int i = 0; i < 20000; ++i) {
        nRZSceneGraph::cBranchNodesIIDFilter filter{};
        filter.mVTable = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
        filter.mIID = rng();
        const void *node[2] = {gNodeVTable, reinterpret_cast<void *>(static_cast<uintptr_t>(rng()))};
        uint32_t answer = rng() % 3 == 0 ? 0 : rng() % 2 ? 1 : (rng() & 0xffffff00u) | (rng() & 1);
        uint32_t results[2], iids[2];
        bool selves[2];
        int calls[2];
        for (int side = 0; side < 2; ++side) {
            gAnswer = answer;
            gCalls = 0;
            gAskedSelf = nullptr;
            gAskedIid = 0;
            results[side] = side == 0 ? orig(&filter, node) : filter.IsIncluded(node);
            iids[side] = gAskedIid;
            selves[side] = gAskedSelf == node;
            calls[side] = gCalls;
        }
        ++ctx.cases;
        if (results[0] != results[1] || iids[0] != iids[1] || selves[0] != selves[1] || calls[0] != calls[1])
            ctx.Fail("answer 0x%x: original %u (iid 0x%x, %d calls), ours %u (iid 0x%x, %d calls)", answer, results[0], iids[0],
                     calls[0], results[1], iids[1], calls[1]);
    }
}

// System codes across both of the original's text tables (WSA codes 10004-10071, 10091-10112, 11001-11004), their
// edges and random values: the result, the format string, code and text pointer slot 31 receives, its receiver and
// the call count are compared; the error object must be unchanged.
DIFF_TEST(RZSocketError_Describe, 0x99a0ac)
{
    std::mt19937 rng(0x48580002);
    gStringVTable[31] = reinterpret_cast<void *>(&FakeFormat);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x99a0ac);
    const void *text[2] = {gStringVTable, nullptr};
    for (int i = 0; i < 20000; ++i) {
        int32_t code;
        switch (rng() % 4) {
        case 0: code = 10000 + static_cast<int32_t>(rng() % 120); break;
        case 1: code = 10990 + static_cast<int32_t>(rng() % 20); break;
        case 2: code = static_cast<int32_t>(rng() % 3) - 1; break;
        default: code = static_cast<int32_t>(rng());
        }
        cRZSocketError error[2];
        error[0].mVTable = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
        error[0].mCode = static_cast<int32_t>(rng());
        error[0].mSystemCode = code;
        error[1] = error[0];
        uint32_t result = rng();
        uint32_t results[2];
        const char *formats[2], *texts[2];
        int32_t codes[2];
        bool selves[2];
        int calls[2];
        for (int side = 0; side < 2; ++side) {
            gFormatResult = result;
            gFormatCalls = 0;
            gFormatSelf = nullptr;
            results[side] = side == 0 ? orig(&error[0], text) : error[1].Describe(text);
            formats[side] = gFormat;
            texts[side] = gFormatText;
            codes[side] = gFormatCode;
            selves[side] = gFormatSelf == text;
            calls[side] = gFormatCalls;
        }
        ++ctx.cases;
        if (results[0] != results[1] || formats[0] != formats[1] || texts[0] != texts[1] || codes[0] != codes[1] ||
            selves[0] != selves[1] || calls[0] != calls[1] || std::memcmp(&error[0], &error[1], sizeof error[0]) != 0)
            ctx.Fail("code %d: original %u text %p, ours %u text %p", code, results[0], texts[0], results[1], texts[1]);
    }
}
