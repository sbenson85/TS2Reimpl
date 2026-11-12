// Differential tests for the object-imposter definitions (lane 261111u, scenegraph row LT2a2a;
// src/sg/cLotImposterManager.h): cImposterDefinition's constructor 0x3f72b0, destructor 0x3f73b0, operator= 0x3f7430,
// AddEntry 0x3f7500 and Find 0x3f8300; cImposterDefinitions' Define 0x3f7890 (two arguments) / 0x3f76e0 (six) and Map
// 0x3f83d0. Pure data code: the game's new / delete (0x824632 / 0x824624) are patched to the test's heap so both sides'
// blocks are counted alike; nothing else is stood in for (Map's callees, cDesignModes' Clear / SetName / AddDesignMode,
// are the original's on its side and ours on ours, each verified by lane 261108j; the wildcard match RZWildcard::MatchCase
// is the image's on its side and ours on ours).
//
// Texts come from a pool with null (where a callee takes it), the empty text, case variants, and the 15 / 16 character
// boundary of the string's inline buffer. Compared after every call: the result, every string's vtable word, refcount,
// size, capacity and text, every map's size and contents in order, the map heads' links (an emptied head links to
// itself), and the heap: blocks left and blocks allocated. Refcounts are seeded with non-zero words so a string rebuilt
// instead of assigned shows.
//
// LotImposterDefinition_Life: the constructor into junk memory (each model text, null), the head built, entries added by
// the test, then the destructor (the words left after it, the head's address kept).
// LotImposterDefinition_Assign: a = b and a = a with filled or empty entries on either side.
// LotImposterDefinition_AddEntry: one to five calls (one in four with one argument null) on a filled definition: insert
// versus overwrite, the overwritten value's refcounts.
// LotImposterDefinition_Find: a key present or not; `out` filled beforehand, or `out` the found entry's own value
// (the self-assignment), or the key itself.
// LotImposterDefinitions_Define: a run of one to eight two- and six-argument Defines on an empty or filled map (the
// second form on an existing name adds the entry there; the first does nothing).
// LotImposterDefinitions_Map: zero to five wildcard keys ("*", "chair*", "?hair*", ...) in key order, a loaded set's tag
// and modes, `out` filled beforehand or the loaded set itself.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "hook/hooks.h"
#include "sg/cLotImposterDesignModes.h"
#include "sg/cLotImposterManager.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace lotimpdef_test {

using svcfx::Blob;
using svcfx::P;
using nTSSG::cDesignModes;
using nTSSG::cImposterDefinition;
using nTSSG::cImposterDefinitions;
using nTSSG::cImposterString;
using nTSSG::cImposterStringPair;

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

uint32_t W(const void *o, uint32_t off)
{
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(o) + off, 4);
    return v;
}
void PutW(void *o, uint32_t off, uint32_t v) { std::memcpy(static_cast<uint8_t *>(o) + off, &v, 4); }

uint32_t Hash(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (char c : s) h = (h ^ uint8_t(c)) * 16777619u;
    return h;
}

// Index 0 is null; 1 the empty text.
const char *const kTexts[] = {nullptr,          "",
                              "a",              "B",
                              "chair",          "Chair",
                              "chair_01",       "fifteen_chars_x",
                              "sixteen_chars_xx", "a_much_longer_text_over_sixteen"};
constexpr unsigned kTextCount = 10;

