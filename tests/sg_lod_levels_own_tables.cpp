// Differential test for the scene graph's own vtables (lane VK): cLODLevelsTable's three vtables and two virtual base
// tables, its LOD command's vtable (src/sg/cLODLevelsTables.cpp), and cBranchNodesIIDFilter's (src/misc/SmallVirtuals.cpp):
// what an image-free program stores in place of the image's tables (standalone/owned_statics.cpp aliases the owned
// statics to them).
//
// Slot for slot, as tests/gz_base_parser_tables.cpp does for cBaseParser: an entry the image hooks is ours exactly when
// it is what that hook installs; the primary slots 1..4 are the image's cBaseParser entries (so ours are our cBaseParser
// table's); cIBaseParser slot 33 is slot 32's entry in both; the command's other slots are the image's cCommand or
// include command entries (so ours are those of our tables), and its Execute / GetName are the members the hooked
// adapters forward to. The virtual base tables' words are the image's. Then dispatch on whole tables: the original's made
// by the image's factory (its tables), ours by our factory with our tables stored, the same results slot for slot; and the
// command's name through each table.
#include "code_patch.h"
#include "difftest.h"
#include "gz_base_parser_form.h"
#include "shape.h"

#include "gz/CommandParser.h"
#include "gz/cBaseParser.h"
#include "hook/hooks.h"
#include "misc/SmallVirtuals.h"
#include "sg/cLODLevels.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <random>
#include <string>

extern "C" const void *ts2_own_vt_e7d72c[10]; // cCommand's (gz/CommandParserTables.cpp)
extern "C" const void *ts2_own_vt_e7dc6c[10]; // cIncludeCommand's
extern "C" const void *ts2_own_vt_e7cf54[2];  // cBranchNodesIIDFilter's (misc/SmallVirtuals.cpp)

using nGZCommandParser::cBaseParser;
using nGZSceneGraph::cLODCommand;
using nGZSceneGraph::cLODLevelsTable;

namespace {

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof(b), "%x", v);
    return b;
}

const HookSpec *HookAt(uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return h;
    return nullptr;
}

template <typename T> const void *Member(T pmf)
{
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

constexpr uint32_t kCommandVTable = 0xe7d72c;
constexpr uint32_t kIncludeCommandVTable = 0xe7dc6c;

} // namespace

