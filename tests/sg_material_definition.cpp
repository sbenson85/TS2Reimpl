// Differential tests for nGZSceneGraph::cMaterialDefinition (lane RP, scenegraph M2a; src/sg/cMaterialDefinition.*) and
// 0xae88d0.
//
// SG_MaterialDefinition_Life: a Life-style test through the class's outer slots. Each side makes definitions with its
// factory (the game's 0xaadf20: pool new 0xaad480 + constructor 0xaad1c0; ours Create) and runs the same seeded sequence
// of slot calls through each object's own vtables: SetMaterialName / SetMaterialType / SetProperty / AddTextureById /
// AddTextureByName with names that meet ignoring case, equal names, empty and null names, sorted inserts at the front,
// the middle, the end and at capacity; ClearProperties; Clone (counted by the test, then a full object of its own);
// IsEqual / IsEqualProperties against itself, clones, others and a non-material fake whose QueryInterface fails; the
// getters (name, type, counts, every property and texture name), FindProperty / FindPropertyNoCase (present, absent,
// case variants); QueryInterface (the face, self, the resource and referent iids, others; a null out), AddRef / Release,
// GetResource / GetResourceConst and the virtual base's records 18 / 19, GetTypeInfo, GetOwnerInfo. After every few
// steps each object's state is dumped raw (the resource words, the name and type cells, both vectors' sizes, capacities
// and element words), and at the end the last Release of each object (its 0x58 block back at the head of the pool's free
// list), the string pool's and the texture manager's counts per handle, and the heap blocks left (none).
//
// The fakes: the string pool in 0x10f9070 (slot 5 interns: flag 0 ignoring case, flag 1 exact, so a cell opened with the
// wrong flag shows; 6 / 7 count; 8 the text), the texture manager in 0x10f8f94 (slot 24 a record whose first word is a
// name text, 18 / 19 count texture ids), the RZFastFixed0x58 pool 0x10f6704 (fresh pages per side). The order of the
// count calls is not compared (only the final counts and that none went below zero): the two STLs copy and destroy
// elements in different orders when a vector moves, which nothing can observe.
//
// The RVAs SG_MaterialDefinition_Life verifies (ours through the factory): the factory 0xaadf20, pool new 0xaad480,
// constructor 0xaad1c0, destructor 0xaad420, deleting destructor 0xaad530, pool free 0xaad4b0, QueryInterface 0xaae210,
// and the face's slots Clone 0xaad790, SetMaterialName 0xaaed10, ClearProperties 0xaad750, SetMaterialType 0xaaecb0,
// SetProperty 0xaaed70, AddTextureById 0xaad570, AddTextureByName 0xaad680, IsEqual 0xaadf90, IsEqualProperties
// 0xaae010, GetMaterialName 0xaae090, GetMaterialType 0xaadee0, CountProperties 0xaae0f0, GetProperty 0xaae100,
// FindProperty 0xaae140, FindPropertyNoCase 0xaae1c0, CountTextures 0xaae0e0, GetTextureName 0xaadf00. Slots 3 / 5 / 14
// (Read 0xaae280, Write 0xaaefc0, 0xaad850) are SG_MaterialDefinition_ReadWrite's.
//
// SG_MaterialDefinition_ReadWrite: the game's Read / Write / slot 14 on the game's object against ours on ours (its
// section below).
//
// SG_OpenNameCellCaseSensitive: 0xae88d0 against OpenNameCellCaseSensitive.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "rz/RZFixedAllocator.h"
#include "rz/StaticGetters2.h"
#include "rz/cRZString.h"
#include "sg/cMaterialDefinition.h"

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace nGZSceneGraph;

namespace {

constexpr uint32_t kFactory = 0xaadf20;
constexpr uint32_t kStringPoolCell = 0x10f9070; // GetCachedObjectB's cache: the string pool
constexpr uint32_t kTextureCell = 0x10f8f94;    // the texture manager's cache (0xae8750)
constexpr uint32_t kFace = 0x2c;                // the face's offset in the object

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
template <class T> T Rd(const void *base, uint32_t off) { return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + off); }

// ---- notes --------------------------------------------------------------------------------------------------------------
struct Note {
    std::string what;
    std::vector<uint32_t> words;
    std::string text;
    bool operator==(const Note &o) const { return what == o.what && words == o.words && text == o.text; }
};

struct Run {
    std::vector<Note> notes;
    // Copies under NotCounted: the caller's temporaries are freed as counted as they were made, the notes are not counted.
    void Add(const std::string &what, const std::vector<uint32_t> &words, const std::string &text = {})
    {
        difftest::NotCounted quiet;
        notes.push_back({what, words, text});
    }
};

std::string Show(const Note &n)
{
    std::string s = n.what;
    char buf[16];
    for (uint32_t w : n.words) {
        std::snprintf(buf, sizeof(buf), " %x", w);
        s += buf;
    }
    if (!n.text.empty()) s += " '" + n.text + "'";
    return s;
}

std::string Text(const char *s) { return s ? std::string("=") + s : std::string("<null>"); }

Run *gRun = nullptr; // the side running: the fakes note into it

// ---- the string pool (0x10f9070) ----------------------------------------------------------------------------------------
constexpr uint32_t kFirstHandle = 0x1000;
struct StringPool {
    std::deque<std::string> texts; // by handle - kFirstHandle (a deque: the texts never move)
    std::vector<std::string> keys;
    std::vector<int> counts;
    bool negative = false;
    uint32_t unknown = 0; // counts asked of handles the pool never gave (not -1)
    void Reset()
    {
        texts.clear();
        keys.clear();
        counts.clear();
        negative = false;
        unknown = 0;
    }
    int *Count(uint32_t h)
    {
        if (h == 0xffffffffu) return nullptr;
        if (h < kFirstHandle || h - kFirstHandle >= counts.size()) {
            ++unknown;
            return nullptr;
        }
        return &counts[h - kFirstHandle];
    }
} gStrings;