// A string's words: the vtable (the image's either way), size, capacity, refcount and text.
void LogString(Blob &b, const void *s)
{
    const auto &r = *static_cast<const cRZString *>(s);
    b.push_back(svcfx::Norm(P(r.mVTable)));
    b.push_back(W(s, 0x14)), b.push_back(W(s, 0x18)), b.push_back(r.mRefCount);
    b.push_back(Hash(r.mString));
}
// A map head: its three links pointing at itself when the map is empty, colour and nil bytes.
void LogHead(Blob &b, const void *container)
{
    const auto *head = reinterpret_cast<const uint8_t *>(uintptr_t(W(container, 0)));
    b.push_back(W(container, 4));
    const uint32_t self = P(head);
    b.push_back((W(head, 0) == self) | (W(head, 4) == self) << 1 | (W(head, 8) == self) << 2);
    b.push_back(head[12]), b.push_back(head[13]);
}
void LogDefinition(Blob &b, const cImposterDefinition &d)
{
    b.push_back(0xdef1);
    LogString(b, &d.name);
    LogHead(b, &d.entries);
    for (const auto &[k, v] : d.entries) {
        LogString(b, &k.first), LogString(b, &k.second);
        LogString(b, &v.first), LogString(b, &v.second);
    }
}
void LogDefinitions(Blob &b, const cImposterDefinitions &d)
{
    b.push_back(0xdef5), b.push_back(d.mId);
    LogHead(b, &d.mDefinitions);
    for (const auto &[key, def] : d.mDefinitions) LogString(b, &key), LogDefinition(b, def);
}
void LogModes(Blob &b, const cDesignModes &m)
{
    b.push_back(0x5e75);
    LogString(b, &m.mTag), LogString(b, &m.mName);
    LogHead(b, &m.mModes);
    for (const auto &[k, v] : m.mModes) LogString(b, &k), LogString(b, &v);
}

struct Rng {
    std::mt19937 gen;
    uint32_t operator()(uint32_t n) { return uint32_t(gen() % n); }
    // Null one time in six when allowed; else one of the first `span` texts after null.
    const char *Text(bool allowNull, unsigned span = kTextCount - 1)
    {
        const uint32_t pick = (*this)(6 * span);
        return allowNull && pick < span ? nullptr : kTexts[1 + pick % span];
    }
};

// Entries from the pool (no nulls; keys from its first `span` texts), each string's refcount a non-zero word.
void Fill(cImposterDefinition &d, Rng &r, unsigned count, unsigned span = kTextCount - 1)
{
    for (unsigned i = 0; i < count; ++i) {
        cImposterStringPair k(r.Text(false, span), r.Text(false, span)), v(r.Text(false), r.Text(false));
        auto &slot = d.entries[k];
        slot = v;
        slot.first.s.mRefCount = 0x100 + i, slot.second.s.mRefCount = 0x200 + i;
    }
    for (auto &[k, v] : d.entries) {
        const_cast<cImposterStringPair &>(k).first.s.mRefCount = 0x300;
        (void)v;
    }
}

// Both sides' heap: blocks left over and blocks allocated.
void LogHeap(Blob &b, const difftest::AllocationCounter &c)
{
    b.push_back(0x4ea9), b.push_back(uint32_t(c.Blocks())), b.push_back(uint32_t(c.Allocations()));
}

#define GAME_HEAP                                                                                                      \
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));                                                       \
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete))

} // namespace lotimpdef_test

using namespace lotimpdef_test;

