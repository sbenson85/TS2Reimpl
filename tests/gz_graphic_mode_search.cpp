// Differential tests for the graphic system's mode and format searches and the device switch (lane RJ,
// src/gz/GraphicModeSearch.cpp): 0x18b490 FindClosestDisplayMode, 0x18a9b0 FindClosestColorFormat, 0x18ab60
// FindClosestDepthFormat, 0x18b250 SetDeviceMode and 0x18b440 SetDeviceDesktopMode.
//
// Every object the originals touch is a recording fake: the iterators (slot 2 Release, 4 IsEqual, 5 Get, 6 Next), the
// pixel formats (slot 0 QueryInterface answering a face or null) and their faces (slot 2 Release; 6, 11, 12, 13, 15
// answering generated words), the framework service (image 0x10c9378, slot 5 GetSystemService), the graphic system
// (2, 15, 21, 22, 23, 24), the devices (23, 24, 25, 33, 34, 42) and the window behaviour (2, 20). Compared: the answer,
// the mode written out, and every call in order with its receiver and arguments (settings by content).
//
// The generators reach every value the originals branch on: signed and unsigned bounds both ways (values with the top
// bit set), equal areas, widths, bits, unused bits and refreshes, null faces, the float flag both ways, minAlphaBits
// zero and non-zero, ranges of 0 to 5 entries with and without a begin iterator; and for the switch, a negative
// current device, -1, in-range and past-the-count indices, the same and another device, the desktop request (also
// with a non-zero desktop record and a NaN aspect) and other modes, the window's device or another, the target's
// apply answer, the old device's had-device flag and its restore answer.
#include "difftest.h"

#include "gz/GraphicModeSearch.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace {

using nGZGraphic4::cDisplayMode;
using nGZGraphic4::cDisplayModeRange;
using nGZGraphic4::cIPixelFormat;
using nGZGraphic4::cPixelFormatRange;

struct Call {
    int slot;
    const void *self;
    uint32_t args[11];
};
std::vector<Call> gCalls;

void Record(int slot, const void *self, std::initializer_list<uint32_t> args = {})
{
    Call call{slot, self, {}};
    size_t i = 0;
    for (uint32_t a : args) call.args[i++] = a;
    gCalls.push_back(call);
}

bool SameCalls(const std::vector<Call> &a, const std::vector<Call> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::memcmp(&a[i], &b[i], sizeof(Call)) != 0) return false;
    return true;
}

uint32_t gSeed = 1;
uint32_t Rand()
{
    gSeed = gSeed * 1103515245u + 12345u;
    return gSeed >> 8;
}
template <class T, size_t N> T Pick(const T (&pool)[N]) { return pool[Rand() % N]; }

// ---- pixel formats and their faces -------------------------------------------------------------------------------

constexpr int kMaxEntries = 6;

struct Face {
    void *vtable;
    uint32_t words[16]; // answers by slot
};
struct Format {
    void *vtable;
    Face *face; // null: no face of any kind
};
void *gFaceTable[16];
void *gFormatTable[4];
Face gFaces[kMaxEntries];
Format gFormats[kMaxEntries];

uint32_t __fastcall FormatQuery(void *self, void *, uint32_t iid, void **out)
{
    Record(0, self, {iid});
    Format *const format = static_cast<Format *>(self);
    *out = format->face;
    return format->face != nullptr;
}
uint32_t __fastcall FaceRelease(void *self, void *)
{
    Record(2, self);
    return 1;
}
template <int Slot> uint32_t __fastcall FaceWord(void *self, void *)
{
    Record(Slot, self);
    return static_cast<Face *>(self)->words[Slot];
}
bool __fastcall FaceFlag(void *self, void *)
{
    Record(12, self);
    return static_cast<Face *>(self)->words[12] != 0;
}

