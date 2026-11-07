// Differential tests for cGZLegacyControl's 22 window-class registrations (lane ME, foundations F6 part 1;
// src/gz/cGZLegacyControlRegistrations.h): each original, run by its RVA, against ours.
//
// The world per side: the shared vocabulary cell (0x10e6890) holds a recording vocabulary (a real class: its AddId logs
// the name's characters and the id, with the tables' sizes at that moment, and answers by the id's low bit, which both
// sides must ignore); the types and
// handlers tables (0x10e689c / 0x10e6894) are the game's std::maps built in place, filled beforehand with a seed's entries:
// some on the very keys the registration inserts (to a different value: an existing key must keep its value), some
// unrelated. Compared per case: the vocabulary's log (every call, in order, the name by content), then both tables
// entry by entry (key, value; a handler is an image address on both sides, except Bitmap's, which is ours since lane MF and compared by identity).
// The game's new / delete are stood in for by ours (the maps' nodes and the longer names), as in gz_legacy_control.cpp.
//
// Each registration has its own test (its RVA in the DIFF_TEST line); LegacyRegistrations_Sequence runs all 22 in the
// constructor's order on the pre-filled tables (later registrations see the earlier ones' entries).
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"

#include "game/game.h"
#include "gz/cGZLegacyControl.h"
#include "gz/cGZLegacyControlRegistrations.h"
#include "gz/cGZLegacyHandlers.h"
#include "gz/cIGZString.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace legacy_registrations_test {

using Ctl = cGZLegacyControl;
using Words = std::vector<uint32_t>;
using Original = void(__cdecl *)();

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

class RecordingVocab : public cIGZScriptVocab {
public:
    bool QueryInterface(uint32_t, void **) override { return false; }
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }
    bool AddId(const cIGZString &name, uint32_t id) override
    {
        log.push_back(0xadd);
        // Where the call falls among the table insertions: the tables' sizes now.
        log.push_back(uint32_t(GameAddress<LegacyTypeMap>(Ctl::kTypesCell)->size()));
        log.push_back(uint32_t(GameAddress<LegacyHandlerMap>(Ctl::kHandlersCell)->size()));
        log.push_back(name.Strlen());
        for (uint32_t i = 0; i < name.Strlen(); ++i) log.push_back(uint8_t(name.Data()[i]));
        log.push_back(id);
        return (id & 1) != 0;
    }
    bool AddObject(const cIGZString &, cIGZUnknown *) override { log.push_back(0xbad); return false; }
    bool Remove(const cIGZString &) override { log.push_back(0xbad); return false; }
    bool Lookup(const cIGZString &, cIGZUnknown **) override { log.push_back(0xbad); return false; }
    bool Clear() override { log.push_back(0xbad); return false; }
    Words log;
};

struct Entry {
    uint32_t rva;
    const char *name;
    Original ours;
};
const Entry kEntries[] = {
    {0x85e431, "CommonNames", &RegisterCommonNames},
    {0x86024e, "FlatRect", &RegisterFlatRect},
    {0x86249d, "Text", &RegisterText},
    {0x86288d, "TextEdit", &RegisterTextEdit},
    {0x861263, "ListBox", &RegisterListBox},
    {0x85e28d, "Combo", &RegisterCombo},
    {0x85dba9, "Button", &RegisterButton},
    {0x85da3d, "Bitmap", &RegisterBitmap},
    {0x861fd1, "Slider", &RegisterSlider},
    {0x861e67, "Scrollbar", &RegisterScrollbar},
    {0x860f01, "LineInput", &RegisterLineInput},
    {0x8614af, "OptionGroup", &RegisterOptionGroup},
    {0x862173, "Spinner", &RegisterSpinner},
    {0x861999, "Outline", &RegisterOutline},
    {0x862fa9, "TextTicker", &RegisterTextTicker},
    {0x85f31a, "FileBrowser", &RegisterFileBrowser},
    {0x863115, "TreeView", &RegisterTreeView},
    {0x860508, "Folders", &RegisterFolders},
    {0x8605b7, "Grid", &RegisterGrid},
    {0x85f26b, "Custom", &RegisterCustom},
    {0x861b05, "Scrollbar2", &RegisterScrollbar2},
    {0x8635c7, "Gen", &RegisterGen},
};
constexpr unsigned kCount = sizeof(kEntries) / sizeof(kEntries[0]);

// The game's tables, built in place for a side and destroyed after it.
struct Tables {
    Tables()
    {
        types = ::new (GameAddress<void>(Ctl::kTypesCell)) LegacyTypeMap;
        handlers = ::new (GameAddress<void>(Ctl::kHandlersCell)) LegacyHandlerMap;
    }
    ~Tables()
    {
        types->~LegacyTypeMap();
        handlers->~LegacyHandlerMap();
        std::memset(GameAddress<void>(Ctl::kTypesCell), 0, sizeof(LegacyTypeMap));
        std::memset(GameAddress<void>(Ctl::kHandlersCell), 0, sizeof(LegacyHandlerMap));
    }
    Tables(const Tables &) = delete;
    Tables &operator=(const Tables &) = delete;
    LegacyTypeMap *types;
    LegacyHandlerMap *handlers;
};

