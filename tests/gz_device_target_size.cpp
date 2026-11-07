// Differential test for the current device's render-target size 0x18aea0 (lane VU, src/gz/DeviceTargetSize.cpp).
//
// Stood in on both sides: the current device getter 0x18ae40 (answers the fake device). Fakes: a device (cIDeviceDX:
// slot 16 the enumeration, 35 ready, 39 a back buffer), its enumeration (cIDeviceEnum slot 6, the adapter display mode
// through the hidden result pointer) and the back buffer (cISurface slots 20 / 21 / 22). Every other slot of the three
// notes its index (and would fail the comparison). Slot 35 answers a word whose low byte decides (high bytes seeded).
// The mode's words are seeded raw (aspects include signalling NaNs: a copy must keep the bits). Compared: the call log
// with its arguments, the result, and the out buffer with a word of slack on each side.
#include "code_patch.h"
#include "difftest.h"

#include "gz/DeviceTargetSize.h"
#include "gz/GraphicServices.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <utility>
#include <vector>

namespace {

struct Case {
    uint32_t mode[5];  // width, height, aspect bits, refresh, format
    uint32_t ready;    // slot 35's eax
    uint32_t back[3];  // width, height, format
};
const Case *gCase = nullptr;
std::vector<uint32_t> gLog;

struct Fake {
    const void *const *vtable;
};
Fake gDevice, gEnum, gBack;

// Who called: 1 the device, 2 the enumeration, 3 the back buffer, 0 anything else.
uint32_t Who(const void *self) { return self == &gDevice ? 1 : self == &gEnum ? 2 : self == &gBack ? 3 : 0; }

template <unsigned N> uint32_t __fastcall Unexpected(void *self, void *)
{
    gLog.insert(gLog.end(), {0xdead0000u | N, Who(self)});
    return 0;
}

void *__fastcall DeviceEnum(void *self, void *)
{
    gLog.insert(gLog.end(), {16, Who(self)});
    return &gEnum;
}
uint32_t __fastcall DeviceReady(void *self, void *)
{
    gLog.insert(gLog.end(), {35, Who(self)});
    return gCase->ready;
}
void *__fastcall DeviceBackBuffer(void *self, void *, uint32_t swapChain)
{
    gLog.insert(gLog.end(), {39, Who(self), swapChain});
    return &gBack;
}
void *__fastcall EnumMode(void *self, void *, uint32_t *result)
{
    gLog.insert(gLog.end(), {6, Who(self), result != nullptr});
    std::memcpy(result, gCase->mode, sizeof gCase->mode);
    return result;
}
uint32_t __fastcall BackWidth(void *self, void *)
{
    gLog.insert(gLog.end(), {20, Who(self)});
    return gCase->back[0];
}
uint32_t __fastcall BackHeight(void *self, void *)
{
    gLog.insert(gLog.end(), {21, Who(self)});
    return gCase->back[1];
}
uint32_t __fastcall BackFormat(void *self, void *)
{
    gLog.insert(gLog.end(), {22, Who(self)});
    return gCase->back[2];
}

template <size_t... I> std::vector<const void *> Unexpecteds(std::index_sequence<I...>)
{
    return {reinterpret_cast<const void *>(&Unexpected<I>)...};
}

std::vector<const void *> gDeviceTable, gEnumTable, gBackTable;

void BuildTables()
{
    gDeviceTable = Unexpecteds(std::make_index_sequence<50>());
    gEnumTable = Unexpecteds(std::make_index_sequence<30>());
    gBackTable = Unexpecteds(std::make_index_sequence<30>());
    gDeviceTable[16] = reinterpret_cast<const void *>(&DeviceEnum);
    gDeviceTable[35] = reinterpret_cast<const void *>(&DeviceReady);
    gDeviceTable[39] = reinterpret_cast<const void *>(&DeviceBackBuffer);
    gEnumTable[6] = reinterpret_cast<const void *>(&EnumMode);
    gBackTable[20] = reinterpret_cast<const void *>(&BackWidth);
    gBackTable[21] = reinterpret_cast<const void *>(&BackHeight);
    gBackTable[22] = reinterpret_cast<const void *>(&BackFormat);
    gDevice.vtable = gDeviceTable.data();
    gEnum.vtable = gEnumTable.data();
    gBack.vtable = gBackTable.data();
}

nGZGraphic4::cIDeviceDX *__cdecl StandDevice()
{
    gLog.push_back(0x18ae40);
    return reinterpret_cast<nGZGraphic4::cIDeviceDX *>(&gDevice);
}

uint32_t Word(std::mt19937 &rng)
{
    static constexpr uint32_t kWords[] = {0, 1, 640, 480, 1024, 768, 1600, 0x7fffffffu, 0x80000000u, 0xffffffffu};
    return rng() % 3 == 0 ? rng() : kWords[rng() % 10];
}

Case Seed(std::mt19937 &rng)
{
    static constexpr uint32_t kAspects[] = {0x3faaaaab, 0x3f400000, 0x3fe38e39, 0x3f800000, 0x7f800001, 0xffc00000,
                                            0x7fa00000, 0x80000000, 0};
    Case c;
    for (uint32_t &w : c.mode) w = Word(rng);
    c.mode[2] = rng() % 4 == 0 ? rng() : kAspects[rng() % 9];
    c.ready = (rng() % 2 ? rng() & 0xffffff00u : rng()) | (rng() % 2 ? 0 : 1 + rng() % 0xff);
    if (rng() % 3 == 0) c.ready &= 0xffffff00u;
    for (uint32_t &w : c.back) w = Word(rng);
    return c;
}

struct Result {
    std::vector<uint32_t> log;
    uint32_t out[6];
    bool returnsOut;
};

} // namespace