void SetupFormats()
{
    std::memset(gFaceTable, 0, sizeof(gFaceTable));
    std::memset(gFormatTable, 0, sizeof(gFormatTable));
    gFormatTable[0] = reinterpret_cast<void *>(&FormatQuery);
    gFaceTable[2] = reinterpret_cast<void *>(&FaceRelease);
    gFaceTable[6] = reinterpret_cast<void *>(&FaceWord<6>);
    gFaceTable[11] = reinterpret_cast<void *>(&FaceWord<11>);
    gFaceTable[12] = reinterpret_cast<void *>(&FaceFlag);
    gFaceTable[13] = reinterpret_cast<void *>(&FaceWord<13>);
    gFaceTable[15] = reinterpret_cast<void *>(&FaceWord<15>);
    for (int i = 0; i < kMaxEntries; ++i) {
        gFaces[i] = {gFaceTable, {}};
        gFormats[i] = {gFormatTable, &gFaces[i]};
    }
}

// ---- iterators ---------------------------------------------------------------------------------------------------

struct Iterator {
    void *vtable;
    int pos;
};
void *gModeIterTable[8];
void *gFormatIterTable[8];
Iterator gCurrent, gBegin, gEnd;
cDisplayMode gModes[kMaxEntries];
Format *gListed[kMaxEntries];

uint32_t __fastcall IterRelease(void *self, void *)
{
    Record(2, self);
    return 0;
}
bool __fastcall IterIsEqual(void *self, void *, const Iterator *other)
{
    Record(4, self, {uint32_t(reinterpret_cast<uintptr_t>(other))});
    return static_cast<Iterator *>(self)->pos == other->pos;
}
cDisplayMode *__fastcall ModeGet(void *self, void *, cDisplayMode *out)
{
    Record(5, self);
    *out = gModes[static_cast<Iterator *>(self)->pos];
    return out;
}
void *__fastcall FormatGet(void *self, void *)
{
    Record(5, self);
    return gListed[static_cast<Iterator *>(self)->pos];
}
void __fastcall IterNext(void *self, void *)
{
    Record(6, self);
    ++static_cast<Iterator *>(self)->pos;
}

void SetupIterators()
{
    std::memset(gModeIterTable, 0, sizeof(gModeIterTable));
    std::memset(gFormatIterTable, 0, sizeof(gFormatIterTable));
    for (void **table : {gModeIterTable, gFormatIterTable}) {
        table[2] = reinterpret_cast<void *>(&IterRelease);
        table[4] = reinterpret_cast<void *>(&IterIsEqual);
        table[6] = reinterpret_cast<void *>(&IterNext);
    }
    gModeIterTable[5] = reinterpret_cast<void *>(&ModeGet);
    gFormatIterTable[5] = reinterpret_cast<void *>(&FormatGet);
}

// A fresh range of n entries over the given iterator table; the begin iterator is sometimes absent.
template <class Range> Range MakeRange(void **table, int n, bool withBegin)
{
    gCurrent = {table, 0};
    gBegin = {table, 0};
    gEnd = {table, n};
    Range range;
    range.mCurrent = reinterpret_cast<decltype(range.mCurrent)>(&gCurrent);
    range.mBegin = withBegin ? reinterpret_cast<decltype(range.mBegin)>(&gBegin) : nullptr;
    range.mEnd = reinterpret_cast<decltype(range.mEnd)>(&gEnd);
    return range;
}

void SetFaceWords(Face &face, uint32_t s6, uint32_t s11, uint32_t s12, uint32_t s13, uint32_t s15)
{
    face.words[6] = s6;
    face.words[11] = s11;
    face.words[12] = s12;
    face.words[13] = s13;
    face.words[15] = s15;
}

} // namespace