uint32_t __fastcall PoolOpen(void *, void *, uint32_t a, uint32_t flag)
{
    difftest::NotCounted quiet;
    const char *text = reinterpret_cast<const char *>(static_cast<uintptr_t>(a));
    std::string s = text ? text : "";
    std::string key = (flag ? "S:" : "I:");
    for (char c : s) key += flag ? c : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    uint32_t h = 0;
    for (size_t i = 0; i < gStrings.keys.size(); ++i)
        if (gStrings.keys[i] == key) h = kFirstHandle + static_cast<uint32_t>(i);
    if (!h) {
        h = kFirstHandle + static_cast<uint32_t>(gStrings.keys.size());
        gStrings.keys.push_back(key);
        gStrings.texts.push_back(s);
        gStrings.counts.push_back(0);
    }
    if (gRun) gRun->Add("pool open", {h, flag}, s);
    return h;
}
uint32_t __fastcall PoolUse(void *, void *, uint32_t h)
{
    if (int *c = gStrings.Count(h)) ++*c;
    return 0;
}
uint32_t __fastcall PoolClose(void *, void *, uint32_t h)
{
    if (int *c = gStrings.Count(h)) {
        if (--*c < 0) gStrings.negative = true;
    }
    return 0;
}
uint32_t __fastcall PoolText(void *, void *, uint32_t h)
{
    if (h >= kFirstHandle && h - kFirstHandle < gStrings.texts.size()) return P(gStrings.texts[h - kFirstHandle].c_str());
    return P("");
}
// Slot 9: the text's length (0xaeb7c0; slot 14 0xaad850 asks it).
uint32_t __fastcall PoolLength(void *, void *, uint32_t h)
{
    if (h >= kFirstHandle && h - kFirstHandle < gStrings.texts.size())
        return static_cast<uint32_t>(gStrings.texts[h - kFirstHandle].size());
    return 0;
}
uint32_t __fastcall PoolTrap(void *, void *)
{
    if (gRun) gRun->Add("string pool: an unexpected slot", {});
    return 0;
}
void *gPoolVt[10];
void *gPoolObject[1] = {gPoolVt};

// ---- the texture manager (0x10f8f94) -------------------------------------------------------------------------------------
const char *const kTextureNames[] = {"wall_a", "WALL_A", "floor01", "Floor01", "a", "zz", "", "m#1", "Wall_A", "brick"};
constexpr uint32_t kTextureNameCount = sizeof(kTextureNames) / sizeof(kTextureNames[0]);

struct TextureManager {
    std::map<uint32_t, int> counts;
    bool negative = false;
    void Reset()
    {
        counts.clear();
        negative = false;
    }
} gTextures;

void __fastcall TexAddRef(void *, void *, uint32_t id)
{
    difftest::NotCounted quiet;
    ++gTextures.counts[id];
}
void __fastcall TexRelease(void *, void *, uint32_t id)
{
    difftest::NotCounted quiet;
    if (--gTextures.counts[id] < 0) gTextures.negative = true;
}
// Slot 24 (thiscall (out, id), ret 8): the record, both words written; returns out.
uint32_t *__fastcall TexRecord(void *, void *, uint32_t *out, uint32_t id)
{
    out[0] = P(kTextureNames[id % kTextureNameCount]);
    out[1] = id ^ 0x5a5a0000u;
    if (gRun) gRun->Add("texture record", {id});
    return out;
}
uint32_t __fastcall TexTrap(void *, void *)
{
    if (gRun) gRun->Add("texture manager: an unexpected slot", {});
    return 0;
}
void *gTexVt[32];
void *gTexObject[1] = {gTexVt};

// ---- the RZFastFixed0x58 pool (0x10f6704), fresh pages per side ---------------------------------------------------------
constexpr uint32_t kPageBytes = 0x2000, kBlockBytes = 0x58;
constexpr unsigned kPoolPages = 4;
struct BlockPool {
    uint8_t *region = nullptr;
    unsigned next = 0;
    uint32_t saved[3] = {};
    void *vt[4] = {};
    std::vector<uint32_t> freedPages;
} gBlocks;

uint32_t Norm(const void *p)
{
    const uint8_t *b = static_cast<const uint8_t *>(p);
    if (!p) return 0;
    if (b >= gBlocks.region && b < gBlocks.region + kPoolPages * kPageBytes) return 0x70000000u + P(b) - P(gBlocks.region);
    return 0xeeeeeeeeu;
}

RZFixedPage *InitPage()
{
    auto *page = reinterpret_cast<RZFixedPage *>(gBlocks.region + (gBlocks.next++ % kPoolPages) * kPageBytes);
    page->next = page;
    page->prev = page;
    page->used = 0;
    uint8_t *base = reinterpret_cast<uint8_t *>(page);
    const uint32_t blocks = (kPageBytes - 0x10) / kBlockBytes;
    for (uint32_t k = 0; k < blocks; ++k)
        *reinterpret_cast<void **>(base + 0x10 + k * kBlockBytes) =
            k + 1 < blocks ? base + 0x10 + (k + 1) * kBlockBytes : nullptr;
    page->freeList = base + 0x10;
    return page;
}
RZFixedPage *__fastcall FakeNewPage(void *, void *) { return InitPage(); }
void __fastcall FakeFreePage(void *, void *, RZFixedPage *page)
{
    difftest::NotCounted quiet;
    gBlocks.freedPages.push_back(P(page) - P(gBlocks.region));
}
void *__fastcall BlockAllocTrap(void *, void *)
{
    if (gRun) gRun->Add("block pool: slot 0 called", {});
    return nullptr;
}
void __fastcall BlockFreeTrap(void *, void *, void *)
{
    if (gRun) gRun->Add("block pool: slot 1 called", {});
}

