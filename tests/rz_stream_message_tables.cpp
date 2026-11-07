// Differential tests for the own vtables of cRZTextOStream (0xda2b4c), cRZTextODSStream (0xda2bb0;
// src/rz/RZFileStreamsTables.cpp) and cRZMessage2Standard (0xda4064; src/rz/cRZMessage2StandardTable.cpp), lane RU:
// what an image-free program's objects store in place of the image's tables (standalone/owned_statics.cpp aliases the
// owned statics to them).
//
// Slot for slot: an entry whose image body is hooked is the replacement the hook installs; the image's _purecall is
// ours; a folded body (shared with other classes, never hooked) is the member listed for it, which the dispatch test
// runs against that body. Each table is as long as the image's (the word after it is no code). Dispatch: random
// objects, once with the image's table stored, once with ours, through the same slot; the same result and the same
// words afterwards. The slots taking part are those with no argument and no calls out (Release only with a count of
// 2 or more, so it deletes nothing) and the byte-order setter.
#include "difftest.h"

#include "game/game.h"
#include "hook/hooks.h"
#include "rz/RZFileStreams.h"
#include "rz/cRZMessage2Standard.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

extern "C" int __cdecl _purecall();

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

constexpr uint32_t kPurecall = 0x8261ba;

struct Folded { uint32_t rva; const void *ours; };

// Kinds of slot for the dispatch test.
enum Kind { kSkip, kWord, kByte, kVoid, kRelease, kByteOrder };

struct Table {
    const char *name;
    uint32_t rva;
    const void *const *ours;
    uint32_t count;
    std::vector<Folded> folded;
    std::vector<Kind> kinds; // one per slot
    const OwnedVTable *owned;
};

std::vector<Table> Tables()
{
    using T = cRZTextOStream;
    using O = cRZOStream;
    using M = cRZMessage2Standard;
    const std::vector<Folded> streamFolded = {
        {0x148fb0, Member(&T::AddRef)},        {0x1b5fc4, Member(&O::Release)},
        {0x140fe0, Member(&T::Flush)},         {0x144910, Member(&T::ReturnsTrue)},
        {0x1b6617, Member(&O::SetOwner)},      {0x145770, Member(&T::GetOwner)},
        {0x1b5f3c, Member(&O::SetByteOrder)},
    };
    std::vector<Kind> stream(24, kSkip);
    stream[1] = kWord;
    stream[2] = kRelease;
    stream[3] = kVoid;
    stream[18] = stream[23] = kByte;
    stream[20] = kWord;
    stream[22] = kByteOrder;
    std::vector<Kind> message(38, kSkip);
    for (uint32_t i : {1u, 4u, 9u, 10u, 14u, 15u, 19u, 20u, 24u, 25u, 32u}) message[i] = kWord;
    message[2] = kRelease;
    return {
        {"cRZTextOStream", 0xda2b4c, ts2_own_vt_da2b4c, 24, streamFolded, stream, &ts2_rva_da2b4c},
        {"cRZTextODSStream", 0xda2bb0, ts2_own_vt_da2bb0, 24, streamFolded, stream, &ts2_rva_da2bb0},
        {"cRZMessage2Standard", 0xda4064, ts2_own_vt_da4064, 38,
         {{0x148fb0, Member(&M::AddRef)},
          {0x145770, Member(&M::GetId)},
          {0x149cf0, Member(&M::GetValue0)},
          {0x149d00, Member(&M::GetValue1)},
          {0x148ba0, Member(&M::GetValue2)},
          {0x147d00, Member(&M::GetValue3)},
          {0x15e970, Member(&M::GetObjectField)}},
         message, &ts2_rva_da4064},
    };
}

using Ret0Fn = uint32_t(__thiscall *)(void *);
using Ret4Fn = uint32_t(__thiscall *)(void *, uint32_t);

} // namespace

DIFF_TEST(RZStreamMessage_OwnTables, 0xda2bb0)
{
    const uint32_t base = P(GameAddress<void>(0));
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
            if (irva == kPurecall) {
                if (ours != reinterpret_cast<const void *>(&_purecall))
                    ctx.Fail("%s slot %u: the image's is _purecall, ours %p", table.name, i, ours);
                continue;
            }
            if (const HookSpec *h = HookAt(irva)) {
                if (h->replacement != ours)
                    ctx.Fail("%s slot %u: ours is %p, the hook of %x installs %p", table.name, i, ours, irva, h->replacement);
                continue;
            }
            const void *want = nullptr;
            for (const Folded &f : table.folded)
                if (f.rva == irva) want = f.ours;
            if (!want) ctx.Fail("%s slot %u: the image's entry %x is neither hooked nor listed", table.name, i, irva);
            else if (want != ours) ctx.Fail("%s slot %u: ours is %p, the member doing %x's work is %p", table.name, i, ours, irva, want);
        }
        ++ctx.cases;
        if (IsCode(image[table.count] - base)) ctx.Fail("%s: the image's table has more than %u slots", table.name, table.count);
        // In a hooked build the owned static is the image's table.
        ++ctx.cases;
        if (P(table.owned) != P(image)) ctx.Fail("%s: the owned static is %p, the image's table %p", table.name, table.owned, image);
    }
}

DIFF_TEST(RZStreamMessage_OwnTablesDispatch, 0xda4064)
{
    std::mt19937 gen(0xda4064);
    for (const Table &table : Tables()) {
        const uint32_t *image = GameAddress<uint32_t>(table.rva);
        for (uint32_t i = 0; i < table.count; ++i) {
            const Kind kind = table.kinds[i];
            // A pure slot (cRZTextOStream's 3, 14 and 21) aborts on both sides: not run.
            if (kind == kSkip || image[i] - P(GameAddress<void>(0)) == kPurecall) continue;
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
                if (kind != kVoid && (byteResult ? (ra & 0xff) != (rb & 0xff) : ra != rb))
                    ctx.Fail("%s slot %u case %d: returned %x, the image's table %x", table.name, i, c, rb, ra);
                if (std::memcmp(after, words, sizeof(words)) != 0)
                    ctx.Fail("%s slot %u case %d: the object differs from the image's table's run", table.name, i, c);
            }
        }
    }
}