DIFF_TEST(GraphicModeSearch_FindClosestDisplayMode, 0x18b490)
{
    using Fn = bool(__cdecl *)(cDisplayMode *, cDisplayModeRange, uint32_t, uint32_t, int32_t, int32_t, uint32_t, uint32_t);
    const Fn orig = ctx.Original<Fn>(0x18b490);
    const Fn ours = &nGZGraphic4::FindClosestDisplayMode;
    SetupFormats();
    SetupIterators();
    gSeed = 0x18b490;
    const uint32_t widths[] = {640, 640, 800, 1024, 0x80000000u, 0x3fffffffu, 0};
    const uint32_t heights[] = {480, 480, 600, 768, 0x80000001u, 1};
    const uint32_t bitsPool[] = {16, 32, 32, 24, 0xffffffffu, 0x80000000u};
    const uint32_t unusedPool[] = {0, 0, 8, 0xfffffff8u, 0x40000000u};
    const uint32_t refreshes[] = {60, 60, 75, 0, 0x80000000u, 0xffffffffu};
    const uint32_t minWidths[] = {0, 640, 800, 0x80000000u};
    const uint32_t minHeights[] = {0, 480, 0x80000000u};
    const int32_t minBitsPool[] = {int32_t(0x80000000u), -1, 0, 16, 32};
    const int32_t maxBitsPool[] = {-1, 16, 24, 32, 0x7fffffff};
    const uint32_t minRefreshes[] = {0, 60, 0x80000000u};
    const uint32_t maxRefreshes[] = {0, 60, 75, 0xffffffffu, 0x7fffffffu};
    const float aspects[] = {1.3333334f, 1.6f, 0.0f};
    for (int round = 0; round < 6000; ++round) {
        const int n = int(Rand() % kMaxEntries);
        for (int i = 0; i < n; ++i) {
            // Each listed mode has its own format whose ARGB face answers generated words (never null: unchecked).
            SetFaceWords(gFaces[i], Pick(bitsPool), 0, 0, 0, Pick(unusedPool));
            gModes[i] = {Pick(widths), Pick(heights), Pick(aspects), Pick(refreshes),
                         reinterpret_cast<cIPixelFormat *>(&gFormats[i])};
        }
        const bool withBegin = (Rand() % 4) != 0;
        uint32_t minW = Pick(minWidths), minH = Pick(minHeights), minR = Pick(minRefreshes), maxR = Pick(maxRefreshes);
        int32_t minB = Pick(minBitsPool), maxB = Pick(maxBitsPool);
        if (round % 3 == 0) minW = minH = 0, minB = int32_t(0x80000000u), maxB = 0x7fffffff, minR = 0, maxR = 0xffffffffu;

        const cDisplayMode sentinel{0xdead0001u, 0xdead0002u, -2.5f, 0xdead0004u, reinterpret_cast<cIPixelFormat *>(0x5000)};
        cDisplayMode outA = sentinel, outB = sentinel;
        gCalls.clear();
        const bool a = orig(&outA, MakeRange<cDisplayModeRange>(gModeIterTable, n, withBegin), minW, minH, minB, maxB,
                            minR, maxR);
        const std::vector<Call> callsA = gCalls;
        gCalls.clear();
        const bool b = ours(&outB, MakeRange<cDisplayModeRange>(gModeIterTable, n, withBegin), minW, minH, minB, maxB,
                            minR, maxR);
        ++ctx.cases;
        if (a != b || std::memcmp(&outA, &outB, sizeof(cDisplayMode)) != 0 || !SameCalls(callsA, gCalls))
            ctx.Fail("mode search round %d (%d modes, min %ux%u bits %d..%d refresh %u..%u): original %d %ux%u@%u "
                     "(%zu calls), ours %d %ux%u@%u (%zu calls)",
                     round, n, minW, minH, minB, maxB, minR, maxR, a, outA.mWidth, outA.mHeight, outA.mRefresh,
                     callsA.size(), b, outB.mWidth, outB.mHeight, outB.mRefresh, gCalls.size());
    }
}