// The handlers that are ours (F6 part 2): on the original side the table holds the image's address, on ours our
// function. A word is compared as the image address; an image address found on our side shows as a marker (a
// registration that kept the original), and so does any other handler value as it is.
struct OurHandler {
    uint32_t rva;
    LegacyHandler ours;
};
const OurHandler kOurHandlers[] = {
    {0x864376, &ApplyBitmapProperties},
    {0x8674f6, &ApplyFlatRectProperties},
    {0x86b2be, &ApplyTextProperties},
    {0x86aa7f, &ApplySliderProperties},
    {0x86a820, &ApplyScrollbarProperties},
    {0x86a2a5, &ApplyOutlineProperties},
    {0x8676ea, &ApplyFoldersProperties},
    {0x865043, &ApplyComboProperties},
    {0x868e93, &ApplyListBoxProperties},
    {0x8688a3, &ApplyLineInputProperties},
    {0x86a4a8, &ApplyScrollbar2Properties},
    {0x86ac57, &ApplySpinnerProperties},
    {0x86be58, &ApplyTextTickerProperties},
    {0x86b5b0, &ApplyTextEditProperties},
    {0x8645be, &ApplyButtonProperties},
    {0x86942b, &ApplyOptionGroupProperties},
    {0x86d5ee, &ApplyGenProperties},
    {0x866410, &ApplyFileBrowserProperties},
    {0x867920, &ApplyGridProperties},
    {0x86c19e, &ApplyTreeViewProperties},
};

uint32_t HandlerWord(LegacyHandler handler, bool oursSide)
{
    for (const OurHandler &h : kOurHandlers) {
        const auto image = reinterpret_cast<LegacyHandler>(GameAddress<void>(h.rva));
        if (oursSide && handler == h.ours) return uint32_t(reinterpret_cast<uintptr_t>(image));
        if (oursSide && handler == image) return 0xbad00000 | h.rva;
        if (!oursSide && handler == h.ours) return 0xbad10000 | h.rva;
    }
    return uint32_t(reinterpret_cast<uintptr_t>(handler));
}

void Dump(Words &out, const Tables &tables, bool oursSide)
{
    out.push_back(0x7e5);
    out.push_back(uint32_t(tables.types->size()));
    for (const auto &entry : *tables.types) {
        out.push_back(entry.first);
        out.push_back(entry.second);
    }
    out.push_back(0x4a0d);
    out.push_back(uint32_t(tables.handlers->size()));
    for (const auto &entry : *tables.handlers) {
        out.push_back(entry.first);
        out.push_back(HandlerWord(entry.second, oursSide));
    }
}

// One side: the tables filled from `seed` (keys the first probe found collide with a probability), then `run`.
template <class Run> Words Side(const Words &probeKeys, uint32_t seed, Run run, bool oursSide)
{
    Words result;
    RecordingVocab vocab;
    Tables tables;
    std::mt19937 rng(seed * 2654435761u + 11);
    if (seed != 0) {
        for (size_t i = 0; i + 1 < probeKeys.size(); i += 2) {
            const uint32_t roll = rng() % 4;
            if (roll == 0) (*tables.types)[probeKeys[i]] = 0xdead0000 + uint32_t(i);
            if (roll == 1) (*tables.handlers)[probeKeys[i + 1]] = reinterpret_cast<LegacyHandler>(uintptr_t(0x1000 + i));
            if (roll == 2) {
                (*tables.types)[probeKeys[i]] = 0xdead0000 + uint32_t(i);
                (*tables.handlers)[probeKeys[i + 1]] = reinterpret_cast<LegacyHandler>(uintptr_t(0x1000 + i));
            }
        }
        for (unsigned i = rng() % 5; i; --i) {
            (*tables.types)[rng()] = rng();
            (*tables.handlers)[rng()] = reinterpret_cast<LegacyHandler>(uintptr_t(rng() | 1));
        }
    }
    Dump(result, tables, oursSide);
    *GameAddress<cIGZScriptVocab *>(Ctl::kVocabCell) = &vocab;
    run();
    *GameAddress<cIGZScriptVocab *>(Ctl::kVocabCell) = nullptr;
    result.push_back(0x109);
    result.insert(result.end(), vocab.log.begin(), vocab.log.end());
    Dump(result, tables, oursSide);
    return result;
}

