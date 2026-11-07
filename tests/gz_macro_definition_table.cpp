// Differential test for cMacroDefinition's own table (lane RU; src/gz/MacroDefinitionTable.cpp): what an image-free
// program's definitions store in place of the image's 0xe7db0c (standalone/owned_statics.cpp aliases it).
//
// Slot for slot: an entry whose image body is hooked is the replacement the hook installs; the image's folded bodies
// (0x148fb0, 0x1b9538, 0x147a30) are our members, run here against those bodies. The table is as long as the image's
// (the word after it is no code). Then dispatch on random objects: the same words, once with the image's table stored,
// once with ours, the same results and the same words afterwards (slot 1 only with a count of 2 or more, where it
// deletes nothing; slot 0 deletes and is the hooked deleting destructor, tested in gz_macro_definition.cpp).
#include "difftest.h"

#include "game/game.h"
#include "gz/MacroDefinition.h"
#include "hook/hooks.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <random>

using nGZCommandParser::cMacroDefinition;

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

constexpr uint32_t kSlots = sizeof(ts2_own_vt_e7db0c) / sizeof(ts2_own_vt_e7db0c[0]);

using Ret0Fn = uint32_t(__thiscall *)(void *);

} // namespace

DIFF_TEST(MacroDefinition_OwnTable, 0xe7db0c)
{
    const uint32_t base = P(GameAddress<void>(0));
    const uint32_t *image = GameAddress<uint32_t>(cMacroDefinition::kVTable);
    const struct { uint32_t rva; const void *ours; } folded[] = {
        {0x148fb0, Member(&cMacroDefinition::AddRef)},
        {0x1b9538, Member(&cMacroDefinition::RemoveRef)},
        {0x147a30, Member(&cMacroDefinition::RefCount)},
    };
    for (uint32_t i = 0; i < kSlots; ++i) {
        ++ctx.cases;
        const uint32_t irva = image[i] - base;
        const void *ours = ts2_own_vt_e7db0c[i];
        if (!IsCode(irva)) {
            ctx.Fail("slot %u: the image's entry %x is no code (table too long)", i, irva);
            continue;
        }
        if (const HookSpec *h = HookAt(irva)) {
            if (h->replacement != ours) ctx.Fail("slot %u: ours is %p, the hook of %x installs %p", i, ours, irva, h->replacement);
            continue;
        }
        const void *want = nullptr;
        for (const auto &f : folded)
            if (f.rva == irva) want = f.ours;
        if (!want) ctx.Fail("slot %u: the image's entry %x is neither hooked nor listed", i, irva);
        else if (want != ours) ctx.Fail("slot %u: ours is %p, the member doing %x's work is %p", i, ours, irva, want);
    }
    ++ctx.cases;
    if (IsCode(image[kSlots] - base)) ctx.Fail("the image's table has more than %u slots", kSlots);
    // A definition built by our constructor stores the owned table (the image's in a hooked build).
    ++ctx.cases;
    if (&ts2_rva_e7db0c != reinterpret_cast<const OwnedVTable *>(image))
        ctx.Fail("ts2_rva_e7db0c is %p, the image's table %p", static_cast<const void *>(&ts2_rva_e7db0c), image);
}

DIFF_TEST(MacroDefinition_OwnTableDispatch, 0x1b9538)
{
    std::mt19937 gen(0xe7db0c);
    const uint32_t *image = GameAddress<uint32_t>(cMacroDefinition::kVTable);
    for (uint32_t i = 1; i < kSlots; ++i) {
        for (int c = 0; c < 200; ++c) {
            uint32_t words[8];
            for (auto &w : words) w = gen();
            // Counts around the edges: 0, 1, 2, the top, random.
            const uint32_t kinds[] = {0, 1, 2, 0xffffffffu, 0x80000000u, words[1]};
            words[1] = kinds[gen() % 6];
            if (i == 1 && words[1] < 2) words[1] += 2;
            uint32_t saved[8];
            std::memcpy(saved, words, sizeof(words));
            words[0] = P(image);
            const uint32_t ra = reinterpret_cast<Ret0Fn>(image[i])(words);
            uint32_t after[8];
            std::memcpy(after, words, sizeof(words));
            std::memcpy(words, saved, sizeof(words));
            words[0] = P(ts2_own_vt_e7db0c);
            const uint32_t rb = reinterpret_cast<Ret0Fn>(ts2_own_vt_e7db0c[i])(words);
            if (words[0] == P(ts2_own_vt_e7db0c)) words[0] = P(image);
            ++ctx.cases;
            if (ra != rb) ctx.Fail("slot %u case %d (count %x): returned %x, the image's table %x", i, c, saved[1], rb, ra);
            if (std::memcmp(after, words, sizeof(words)) != 0)
                ctx.Fail("slot %u case %d (count %x): count %x, the image's table's %x", i, c, saved[1], words[1], after[1]);
        }
    }
}