DIFF_TEST(GraphicModeSearch_FindClosestColorFormat, 0x18a9b0)
{
    using Fn = cIPixelFormat *(__cdecl *)(cPixelFormatRange, int32_t, int32_t);
    const Fn orig = ctx.Original<Fn>(0x18a9b0);
    const Fn ours = &nGZGraphic4::FindClosestColorFormat;
    SetupFormats();
    SetupIterators();
    gSeed = 0x18a9b0;
    const uint32_t bitsPool[] = {16, 16, 32, 24, 8, 0xfffffff8u, 0x80000000u};
    const uint32_t alphaPool[] = {0, 0, 1, 8, 8, 0xffffffffu};
    const uint32_t unusedPool[] = {0, 0, 8, 0xfffffffcu};
    const int32_t minBitsPool[] = {int32_t(0x80000000u), -10, 0, 16, 24, 40};
    const int32_t minAlphaPool[] = {0, 0, 1, 8, -2};
    for (int round = 0; round < 6000; ++round) {
        const int n = int(Rand() % kMaxEntries);
        for (int i = 0; i < n; ++i) {
            SetFaceWords(gFaces[i], Pick(bitsPool), Pick(alphaPool), 0, 0, Pick(unusedPool));
            gFormats[i].face = (Rand() % 6) == 0 ? nullptr : &gFaces[i];
            gListed[i] = &gFormats[i];
        }
        const bool withBegin = (Rand() % 4) != 0;
        const int32_t minB = Pick(minBitsPool), minA = Pick(minAlphaPool);
        gCalls.clear();
        cIPixelFormat *const a = orig(MakeRange<cPixelFormatRange>(gFormatIterTable, n, withBegin), minB, minA);
        const std::vector<Call> callsA = gCalls;
        gCalls.clear();
        cIPixelFormat *const b = ours(MakeRange<cPixelFormatRange>(gFormatIterTable, n, withBegin), minB, minA);
        ++ctx.cases;
        if (a != b || !SameCalls(callsA, gCalls))
            ctx.Fail("colour format round %d (%d formats, min bits %d alpha %d): original %p (%zu calls), ours %p (%zu calls)",
                     round, n, minB, minA, a, callsA.size(), b, gCalls.size());
    }
}

DIFF_TEST(GraphicModeSearch_FindClosestDepthFormat, 0x18ab60)
{
    using Fn = cIPixelFormat *(__cdecl *)(cPixelFormatRange, int32_t, int32_t, bool);
    const Fn orig = ctx.Original<Fn>(0x18ab60);
    const Fn ours = &nGZGraphic4::FindClosestDepthFormat;
    SetupFormats();
    SetupIterators();
    gSeed = 0x18ab60;
    const uint32_t depthPool[] = {16, 24, 24, 32, 0xffffffffu, 0x80000000u};
    const uint32_t stencilPool[] = {0, 0, 8, 4, 0xffffffffu};
    const int32_t minDepthPool[] = {int32_t(0x80000000u), -1, 0, 16, 24, 40};
    const int32_t minStencilPool[] = {int32_t(0x80000000u), -1, 0, 1, 8};
    for (int round = 0; round < 6000; ++round) {
        const int n = int(Rand() % kMaxEntries);
        for (int i = 0; i < n; ++i) {
            SetFaceWords(gFaces[i], 0, Pick(depthPool), Rand() % 3 == 0, Pick(stencilPool), 0);
            gFormats[i].face = (Rand() % 6) == 0 ? nullptr : &gFaces[i];
            gListed[i] = &gFormats[i];
        }
        const bool withBegin = (Rand() % 4) != 0;
        const int32_t minD = Pick(minDepthPool), minS = Pick(minStencilPool);
        const bool floatDepth = (Rand() % 3) == 0;
        gCalls.clear();
        cIPixelFormat *const a = orig(MakeRange<cPixelFormatRange>(gFormatIterTable, n, withBegin), minD, minS, floatDepth);
        const std::vector<Call> callsA = gCalls;
        gCalls.clear();
        cIPixelFormat *const b = ours(MakeRange<cPixelFormatRange>(gFormatIterTable, n, withBegin), minD, minS, floatDepth);
        ++ctx.cases;
        if (a != b || !SameCalls(callsA, gCalls))
            ctx.Fail("depth format round %d (%d formats, min depth %d stencil %d float %d): original %p (%zu calls), "
                     "ours %p (%zu calls)",
                     round, n, minD, minS, floatDepth, a, callsA.size(), b, gCalls.size());
    }
}

// ---- the device switch -------------------------------------------------------------------------------------------