// The keys a run inserts, found on empty tables: (class id, type id) pairs, flattened.
template <class Run> Words Probe(Run run)
{
    RecordingVocab vocab;
    Tables tables;
    *GameAddress<cIGZScriptVocab *>(Ctl::kVocabCell) = &vocab;
    run();
    *GameAddress<cIGZScriptVocab *>(Ctl::kVocabCell) = nullptr;
    Words keys;
    auto handler = tables.handlers->begin();
    for (const auto &entry : *tables.types) {
        keys.push_back(entry.first);
        keys.push_back(handler != tables.handlers->end() ? (handler++)->first : 0);
    }
    return keys;
}

bool Same(difftest::Context &ctx, const char *name, uint32_t seed, const Words &original, const Words &ours)
{
    ++ctx.cases;
    if (original == ours) return true;
    size_t i = 0;
    while (i < original.size() && i < ours.size() && original[i] == ours[i]) ++i;
    ctx.Fail("%s seed %u: differ at word %zu of %zu / %zu (original %08x, ours %08x)", name, seed, i, original.size(),
             ours.size(), i < original.size() ? original[i] : 0u, i < ours.size() ? ours[i] : 0u);
    return false;
}

constexpr uint32_t kSeeds = 40;

void CompareOne(difftest::Context &ctx, unsigned index)
{
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    const Entry &entry = kEntries[index];
    const Original original = ctx.Original<Original>(entry.rva);
    const Words keys = Probe(original);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        const Words a = Side(keys, seed, original, false);
        const Words b = Side(keys, seed, entry.ours, true);
        if (!Same(ctx, entry.name, seed, a, b)) return;
    }
}

} // namespace legacy_registrations_test

using namespace legacy_registrations_test;

DIFF_TEST(LegacyRegistrations_CommonNames, 0x85e431) { CompareOne(ctx, 0); }
DIFF_TEST(LegacyRegistrations_FlatRect, 0x86024e) { CompareOne(ctx, 1); }
DIFF_TEST(LegacyRegistrations_Text, 0x86249d) { CompareOne(ctx, 2); }
DIFF_TEST(LegacyRegistrations_TextEdit, 0x86288d) { CompareOne(ctx, 3); }
DIFF_TEST(LegacyRegistrations_ListBox, 0x861263) { CompareOne(ctx, 4); }
DIFF_TEST(LegacyRegistrations_Combo, 0x85e28d) { CompareOne(ctx, 5); }
DIFF_TEST(LegacyRegistrations_Button, 0x85dba9) { CompareOne(ctx, 6); }
DIFF_TEST(LegacyRegistrations_Bitmap, 0x85da3d) { CompareOne(ctx, 7); }
DIFF_TEST(LegacyRegistrations_Slider, 0x861fd1) { CompareOne(ctx, 8); }
DIFF_TEST(LegacyRegistrations_Scrollbar, 0x861e67) { CompareOne(ctx, 9); }
DIFF_TEST(LegacyRegistrations_LineInput, 0x860f01) { CompareOne(ctx, 10); }
DIFF_TEST(LegacyRegistrations_OptionGroup, 0x8614af) { CompareOne(ctx, 11); }
DIFF_TEST(LegacyRegistrations_Spinner, 0x862173) { CompareOne(ctx, 12); }
DIFF_TEST(LegacyRegistrations_Outline, 0x861999) { CompareOne(ctx, 13); }
DIFF_TEST(LegacyRegistrations_TextTicker, 0x862fa9) { CompareOne(ctx, 14); }
DIFF_TEST(LegacyRegistrations_FileBrowser, 0x85f31a) { CompareOne(ctx, 15); }
DIFF_TEST(LegacyRegistrations_TreeView, 0x863115) { CompareOne(ctx, 16); }
DIFF_TEST(LegacyRegistrations_Folders, 0x860508) { CompareOne(ctx, 17); }
DIFF_TEST(LegacyRegistrations_Grid, 0x8605b7) { CompareOne(ctx, 18); }
DIFF_TEST(LegacyRegistrations_Custom, 0x85f26b) { CompareOne(ctx, 19); }
DIFF_TEST(LegacyRegistrations_Scrollbar2, 0x861b05) { CompareOne(ctx, 20); }
DIFF_TEST(LegacyRegistrations_Gen, 0x8635c7) { CompareOne(ctx, 21); }

// All 22 in the constructor's order (RunRegistrations), the originals by RVA against ours.
DIFF_TEST(LegacyRegistrations_Sequence, 0x85e431)
{
    // Also reached: every RVA of the per-registration tests above (the entries of kEntries).
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    const auto originals = [&] {
        for (const Entry &entry : kEntries) ctx.Original<Original>(entry.rva)();
    };
    const auto ours = [&] {
        for (const Entry &entry : kEntries) entry.ours();
    };
    const Words keys = Probe(originals);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        const Words a = Side(keys, seed, originals, false);
        const Words b = Side(keys, seed, ours, true);
        if (!Same(ctx, "Sequence", seed, a, b)) return;
    }
}
