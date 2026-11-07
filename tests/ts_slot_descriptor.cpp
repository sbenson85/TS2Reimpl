#include "difftest.h"
#include "ts/cTSSlotDescriptor.h"
#include <array>
#include <cstring>
#include <random>
#include <vector>

namespace {
struct Guarded {
    uint8_t before[16];
    cTSSlotDescriptor slot;
    uint8_t after[16];
};
Guarded gWork;
struct FakeObject { void **vtable; };
void *gStreamVTable[21]{};
void *gOwnerVTable[79]{};
FakeObject gStream{gStreamVTable}, gOwner{gOwnerVTable};
std::vector<uint32_t> gTrace;
std::array<uint32_t, 24> gInput;
uint32_t gVersion, gFailureMask;
uint8_t gError;
unsigned gTransfers;

constexpr uint32_t kFloatEdges[] = {
    0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x00800000,
    0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000,
    0x7fc12345, 0x7f812345, 0xffc12345,
    0x3f800000, 0xbf800000, 0x4f000000, 0xcf000000,
    0x46ffff00, 0xc7000100
};
constexpr unsigned kFloatOffsets[] = {0x0c, 0x10, 0x14, 0x40, 0x44, 0x48, 0x4c, 0x54};

uint32_t Destination(const void *out)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(out);
    const uintptr_t base = reinterpret_cast<uintptr_t>(&gWork.slot);
    if (address >= base && address < base + sizeof(gWork.slot))
        return 0x100 + static_cast<uint32_t>(address - base);
    // All legacy conversions share one local. Its physical stack address is immaterial;
    // field destinations remain distinct, and failed reads never receive filler bytes.
    return 0xffffffff;
}
bool Get(unsigned slot, void *out, unsigned bytes)
{
    const unsigned index = gTransfers++;
    const bool success = (gFailureMask & (1u << index)) == 0;
    const uint32_t value = gInput.at(index) & (bytes == 2 ? 0xffff : 0xffffffff);
    gTrace.insert(gTrace.end(), {slot, Destination(out), value, success});
    if (success) std::memcpy(out, &value, bytes);
    return success;
}
bool __fastcall Get16(FakeObject *, void *, void *out) { return Get(6, out, 2); }
bool __fastcall Get32(FakeObject *, void *, void *out) { return Get(8, out, 4); }
bool __fastcall GetFloat(FakeObject *, void *, void *out) { return Get(12, out, 4); }
bool Put(unsigned slot, uint32_t value)
{
    const unsigned index = gTransfers++;
    const bool success = (gFailureMask & (1u << index)) == 0;
    gTrace.insert(gTrace.end(), {slot, value, success});
    return success;
}
bool __fastcall Put16(FakeObject *, void *, uint32_t value) { return Put(6, value); }
bool __fastcall Put32(FakeObject *, void *, uint32_t value) { return Put(8, value); }
bool __fastcall PutFloat(FakeObject *, void *, float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return Put(12, bits);
}
uint8_t __fastcall Error(FakeObject *, void *)
{
    gTrace.insert(gTrace.end(), {18, gError});
    return gError;
}
void *__fastcall Owner(FakeObject *, void *) { gTrace.push_back(20); return &gOwner; }
const uint32_t *__fastcall Version(FakeObject *, void *)
{
    gTrace.insert(gTrace.end(), {78, gVersion});
    return &gVersion;
}
void SetVTables(bool reading)
{
    gStreamVTable[6] = reading ? reinterpret_cast<void *>(&Get16) : reinterpret_cast<void *>(&Put16);
    gStreamVTable[8] = reading ? reinterpret_cast<void *>(&Get32) : reinterpret_cast<void *>(&Put32);
    gStreamVTable[12] = reading ? reinterpret_cast<void *>(&GetFloat) : reinterpret_cast<void *>(&PutFloat);
    gStreamVTable[18] = reinterpret_cast<void *>(&Error);
    gStreamVTable[20] = reinterpret_cast<void *>(&Owner);
    gOwnerVTable[78] = reinterpret_cast<void *>(&Version);
}
Guarded Initial(std::mt19937 &rng, unsigned iteration)
{
    Guarded initial;
    auto *bytes = reinterpret_cast<uint8_t *>(&initial);
    for (unsigned i = 0; i < sizeof(initial); ++i) bytes[i] = static_cast<uint8_t>(rng());
    // Rotate edge values through every float, including fields left untouched by old formats.
    if (iteration % 2 == 0) {
        for (unsigned i = 0; i < std::size(kFloatOffsets); ++i) {
            const uint32_t bits = kFloatEdges[(iteration / 2 + i) % std::size(kFloatEdges)];
            std::memcpy(reinterpret_cast<uint8_t *>(&initial.slot) + kFloatOffsets[i], &bits, 4);
        }
    }
    for (unsigned i = 0; i < gInput.size(); ++i)
        gInput[i] = iteration % 2 == 0 ? kFloatEdges[(iteration / 2 + i) % std::size(kFloatEdges)] : rng();
    // Exhaust the four combinations of the two old migration flags, also preserving other bits.
    gInput[7] = (rng() & ~0x600u) | ((iteration % 4) << 9);
    gError = std::array<uint8_t, 4>{0, 1, 0x80, 0xff}[iteration % 4];
    return initial;
}