DIFF_TEST(LODLevelsTable_OwnTables, 0xe767b8)
{
    const uint32_t base = P(GameAddress<void>(0));
    struct Table { const char *name; uint32_t image; const void *const *ours; size_t count; };
    const Table tables[] = {
        {"primary", cLODLevelsTable::kVTable, cLODLevelsTable::kOwnVTable, std::size(cLODLevelsTable::kOwnVTable)},
        {"cIGZUnknown", cLODLevelsTable::kUnknownVTable, cLODLevelsTable::kOwnUnknownVTable, std::size(cLODLevelsTable::kOwnUnknownVTable)},
        {"cIBaseParser", cLODLevelsTable::kParserVTable, cLODLevelsTable::kOwnParserVTable, std::size(cLODLevelsTable::kOwnParserVTable)},
    };
    const uint32_t *parserPrimary = GameAddress<uint32_t>(cBaseParser::kVTable);
    for (const Table &t : tables) {
        const uint32_t *image = GameAddress<uint32_t>(t.image);
        for (size_t i = 0; i < t.count; ++i) {
            ++ctx.cases;
            if (!t.ours[i]) ctx.Fail("%s slot %zu: empty", t.name, i);
            const bool primary = t.ours == cLODLevelsTable::kOwnVTable, face = t.ours == cLODLevelsTable::kOwnParserVTable;
            if (primary && i >= 1) {
                if (image[i] != parserPrimary[i]) ctx.Fail("primary slot %zu: the image's entry is not cBaseParser's", i);
                if (t.ours[i] != cBaseParser::kOwnVTable[i]) ctx.Fail("primary slot %zu: ours is not our cBaseParser table's", i);
                continue;
            }
            if (face && i == 33) {
                if (image[33] != image[32]) ctx.Fail("cIBaseParser slot 33: the image's entry is not slot 32's");
                if (t.ours[33] != t.ours[32]) ctx.Fail("cIBaseParser slot 33: ours is not slot 32's");
                continue;
            }
            const HookSpec *h = HookAt(image[i] - base);
            if (!h) ctx.Fail("%s slot %zu: the image's entry %x is not hooked", t.name, i, image[i] - base);
            else if (h->replacement != t.ours[i])
                ctx.Fail("%s slot %zu: ours is %p, the hook of %x installs %p", t.name, i, t.ours[i], image[i] - base, h->replacement);
        }
    }
    ++ctx.cases;
    if (std::memcmp(GameAddress<int32_t>(cLODLevelsTable::kVbTable), cLODLevelsTable::kOwnVbTable, sizeof(cLODLevelsTable::kOwnVbTable)) != 0)
        ctx.Fail("the virtual base table differs from the image's");
    if (std::memcmp(GameAddress<int32_t>(cLODLevelsTable::kParserFaceVbTable), cLODLevelsTable::kOwnParserFaceVbTable,
                    sizeof(cLODLevelsTable::kOwnParserFaceVbTable)) != 0)
        ctx.Fail("the cIBaseParser face's virtual base table differs from the image's");

    // Dispatch on whole tables: the original's from the image's factory, ours from ours with our tables stored.
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    using Fn0 = uint32_t(__thiscall *)(void *);
    using Fn1 = uint32_t(__thiscall *)(void *, uint32_t);
    using Fn2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
    std::mt19937 gen(0xe767b8);
    for (int round = 0; round < 8; ++round) {
        const int32_t version = round == 0 ? -1 : static_cast<int32_t>(gen());
        const uint32_t lowest = gen() % 8, highest = lowest + gen() % 8, field = gen();
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            void *const unknownFace = side == 0 ? ctx.Original<void *(__cdecl *)()>(0xab6fa0)() : cLODLevelsTable::Create();
            if (!unknownFace) {
                ctx.Fail("round %d side %d: no table", round, side);
                return;
            }
            auto *bytes = static_cast<uint8_t *>(unknownFace) - 0x1bc;
            auto *t = reinterpret_cast<cLODLevelsTable *>(bytes);
            if (side == 1) {
                *reinterpret_cast<const void *const **>(bytes) = cLODLevelsTable::kOwnVTable;
                *reinterpret_cast<const int32_t **>(bytes + 8) = cLODLevelsTable::kOwnVbTable;
                t->mUnknownFaceVTable = cLODLevelsTable::kOwnUnknownVTable;
                t->mParserFaceVTable = cLODLevelsTable::kOwnParserVTable;
                t->mParserFaceVbTable = cLODLevelsTable::kOwnParserFaceVbTable;
            }
            auto *o = reinterpret_cast<cBaseParser *>(bytes);
            uint8_t *unknown = bytes + 0x1bc, *parser = bytes + 0x1c4;
            auto **uv = *reinterpret_cast<void ***>(unknown);
            auto **pv = *reinterpret_cast<void ***>(parser);
            auto **vt = *reinterpret_cast<void ***>(bytes);
            std::string &s = got[side];
            auto word = [&](uint32_t v) { s += " " + Hex(v); };
            word(reinterpret_cast<Fn0>(pv[40])(parser));        // CommandCount
            word(reinterpret_cast<Fn0>(pv[42])(parser) & 0xff); // IsInitialised
            word(reinterpret_cast<Fn0>(pv[45])(parser));        // GetVersion
            o->mVersion = version;
            word(reinterpret_cast<Fn0>(pv[45])(parser));
            reinterpret_cast<Fn2>(pv[2])(parser, lowest, highest); // SetVersionRange
            word(reinterpret_cast<Fn0>(pv[45])(parser));
            word(static_cast<uint32_t>(o->mVersion));
            reinterpret_cast<Fn0>(pv[11])(parser);               // the no-op
            word(reinterpret_cast<Fn0>(pv[47])(parser));        // GetLine
            word(reinterpret_cast<Fn0>(uv[1])(unknown));        // AddRef
            word(reinterpret_cast<Fn0>(uv[1])(unknown));
            word(reinterpret_cast<Fn0>(uv[2])(unknown));        // Release
            word(o->mRefCount);
            for (uint32_t value : {0x1234u + round, 0u, 0xffffffffu, field}) {
                reinterpret_cast<Fn1>(vt[2])(bytes, value);
                word(reinterpret_cast<Fn0>(vt[3])(bytes));
            }
            parserform::TearDownParser(*o);
            std::destroy_at(&t->mLevels);
            ::operator delete(bytes);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d: %s / %s", round, got[0].c_str(), got[1].c_str());
    }
}