namespace {

constexpr int kDevices = 4;
struct FakeObject {
    void *vtable;
    int index;
};
void *gFrameworkTable[8];
void *gSystemTable[32];
void *gDeviceTable[48];
void *gWindowTable[24];
FakeObject gFramework, gSystem, gWindow, gDevices[kDevices];

int32_t gCurrentDevice, gWindowDevice;
uint32_t gDeviceCount;
bool gHad[kDevices];
bool gApplyAnswers[2];
int gApplyCount;

uint32_t __fastcall GetSystemService(void *self, void *, uint32_t id, uint32_t iid, void **out)
{
    Record(5, self, {id, iid});
    *out = id == 0x85522803u ? static_cast<void *>(&gSystem) : static_cast<void *>(&gWindow);
    return 1;
}
uint32_t __fastcall ServiceRelease(void *self, void *)
{
    Record(2, self);
    return 0;
}
void __fastcall SetCurrentDevice(void *self, void *, int32_t index)
{
    Record(15, self, {uint32_t(index)});
}
int32_t __fastcall GetCurrentDevice(void *self, void *)
{
    Record(21, self);
    return gCurrentDevice;
}
int32_t __fastcall FindWindowDevice(void *self, void *)
{
    Record(22, self);
    return gWindowDevice;
}
uint32_t __fastcall GetDeviceCount(void *self, void *)
{
    Record(23, self);
    return gDeviceCount;
}
void *__fastcall GetDevice(void *self, void *, uint32_t index)
{
    Record(24, self, {index});
    return &gDevices[index < kDevices ? index : kDevices - 1];
}

void RecordSettings(int slot, const void *self, const uint32_t *settings)
{
    Call call{slot, self, {}};
    std::memcpy(call.args, settings, sizeof(call.args));
    gCalls.push_back(call);
}
bool __fastcall DeviceApply(void *self, void *, const uint32_t *settings)
{
    RecordSettings(23, self, settings);
    return gApplyAnswers[gApplyCount++ & 1];
}
void __fastcall DeviceLost(void *self, void *) { Record(24, self); }
void __fastcall DeviceRelease(void *self, void *) { Record(25, self); }
uint32_t *__fastcall DeviceSettings(void *self, void *, uint32_t *out)
{
    Record(33, self);
    const int index = static_cast<FakeObject *>(self)->index;
    for (uint32_t k = 0; k < 11; ++k) out[k] = 0xa0000000u + uint32_t(index) * 0x100u + k;
    return out;
}
bool __fastcall DeviceHad(void *self, void *)
{
    Record(34, self);
    return gHad[static_cast<FakeObject *>(self)->index];
}
void *__fastcall DeviceMonitor(void *self, void *)
{
    Record(42, self);
    return reinterpret_cast<void *>(0x7000000u + uint32_t(static_cast<FakeObject *>(self)->index) * 0x10u);
}
void __fastcall PlaceOnMonitor(void *self, void *, void *monitor)
{
    Record(20, self, {uint32_t(reinterpret_cast<uintptr_t>(monitor))});
}

void SetupDevices()
{
    std::memset(gFrameworkTable, 0, sizeof(gFrameworkTable));
    std::memset(gSystemTable, 0, sizeof(gSystemTable));
    std::memset(gDeviceTable, 0, sizeof(gDeviceTable));
    std::memset(gWindowTable, 0, sizeof(gWindowTable));
    gFrameworkTable[5] = reinterpret_cast<void *>(&GetSystemService);
    gSystemTable[2] = reinterpret_cast<void *>(&ServiceRelease);
    gSystemTable[15] = reinterpret_cast<void *>(&SetCurrentDevice);
    gSystemTable[21] = reinterpret_cast<void *>(&GetCurrentDevice);
    gSystemTable[22] = reinterpret_cast<void *>(&FindWindowDevice);
    gSystemTable[23] = reinterpret_cast<void *>(&GetDeviceCount);
    gSystemTable[24] = reinterpret_cast<void *>(&GetDevice);
    gDeviceTable[23] = reinterpret_cast<void *>(&DeviceApply);
    gDeviceTable[24] = reinterpret_cast<void *>(&DeviceLost);
    gDeviceTable[25] = reinterpret_cast<void *>(&DeviceRelease);
    gDeviceTable[33] = reinterpret_cast<void *>(&DeviceSettings);
    gDeviceTable[34] = reinterpret_cast<void *>(&DeviceHad);
    gDeviceTable[42] = reinterpret_cast<void *>(&DeviceMonitor);
    gWindowTable[2] = reinterpret_cast<void *>(&ServiceRelease);
    gWindowTable[20] = reinterpret_cast<void *>(&PlaceOnMonitor);
    gFramework = {gFrameworkTable, -1};
    gSystem = {gSystemTable, -2};
    gWindow = {gWindowTable, -3};
    for (int i = 0; i < kDevices; ++i) gDevices[i] = {gDeviceTable, i};
}

} // namespace

