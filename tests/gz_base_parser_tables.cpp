// Differential test for cBaseParser's own tables (lane RQ; src/gz/cBaseParserTables.cpp): what an image-free program
// stores in place of the image's vtables and virtual base tables (standalone/owned_statics.cpp aliases the owned statics
// to them).
//
// Slot for slot: each of our entries is the forwarder the hook of the image's entry installs (so dispatch through our
// table reaches the same adapter the image's thunk does, which the per-function tests check against the original);
// cIBaseParser slot 33 is slot 32's entry in both; the folded entries (primary 2 and 3, face slots 11 and 45) are checked
// by what they do. The virtual base tables' words are the image's; the pure tables are _purecall throughout, as the
// image's are one entry throughout. Then dispatch on whole parsers: the original's built by the image (its tables), ours
// built by our constructor with our tables stored, the same results slot for slot.
#include "difftest.h"
#include "gz_base_parser_form.h"

#include "gz/cBaseParser.h"
#include "gz/cParserThunks.h"
#include "hook/hooks.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <random>
#include <string>

extern "C" int __cdecl _purecall();

using nGZCommandParser::cBaseParser;

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

} // namespace

DIFF_TEST(cBaseParser_OwnTables, 0xe7db24)
{
    struct Table { const char *name; uint32_t image; const void *const *ours; size_t count; };
    const Table tables[] = {
        {"primary", cBaseParser::kVTable, cBaseParser::kOwnVTable, std::size(cBaseParser::kOwnVTable)},
        {"cIGZUnknown", cBaseParser::kUnknownVTable, cBaseParser::kOwnUnknownVTable, std::size(cBaseParser::kOwnUnknownVTable)},
        {"cIBaseParser", cBaseParser::kParserVTable, cBaseParser::kOwnParserVTable, std::size(cBaseParser::kOwnParserVTable)},
    };
    const uint32_t base = P(GameAddress<void>(0));
    for (const Table &t : tables) {
        const uint32_t *image = GameAddress<uint32_t>(t.image);
        for (size_t i = 0; i < t.count; ++i) {
            ++ctx.cases;
            if (!t.ours[i]) ctx.Fail("%s slot %zu: empty", t.name, i);
            const bool primary = t.ours == cBaseParser::kOwnVTable, face = t.ours == cBaseParser::kOwnParserVTable;
            // Folded with other classes' bodies (not hookable): checked by what they do below.
            if ((primary && (i == 2 || i == 3)) || (face && (i == 11 || i == 45))) continue;
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
    // Face slots 11 and 45 are the image's shared thunks of shared bodies: slot 11 a bare return, slot 45 the word at
    // -0xc8 from the face (mVersion). Their bytes, so the behaviour below is the image's own.
    {
        const uint32_t *image = GameAddress<uint32_t>(cBaseParser::kParserVTable);
        ++ctx.cases;
        if (image[11] - base != 0x25f51d || image[45] - base != 0x9d661f) ctx.Fail("face slots 11 / 45 are not the shared thunks the table names");
    }

    ++ctx.cases;
    if (std::memcmp(GameAddress<int32_t>(cBaseParser::kVbTable), cBaseParser::kOwnVbTable, sizeof(cBaseParser::kOwnVbTable)) != 0)
        ctx.Fail("the virtual base table differs from the image's");
    if (std::memcmp(GameAddress<int32_t>(cBaseParser::kParserFaceVbTable), cBaseParser::kOwnParserFaceVbTable, sizeof(cBaseParser::kOwnParserFaceVbTable)) != 0)
        ctx.Fail("the cIBaseParser face's virtual base table differs from the image's");

    // The pure tables: the image's one entry (0x8261ba, _purecall) throughout, ours the toolchain's _purecall throughout.
    struct Pure { const char *name; uint32_t image; const void *const *ours; size_t count; };
    const Pure pures[] = {
        {"cIGZUnknown", cBaseParser::kIGZUnknownVTable, cBaseParser::kOwnPureUnknownVTable, 3},
        {"cIBaseParser", cBaseParser::kIBaseParserVTable, cBaseParser::kOwnPureParserVTable, 51},
        {"cIBaseParser's cIGZUnknown", cBaseParser::kIBaseParserUnknownVTable, cBaseParser::kOwnPureUnknownVTable, 3},
    };
    for (const Pure &t : pures) {
        const uint32_t *image = GameAddress<uint32_t>(t.image);
        for (size_t i = 0; i < t.count; ++i) {
            ++ctx.cases;
            if (image[i] - base != 0x8261ba) ctx.Fail("%s pure slot %zu: the image's entry is %x", t.name, i, image[i] - base);
            if (t.ours[i] != reinterpret_cast<const void *>(&_purecall)) ctx.Fail("%s pure slot %zu: ours is not _purecall", t.name, i);
        }
    }

    // Dispatch on whole parsers.
    using Fn0 = uint32_t(__thiscall *)(void *);
    using Fn1 = uint32_t(__thiscall *)(void *, uint32_t);
    using Fn2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
    std::mt19937 gen(0xe7db24);
    for (int round = 0; round < 8; ++round) {
        const int32_t version = round == 0 ? -1 : static_cast<int32_t>(gen());
        const uint32_t lowest = gen() % 8, highest = lowest + gen() % 8, field = gen();
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            alignas(8) static uint8_t memory[2][sizeof(cBaseParser) + 0x10];
            auto *o = reinterpret_cast<cBaseParser *>(memory[side]);
            uint8_t *bytes = memory[side];
            if (side == 0) {
                ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xafd800)(o, 1);
            } else {
                o->Construct(1);
                o->mVTable = cBaseParser::kOwnVTable;
                o->mVbTable = cBaseParser::kOwnVbTable;
                o->mParserFaceVbTable = cBaseParser::kOwnParserFaceVbTable;
                *reinterpret_cast<const void *const **>(bytes + 8 + cBaseParser::kUnknownOffset) = cBaseParser::kOwnUnknownVTable;
                *reinterpret_cast<const void *const **>(bytes + 8 + cBaseParser::kParserOffset) = cBaseParser::kOwnParserVTable;
            }
            uint8_t *unknown = bytes + 8 + cBaseParser::kUnknownOffset, *parser = bytes + 8 + cBaseParser::kParserOffset;
            auto **uv = *reinterpret_cast<void ***>(unknown);
            auto **pv = *reinterpret_cast<void ***>(parser);
            auto **vt = *reinterpret_cast<void ***>(o);
            std::string &s = got[side];
            auto word = [&](uint32_t v) { s += " " + Hex(v); };
            word(reinterpret_cast<Fn0>(pv[40])(parser));        // CommandCount
            word(reinterpret_cast<Fn0>(pv[42])(parser) & 0xff); // IsInitialised
            word(reinterpret_cast<Fn0>(pv[43])(parser) & 0xff); // IsCaseSensitive
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
                reinterpret_cast<Fn1>(vt[2])(o, value);
                word(reinterpret_cast<Fn0>(vt[3])(o));
                word(o->mField158);
            }
            parserform::TearDownParser(*o);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d: %s / %s", round, got[0].c_str(), got[1].c_str());
    }
}