// Everything the test puts in the image's globals, put back when it ends.
struct Fakes {
    void **poolCell, **texCell;
    void *savedPool, *savedTex;
    explicit Fakes(difftest::Context &ctx)
        : poolCell(ctx.image.At<void *>(kStringPoolCell)), texCell(ctx.image.At<void *>(kTextureCell)),
          savedPool(*poolCell), savedTex(*texCell)
    {
        for (void *&s : gPoolVt) s = reinterpret_cast<void *>(&PoolTrap);
        gPoolVt[5] = reinterpret_cast<void *>(&PoolOpen);
        gPoolVt[6] = reinterpret_cast<void *>(&PoolUse);
        gPoolVt[7] = reinterpret_cast<void *>(&PoolClose);
        gPoolVt[8] = reinterpret_cast<void *>(&PoolText);
        gPoolVt[9] = reinterpret_cast<void *>(&PoolLength);
        for (void *&s : gTexVt) s = reinterpret_cast<void *>(&TexTrap);
        gTexVt[18] = reinterpret_cast<void *>(&TexAddRef);
        gTexVt[19] = reinterpret_cast<void *>(&TexRelease);
        gTexVt[24] = reinterpret_cast<void *>(&TexRecord);
        *poolCell = gPoolObject;
        *texCell = gTexObject;
        gBlocks.vt[0] = reinterpret_cast<void *>(&BlockAllocTrap);
        gBlocks.vt[1] = reinterpret_cast<void *>(&BlockFreeTrap);
        gBlocks.vt[2] = reinterpret_cast<void *>(&FakeNewPage);
        gBlocks.vt[3] = reinterpret_cast<void *>(&FakeFreePage);
        gBlocks.region = static_cast<uint8_t *>(
            VirtualAlloc(nullptr, kPoolPages * kPageBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        std::memcpy(gBlocks.saved, GameAddress<void>(cMaterialDefinition::kPool), sizeof(gBlocks.saved));
    }
    ~Fakes()
    {
        std::memcpy(GameAddress<void>(cMaterialDefinition::kPool), gBlocks.saved, sizeof(gBlocks.saved));
        VirtualFree(gBlocks.region, 0, MEM_RELEASE);
        *poolCell = savedPool;
        *texCell = savedTex;
    }
    Fakes(const Fakes &) = delete;
    // Fresh for a side.
    static void Reset()
    {
        gStrings.Reset();
        gTextures.Reset();
        gBlocks.next = 0;
        gBlocks.freedPages.clear();
        std::memset(gBlocks.region, 0xcd, kPoolPages * kPageBytes);
        uint32_t *words = GameAddress<uint32_t>(cMaterialDefinition::kPool);
        words[0] = P(gBlocks.vt);
        words[1] = P(InitPage());
        words[2] = 0;
    }
};

void NotePools(Run &run)
{
    const uint32_t *words = GameAddress<uint32_t>(cMaterialDefinition::kPool);
    std::vector<uint32_t> w{Norm(reinterpret_cast<void *>(static_cast<uintptr_t>(words[1]))), words[2], gBlocks.next};
    for (unsigned i = 0; i < gBlocks.next && i < kPoolPages; ++i) {
        const auto *page = reinterpret_cast<const RZFixedPage *>(gBlocks.region + i * kPageBytes);
        w.insert(w.end(), {Norm(page->next), Norm(page->prev), Norm(page->freeList), page->used});
    }
    w.insert(w.end(), gBlocks.freedPages.begin(), gBlocks.freedPages.end());
    run.Add("block pool", std::move(w));
    std::vector<uint32_t> counts;
    for (size_t i = 0; i < gStrings.counts.size(); ++i) counts.push_back(static_cast<uint32_t>(gStrings.counts[i]));
    run.Add("string counts", std::move(counts), "");
    run.Add("string pool errors", {gStrings.negative, gStrings.unknown});
    std::vector<uint32_t> tex;
    for (const auto &kv : gTextures.counts) tex.insert(tex.end(), {kv.first, static_cast<uint32_t>(kv.second)});
    run.Add("texture counts", std::move(tex));
    run.Add("texture errors", {gTextures.negative});
}

// ---- the non-material object: its QueryInterface fails ------------------------------------------------------------------
bool __fastcall AlienQueryInterface(void *, void *, uint32_t iid, void **out)
{
    if (gRun) gRun->Add("alien qi", {iid, out != nullptr});
    return false;
}
uint32_t __fastcall AlienCount(void *, void *) { return 1; }
void *gAlienVt[3] = {reinterpret_cast<void *>(&AlienQueryInterface), reinterpret_cast<void *>(&AlienCount),
                     reinterpret_cast<void *>(&AlienCount)};
void *gAlien[1] = {gAlienVt};

// ---- the inputs ----------------------------------------------------------------------------------------------------------
struct Gen {
    std::mt19937 rng;
    explicit Gen(uint32_t seed) : rng(seed * 2654435761u + 0x11u) {}
    uint32_t Below(uint32_t n) { return n ? rng() % n : 0; }
    bool Coin() { return rng() & 1; }
};

const char *const kNames[] = {"diffuse",       "Diffuse", "DIFFUSE", "alpha", "stdMatSpecCoef", "stdMatDiffCoef", "a",
                              "b",             "",        "zeta",    "Zeta",  "x_y",            "m#1",            "zz",
                              "a_name_well_past_the_fifteen_byte_buffer_of_a_string"};
constexpr uint32_t kNameCount = sizeof(kNames) / sizeof(kNames[0]);
const char *const kValues[] = {"1", "0", "0.5,0.5,0.5", "true", "", "Value", "value", "a much longer value text, 40 chars"};
constexpr uint32_t kValueCount = sizeof(kValues) / sizeof(kValues[0]);

// A name: one of a few (case variants of each other, empty, long), now and then null when `null` allows.
const char *Name(Gen &g, bool null)
{
    if (null && g.Below(12) == 0) return nullptr;
    return kNames[g.Below(kNameCount)];
}
const char *Value(Gen &g)
{
    if (g.Below(16) == 0) return nullptr;
    return kValues[g.Below(kValueCount)];
}
uint32_t TextureId(Gen &g)
{
    const uint32_t r = g.Below(20);
    if (r == 0) return 0xffffffffu;
    if (r == 1) return 0x80000000u + g.Below(kTextureNameCount);
    return r % 13;
}

const uint32_t kIids[] = {cIMaterialDefinition::kIid, cMaterialDefinition::kIidSelf, cISGResource::kIid,
                          cSGResource::kIidReferent, cSGResource::kIidReferent2, cSGResource::kIidPersistResource2,
                          1, 0, 0x49596978, 0x12345678};

// ---- the objects ---------------------------------------------------------------------------------------------------------
struct Obj {
    cIMaterialDefinition *face;
    uint8_t *base;
    uint32_t refs; // the test's references
};

void DumpVector(std::vector<uint32_t> &w, const uint8_t *base, uint32_t off)
{
    const auto *first = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(Rd<uint32_t>(base, off)));
    const uint32_t last = Rd<uint32_t>(base, off + 4), end = Rd<uint32_t>(base, off + 8);
    const uint32_t size = first ? (last - P(first)) / 8 : 0xdead, cap = first ? (end - P(first)) / 8 : 0;
    w.insert(w.end(), {first == nullptr, size, cap});
    if (first)
        for (uint32_t i = 0; i < size * 2; ++i) w.push_back(first[i]);
}

void Dump(Run &run, const Obj &o, uint32_t index)
{
    std::vector<uint32_t> w{index, Norm(o.base)};
    for (uint32_t off : {0x04u, 0x0cu, 0x10u, 0x14u, 0x18u, 0x20u, 0x24u, 0x30u, 0x34u, 0x50u}) w.push_back(Rd<uint32_t>(o.base, off));
    w.push_back(Rd<uint16_t>(o.base, 0x28));
    DumpVector(w, o.base, 0x38);
    DumpVector(w, o.base, 0x44);
    run.Add("state", std::move(w));
}

uint32_t Offset(const Obj &o, const void *p)
{
    const uint8_t *b = static_cast<const uint8_t *>(p);
    if (b >= o.base && b < o.base + cMaterialDefinition::kSize) return P(b) - P(o.base);
    return p ? 0xf0000000u : 0xffffffffu;
}

Obj Adopt(cIMaterialDefinition *face)
{
    face->AddRef();
    return {face, reinterpret_cast<uint8_t *>(face) - kFace, 1};
}

// A whole object released: the last release deletes it, its block at the head of the pool's free list.
void Drop(Run &run, Obj &o)
{
    uint32_t last = 0;
    for (; o.refs > 0; --o.refs) last = o.face->Release();
    const uint32_t *words = GameAddress<uint32_t>(cMaterialDefinition::kPool);
    const auto *page = reinterpret_cast<const RZFixedPage *>(static_cast<uintptr_t>(words[1]));
    run.Add("drop", {last, Norm(o.base), page ? Norm(page->freeList) : 0u});
}

Run RunSide(difftest::Context &ctx, bool game, uint32_t seed)
{
    Run run;
    gRun = &run;
    Fakes::Reset();
    std::vector<Obj> objs;
    {
        difftest::NotCounted quiet;
        objs.reserve(16);
    }
    difftest::AllocationCounter counter;
    auto make = [&]() -> cIMaterialDefinition * {
        return game ? ctx.Original<cIMaterialDefinition *(__cdecl *)()>(kFactory)() : cMaterialDefinition::Create();
    };
    Gen g(seed);
    objs.push_back(Adopt(make()));
    run.Add("made", {Norm(objs[0].base), Rd<uint32_t>(objs[0].base, 4)});
    if (seed % 5 == 0) {
        // The count check of IsEqual / IsEqualProperties: `a` cleared (its old slots, past the end, hold reset cells
        // 0xffffffff), `b` holding only a property with a null name and value (cells 0xffffffff too), the same name and
        // type (none). Only the count tells them apart.
        objs.push_back(Adopt(make()));
        objs.push_back(Adopt(make()));
        Obj &a = objs[objs.size() - 2];
        Obj &b = objs[objs.size() - 1];
        a.face->SetProperty("x", "y");
        a.face->SetProperty("z", "w");
        a.face->ClearProperties();
        b.face->SetProperty(nullptr, nullptr);
        run.Add("count check",
                {a.face->IsEqualProperties(b.face), b.face->IsEqualProperties(a.face), a.face->IsEqual(b.face),
                 b.face->IsEqual(a.face), b.face->CountProperties()});
        // IsEqual's texture checks: `c` and `d` alike but for one texture's name (both added by name, so both handles
        // 0xffffffff); `c` against `a` (no textures at all) differs only in the texture count.
        objs.push_back(Adopt(make()));
        objs.push_back(Adopt(make()));
        Obj &c = objs[objs.size() - 2];
        Obj &d = objs[objs.size() - 1];
        Obj &a2 = objs[objs.size() - 4];
        c.face->AddTextureByName("t1");
        d.face->AddTextureByName("t2");
        run.Add("texture checks",
                {c.face->IsEqual(d.face), d.face->IsEqual(c.face), c.face->IsEqualProperties(d.face),
                 c.face->IsEqual(a2.face), a2.face->IsEqual(c.face)});
    }
    const int ops = 60 + static_cast<int>(g.Below(seed % 7 == 0 ? 500 : 200));
    for (int i = 0; i < ops; ++i) {
        char mark[64];
        std::snprintf(mark, sizeof(mark), "%s side, seed %u, step %d", game ? "game" : "our", seed, i);
        ctx.Mark(mark);
        const uint32_t which = g.Below(static_cast<uint32_t>(objs.size()));
        Obj &o = objs[which];
        cIMaterialDefinition *const f = o.face;
        const uint32_t op = g.Below(100);
        if (op < 4) {
            const char *name = Name(g, true);
            f->SetMaterialName(name);
            run.Add("set name", {which}, Text(name));
        } else if (op < 8) {
            const char *type = Name(g, true);
            f->SetMaterialType(type);
            run.Add("set type", {which}, Text(type));
        } else if (op < 36) {
            const char *name = Name(g, true), *value = Value(g);
            f->SetProperty(name, value);
            run.Add("set property", {which}, Text(name) + " " + Text(value));
        } else if (op < 38) {
            f->ClearProperties();
            run.Add("clear", {which});
        } else if (op < 48) {
            const uint32_t id = TextureId(g);
            f->AddTextureById(id);
            run.Add("add texture id", {which, id});
        } else if (op < 54) {
            const char *name = g.Below(3) == 0 ? kTextureNames[g.Below(kTextureNameCount)] : Name(g, true);
            f->AddTextureByName(name);
            run.Add("add texture name", {which}, Text(name));
        } else if (op < 58) {
            if (objs.size() < 6) {
                cIMaterialDefinition *c = f->Clone();
                objs.push_back(Adopt(c));
                run.Add("clone", {which, Norm(objs.back().base), Rd<uint32_t>(objs.back().base, 4)});
                Dump(run, objs.back(), static_cast<uint32_t>(objs.size() - 1));
            }
        } else if (op < 64) {
            const uint32_t pick = g.Below(static_cast<uint32_t>(objs.size()) + 1);
            cIMaterialDefinition *other =
                pick == objs.size() ? reinterpret_cast<cIMaterialDefinition *>(gAlien) : objs[pick].face;
            const bool full = g.Coin();
            const bool r = full ? f->IsEqual(other) : f->IsEqualProperties(other);
            run.Add(full ? "is equal" : "is equal properties", {which, pick, r});
        } else if (op < 68) {
            const char *n = f->GetMaterialName();
            const char *t = f->GetMaterialType();
            run.Add("name and type", {which, n != nullptr, t != nullptr}, Text(n) + " " + Text(t));
        } else if (op < 72) {
            const uint32_t count = f->CountProperties();
            run.Add("count properties", {which, count});
            for (uint32_t k = 0; k < count; ++k) {
                MaterialPropertyText out{reinterpret_cast<const char *>(1), reinterpret_cast<const char *>(2)};
                MaterialPropertyText *r = f->GetProperty(&out, k);
                run.Add("property", {k, r == &out}, Text(out.mName) + " " + Text(out.mValue));
            }
        } else if (op < 78) {
            const char *name = Name(g, false);
            const bool noCase = g.Coin();
            const char *v = noCase ? f->FindPropertyNoCase(name) : f->FindProperty(name);
            run.Add(noCase ? "find no case" : "find", {which}, Text(name) + " -> " + Text(v));
        } else if (op < 82) {
            const uint32_t count = f->CountTextures();
            run.Add("count textures", {which, count});
            for (uint32_t k = 0; k < count; ++k) run.Add("texture", {k}, Text(f->GetTextureName(k)));
        } else if (op < 86) {
            const uint32_t iid = g.Below(8) == 0 ? static_cast<uint32_t>(g.rng()) : kIids[g.Below(sizeof(kIids) / 4)];
            const bool nullOut = g.Below(10) == 0;
            void *out = reinterpret_cast<void *>(0x5a5a5a5a);
            const uint32_t before = Rd<uint32_t>(o.base, 4);
            const bool r = f->QueryInterface(iid, nullOut ? nullptr : &out);
            const uint32_t after = Rd<uint32_t>(o.base, 4);
            run.Add("qi", {which, iid, r, nullOut ? 0u : Offset(o, out), before, after});
            if (r && !nullOut)
                for (uint32_t k = before; k < after; ++k) static_cast<cIGZUnknown *>(out)->Release();
        } else if (op < 89) {
            const uint32_t a = f->AddRef();
            const uint32_t r = f->Release();
            run.Add("addref release", {which, a, r});
        } else if (op < 92) {
            cISGResource *res = f->GetResource();
            cISGResource *cres = f->GetResourceConst();
            const char *r18 = static_cast<const char *>(res->GetRecord18());
            const char *r19 = static_cast<const char *>(res->GetRecord19());
            run.Add("resource", {which, Offset(o, res), Offset(o, cres), Rd<uint32_t>(o.base, 4)},
                    Text(r18) + " " + Text(r19) + " " + Text(res->GetName()));
        } else if (op < 94) {
            cPersistTypeInfo info = f->GetTypeInfo();
            const std::string name = info.mName ? reinterpret_cast<const cRZString *>(info.mName)->ToChar() : "<null>";
            run.Add("type info", {info.mWord4, info.mVersion}, name);
            if (info.mName) info.mName->Release();
            run.Add("owner info", {which, f->GetOwnerInfo()});
        } else if (op < 97) {
            Dump(run, o, which);
        } else if (objs.size() > 1) {
            Drop(run, o);
            objs.erase(objs.begin() + which);
        }
    }
    for (uint32_t k = 0; k < objs.size(); ++k) Dump(run, objs[k], k);
    NotePools(run);
    for (Obj &o : objs) Drop(run, o);
    NotePools(run);
    uint32_t strings = 0, textures = 0;
    for (int c : gStrings.counts) strings += c != 0;
    for (const auto &kv : gTextures.counts) textures += kv.first != 0xffffffffu && kv.second != 0;
    run.Add("counted at the end", {strings, textures});
    counter.Stop();
    run.Add("heap blocks left", {static_cast<uint32_t>(counter.Blocks())});
    gRun = nullptr;
    return run;
}

bool Compare(difftest::Context &ctx, uint32_t seed, const Run &a, const Run &b)
{
    ++ctx.cases;
    for (size_t i = 0; i < a.notes.size() && i < b.notes.size(); ++i) {
        if (!(a.notes[i] == b.notes[i])) {
            ctx.Fail("seed %u note %zu: original %s, ours %s", seed, i, Show(a.notes[i]).c_str(), Show(b.notes[i]).c_str());
            return false;
        }
    }
    if (a.notes.size() != b.notes.size()) {
        ctx.Fail("seed %u: %zu notes from the original, %zu from ours", seed, a.notes.size(), b.notes.size());
        return false;
    }
    return true;
}

} // namespace