DIFF_TEST(GraphicModeSearch_SetDeviceMode, 0x18b250)
{
    constexpr uint32_t kFramework = 0x10c9378, kDesktop = 0x10ca868;
    using Fn = bool(__cdecl *)(int32_t, const cDisplayMode *, cIPixelFormat *, cIPixelFormat *, uint32_t, uint32_t,
                               uint32_t, uint32_t);
    using DesktopFn = bool(__cdecl *)(int32_t, cIPixelFormat *, cIPixelFormat *, uint32_t, uint32_t, uint32_t, uint32_t);
    const Fn orig = ctx.Original<Fn>(0x18b250);
    const Fn ours = &nGZGraphic4::SetDeviceMode;
    const DesktopFn origDesktop = ctx.Original<DesktopFn>(0x18b440);
    const DesktopFn oursDesktop = &nGZGraphic4::SetDeviceDesktopMode;
    SetupDevices();
    *ctx.image.At<void *>(kFramework) = &gFramework;
    cDisplayMode *const desktop = ctx.image.At<cDisplayMode>(kDesktop);
    const cDisplayMode savedDesktop = *desktop;

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const cDisplayMode desktops[] = {{0, 0, 0.0f, 0, nullptr}, {1024, 768, 1.3333334f, 60, nullptr}, {0, 0, nan, 0, nullptr}};
    const int32_t currents[] = {-1, int32_t(0x80000000u), 0, 1, 2};
    const uint32_t counts[] = {0, 1, 3};
    const int32_t devices[] = {-1, 0, 1, 2, 3, -3};
    const int32_t windows[] = {0, 2};
    auto *const color = reinterpret_cast<cIPixelFormat *>(0x6100);
    auto *const depth = reinterpret_cast<cIPixelFormat *>(0x6200);
    int caseIndex = 0;
    for (const cDisplayMode &desktopMode : desktops) {
        *desktop = desktopMode;
        // Requests: the desktop record itself, one differing in each word, and the desktop by 0x18b440.
        cDisplayMode requests[7];
        for (cDisplayMode &r : requests) r = desktopMode;
        requests[1].mWidth ^= 1;
        requests[2].mHeight ^= 1;
        requests[3].mAspect = 2.0f;
        requests[4].mRefresh ^= 1;
        requests[5].mFormat = color;
        for (int r = 0; r < 7; ++r)
            for (int32_t current : currents)
                for (uint32_t count : counts)
                    for (int32_t device : devices)
                        for (int32_t window : windows)
                            for (int answers = 0; answers < 8; ++answers) {
                                if (r == 5 && (answers & 4)) continue; // fewer cases on the format word
                                gCurrentDevice = current;
                                gDeviceCount = count;
                                gWindowDevice = window;
                                const uint32_t extra = uint32_t(caseIndex++);
                                bool a, b;
                                std::vector<Call> callsA;
                                for (int side = 0; side < 2; ++side) {
                                    for (bool &had : gHad) had = (answers & 1) != 0;
                                    gApplyAnswers[0] = (answers & 2) != 0;
                                    gApplyAnswers[1] = (answers & 4) != 0;
                                    gApplyCount = 0;
                                    gCalls.clear();
                                    bool result;
                                    if (r == 6)
                                        result = (side == 0 ? origDesktop : oursDesktop)(device, color, depth, extra, 2, 3,
                                                                                         0x40);
                                    else
                                        result = (side == 0 ? orig : ours)(device, &requests[r], color, depth, extra, 2, 3,
                                                                           0x40);
                                    if (side == 0) {
                                        a = result;
                                        callsA = gCalls;
                                    } else {
                                        b = result;
                                    }
                                }
                                ++ctx.cases;
                                if (a != b || !SameCalls(callsA, gCalls))
                                    ctx.Fail("device switch request %d current %d count %u device %d window %d answers %d: "
                                             "original %d (%zu calls), ours %d (%zu calls)",
                                             r, current, count, device, window, answers, a, callsA.size(), b,
                                             gCalls.size());
                            }
    }
    *desktop = savedDesktop;
    *ctx.image.At<void *>(kFramework) = nullptr;
}
