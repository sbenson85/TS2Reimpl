// Differential tests for the QueryInterface implementations in rz/QueryInterfaces.h. Each case runs the
// original and ours on identical copies of one object block: the block carries recording vtables at +0,
// +4 and +0x320 (the thunk's offset), and its ppv slot is at +0x7f0 with a sentinel. AddRef calls are
// recorded with the offset of the subobject they hit, and the whole block is compared afterwards, so the
// returned al, the stored pointer, the AddRef sequence and any stray write all count.
//
// The riid values are the ones each function compares against, their neighbours, and random values, so
// every branch and the fall-throughs to cRZWinProc4 / RZUnknownQueryInterface are reached. A null ppv is
// tried for every id, except where the original itself faults (noted per test, ORIGINAL-BUG in src/).
#include "difftest.h"
#include "rz/QueryInterfaces.h"
#include "rz/RZFrameworkHelpers.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

using OriginalQI = uint8_t(__thiscall *)(void *, uint32_t, void **);
using OurQI = bool(__fastcall *)(void *, void *, uint32_t, void **);

constexpr uint32_t kSize = 0x800;
constexpr uint32_t kOut = 0x7f0; // the ppv slot is at +0x7f0 inside the block
constexpr uint32_t kSentinel = 0xdeadbeef;

alignas(16) uint8_t gBlock[kSize];
uint8_t *gBase = nullptr;
std::vector<int> gEvents;
void *gRecordingVTable[4];

int OffsetOf(const void *p)
{
    return static_cast<int>(static_cast<const uint8_t *>(p) - gBase);
}

// AddRef / Release recorders. They take no stack arguments, so __fastcall with a dummy edx matches
// the thiscall the original uses.
uint32_t __fastcall RecAddRef(void *self, void * /*edx*/)
{
    gEvents.push_back(1000 + OffsetOf(self));
    return 1;
}

uint32_t __fastcall RecRelease(void *self, void * /*edx*/)
{
    gEvents.push_back(2000 + OffsetOf(self));
    return 1;
}

void SetupRecording()
{
    gRecordingVTable[1] = reinterpret_cast<void *>(&RecAddRef);
    gRecordingVTable[2] = reinterpret_cast<void *>(&RecRelease);
}

struct Outcome {
    uint8_t al;
    std::vector<int> events;
    std::vector<uint8_t> block;
};

// One call on a freshly built block. `call` takes (this, riid, ppv) and returns the al result.
template <typename Call> Outcome RunOne(Call call, uint32_t riid, bool nullOut)
{
    std::memset(gBlock, 0, sizeof(gBlock));
    void *vt = gRecordingVTable;
    std::memcpy(gBlock + 0x000, &vt, sizeof(vt));
    std::memcpy(gBlock + 0x004, &vt, sizeof(vt));
    std::memcpy(gBlock + 0x320, &vt, sizeof(vt));
    uint32_t sentinel = kSentinel;
    std::memcpy(gBlock + kOut, &sentinel, sizeof(sentinel));

    gBase = gBlock;
    gEvents.clear();
    void **ppv = nullOut ? nullptr : reinterpret_cast<void **>(gBlock + kOut);
    Outcome o;
    o.al = call(static_cast<void *>(gBlock), riid, ppv);
    o.events = gEvents;
    o.block.assign(gBlock, gBlock + kSize);
    return o;
}

// The ids every test tries: each value a function in this file compares against, plus or minus one,
// the edges, and random values.
std::vector<uint32_t> Ids()
{
    static const uint32_t kSpecial[] = {
        0,          1,          2,          3,          0x22e85d8e, 0x287fb697, 0x22ba0121, 0xe98b2f57,
        0x2c69749d, 0xacc9d81c, 0x0797ea13, 0xcbb39432, 0x6c9a93a2, 0x11670ab9, 0x0bd09a11, 0x456b8f1d,
        0x51f85085, 0x8c1571d6, 0xb1096a8c, 0x7fffffff, 0x80000000, 0xffffffff,
    };
    std::vector<uint32_t> ids;
    for (uint32_t s : kSpecial) {
        ids.push_back(s);
        ids.push_back(s + 1);
        ids.push_back(s - 1);
    }
    std::mt19937 rng(0x51f85085);
    for (int i = 0; i < 2000; ++i) ids.push_back(static_cast<uint32_t>(rng()));
    return ids;
}