DIFF_TEST(SG_MaterialDefinition_Life, kFactory)
{
    Fakes fakes(ctx);
    unsigned failed = 0;
    for (uint32_t seed = 1; seed <= 200 && failed < 3; ++seed) {
        const Run a = RunSide(ctx, true, seed);
        const Run b = RunSide(ctx, false, seed);
        if (!Compare(ctx, seed, a, b)) ++failed;
        // Everything given back: no handle or texture still counted, no heap block left.
        ++ctx.cases;
        const Note &leak = b.notes.back();
        if (leak.words.empty() || leak.words[0] != 0) ctx.Fail("seed %u: ours left %d heap blocks", seed, leak.words.empty() ? -1 : int(leak.words[0]));
        const Note &left = b.notes[b.notes.size() - 2];
        if (left.words.size() != 2 || left.words[0] != 0 || left.words[1] != 0)
            ctx.Fail("seed %u: ours left string handles or textures counted", seed);
    }
}

// ---- 0xae88d0 -----------------------------------------------------------------------------------------------------------
namespace {

std::string gCellLog;
uint32_t __fastcall CellOpen(void *, void *, uint32_t a, uint32_t b)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "open %x %x\n", a, b);
    gCellLog += buf;
    return a * 3 + 0x100;
}
uint32_t __fastcall CellUse(void *, void *, uint32_t v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "use %x\n", v);
    gCellLog += buf;
    return 0;
}
void *gCellVt[9];
void *gCellObject[1] = {gCellVt};

} // namespace

