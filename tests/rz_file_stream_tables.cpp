// Differential tests for the own vtables of cRZOFStream (0xda2c14), cRZIFStream (0xda2c78) and cDebugStream (0xdcf704)
// (src/rz/RZFileStreamsTables.cpp, src/rz/cDebugStream.cpp), lane FB: what an image-free program's objects store in
// place of the image's tables (standalone/owned_statics.cpp aliases the owned statics to them). The constructors'
// stored words are compared on both sides in tests/rz_file_streams.cpp and tests/rz_debug_stream.cpp (each side's
// constructor against the original's).
//
// Slot for slot: an entry whose image body is hooked is the replacement the hook installs; a folded body (shared with
// other classes, never hooked) is the member listed for it, which the dispatch test runs against that body; GetError
// 0x1b5941 is a shared body too (cRZIFStream, cRZOFStream and cDebugStream; no hook, ours is the member). Each table is
// as long as the image's. Dispatch: random objects, once with the image's table stored, once with ours, through the
// same slot; the same result and the same words afterwards. The slots taking part are those with no argument and no
// calls out (Release only with a count of 2 or more, so it deletes nothing) and the byte-order setter.
#include "difftest.h"

#include "game/game.h"
#include "hook/hooks.h"
#include "rz/RZFileStreams.h"
#include "rz/cDebugStream.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

const HookSpec *HookAt(uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return h;
    return nullptr;
}

template <typename T> const void *Member(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance members only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

bool IsCode(uint32_t rva)
{
    const auto *dos = GameAddress<IMAGE_DOS_HEADER>(0);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(reinterpret_cast<const char *>(dos) + dos->e_lfanew);
    const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s)
        if (rva >= s->VirtualAddress && rva < s->VirtualAddress + s->Misc.VirtualSize)
            return (s->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
    return false;
}

struct Folded { uint32_t rva; const void *ours; };
enum Kind { kSkip, kWord, kByte, kRelease, kByteOrder };

struct Table {
    const char *name;
    uint32_t rva;
    const void *const *ours;
    uint32_t count;
    std::vector<Kind> kinds;
    const OwnedVTable *owned;
};

std::vector<Folded> FoldedBodies()
{
    using T = cRZTextOStream;
    using O = cRZOStream;
    return {
        {0x148fb0, Member(&T::AddRef)},    {0x1b5fc4, Member(&O::Release)},   {0x144910, Member(&T::ReturnsTrue)},
        {0x1b6617, Member(&O::SetOwner)},  {0x145770, Member(&T::GetOwner)},  {0x1b5f3c, Member(&O::SetByteOrder)},
        {0x1b5941, Member(&cRZOFStream::GetError)},
    };
}

std::vector<Table> Tables()
{
    std::vector<Kind> kinds(24, kSkip);
    kinds[1] = kWord;
    kinds[2] = kRelease;
    kinds[23] = kByte;
    kinds[20] = kWord;
    kinds[22] = kByteOrder;
    return {
        {"cRZOFStream", 0xda2c14, ts2_own_vt_da2c14, 24, kinds, &ts2_rva_da2c14},
        {"cRZIFStream", 0xda2c78, ts2_own_vt_da2c78, 24, kinds, &ts2_rva_da2c78},
        {"cDebugStream", 0xdcf704, ts2_own_vt_dcf704, 24, kinds, &ts2_rva_dcf704},
    };
}

using Ret0Fn = uint32_t(__thiscall *)(void *);
using Ret4Fn = uint32_t(__thiscall *)(void *, uint32_t);

} // namespace

DIFF_TEST(RZFileStreamTables_OwnTables, 0xda2c14)
{
    const uint32_t base = P(GameAddress<void>(0));
    const std::vector<Folded> folded = FoldedBodies();
    for (const Table &table : Tables()) {
        const uint32_t *image = GameAddress<uint32_t>(table.rva);
        for (uint32_t i = 0; i < table.count; ++i) {
            ++ctx.cases;
            const uint32_t irva = image[i] - base;
            const void *ours = table.ours[i];
            if (!IsCode(irva)) {
                ctx.Fail("%s slot %u: the image's entry %x is no code (table too long)", table.name, i, irva);
                continue;
            }
            if (const HookSpec *h = HookAt(irva)) {
                if (h->replacement != ours)
                    ctx.Fail("%s slot %u: ours is %p, the hook of %x installs %p", table.name, i, ours, irva, h->replacement);
                continue;
            }
            const void *want = nullptr;
            for (const Folded &f : folded)
                if (f.rva == irva) want = f.ours;
            if (!want) ctx.Fail("%s slot %u: the image's entry %x is neither hooked nor listed", table.name, i, irva);
            else if (want != ours) ctx.Fail("%s slot %u: ours is %p, the member doing %x's work is %p", table.name, i, ours, irva, want);
        }
        ++ctx.cases;
        if (IsCode(image[table.count] - base)) ctx.Fail("%s: the image's table has more than %u slots", table.name, table.count);
        ++ctx.cases;
        if (P(table.owned) != P(image)) ctx.Fail("%s: the owned static is %p, the image's table %p", table.name, table.owned, image);
    }
}

DIFF_TEST(RZFileStreamTables_OwnTablesDispatch, 0xda2c78)
{
    std::mt19937 gen(0xda2c78);
    for (const Table &table : Tables()) {
        const uint32_t *image = GameAddress<uint32_t>(table.rva);
        for (uint32_t i = 0; i < table.count; ++i) {
            const Kind kind = table.kinds[i];
            if (kind == kSkip) continue;
            for (int c = 0; c < 200; ++c) {
                uint32_t words[12];
                for (auto &w : words) w = gen();
                const uint32_t counts[] = {0, 1, 2, 0x80000000u, 0xffffffffu, words[1]};
                words[1] = counts[gen() % 6];
                if (kind == kRelease && words[1] < 2) words[1] += 2;
                const uint32_t arg = gen() % 3 == 0 ? gen() % 2 : gen();
                uint32_t saved[12];
                std::memcpy(saved, words, sizeof(words));
                auto run = [&](const void *const *slots) -> uint32_t {
                    if (kind == kByteOrder) return reinterpret_cast<Ret4Fn>(slots[i])(words, arg);
                    return reinterpret_cast<Ret0Fn>(slots[i])(words);
                };
                words[0] = P(image);
                const uint32_t ra = run(reinterpret_cast<const void *const *>(image));
                uint32_t after[12];
                std::memcpy(after, words, sizeof(words));
                std::memcpy(words, saved, sizeof(words));
                words[0] = P(table.ours);
                const uint32_t rb = run(table.ours);
                if (words[0] == P(table.ours)) words[0] = P(image);
                ++ctx.cases;
                const bool byteResult = kind == kByte || kind == kByteOrder;
                if (byteResult ? (ra & 0xff) != (rb & 0xff) : ra != rb)
                    ctx.Fail("%s slot %u case %d: returned %x, the image's table %x", table.name, i, c, rb, ra);
                if (std::memcmp(after, words, sizeof(words)) != 0)
                    ctx.Fail("%s slot %u case %d: the object differs from the image's table's run", table.name, i, c);
            }
        }
    }
}