DIFF_TEST(LotImposterDefinition_Life, 0x3f72b0)
{
    GAME_HEAP;
    using CtorFn = void *(__thiscall *)(void *, const char *);
    using DtorFn = void(__thiscall *)(void *);
    const CtorFn ctorO = ctx.Original<CtorFn>(0x3f72b0),
                 ctorU = reinterpret_cast<CtorFn>(&cImposterDefinition::Construct);
    const DtorFn dtorO = ctx.Original<DtorFn>(0x3f73b0), dtorU = reinterpret_cast<DtorFn>(&cImposterDefinition::Destruct);
    int nulls = 0, filled = 0, mixed = 0;
    for (uint32_t seed = 0; seed < 1000; ++seed) {
        const uint32_t base = seed * 2654435761u + 0x3f72b0u;
        Blob outcome[2];
        // Ours: 0 / 1 both; 2 the destructor alone (after the original constructor); 3 the constructor alone.
        const unsigned way = seed % 4;
        for (int side = 0; side < 2; ++side) {
            ctx.Mark(side ? "ours" : "original");
            Rng r{std::mt19937(base)};
            const char *model = r.Text(true);
            const unsigned count = r(4);
            Blob &b = outcome[side];
            difftest::AllocationCounter counter;
            {
                alignas(4) uint8_t raw[0x28];
                std::memset(raw, 0xd7, sizeof(raw));
                const bool ourCtor = side && way != 2, ourDtor = side && way != 3;
                auto &d = *reinterpret_cast<cImposterDefinition *>(raw);
                void *back = (ourCtor ? ctorU : ctorO)(raw, model);
                uint32_t head;
                {
                    // The test's own entries (their nodes, made uncounted, are freed by the destructor on both sides).
                    difftest::NotCounted bookkeeping;
                    b.push_back(back == raw);
                    LogDefinition(b, d);
                    Fill(d, r, count);
                    LogDefinition(b, d);
                    head = W(raw, 0x20);
                }
                (ourDtor ? dtorU : dtorO)(raw);
                difftest::NotCounted bookkeeping;
                b.push_back(W(raw, 0x20) == head);
                b.push_back(svcfx::Norm(W(raw, 0)));
                for (uint32_t off = 4; off < 0x20; off += 4) b.push_back(off == 4 ? raw[4] : W(raw, off));
                b.push_back(W(raw, 0x24));
                if (side) nulls += model == nullptr, filled += count > 0, mixed += way >= 2;
            }
            LogHeap(b, counter);
        }
        svcfx::Same(ctx, outcome[0], outcome[1], "life", seed);
        if (ctx.failures.size() >= 6) return;
    }
    if (nulls < 50 || filled < 500 || mixed < 400) ctx.Fail("coverage: null %d, filled %d, mixed %d", nulls, filled, mixed);
}

DIFF_TEST(LotImposterDefinition_Assign, 0x3f7430)
{
    GAME_HEAP;
    using Fn = void *(__thiscall *)(void *, const void *);
    const Fn orig = ctx.Original<Fn>(0x3f7430), ours = reinterpret_cast<Fn>(MemberAddress(&cImposterDefinition::operator=));
    int self = 0, emptied = 0, sameName = 0;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        const uint32_t base = seed * 2246822519u + 0x3f7430u;
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ctx.Mark(side ? "ours" : "original");
            Rng r{std::mt19937(base)};
            Blob &b = outcome[side];
            difftest::AllocationCounter counter;
            {
                cImposterDefinition *a, *o;
                uint32_t head;
                const bool alias = r(5) == 0;
                {
                    difftest::NotCounted bookkeeping;
                    a = new cImposterDefinition(r.Text(false, 4));
                    o = new cImposterDefinition(r.Text(false, 4));
                    Fill(*a, r, r(4)), Fill(*o, r, r(4));
                    a->name.s.mRefCount = 0x51, o->name.s.mRefCount = 0x52;
                    head = W(a, 0x20);
                    if (side) {
                        self += alias, emptied += !alias && o->entries.empty() && !a->entries.empty();
                        sameName += !alias && a->name.s.mString == o->name.s.mString;
                    }
                }
                void *back = (side ? ours : orig)(a, alias ? a : o);
                difftest::NotCounted bookkeeping;
                b.push_back(back == a), b.push_back(W(a, 0x20) == head);
                LogDefinition(b, *a), LogDefinition(b, *o);
                delete a;
                delete o;
            }
            LogHeap(b, counter);
        }
        svcfx::Same(ctx, outcome[0], outcome[1], "assign", seed);
        if (ctx.failures.size() >= 6) return;
    }
    if (self < 200 || emptied < 100 || sameName < 100)
        ctx.Fail("coverage: self %d, emptied %d, same name %d", self, emptied, sameName);
}