DIFF_TEST(SG_OpenNameCellCaseSensitive, 0xae88d0)
{
    auto original = ctx.Original<ServiceCell *(__cdecl *)(ServiceCell *, uint32_t)>(0xae88d0);
    void **cache = ctx.image.At<void *>(kStringPoolCell);
    void *saved = *cache;
    std::memset(gCellVt, 0, sizeof(gCellVt));
    gCellVt[5] = reinterpret_cast<void *>(&CellOpen);
    gCellVt[6] = reinterpret_cast<void *>(&CellUse);
    *cache = gCellObject;
    std::mt19937 rng(0xae88d0);
    for (int i = 0; i < 400; ++i) {
        const uint32_t name = i % 4 == 0 ? 0 : i % 4 == 1 ? 1 + rng() % 5 : rng();
        const uint32_t junk = i % 7 == 0 ? 0xffffffffu : rng();
        ServiceCell a{junk}, b{junk};
        gCellLog.clear();
        ServiceCell *ra = original(&a, name);
        const std::string logA = gCellLog;
        gCellLog.clear();
        ServiceCell *rb = OpenNameCellCaseSensitive(&b, name);
        const std::string logB = gCellLog;
        ++ctx.cases;
        if ((ra == &a) != (rb == &b) || a.mValue != b.mValue || logA != logB)
            ctx.Fail("name %x: original cell %x returns self %d log '%s', ours cell %x self %d log '%s'", name, a.mValue,
                     ra == &a, logA.c_str(), b.mValue, rb == &b, logB.c_str());
    }
    *cache = saved;
}

