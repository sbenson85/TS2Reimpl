// nTSWorld::cWallSection (lane VR, campaign world-lot WL-0d): differential tests, original (by RVA) against ours on equal
// 0x68-byte blocks: the constructor 0x81fc70 (edges built, owners and indexes set, the Onext links), the destructor body
// 0x81fd40, the deleting destructor 0x81fd60, the entry accessor 0x81fd80 (slots 1 and 2), and the shared one-word
// bodies 0x1489e0 (slot 0, SetId) and 0x147a30 (slot 3, Id), which are ours only in our vtable.
// - Three routes: the original by RVA on a block it built; ours through our vtable (the object our constructor built); the
//   adapter on a block with the game's vtables (built by the original constructor), and with a trap in the slot under
//   test where the adapter must not dispatch.
// - After each call the whole block is dumped field by field (pointers as edge numbers, the section as 200) and compared.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "shape.h"
#include "world/cWallSection.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace {
using nTSWorld::cIWallEdge;
using nTSWorld::cIWallSection;
using nTSWorld::cWallEdge;
using nTSWorld::cWallSection;
using nTSWorld::cWallSectionAdapters;
using Blob = std::vector<uint32_t>;

constexpr uint32_t kSize = sizeof(cWallSection);

uint32_t SectionVt(const void *block)
{
    static const uint32_t kFace[] = {0};
    const uint32_t word = shape::Word(block, 0);
    return word == shape::P(shape::GameVTable(cWallSection::kVTable)) || word == shape::P(shape::Ours<cWallSection>(kFace).face[0]) ? 1u : word;
}

uint32_t EdgeVt(const void *edge)
{
    static const uint32_t kFace[] = {0};
    const uint32_t word = shape::Word(edge, 0);
    return word == shape::P(shape::GameVTable(cWallEdge::kVTable)) || word == shape::P(shape::Ours<cWallEdge>(kFace).face[0]) ? 1u : word;
}

void Dump(Blob &out, const void *block)
{
    const auto *b = static_cast<const uint8_t *>(block);
    out.push_back(SectionVt(block));
    out.push_back(shape::Word(block, 4));
    for (int i = 0; i < 4; ++i) {
        const uint8_t *edge = b + 8 + 24 * i;
        out.push_back(EdgeVt(edge));
        out.push_back(shape::Word(edge, 4) & 0xff);
        out.push_back(shape::Word(edge, 8));
        for (uint32_t off : {0xcu, 0x10u, 0x14u}) {
            const uint32_t w = shape::Word(edge, off);
            const uint32_t base = shape::P(b);
            if (w == base) out.push_back(200);
            else if (w >= base + 8 && w < base + kSize && (w - base - 8) % 24 == 0) out.push_back((w - base - 8) / 24);
            else out.push_back(w);
        }
    }
}

void *NewBlock(uint32_t fill)
{
    void *block = ::operator new(kSize);
    std::memset(block, int(fill), kSize);
    return block;
}

int gTrapHits = 0;
uint32_t __fastcall Trap(void *, void *, uint32_t) { ++gTrapHits; return 0; }

void CtorCases(difftest::Context &ctx)
{
    for (uint32_t fill : {0u, 0xcdu, 0x5au, 0xffu}) {
        Blob d[3];
        uint32_t returned[3];
        for (int route = 0; route < 3; ++route) {
            void *block = NewBlock(fill);
            void *r;
            if (route == 0) r = ctx.Original<void *(__thiscall *)(void *)>(0x81fc70)(block);
            else if (route == 1) r = static_cast<cIWallSection *>(new (block) cWallSection());
            else r = cWallSectionAdapters::Construct(static_cast<cWallSection *>(block), nullptr);
            returned[route] = uint32_t(r == block);
            Dump(d[route], block);
            ::operator delete(block, kSize);
        }
        ++ctx.cases;
        if (d[0] != d[1]) ctx.Fail("constructor over 0x%02x: ours differs from the original", fill);
        if (d[0] != d[2]) ctx.Fail("constructor over 0x%02x: the adapter differs from the original", fill);
        if (returned[0] != returned[2]) ctx.Fail("constructor over 0x%02x: result original %u, adapter %u", fill, returned[0], returned[2]);
    }
}

// A built section whose ids and edge fields are scrambled, so a destructor's clearing shows.
void *BuiltSection(difftest::Context &ctx, int route, uint32_t seed)
{
    void *block = NewBlock(0x11);
    if (route == 0) ctx.Original<void *(__thiscall *)(void *)>(0x81fc70)(block);
    else new (block) cWallSection();
    std::mt19937 rng(seed);
    *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(block) + 4) = uint32_t(rng());
    for (int i = 0; i < 4; ++i) {
        auto *edge = static_cast<uint8_t *>(block) + 8 + 24 * i;
        edge[4] = uint8_t(rng());
        *reinterpret_cast<uint32_t *>(edge + 0x10) = 0x7000 + rng() % 16;
        if (rng() % 2) *reinterpret_cast<uint32_t *>(edge + 0x14) = 0x1000 + rng() % 8; // an owner other than the section
    }
    return block;
}