DIFF_TEST(LotImposterDefinition_AddEntry, 0x3f7500)
{
    GAME_HEAP;
    using Fn = void(__thiscall *)(void *, const char *, const char *, const char *, const char *);
    const Fn orig = ctx.Original<Fn>(0x3f7500), ours = reinterpret_cast<Fn>(MemberAddress(&cImposterDefinition::AddEntry));
    int nulls = 0, inserted = 0, overwritten = 0;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        const uint32_t base = seed * 2654435761u + 0x3f7500u;
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ctx.Mark(side ? "ours" : "original");
            Rng r{std::mt19937(base)};
            Blob &b = outcome[side];
            difftest::AllocationCounter counter;
            {
                cImposterDefinition *d;
                {
                    difftest::NotCounted bookkeeping;
                    d = new cImposterDefinition("model");
                    Fill(*d, r, r(5), 3);
                }
                const unsigned calls = 1 + r(5);
                for (unsigned i = 0; i < calls; ++i) {
                    // One argument in four calls null (each position alike).
                    const char *args[4] = {r.Text(false, 3), r.Text(false, 3), r.Text(false), r.Text(false)};
                    const unsigned nulled = r(16);
                    if (nulled < 4) args[nulled] = nullptr;
                    const char *a = args[0], *k = args[1], *c = args[2], *e = args[3];
                    size_t before = d->entries.size();
                    (side ? ours : orig)(d, a, k, c, e);
                    difftest::NotCounted bookkeeping;
                    LogDefinition(b, *d);
                    if (side) {
                        const bool any = !a || !k || !c || !e;
                        nulls += any, inserted += !any && d->entries.size() > before;
                        overwritten += !any && d->entries.size() == before;
                    }
                }
                difftest::NotCounted bookkeeping;
                delete d;
            }
            LogHeap(b, counter);
        }
        svcfx::Same(ctx, outcome[0], outcome[1], "add", seed);
        if (ctx.failures.size() >= 6) return;
    }
    if (nulls < 300 || inserted < 1000 || overwritten < 500)
        ctx.Fail("coverage: nulls %d, inserted %d, overwritten %d", nulls, inserted, overwritten);
}

DIFF_TEST(LotImposterDefinition_Find, 0x3f8300)
{
    GAME_HEAP;
    using Fn = uint32_t(__thiscall *)(void *, const void *, void *);
    const Fn orig = ctx.Original<Fn>(0x3f8300), ours = reinterpret_cast<Fn>(MemberAddress(&cImposterDefinition::Find));
    int found = 0, missed = 0, ownValue = 0, keyOut = 0;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        const uint32_t base = seed * 2246822519u + 0x3f8300u;
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ctx.Mark(side ? "ours" : "original");
            Rng r{std::mt19937(base)};
            Blob &b = outcome[side];
            difftest::AllocationCounter counter;
            {
                cImposterDefinition *d;
                cImposterStringPair *key, *fresh, *out;
                unsigned mode;
                {
                    difftest::NotCounted bookkeeping;
                    d = new cImposterDefinition("model");
                    Fill(*d, r, 2 + r(5), 3);
                    key = new cImposterStringPair(r.Text(false, 3), r.Text(false, 3));
                    fresh = new cImposterStringPair(r.Text(false), r.Text(false));
                    key->first.s.mRefCount = 0x71, fresh->first.s.mRefCount = 0x61, fresh->second.s.mRefCount = 0x62;
                    mode = r(4); // 0 / 1 a filled pair; 2 the found entry's own value; 3 the key
                    const auto it = d->entries.find(*key);
                    out = mode == 3 ? key : mode == 2 && it != d->entries.end() ? &it->second : fresh;
                }
                const uint32_t result = (side ? ours : orig)(d, key, out) & 0xff;
                difftest::NotCounted bookkeeping;
                b.push_back(result);
                LogString(b, &key->first), LogString(b, &key->second);
                LogString(b, &fresh->first), LogString(b, &fresh->second);
                LogDefinition(b, *d);
                if (side) {
                    found += result != 0, missed += result == 0;
                    ownValue += out != key && out != fresh, keyOut += out == key && result;
                }
                delete d;
                delete key;
                delete fresh;
            }
            LogHeap(b, counter);
        }
        svcfx::Same(ctx, outcome[0], outcome[1], "find", seed);
        if (ctx.failures.size() >= 6) return;
    }
    if (found < 300 || missed < 300 || ownValue < 100 || keyOut < 50)
        ctx.Fail("coverage: found %d, missed %d, own value %d, key out %d", found, missed, ownValue, keyOut);
}