// ---- 0xaaef20 at its RVA (the game's Write calls it directly) -----------------------------------------------------------
// The adapter against the original, called as Write calls it: thiscall with `this` the face (any: neither reads it) and
// the hidden out (prefilled), the out returned; the name object's text and count, the record's words, the heap.
DIFF_TEST(SG_MaterialDefinition_GetTypeInfoAt, 0xaaef20)
{
    using Fn = cPersistTypeInfo *(__fastcall *)(void *, void *, cPersistTypeInfo *);
    const Fn original = ctx.Original<Fn>(0xaaef20);
    const Fn ours = &cMaterialDefinition::GetTypeInfoAt;
    for (int i = 0; i < 4; ++i) {
        void *face = reinterpret_cast<void *>(static_cast<uintptr_t>(0x1000 + i * 0x44));
        Run runs[2];
        for (int side = 0; side < 2; ++side) {
            cPersistTypeInfo info;
            std::memset(&info, 0xa5 + i, sizeof(info));
            difftest::AllocationCounter counter;
            cPersistTypeInfo *r = (side ? ours : original)(face, nullptr, &info);
            const long long made = counter.Blocks();
            const std::string text = info.mName ? reinterpret_cast<const cRZString *>(info.mName)->ToChar() : "<null>";
            const uint32_t count = info.mName ? info.mName->AddRef() : 0;
            if (info.mName) {
                info.mName->Release();
                info.mName->Release();
            }
            counter.Stop();
            runs[side].Add("type info", {r == &info, info.mWord4, info.mVersion, count, static_cast<uint32_t>(made),
                                         static_cast<uint32_t>(counter.Blocks())},
                           text);
        }
        ++ctx.cases;
        if (!(runs[0].notes[0] == runs[1].notes[0]))
            ctx.Fail("call %d: original %s, ours %s", i, Show(runs[0].notes[0]).c_str(), Show(runs[1].notes[0]).c_str());
    }
}