DIFF_TEST(DeviceTargetSize, 0x18aea0)
{
    BuildTables();
    shape::StandIn device{0x18ae40, &nGZGraphic4::GetCurrentDeviceCached, &StandDevice};
    using Fn = uint32_t *(__cdecl *)(uint32_t *);
    const Fn orig = ctx.Original<Fn>(0x18aea0);
    std::mt19937 rng(0x18aea0);
    unsigned ready = 0, notReady = 0;
    for (int i = 0; i < 3000; ++i) {
        const Case c = Seed(rng);
        gCase = &c;
        uint32_t fill[6];
        for (uint32_t &w : fill) w = rng();
        Result r[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            std::memcpy(r[side].out, fill, sizeof fill);
            uint32_t *const out = r[side].out + 1;
            const void *result =
                side == 0 ? static_cast<const void *>(orig(out))
                          : static_cast<const void *>(nGZGraphic4::GetDeviceTargetSize(
                                reinterpret_cast<nGZGraphic4::cDeviceDisplaySize *>(out)));
            r[side].returnsOut = result == out;
            r[side].log = gLog;
        }
        ++ctx.cases;
        ((c.ready & 0xff) ? ready : notReady)++;
        if (r[0].log != r[1].log || r[0].returnsOut != r[1].returnsOut || !r[0].returnsOut ||
            std::memcmp(r[0].out, r[1].out, sizeof r[0].out) != 0) {
            ctx.Fail("case %d (ready %08x): log %zu / %zu words, returns out %d / %d, out %08x %08x %08x %08x / %08x %08x "
                     "%08x %08x",
                     i, c.ready, r[0].log.size(), r[1].log.size(), r[0].returnsOut, r[1].returnsOut, r[0].out[1],
                     r[0].out[2], r[0].out[3], r[0].out[4], r[1].out[1], r[1].out[2], r[1].out[3], r[1].out[4]);
            return;
        }
    }
    if (!ready || !notReady) ctx.Fail("a path never ran (ready %u, not ready %u)", ready, notReady);
}