DIFF_TEST(LotImposterDefinitions_Define, 0x3f76e0)
{
    GAME_HEAP;
    using Fn2 = void(__thiscall *)(void *, const char *, const char *);
    using Fn6 = void(__thiscall *)(void *, const char *, const char *, const char *, const char *, const char *,
                                   const char *);
    const Fn2 orig2 = ctx.Original<Fn2>(0x3f7890),
              ours2 = reinterpret_cast<Fn2>(MemberAddress(
                  static_cast<void (cImposterDefinitions::*)(const char *, const char *)>(&cImposterDefinitions::Define)));
    const Fn6 orig6 = ctx.Original<Fn6>(0x3f76e0),
              ours6 = reinterpret_cast<Fn6>(MemberAddress(
                  static_cast<void (cImposterDefinitions::*)(const char *, const char *, const char *, const char *,
                                                             const char *, const char *)>(&cImposterDefinitions::Define)));
    int fresh[2] = {}, again[2] = {};
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        const uint32_t base = seed * 2654435761u + 0x3f76e0u;
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ctx.Mark(side ? "ours" : "original");
            Rng r{std::mt19937(base)};
            Blob &b = outcome[side];
            difftest::AllocationCounter counter;
            {
                cImposterDefinitions *d;
                {
                    difftest::NotCounted bookkeeping;
                    d = new cImposterDefinitions();
                    const unsigned pre = r(3);
                    for (unsigned i = 0; i < pre; ++i) {
                        cImposterDefinition &def = d->mDefinitions[cImposterString(r.Text(false, 4))];
                        def.name.s.mString = r.Text(false);
                        def.name.s.mRefCount = 0x41;
                        Fill(def, r, r(3), 3);
                    }
                }
                const unsigned calls = 1 + r(8);
                for (unsigned i = 0; i < calls; ++i) {
                    const unsigned form = r(2);
                    const char *name = r.Text(false, 4), *model = r.Text(true);
                    const char *a = r.Text(true, 3), *k = r.Text(true, 3), *c = r.Text(true), *e = r.Text(true);
                    bool had;
                    {
                        difftest::NotCounted bookkeeping;
                        had = d->mDefinitions.count(cImposterString(name)) != 0;
                    }
                    if (form) (side ? ours6 : orig6)(d, name, a, k, model, c, e);
                    else (side ? ours2 : orig2)(d, name, model);
                    difftest::NotCounted bookkeeping;
                    b.push_back(0xca11);
                    LogDefinitions(b, *d);
                    if (side) ++(had ? again : fresh)[form];
                }
                difftest::NotCounted bookkeeping;
                delete d;
            }
            LogHeap(b, counter);
        }
        svcfx::Same(ctx, outcome[0], outcome[1], "define", seed);
        if (ctx.failures.size() >= 6) return;
    }
    if (fresh[0] < 300 || fresh[1] < 300 || again[0] < 500 || again[1] < 500)
        ctx.Fail("coverage: new 2 %d, new 6 %d, again 2 %d, again 6 %d", fresh[0], fresh[1], again[0], again[1]);
}

