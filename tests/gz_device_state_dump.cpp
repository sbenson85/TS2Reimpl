// Differential test for cDeviceStateDX::DumpInfo 0x17da90 (lane RJ, src/gz/cDeviceStateDXDump.cpp): the state cache's
// lines of the device's info dump, written to the stream it is given.
//
// The state cache is a zeroed block of the class's size with its block list (+0x9a08) built by the test; the blocks are
// zeroed blocks of cStateBlockDX's size with a record vector (+0xc) whose first word is the record's size (16 bits) and
// whose byte 3 its change count. The stream is a fake cIGZOStream: slot 9 (SetUint32) and slot 14 (SetRZCharStr) are
// logged with their values (the text by content); any other slot is logged as a stray call. Compared: the log, the list
// and the records left as they were. Cases: no blocks (the averages' divisor of 1), one, a few and many (up to 300), with
// sizes 0, small, large (up to 0xffff, so the totals need the 32-bit sums and the float conversion of large values) and
// change counts 0..255, records of 4 to 12 bytes (the bytes past the third read as noise).
#include "difftest.h"

#include "gz/cDeviceStateDX.h"
#include "gz/cStateBlockDX.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <vector>

using nGZGraphic4::cDeviceStateDX;
using nGZGraphic4::cStateBlockDX;

namespace {

std::string gLog;

void Note(const char *text)
{
    gLog += text;
    gLog += '|';
}

template <int N>
uint32_t __fastcall Stray(void *, void *)
{
    char line[32];
    std::snprintf(line, sizeof line, "stray slot %d", N);
    Note(line);
    return 0;
}
uint32_t __fastcall SetUint32(void *, void *, uint32_t value)
{
    char line[32];
    std::snprintf(line, sizeof line, "u32 %u", value);
    Note(line);
    return 1;
}
uint32_t __fastcall SetRZCharStr(void *, void *, const char *text)
{
    Note(("str " + std::string(text ? text : "<null>")).c_str());
    return 1;
}

void *const kStreamTable[] = {
    reinterpret_cast<void *>(&Stray<0>),  reinterpret_cast<void *>(&Stray<1>),  reinterpret_cast<void *>(&Stray<2>),
    reinterpret_cast<void *>(&Stray<3>),  reinterpret_cast<void *>(&Stray<4>),  reinterpret_cast<void *>(&Stray<5>),
    reinterpret_cast<void *>(&Stray<6>),  reinterpret_cast<void *>(&Stray<7>),  reinterpret_cast<void *>(&Stray<8>),
    reinterpret_cast<void *>(&SetUint32), reinterpret_cast<void *>(&Stray<10>), reinterpret_cast<void *>(&Stray<11>),
    reinterpret_cast<void *>(&Stray<12>), reinterpret_cast<void *>(&Stray<13>), reinterpret_cast<void *>(&SetRZCharStr),
    reinterpret_cast<void *>(&Stray<15>), reinterpret_cast<void *>(&Stray<16>), reinterpret_cast<void *>(&Stray<17>),
};
struct Stream {
    void *const *vtable = kStreamTable;
    uint32_t pad[3] = {};
};

// A state cache with its block list, and the blocks.
class World {
public:
    explicit World(const std::vector<std::vector<uint8_t>> &records)
        : state_(std::make_unique<uint32_t[]>(sizeof(cDeviceStateDX) / 4 + 1))
    {
        list_ = ::new (reinterpret_cast<uint8_t *>(state_.get()) + 0x9a08) std::vector<cStateBlockDX *>;
        for (const auto &r : records) {
            auto *block = reinterpret_cast<uint8_t *>(new uint32_t[sizeof(cStateBlockDX) / 4]());
            ::new (block + 0xc) std::vector<uint8_t>(r);
            list_->push_back(reinterpret_cast<cStateBlockDX *>(block));
        }
    }
    ~World()
    {
        for (cStateBlockDX *b : *list_) {
            auto *raw = reinterpret_cast<uint8_t *>(b);
            std::destroy_at(reinterpret_cast<std::vector<uint8_t> *>(raw + 0xc));
            delete[] reinterpret_cast<uint32_t *>(raw);
        }
        std::destroy_at(list_);
    }
    World(const World &) = delete;
    World &operator=(const World &) = delete;

    cDeviceStateDX *State() { return reinterpret_cast<cDeviceStateDX *>(state_.get()); }
    // The list and every record, after the call.
    std::string Contents() const
    {
        std::string out;
        char word[16];
        for (cStateBlockDX *b : *list_) {
            const auto &r = *reinterpret_cast<const std::vector<uint8_t> *>(reinterpret_cast<const uint8_t *>(b) + 0xc);
            for (uint8_t v : r) {
                std::snprintf(word, sizeof word, "%02x", v);
                out += word;
            }
            out += ';';
        }
        return out;
    }

private:
    std::unique_ptr<uint32_t[]> state_;
    std::vector<cStateBlockDX *> *list_ = nullptr;
};

std::vector<std::vector<uint8_t>> Records(std::mt19937 &rng)
{
    static const unsigned kCounts[] = {0, 1, 2, 3, 5, 7, 16, 40, 300};
    const unsigned count = rng() % 3 == 0 ? rng() % 12 : kCounts[rng() % (sizeof kCounts / sizeof kCounts[0])];
    std::vector<std::vector<uint8_t>> out(count);
    for (auto &r : out) {
        r.resize(4 + rng() % 9);
        for (auto &b : r) b = static_cast<uint8_t>(rng());
        uint16_t size;
        switch (rng() % 4) {
        case 0: size = 0; break;
        case 1: size = static_cast<uint16_t>(rng() % 64); break;
        case 2: size = static_cast<uint16_t>(0xff00u + rng() % 0x100); break;
        default: size = static_cast<uint16_t>(rng()); break;
        }
        std::memcpy(r.data(), &size, 2);
        switch (rng() % 4) {
        case 0: r[3] = 0; break;
        case 1: r[3] = 0xff; break;
        default: break;
        }
    }
    return out;
}

std::string Run(difftest::Context &ctx, bool ours, const std::vector<std::vector<uint8_t>> &records)
{
    World world(records);
    Stream stream;
    gLog.clear();
    if (ours)
        world.State()->DumpInfo(reinterpret_cast<cIGZUnknown *>(&stream));
    else
        ctx.Original<void(__thiscall *)(void *, void *)>(0x17da90)(world.State(), &stream);
    return gLog + "#" + world.Contents();
}

} // namespace

DIFF_TEST(cDeviceStateDX_DumpInfo, 0x17da90)
{
    for (uint32_t seed = 1; seed <= 400; ++seed) {
        std::mt19937 rng(seed * 2654435761u);
        const auto records = Records(rng);
        const std::string original = Run(ctx, false, records);
        const std::string ours = Run(ctx, true, records);
        ++ctx.cases;
        if (original != ours) {
            ctx.Fail("seed %u (%zu blocks):\n  original %s\n  ours     %s", seed, records.size(), original.c_str(), ours.c_str());
            return;
        }
    }
}