// ---- Read 0xaae280, Write 0xaaefc0 and slot 14 0xaad850 on both sides' objects -------------------------------------------
// The game's three on an object the game's factory made, against ours on an object our factory made: the slots are called
// through each object's own vtable, so the game side runs only the game's code and ours only ours (apart from the shared
// helpers the notes list: the texture manager, 0xae7340). Each seed makes a definition by each side's factory, fills it a
// little through the slots (so Read shrinks, grows within capacity and grows past it), then Reads a generated stream,
// Writes it, merges it (slot 14), Reads a second stream and Writes again; after each step the object is dumped raw and
// through the getters, with the heap blocks counted so far (the stream itself is built uncounted). Property lists run to
// 24 and texture lists to 13, past the fill's sizes, so the getters see lists only Read grew. Every value the code
// branches on is generated before the mutants are run (versions 0-12, the groups, the brackets, the runs).
//
// The stream (what Read takes, in order): the material's type-info record (version 0-12, its name null or a counted fake),
// cSGResource's record and resource name, the name, before version 8 a string Read drops, the type, the property count and
// each property's name and value (duplicates, unsorted, "textureName" with the reader's group id 0 / 0x1c0532fa / another
// one, X / Y / Z runs for slot 14, values in brackets with spaces and commas for versions below 11, '!' and '##' texts), then
// from version 9 the texture count and names. The reader's fake (29 slots) writes the whole record in ReadTypeInfo; its
// GetGZStr fills the caller's string through FromChar (slot 3). The writer's fake notes each word, text and record. The
// framework (0x10c9378) hands cSGResource's Read a resource manager whose FindSegmentWithKey (34) writes null.
//
// Slot 14 is called only with two or more properties: below two, its loop bound (count - 2, unsigned) wraps and it walks
// four billion empty rounds (the game's, kept in ours as an ORIGINAL-BUG).
namespace {

// The reader.
struct RwStream {
    std::vector<uint32_t> versions;    // a record each
    std::vector<bool> namedRecords;    // the record's name a counted fake
    std::vector<std::string> texts;
    std::vector<uint32_t> words;
    size_t versionAt = 0, textAt = 0, wordAt = 0;
    uint32_t group = 0;               // slot 28's answer
    uint32_t group28Calls = 0;
} gStream;

struct CountedFake {
    void **vt;
    int32_t refs;
};
uint32_t __fastcall CfQi(CountedFake *, void *, uint32_t, void **out)
{
    if (out) *out = nullptr;
    if (gRun) gRun->Add("record name: qi", {});
    return 0;
}
uint32_t __fastcall CfAddRef(CountedFake *self, void *) { return static_cast<uint32_t>(++self->refs); }
uint32_t __fastcall CfRelease(CountedFake *self, void *) { return static_cast<uint32_t>(--self->refs); }
void *gCfVt[3] = {reinterpret_cast<void *>(&CfQi), reinterpret_cast<void *>(&CfAddRef), reinterpret_cast<void *>(&CfRelease)};
CountedFake gRecordNames[4];

uint32_t __fastcall RdTypeInfo(void *, void *, uint32_t *record)
{
    const size_t i = gStream.versionAt++;
    CountedFake *name = nullptr;
    if (i < gStream.versions.size() && gStream.namedRecords[i] && i < 4) {
        name = &gRecordNames[i];
        ++name->refs;
    }
    record[0] = P(name);
    record[1] = 0;
    record[2] = i < gStream.versions.size() ? gStream.versions[i] : 0;
    if (gRun) gRun->Add("read record", {static_cast<uint32_t>(i)});
    return P(record);
}
uint32_t __fastcall RdUint32(void *, void *)
{
    const size_t i = gStream.wordAt++;
    if (gRun) gRun->Add("read word", {static_cast<uint32_t>(i)});
    return i < gStream.words.size() ? gStream.words[i] : 0;
}
uint32_t __fastcall RdString(void *, void *, void *s)
{
    const size_t i = gStream.textAt++;
    const char *text = i < gStream.texts.size() ? gStream.texts[i].c_str() : "";
    if (gRun) gRun->Add("read text", {static_cast<uint32_t>(i)});
    using FromCharFn = void(__thiscall *)(void *, const char *, uint32_t);
    reinterpret_cast<FromCharFn>((*static_cast<void ***>(s))[3])(s, text, static_cast<uint32_t>(std::strlen(text)));
    return 1;
}
uint32_t __fastcall RdGroup(void *, void *)
{
    ++gStream.group28Calls;
    return gStream.group;
}
template <unsigned N> uint32_t __fastcall RwTrap(void *, void *)
{
    if (gRun) gRun->Add("persist fake: an unexpected slot", {N});
    return 0;
}
template <unsigned... N> void FillTraps(void **vt, std::integer_sequence<unsigned, N...>)
{
    ((vt[N] = reinterpret_cast<void *>(&RwTrap<N>)), ...);
}
void *gReaderVt[29];
void *gReaderObject[1] = {gReaderVt};

// The writer: each call noted.
std::string GzText(void *s)
{
    if (!s) return "<null>";
    using DataFn = const char *(__thiscall *)(void *);
    using LenFn = uint32_t(__thiscall *)(void *);
    void **vt = *static_cast<void ***>(s);
    const char *data = reinterpret_cast<DataFn>(vt[6])(s);
    return data ? std::string(data, reinterpret_cast<LenFn>(vt[7])(s)) : std::string("<no data>");
}
uint32_t __fastcall WrUint32(void *, void *, uint32_t v)
{
    if (gRun) gRun->Add("write word", {v});
    return 1;
}
uint32_t __fastcall WrText(void *, void *, const char *text)
{
    if (gRun) gRun->Add("write text", {text != nullptr}, Text(text));
    return 1;
}
uint32_t __fastcall WrRecord(void *, void *, const uint32_t *record)
{
    if (gRun) gRun->Add("write record", {record[1], record[2], record[0] != 0}, GzText(reinterpret_cast<void *>(record[0])));
    return 1;
}
void *gWriterVt[29];
void *gWriterObject[1] = {gWriterVt};

// The framework and its resource manager.
uint32_t __fastcall MgrQi(void *, void *, uint32_t, void **out)
{
    if (out) *out = nullptr;
    return 0;
}
int32_t gManagerRefs = 0;
uint32_t __fastcall MgrAddRef(void *, void *) { return static_cast<uint32_t>(++gManagerRefs); }
uint32_t __fastcall MgrRelease(void *, void *) { return static_cast<uint32_t>(--gManagerRefs); }
uint32_t __fastcall MgrFindSegment(void *, void *, const uint32_t *key, void **out)
{
    if (gRun) gRun->Add("find segment", {key[0], key[1], key[2]});
    *out = nullptr;
    return 0;
}
void *gManagerVt[46];
void *gManagerObject[1] = {gManagerVt};
uint32_t __fastcall FwGetService(void *, void *, uint32_t service, uint32_t iid, void **out)
{
    if (gRun) gRun->Add("get service", {service, iid});
    *out = gManagerObject;
    ++gManagerRefs;
    return 1;
}
void *gFrameworkVt[11];
void *gFrameworkObject[1] = {gFrameworkVt};

struct RwFakes {
    void **framework;
    void *saved;
    explicit RwFakes(difftest::Context &ctx) : framework(ctx.image.At<void *>(0x10c9378)), saved(*framework)
    {
        FillTraps(gReaderVt, std::make_integer_sequence<unsigned, 29>());
        gReaderVt[10] = reinterpret_cast<void *>(&RdUint32);
        gReaderVt[22] = reinterpret_cast<void *>(&RdString);
        gReaderVt[26] = reinterpret_cast<void *>(&RdTypeInfo);
        gReaderVt[28] = reinterpret_cast<void *>(&RdGroup);
        FillTraps(gWriterVt, std::make_integer_sequence<unsigned, 29>());
        gWriterVt[10] = reinterpret_cast<void *>(&WrUint32);
        gWriterVt[23] = reinterpret_cast<void *>(&WrText);
        gWriterVt[28] = reinterpret_cast<void *>(&WrRecord);
        FillTraps(gManagerVt, std::make_integer_sequence<unsigned, 46>());
        gManagerVt[0] = reinterpret_cast<void *>(&MgrQi);
        gManagerVt[1] = reinterpret_cast<void *>(&MgrAddRef);
        gManagerVt[2] = reinterpret_cast<void *>(&MgrRelease);
        gManagerVt[34] = reinterpret_cast<void *>(&MgrFindSegment);
        FillTraps(gFrameworkVt, std::make_integer_sequence<unsigned, 11>());
        gFrameworkVt[5] = reinterpret_cast<void *>(&FwGetService);
        *framework = gFrameworkObject;
    }
    ~RwFakes() { *framework = saved; }
    RwFakes(const RwFakes &) = delete;
};

// The generated inputs.
const char *const kRwNames[] = {"diffuse", "Diffuse", "alpha", "a", "b", "", "textureName", "TextureName", "colorX",
                                "colorY", "colorZ", "X", "Y", "zz", "stdMatSpecCoef",
                                "a_name_well_past_the_fifteen_byte_buffer_of_a_string"};
const char *const kRwValues[] = {"1", "0.5", "", "(1 2 3)", "[1,2,3]", "( 1  2 3 )", "(1,2)", "[a b]", "(12)", "(1 2)",
                                 "()", "(abcd", "abcd)", "[  ]", "( 1 , 2 )", "(1.000000 0.500000 0.250000 1.000000)",
                                 "tex!name", "a!b!c", "!x", "##raw", "#0x00000001!t", "wall_a", "Value"};
const char *const kRwRuns[][3] = {{"colorX", "colorY", "colorZ"}, {"dirX", "dirY", "dirZ"}, {"pX", "pY", "pW"},
                                  {"qX", "qY", nullptr}, {"X", "Y", "Z"}, {"aX", "bY", "cZ"},
                                  {"abAX", "abY", nullptr}, {"abY", "abAX", nullptr}, {"abX", "abcY", nullptr}, {"abcY", "abX", nullptr},
                                  {"aX", "aY", "aZ"}, {"aZ", "aY", "aX"}, {"ab", "abAX", "abY"}, {"abX", "ab", "abcY"},
                                  {"abAX", "ab", "abY"}, {"aX", "a", "aY"}};

// One stream: the records, the texts and the words, and the property values (for the notes).
void MakeStream(Gen &g, uint32_t version)
{
    difftest::NotCounted quiet; // the stream is the test's, never the code under test's
    gStream = {};
    gStream.versions = {version, g.Below(4)};
    gStream.namedRecords = {g.Below(3) != 0, g.Coin()};
    const uint32_t groups[] = {0, 0x1c0532fau, 0x12345678u};
    gStream.group = groups[g.Below(3)];
    auto &t = gStream.texts;
    t.push_back(g.Below(4) ? "resource_name" : "");
    t.push_back(g.Below(6) ? kRwNames[g.Below(16)] : "");
    if (version < 8) t.push_back("dropped");
    t.push_back(g.Below(5) ? kNames[g.Below(kNameCount)] : "");
    const uint32_t r = g.Below(10);
    const uint32_t count = r == 0 ? 0 : r == 1 ? 1 : r == 2 ? 2 : r == 3 ? 10 + g.Below(15) : r == 4 ? 33 + g.Below(30) : g.Below(10);
    std::vector<std::string> names;
    while (names.size() < count) {
        if (g.Below(4) == 0) {
            const auto &run = kRwRuns[g.Below(16)];
            for (const char *n : run)
                if (n && names.size() < count) names.push_back(n);
        } else {
            names.push_back(kRwNames[g.Below(16)]);
        }
    }
    gStream.words.push_back(count);
    for (const std::string &n : names) {
        t.push_back(n);
        t.push_back(g.Below(12) == 0 ? std::string(kRwValues[g.Below(23)]) + " " + kRwValues[g.Below(23)]
                                     : std::string(kRwValues[g.Below(23)]));
    }
    if (version > 8) {
        const uint32_t textures = g.Below(4) == 0 ? 0 : g.Below(14);
        gStream.words.push_back(textures);
        for (uint32_t k = 0; k < textures; ++k) t.push_back(kTextureNames[g.Below(kTextureNameCount)]);
    }
}

uint32_t Version(Gen &g)
{
    const uint32_t picks[] = {0, 7, 8, 9, 10, 11, 12, 0xb};
    return g.Below(4) == 0 ? g.Below(13) : picks[g.Below(8)];
}

// The object through its getters.
void NoteThrough(Run &run, cIMaterialDefinition *f)
{
    run.Add("name and type", {}, Text(f->GetMaterialName()) + " " + Text(f->GetMaterialType()));
    const uint32_t count = f->CountProperties();
    run.Add("count properties", {count});
    for (uint32_t k = 0; k < count; ++k) {
        MaterialPropertyText out{};
        f->GetProperty(&out, k);
        run.Add("property", {k}, Text(out.mName) + " " + Text(out.mValue));
    }
    const uint32_t textures = f->CountTextures();
    run.Add("count textures", {textures});
    for (uint32_t k = 0; k < textures; ++k) run.Add("texture", {k}, Text(f->GetTextureName(k)));
    run.Add("resource name", {}, Text(f->GetResource()->GetName()));
}

void ReadStream(Run &run, Obj &o)
{
    for (CountedFake &n : gRecordNames) n = {gCfVt, 0};
    o.face->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(gReaderObject));
    run.Add("read taken", {static_cast<uint32_t>(gStream.versionAt), static_cast<uint32_t>(gStream.textAt),
                           static_cast<uint32_t>(gStream.wordAt), gStream.group28Calls,
                           static_cast<uint32_t>(gRecordNames[0].refs), static_cast<uint32_t>(gRecordNames[1].refs)});
}