// Runs the original and ours on every id, with a valid ppv and with a null one, except the null-ppv
// cases the original faults on (crashesOnNull). Compares al, the AddRef sequence and the whole block.
void RunAll(difftest::Context &ctx, uint32_t rva, OurQI ours, bool (*crashesOnNull)(uint32_t riid))
{
    auto orig = ctx.Original<OriginalQI>(rva);
    SetupRecording();
    for (uint32_t riid : Ids()) {
        for (int nullOut = 0; nullOut < 2; ++nullOut) {
            if (nullOut && crashesOnNull(riid)) continue;
            Outcome a = RunOne([&](void *self, uint32_t id, void **ppv) { return orig(self, id, ppv); }, riid, nullOut != 0);
            Outcome b = RunOne(
                [&](void *self, uint32_t id, void **ppv) { return static_cast<uint8_t>(ours(self, nullptr, id, ppv)); }, riid,
                nullOut != 0);
            ++ctx.cases;
            if (a.al != b.al) ctx.Fail("riid %08x out %s: al original %u, ours %u", riid, nullOut ? "null" : "set", a.al, b.al);
            if (a.events != b.events)
                ctx.Fail("riid %08x out %s: AddRef events differ (original %zu, ours %zu)", riid, nullOut ? "null" : "set",
                         a.events.size(), b.events.size());
            if (a.block != b.block) ctx.Fail("riid %08x out %s: block differs", riid, nullOut ? "null" : "set");
        }
    }
}

bool NeverCrashes(uint32_t) { return false; }

} // namespace

// cRZWinProc4: riid 1 faults with a null ppv (no null check).
DIFF_TEST(QueryInterfaces_WinProc4, 0xb6e020)
{
    RunAll(ctx, 0xb6e020, &qi::WinProc4QueryInterface, [](uint32_t r) { return r == 1; });
}

// cRZSystemService: 0x287fb697 faults with a null ppv, and so does riid 1 on the fall-through to
// RZUnknownQueryInterface (0x1b945c, which does no null check).
DIFF_TEST(QueryInterfaces_SystemService, 0x1b9650)
{
    RunAll(ctx, 0x1b9650, &qi::SystemServiceQueryInterface, [](uint32_t r) { return r == 0x287fb697 || r == 1; });
}

DIFF_TEST(QueryInterfaces_TSWinProcGrid, 0x747105)
{
    RunAll(ctx, 0x747105, &qi::TSWinProcGridQueryInterface, NeverCrashes);
}

// cGZWin: the matching ids store without a null check.
DIFF_TEST(QueryInterfaces_GZWin, 0x9325d8)
{
    RunAll(ctx, 0x9325d8, &qi::GZWinQueryInterface, [](uint32_t r) { return r == 1 || r == 0x22ba0121 || r == 0xe98b2f57; });
}

DIFF_TEST(QueryInterfaces_TSWinProcPopup, 0x749010)
{
    RunAll(ctx, 0x749010, &qi::TSWinProcPopupQueryInterface, NeverCrashes);
}

DIFF_TEST(QueryInterfaces_TSWinProcBaseObjectDialog, 0x7c39bb)
{
    RunAll(ctx, 0x7c39bb, &qi::TSWinProcBaseObjectDialogQueryInterface, NeverCrashes);
}

DIFF_TEST(QueryInterfaces_TSWinProcUCPSubPanel, 0x672a95)
{
    RunAll(ctx, 0x672a95, &qi::TSWinProcUCPSubPanelQueryInterface, NeverCrashes);
}

DIFF_TEST(QueryInterfaces_TSWinProcOptionsSubPanel, 0x6487b9)
{
    RunAll(ctx, 0x6487b9, &qi::TSWinProcOptionsSubPanelQueryInterface, NeverCrashes);
}

// cTSWinProcGridThumbnail: a null ppv falls through to cRZWinProc4, which faults on riid 1.
DIFF_TEST(QueryInterfaces_TSWinProcGridThumbnail, 0x75dc5b)
{
    RunAll(ctx, 0x75dc5b, &qi::TSWinProcGridThumbnailQueryInterface, [](uint32_t r) { return r == 1; });
}

DIFF_TEST(QueryInterfaces_TSUIService, 0x75f340)
{
    RunAll(ctx, 0x75f340, &qi::TSUIServiceQueryInterface, NeverCrashes);
}

// nTSCASCore::cTSSkinBase: the matching ids store without a null check, so every one of them faults
// with a null ppv.
DIFF_TEST(QueryInterfaces_TSSkinBase, 0xb7bbe0)
{
    RunAll(ctx, 0xb7bbe0, &qi::TSSkinBaseQueryInterface, [](uint32_t r) {
        return r == 1 || r == 0x456b8f1d || r == 0x51f85085 || r == 0x8c1571d6 || r == 0xb1096a8c;
    });
}

// cTSWinProcSimEditorDialog: a thunk that adds 0x320 and tail-jumps to the base dialog's QueryInterface.
DIFF_TEST(QueryInterfaces_TSWinProcSimEditorDialog, 0x7c741f)
{
    RunAll(ctx, 0x7c741f, &qi::TSWinProcSimEditorDialogQueryInterface, NeverCrashes);
}