DIFF_TEST(LODCommand_OwnTable, 0xe76914)
{
    const uint32_t base = P(GameAddress<void>(0));
    const uint32_t *image = GameAddress<uint32_t>(cLODCommand::kVTable);
    const uint32_t *command = GameAddress<uint32_t>(kCommandVTable);
    const uint32_t *include = GameAddress<uint32_t>(kIncludeCommandVTable);
    for (size_t i = 0; i < std::size(cLODCommand::kOwnVTable); ++i) {
        ++ctx.cases;
        const void *ours = cLODCommand::kOwnVTable[i];
        if (i == 4 || i == 5) {
            const HookSpec *h = HookAt(image[i] - base);
            const void *member = i == 4 ? Member(&cLODCommand::Execute) : Member(&cLODCommand::GetName);
            if (!h) ctx.Fail("slot %zu: the image's entry %x is not hooked", i, image[i] - base);
            if (ours != member) ctx.Fail("slot %zu: ours is not the member the hook's adapter forwards to", i);
            continue;
        }
        // cCommand's entry where the image's is cCommand's (0..2, 6..8), else cCommandT's (the include command's: 3, 9).
        if (image[i] == command[i]) {
            if (ours != ts2_own_vt_e7d72c[i]) ctx.Fail("slot %zu: ours is not our cCommand table's", i);
        } else if (image[i] == include[i]) {
            if (ours != ts2_own_vt_e7dc6c[i]) ctx.Fail("slot %zu: ours is not our include command table's", i);
        } else {
            ctx.Fail("slot %zu: the image's entry %x is neither cCommand's nor the include command's", i, image[i] - base);
        }
    }
    // The name through each table.
    alignas(4) uint8_t junk[16] = {};
    using Fn = const char *(__thiscall *)(void *);
    const char *a = reinterpret_cast<Fn>(image[5])(junk);
    const char *b = reinterpret_cast<Fn>(cLODCommand::kOwnVTable[5])(junk);
    ++ctx.cases;
    if (!a || !b || std::strcmp(a, b) != 0) ctx.Fail("the names differ");
}

DIFF_TEST(cBranchNodesIIDFilter_OwnTable, 0xe7cf54)
{
    const uint32_t base = P(GameAddress<void>(0));
    const uint32_t *image = GameAddress<uint32_t>(nRZSceneGraph::cBranchNodesIIDFilter::kVTable);
    for (size_t i = 0; i < std::size(ts2_own_vt_e7cf54); ++i) {
        ++ctx.cases;
        const HookSpec *h = HookAt(image[i] - base);
        if (!h) ctx.Fail("slot %zu: the image's entry %x is not hooked", i, image[i] - base);
        else if (h->replacement != ts2_own_vt_e7cf54[i])
            ctx.Fail("slot %zu: ours is %p, the hook of %x installs %p", i, ts2_own_vt_e7cf54[i], image[i] - base, h->replacement);
    }
}