Run RunReadWrite(difftest::Context &ctx, bool game, uint32_t seed)
{
    Run run;
    gRun = &run;
    Fakes::Reset();
    gManagerRefs = 0;
    difftest::AllocationCounter counter;
    Gen g(seed ^ 0x5eed0000u);
    cIMaterialDefinition *made =
        game ? ctx.Original<cIMaterialDefinition *(__cdecl *)()>(kFactory)() : cMaterialDefinition::Create();
    Obj o = Adopt(made);
    char mark[64];
    std::snprintf(mark, sizeof(mark), "%s side, seed %u, fill", game ? "game" : "our", seed);
    ctx.Mark(mark);
    run.Add("blocks", {static_cast<uint32_t>(counter.Blocks())});
    // Some state first, so Read shrinks or grows what is there.
    const uint32_t props = g.Below(4) == 0 ? 0 : g.Below(8);
    for (uint32_t k = 0; k < props; ++k) o.face->SetProperty(kNames[g.Below(kNameCount)], kValues[g.Below(kValueCount)]);
    const uint32_t textures = g.Below(3) == 0 ? 0 : g.Below(6);
    for (uint32_t k = 0; k < textures; ++k) {
        if (g.Coin()) o.face->AddTextureById(g.Below(13));
        else o.face->AddTextureByName(kTextureNames[g.Below(kTextureNameCount)]);
    }
    if (g.Coin()) o.face->SetMaterialName("old_name");
    Dump(run, o, 0);
    for (int round = 0; round < 2; ++round) {
        MakeStream(g, Version(g));
        std::snprintf(mark, sizeof(mark), "%s side, seed %u, read %d", game ? "game" : "our", seed, round);
        ctx.Mark(mark);
        run.Add("blocks", {static_cast<uint32_t>(counter.Blocks())});
        ReadStream(run, o);
        Dump(run, o, 1);
        NoteThrough(run, o.face);
        std::snprintf(mark, sizeof(mark), "%s side, seed %u, write %d", game ? "game" : "our", seed, round);
        ctx.Mark(mark);
        run.Add("blocks", {static_cast<uint32_t>(counter.Blocks())});
        o.face->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(gWriterObject));
        if (o.face->CountProperties() >= 2) {
            std::snprintf(mark, sizeof(mark), "%s side, seed %u, slot 14", game ? "game" : "our", seed);
            ctx.Mark(mark);
            run.Add("blocks", {static_cast<uint32_t>(counter.Blocks())});
            run.Add("merge", {o.face->MergeVectorProperties()});
            Dump(run, o, 2);
            NoteThrough(run, o.face);
            o.face->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(gWriterObject));
        }
    }
    run.Add("blocks", {static_cast<uint32_t>(counter.Blocks())});
    Drop(run, o);
    run.Add("blocks", {static_cast<uint32_t>(counter.Blocks())});
    NotePools(run);
    run.Add("manager refs", {static_cast<uint32_t>(gManagerRefs)});
    counter.Stop();
    run.Add("heap blocks left", {static_cast<uint32_t>(counter.Blocks())});
    gRun = nullptr;
    return run;
}

} // namespace

DIFF_TEST(SG_MaterialDefinition_ReadWrite, 0xaae280)
{
    Fakes fakes(ctx);
    RwFakes rw(ctx);
    unsigned failed = 0;
    for (uint32_t seed = 1; seed <= 300 && failed < 3; ++seed) {
        const Run a = RunReadWrite(ctx, true, seed);
        const Run b = RunReadWrite(ctx, false, seed);
        if (!Compare(ctx, seed, a, b)) ++failed;
    }
}