// Transfer counts independently describe the format's field presence and the discarded v2 word.
unsigned ReadTransferCount(uint32_t version)
{
    if (version < 3) return version == 2 ? 5 : 4;
    return 8 + (version >= 4) + 5 * (version >= 5) + (version >= 7) +
        (version >= 8) + (version >= 9) + (version >= 10) +
        (version >= 0x40) + (version >= 0x41);
}

void RunPair(difftest::Context &ctx, bool reading, uint32_t rva, const Guarded &initial, unsigned iteration)
{
    using Fn = bool(__thiscall *)(cTSSlotDescriptor *, void *);
    // Use identical object AND stream addresses on both runs: failed legacy integer reads
    // can consume the stream argument's bits. No undefined stack bytes are excluded.
    gWork = initial;
    gTrace.clear();
    gTransfers = 0;
    const bool originalResult = ctx.Original<Fn>(rva)(&gWork.slot, &gStream);
    const Guarded originalState = gWork;
    const auto originalTrace = gTrace;
    const unsigned originalTransfers = gTransfers;
    gWork = initial;
    gTrace.clear();
    gTransfers = 0;
    const bool ourResult = reading ? gWork.slot.Read(&gStream) : gWork.slot.Write(&gStream);
    ++ctx.cases;
    if (originalResult != ourResult || originalResult != (gError == 0) ||
        std::memcmp(&originalState, &gWork, sizeof(gWork)) != 0 || originalTrace != gTrace)
        ctx.Fail("%s version %08x iteration %u mask %08x differs (return/object/trace)",
            reading ? "Read" : "Write", gVersion, iteration, gFailureMask);
    const unsigned expected = reading ? ReadTransferCount(gVersion) : 20;
    if (originalTransfers != expected || gTransfers != expected)
        ctx.Fail("version %08x transfer count: original %u ours %u expected %u", gVersion, originalTransfers, gTransfers, expected);
    if (!reading && std::memcmp(&initial, &gWork, sizeof(gWork)) != 0)
        ctx.Fail("Write changed object");
}
}

DIFF_TEST(SlotDescriptor_Read, 0x4dc020)
{
    SetVTables(true);
    std::mt19937 rng(0x51d3);
    constexpr uint32_t versions[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0x3f, 0x40, 0x41, 0x42, 0x43,
        0x46, 0x47, 0x48, 0x12345678, 0x80000000, 0xffffffff
    };
    for (uint32_t version : versions) {
        gVersion = version;
        uint32_t failedPositions = 0;
        for (unsigned iteration = 0; iteration < 128; ++iteration) {
            const Guarded initial = Initial(rng, iteration);
            // First 84 cases test success and EACH failed getter under all four error bytes.
            // The rest include arbitrary combinations of failed getters and all failing.
            const unsigned position = iteration / 4;
            gFailureMask = position == 0 ? 0 : position <= 20 ? 1u << (position - 1) :
                iteration == 127 ? 0xffffff : rng() & 0xffffff;
            RunPair(ctx, true, 0x4dc020, initial, iteration);
            failedPositions |= gFailureMask & ((1u << ReadTransferCount(version)) - 1);
        }
        if (failedPositions != (1u << ReadTransferCount(version)) - 1)
            ctx.Fail("version %08x missed failed getter coverage", version);
        std::printf("  Read version %08x: 128 cases, %u/%u getter failure positions\n",
            version, ReadTransferCount(version), ReadTransferCount(version));
    }
}

DIFF_TEST(SlotDescriptor_Write, 0x4dcae0)
{
    SetVTables(false);
    std::mt19937 rng(0x51d4);
    gVersion = 0x47;
    for (unsigned iteration = 0; iteration < 512; ++iteration) {
        const Guarded initial = Initial(rng, iteration);
        gFailureMask = iteration < 4 ? 0 : iteration < 84 ? 1u << (iteration / 4 - 1) : rng() & 0xffffff;
        RunPair(ctx, false, 0x4dcae0, initial, iteration);
    }
    std::printf("  Write: 512 cases, 20 transfer positions, four error bytes, all float edges\n");
}