namespace lotimpdef_test {
const char *const kPatterns[] = {"*", "chair*", "?hair*", "CHAIR", "*_01", "", "a*", "*x*", "fifteen_chars_?"};
const char *const kSubsets[] = {"body", "Body", "legs", "sixteen_chars_xx"};
const char *const kMaterials[] = {"wood", "WOOD", "metal_material_long_name", ""};
const char *const kImposter[] = {"imp_a", "imp_b_long_name_over_15", ""};
void AddMode(cDesignModes &m, const char *k, const char *v)
{
    nTSWorld::cMapString key, value;
    key.mString = k, value.mString = v;
    key.mRefCount = 0x31, value.mRefCount = 0x32;
    m.mModes.emplace(key, value);
}
} // namespace lotimpdef_test

DIFF_TEST(LotImposterDefinitions_Map, 0x3f83d0)
{
    GAME_HEAP;
    using Fn = uint32_t(__thiscall *)(void *, void *, void *);
    const Fn orig = ctx.Original<Fn>(0x3f83d0), ours = reinterpret_cast<Fn>(MemberAddress(&cImposterDefinitions::Map));
    int matched = 0, unmatched = 0, hits = 0, alias = 0, several = 0;
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        const uint32_t base = seed * 2246822519u + 0x3f83d0u;
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ctx.Mark(side ? "ours" : "original");
            Rng r{std::mt19937(base)};
            Blob &b = outcome[side];
            difftest::AllocationCounter counter;
            {
                cImposterDefinitions *d;
                cDesignModes *loaded, *other, *out;
                {
                    difftest::NotCounted bookkeeping;
                    d = new cImposterDefinitions();
                    const unsigned patterns = r(6);
                    for (unsigned i = 0; i < patterns; ++i) {
                        cImposterDefinition &def = d->mDefinitions[cImposterString(kPatterns[r(9)])];
                        def.name.s.mString = kImposter[r(3)];
                        const unsigned entries = r(6);
                        for (unsigned e = 0; e < entries; ++e) {
                            cImposterStringPair k(kSubsets[r(4)], kMaterials[r(4)]), v(kImposter[r(3)], kMaterials[r(4)]);
                            def.entries[k] = v;
                        }
                    }
                    loaded = new cDesignModes();
                    loaded->mTag.mString = kTexts[1 + r(kTextCount - 1)];
                    loaded->mName.mString = r.Text(false);
                    loaded->mTag.mRefCount = 0x21, loaded->mName.mRefCount = 0x22;
                    const unsigned modes = r(5);
                    for (unsigned i = 0; i < modes; ++i) AddMode(*loaded, kSubsets[r(4)], kMaterials[r(4)]);
                    other = new cDesignModes();
                    other->mTag.mString = r.Text(false), other->mName.mString = r.Text(false);
                    other->mTag.mRefCount = 0x23, other->mName.mRefCount = 0x24;
                    const unsigned prior = r(3);
                    for (unsigned i = 0; i < prior; ++i) AddMode(*other, kSubsets[r(4)], kImposter[r(3)]);
                    out = r(10) == 0 ? loaded : other;
                }
                const uint32_t result = (side ? ours : orig)(d, loaded, out) & 0xff;
                difftest::NotCounted bookkeeping;
                b.push_back(result);
                LogModes(b, *loaded), LogModes(b, *other), LogDefinitions(b, *d);
                if (side) {
                    matched += result != 0, unmatched += result == 0, alias += out == loaded;
                    hits += result && !out->mModes.empty();
                    unsigned matching = 0;
                    for (const auto &[pattern, def] : d->mDefinitions)
                        matching += pattern.s.mString == "*" || pattern.s.mString == "chair*";
                    several += result && matching > 1;
                }
                delete d;
                delete loaded;
                delete other;
            }
            LogHeap(b, counter);
        }
        svcfx::Same(ctx, outcome[0], outcome[1], "map", seed);
        if (ctx.failures.size() >= 6) return;
    }
    if (matched < 500 || unmatched < 300 || hits < 150 || alias < 100 || several < 100)
        ctx.Fail("coverage: matched %d, unmatched %d, hits %d, alias %d, several %d", matched, unmatched, hits, alias,
                 several);
}