void DtorCases(difftest::Context &ctx)
{
    for (uint32_t flags : {0u, 1u, 2u, 3u}) for (int body = 0; body < 2; ++body) for (uint32_t seed = 0; seed < 3; ++seed) {
        Blob d[2];
        for (int side = 0; side < 2; ++side) {
            void *block = BuiltSection(ctx, side ? 1 : 0, seed);
            difftest::FreeRecorder rec;
            void *ret = block;
            if (body) {
                if (side) cWallSectionAdapters::DestructBody(static_cast<cWallSection *>(block), nullptr);
                else ctx.Original<void(__thiscall *)(void *)>(0x81fd40)(block);
            } else {
                ret = side ? static_cast<void *>(cWallSectionAdapters::DeletingDestructor(static_cast<cWallSection *>(block), nullptr, flags))
                           : ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x81fd60)(block, flags);
            }
            rec.Stop();
            d[side] = {uint32_t(ret == block), uint32_t(rec.Freed(block))};
            size_t sizes[8]{};
            const size_t n = rec.SortedSizes(sizes, 8);
            d[side].push_back(uint32_t(n));
            for (size_t k = 0; k < n && k < 8; ++k) d[side].push_back(uint32_t(sizes[k]));
            if (!rec.Freed(block)) {
                Dump(d[side], block);
                ::operator delete(block, kSize);
            }
        }
        ++ctx.cases;
        if (d[0] != d[1]) ctx.Fail("%s flags %u seed %u: frees / fields differ", body ? "destructor body" : "deleting destructor", flags, seed);
    }
}

// The deleting destructor through our own vtable, on the object our constructor built.
void VirtualDtorCases(difftest::Context &ctx)
{
    for (int i = 0; i < 3; ++i) {
        difftest::FreeRecorder rec;
        auto *section = static_cast<cIWallSection *>(new cWallSection());
        void *block = section;
        delete section;
        rec.Stop();
        size_t sizes[4]{};
        const size_t n = rec.SortedSizes(sizes, 4);
        ++ctx.cases;
        if (!rec.Freed(block) || n != 1 || sizes[0] != kSize) ctx.Fail("delete through the vtable freed %u blocks (first %u bytes), want one of %u", unsigned(n), unsigned(sizes[0]), unsigned(kSize));
    }
}

void EntryCases(difftest::Context &ctx)
{
    std::mt19937 rng(0x81fd80);
    std::vector<uint32_t> indexes = {0, 1, 2, 3, 4, 5, 0x7fffffff, 0x80000000, 0xffffffff, 0xfffffffe, 0x10000000, 0x0aaaaaab};
    for (int i = 0; i < 300; ++i) indexes.push_back(uint32_t(rng()));
    void *game = BuiltSection(ctx, 0, 1);
    void *oursBlock = BuiltSection(ctx, 1, 1);
    void *trapped = BuiltSection(ctx, 0, 1);
    void *trapTable[5];
    std::memcpy(trapTable, shape::GameVTable(cWallSection::kVTable), sizeof(trapTable));
    trapTable[1] = trapTable[2] = reinterpret_cast<void *>(&Trap);
    shape::VTableAt(trapped, 0) = trapTable;
    auto *ours = static_cast<cIWallSection *>(static_cast<cWallSection *>(oursBlock));
    for (uint32_t index : indexes) {
        gTrapHits = 0;
        const uint32_t a = shape::P(ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x81fd80)(game, index)) - shape::P(game);
        const uint32_t b = shape::P(ours->EdgeAt(index)) - shape::P(oursBlock);
        const uint32_t c = shape::P(ours->EdgeAtC(index)) - shape::P(oursBlock);
        const uint32_t e = shape::P(cWallSectionAdapters::EdgeAt(static_cast<cWallSection *>(trapped), nullptr, index)) - shape::P(trapped);
        ++ctx.cases;
        if (a != b || a != c) ctx.Fail("index %08x: original +%x, slot 1 +%x, slot 2 +%x", index, a, b, c);
        if (a != e) ctx.Fail("index %08x: original +%x, adapter +%x", index, a, e);
        if (gTrapHits) ctx.Fail("index %08x: the adapter dispatched a slot", index);
    }
    Blob dg, dow, dt;
    Dump(dg, game);
    Dump(dow, oursBlock);
    shape::VTableAt(trapped, 0) = shape::GameVTable(cWallSection::kVTable);
    Dump(dt, trapped);
    if (dg != dow || dg != dt) ctx.Fail("an entry call changed a section");
    ::operator delete(game, kSize);
    ::operator delete(oursBlock, kSize);
    ::operator delete(trapped, kSize);
}

// The shared one-word slots, called through our vtable against the bodies in the image.
void FoldedCases(difftest::Context &ctx)
{
    std::mt19937 rng(0x1489e0);
    for (int i = 0; i < 64; ++i) {
        void *game = BuiltSection(ctx, 0, uint32_t(i));
        void *oursBlock = BuiltSection(ctx, 1, uint32_t(i));
        auto *ours = static_cast<cIWallSection *>(static_cast<cWallSection *>(oursBlock));
        const int32_t value = int32_t(rng());
        ctx.Original<void(__thiscall *)(void *, int32_t)>(0x1489e0)(game, value);
        ours->SetId(value);
        const int32_t a = ctx.Original<int32_t(__thiscall *)(void *)>(0x147a30)(game);
        const int32_t b = ours->Id();
        Blob dg, dow;
        Dump(dg, game);
        Dump(dow, oursBlock);
        ++ctx.cases;
        if (a != value || b != value) ctx.Fail("id %d: original answers %d, ours %d", value, a, b);
        if (dg != dow) ctx.Fail("id %d: the sections differ", value);
        ::operator delete(game, kSize);
        ::operator delete(oursBlock, kSize);
    }
}

} // namespace

DIFF_TEST(WallSection_Constructor, 0x81fc70) { CtorCases(ctx); }
DIFF_TEST(WallSection_Destructors, 0x81fd60) { DtorCases(ctx); VirtualDtorCases(ctx); }
DIFF_TEST(WallSection_Entry, 0x81fd80) { EntryCases(ctx); }
DIFF_TEST(WallSection_Folded, 0x147a30) { FoldedCases(ctx); }
