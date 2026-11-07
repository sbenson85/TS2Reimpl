// Differential tests for nGZSceneGraph::cMaterialAttributes (lane RO, src/sg/cMaterialAttributes.*) and 0xae87d0.
//
// SG_MaterialAttributes_Slots: the game's object made by its factory 0xaaa3c0 (constructor 0xaa9e40, vtable 0xe76188),
// ours by `new`; the same seeded sequence of slot calls through each object's own vtable, every answer noted and
// compared: SetName, AddAttribute (names that meet ignoring case, equal names, lengths past the string's 15-byte buffer,
// many attributes in one group so the sort leaves its insertion-sort path; a source fake logs every call the object makes
// on it, in order, and writes data tagged per attribute so the order after the sort shows), GetName, IsEmpty, the group
// count, the nth key, the attribute and value counts (present and absent keys), the total, FindAttribute and
// GetAttribute (the whole out-record, prefilled), ReadAttribute (the data handed to the source's slot 2, its answer
// passed on), QueryInterface (iids 1, 0x0ace5c2b, others; a null out), AddRef / Release, and the final Release that
// deletes (the object's block freed, nothing left over). The state is dumped now and then and at the end: the game's by
// walking its std containers raw, ours through its members.
// SG_MaterialAttributes_Create: a fresh object from each factory (the dump, the bytes allocated).
// SG_OpenNameCell: 0xae87d0 against OpenNameCell over a fake service object (slots 5 and 6 logged).
//
// RVAs verified here: 0xaaa3c0 (factory), 0xaa9e40 (constructor), 0xaa9fe0 (destructor body), 0xaaa0e0 (slot 15), slots
// 0xaaa750 (QueryInterface), 0xaaa780, 0xaaa100, 0xaaa6d0, 0xaaa3b0, 0xaaa430, 0xaaa6f0, 0xaaa620, 0xaaa680, 0xaaa520,
// 0xaaa490, 0xaaa350, the shared AddRef 0x148fb0 / Release 0x16e890 / count 0x15e970 through this class's slots (ours
// implement them; the game's bodies stay unclaimed), and 0xae87d0.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "rz/StaticGetters2.h"
#include "sg/cMaterialAttributes.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace nGZSceneGraph;

namespace {

constexpr uint32_t kFactory = 0xaaa3c0;
constexpr uint32_t kVtable = 0xe76188;

// One answer: a label, words and a text.
struct Note {
    std::string what;
    std::vector<uint32_t> words;
    std::string text;
    bool operator==(const Note &o) const { return what == o.what && words == o.words && text == o.text; }
};

struct Run {
    std::vector<Note> notes;
    void Add(std::string what, std::vector<uint32_t> words, std::string text = {})
    {
        notes.push_back({std::move(what), std::move(words), std::move(text)});
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

uint32_t HashBytes(const void *data, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const uint8_t *>(data)[i]) * 16777619u;
    return h;
}

// ---- the source fake: logs every call, in order -------------------------------------------------------------------------
uint32_t SizeOfKey(uint32_t key) { return (key % 5) * 3; }

struct Source : cIMaterialAttributeSource {
    Run *run;
    uint32_t key, width, tag;
    Source(Run *r, uint32_t k, uint32_t w, uint32_t t) : run(r), key(k), width(w), tag(t) {}
    void Slot0() override { run->Add("source slot 0", {}); }
    void Slot1() override { run->Add("source slot 1", {}); }
    uint32_t ConsumeData(const void *data) override
    {
        const uint32_t h = data ? HashBytes(data, SizeOfKey(key)) : 0;
        run->Add("source consume", {data != nullptr, h});
        return h ^ 0x5a5a;
    }
    uint32_t GetGroupKey() override
    {
        run->Add("source key", {key});
        return key;
    }
    uint32_t GetDataSize() override
    {
        run->Add("source size", {SizeOfKey(key)});
        return SizeOfKey(key);
    }
    uint32_t GetWidth() override
    {
        run->Add("source width", {width});
        return width;
    }
    void FillData(void *data) override
    {
        run->Add("source fill", {data != nullptr, SizeOfKey(key)});
        for (uint32_t i = 0; i < SizeOfKey(key); ++i) static_cast<uint8_t *>(data)[i] = static_cast<uint8_t>(tag + i);
    }
};

// ---- the state ----------------------------------------------------------------------------------------------------------
template <class T> T Rd(const void *base, uint32_t off) { return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + off); }

// A std::string of the image's layout at `base`: 16-byte buffer, size at +0x10, capacity at +0x14.
std::string RawString(const void *base)
{
    const char *text = Rd<uint32_t>(base, 0x14) >= 16 ? Rd<const char *>(base, 0) : static_cast<const char *>(base);
    return std::string(text, Rd<uint32_t>(base, 0x10));
}

void DumpAttribute(Run &run, uint32_t index, const std::string &name, uint32_t width, const void *data, size_t size)
{
    run.Add("attr", {index, width, uint32_t(size), HashBytes(data, size)}, name);
}

// The game's object, walked raw: +4 count, +8 name (0x18), +0x20 the map head, +0x24 its size. A node: left, parent, right,
// the isnil byte at +0xd, key +0x10, the vector's begin / end at +0x14 / +0x18; an Attribute is 0x28: name, width +0x18,
// vector begin at +0x1c, end at +0x20.
void DumpNode(Run &run, const uint8_t *node)
{
    if (Rd<uint8_t>(node, 0xd) != 0) return;
    DumpNode(run, Rd<const uint8_t *>(node, 0));
    const uint8_t *begin = Rd<const uint8_t *>(node, 0x14), *end = Rd<const uint8_t *>(node, 0x18);
    run.Add("group", {Rd<uint32_t>(node, 0x10), uint32_t((end - begin) / 0x28)});
    for (const uint8_t *a = begin; a != end; a += 0x28) {
        const uint8_t *dBegin = Rd<const uint8_t *>(a, 0x1c), *dEnd = Rd<const uint8_t *>(a, 0x20);
        DumpAttribute(run, uint32_t((a - begin) / 0x28), RawString(a), Rd<uint32_t>(a, 0x18), dBegin, size_t(dEnd - dBegin));
    }
    DumpNode(run, Rd<const uint8_t *>(node, 8));
}

void DumpGame(Run &run, const void *obj)
{
    const uint8_t *head = Rd<const uint8_t *>(obj, 0x20);
    run.Add("state", {Rd<uint32_t>(obj, 4), Rd<uint32_t>(obj, 0x24), Rd<uint32_t>(obj, 0x18)},
            std::string(Rd<uint32_t>(obj, 0x1c) >= 16 ? Rd<const char *>(obj, 8) : static_cast<const char *>(obj) + 8,
                        Rd<uint32_t>(obj, 0x18)));
    DumpNode(run, Rd<const uint8_t *>(head, 4));
}

void DumpOurs(Run &run, const cMaterialAttributes *obj)
{
    run.Add("state", {obj->mRefCount, uint32_t(obj->mGroups.size()), uint32_t(obj->mName.size())}, obj->mName);
    for (const auto &entry : obj->mGroups) {
        run.Add("group", {entry.first, uint32_t(entry.second.size())});
        uint32_t i = 0;
        for (const auto &a : entry.second) DumpAttribute(run, i++, a.mName, a.mWidth, a.mData.data(), a.mData.size());
    }
}

// ---- one side -----------------------------------------------------------------------------------------------------------
struct Gen {
    std::mt19937 rng;
    explicit Gen(uint32_t seed) : rng(seed) {}
    uint32_t Below(uint32_t n) { return n ? rng() % n : 0; }
    bool Coin() { return rng() % 2 != 0; }
};

std::string Name(Gen &g)
{
    static const char *const fixed[] = {"diffuse", "Diffuse", "DIFFUSE", "normal", "Normal", "specular", "a", "A", "",
                                        "fifteen_chars__", "sixteen_chars___1", "Sixteen_Chars___1", "tex_0", "tex_1", "TEX_1",
                                        "tex_10", "tex_2", "zz", "Zz", "@[`{"};
    if (g.Below(3) != 0) return fixed[g.Below(sizeof(fixed) / sizeof(fixed[0]))];
    static const char alphabet[] = "abAB_";
    std::string s;
    const uint32_t len = 1 + g.Below(4);
    for (uint32_t k = 0; k < len; ++k) s += alphabet[g.Below(sizeof(alphabet) - 1)];
    return s;
}

const uint32_t kKeys[] = {5, 1, 0x80000001u, 0xffffffffu, 0, 100, 12};

uint32_t PickKey(Gen &g, bool skew) { return skew && g.Coin() ? kKeys[0] : kKeys[g.Below(sizeof(kKeys) / sizeof(kKeys[0]))]; }

void Words(Run &run, const char *what, const AttributeRef &r, const AttributeRef *ret, const AttributeRef *expected)
{
    run.Add(what, {ret == expected, r.mName != nullptr, r.mIndex, r.mWidth, r.mPosition}, r.mName ? r.mName : "");
}

Run RunSide(difftest::Context &ctx, bool game, uint32_t seed)
{
    Run run;
    difftest::AllocationCounter counter;
    cIMaterialAttributes *obj;
    cMaterialAttributes *ours = nullptr;
    if (game) {
        obj = ctx.Original<cIMaterialAttributes *(__cdecl *)()>(kFactory)();
    } else {
        ours = new cMaterialAttributes;
        obj = ours;
    }
    auto dump = [&] {
        if (game) DumpGame(run, obj);
        else DumpOurs(run, ours);
    };
    Gen g(seed);
    uint32_t references = 1;
    obj->AddRef();
    run.Add("start", {Rd<uint32_t>(obj, 4)});
    std::vector<uint32_t> presentKeys;
    uint32_t tag = 1;
    const bool skew = g.Below(4) != 0;
    const int ops = 80 + static_cast<int>(g.Below(seed % 5 == 0 ? 600 : 250));
    for (int i = 0; i < ops; ++i) {
        const uint32_t op = g.Below(100);
        if (i % 40 == 39) dump();
        if (op < 4) {
            const std::string name = Name(g);
            std::string *s = obj->SetName(name.c_str());
            run.Add("setname", {reinterpret_cast<uint8_t *>(s) == reinterpret_cast<uint8_t *>(obj) + 8}, *s);
        } else if (op < 48) {
            const std::string name = Name(g);
            const uint32_t key = PickKey(g, skew);
            Source source(&run, key, g.Below(6), tag);
            tag += 16;
            obj->AddAttribute(name.c_str(), &source);
            bool known = false;
            for (uint32_t k : presentKeys) known |= k == key;
            if (!known) presentKeys.push_back(key);
            run.Add("added", {key});
        } else if (op < 52) {
            const char *name = obj->GetName();
            run.Add("getname", {name != nullptr}, name ? name : "");
        } else if (op < 55) {
            run.Add("isempty", {uint32_t(obj->IsEmpty()) & 0xff});
        } else if (op < 58) {
            run.Add("count", {obj->GetGroupCount()});
        } else if (op < 62) {
            const uint32_t n = obj->GetGroupCount();
            if (n != 0) {
                const uint32_t at = g.Below(n);
                run.Add("groupkey", {at, obj->GetGroupKey(at)});
            }
        } else if (op < 68) {
            const uint32_t key = g.Below(4) == 0 ? g.rng() : PickKey(g, false);
            run.Add("attrcount", {key, obj->GetAttributeCount(key)});
        } else if (op < 74) {
            const uint32_t key = g.Below(4) == 0 ? g.rng() : PickKey(g, false);
            run.Add("valuecount", {key, obj->GetValueCount(key)});
        } else if (op < 77) {
            run.Add("total", {obj->GetTotalValueCount()});
        } else if (op < 87) {
            const std::string name = Name(g);
            const uint32_t key = g.Below(6) == 0 ? g.rng() : PickKey(g, false);
            AttributeRef out;
            std::memset(&out, 0xaa, sizeof(out));
            AttributeRef *ret = obj->FindAttribute(&out, name.c_str(), key);
            Words(run, "find", out, ret, &out);
        } else if (op < 93) {
            if (!presentKeys.empty()) {
                const uint32_t key = presentKeys[g.Below(static_cast<uint32_t>(presentKeys.size()))];
                const uint32_t count = obj->GetAttributeCount(key);
                AttributeRef out;
                std::memset(&out, 0xaa, sizeof(out));
                AttributeRef *ret = obj->GetAttribute(&out, g.Below(count), key);
                Words(run, "get", out, ret, &out);
            }
        } else if (op < 96) {
            if (!presentKeys.empty()) {
                const uint32_t key = presentKeys[g.Below(static_cast<uint32_t>(presentKeys.size()))];
                const uint32_t count = obj->GetAttributeCount(key);
                Source source(&run, key, 0, 0);
                const uint32_t index = g.Below(count);
                run.Add("read", {index, obj->ReadAttribute(&source, index)});
            }
        } else if (op < 99) {
            void *out = reinterpret_cast<void *>(uintptr_t(0x77));
            const uint32_t iid = g.Below(3) == 0 ? 1 : g.Coin() ? cMaterialAttributes::kIidAttributes : g.rng();
            const bool nullOut = g.Below(5) == 0;
            const bool ok = obj->QueryInterface(iid, nullOut ? nullptr : &out);
            run.Add("qi", {iid, nullOut, ok, out == obj, out == reinterpret_cast<void *>(uintptr_t(0x77)), Rd<uint32_t>(obj, 4)});
            if (ok) ++references;
        } else {
            if (g.Coin()) {
                ++references;
                run.Add("addref", {obj->AddRef()});
            } else if (references > 1) {
                --references;
                run.Add("release", {obj->Release()});
            }
        }
    }
    dump();
    // Every reference released: the last deletes the object.
    difftest::FreeRecorder rec;
    uint32_t last = 0;
    for (uint32_t r = references; r > 0; --r) {
        last = obj->Release();
        if (r > 1) run.Add("release", {last});
    }
    rec.Stop();
    run.Add("last release", {last, rec.Freed(obj)});
    counter.Stop();
    run.Add("leak", {uint32_t(counter.Blocks())});
    return run;
}

} // namespace

DIFF_TEST(SG_MaterialAttributes_Slots, kFactory)
{
    for (uint32_t seed = 1; seed <= 150; ++seed) {
        ctx.Mark("game side");
        Run a = RunSide(ctx, true, seed);
        ctx.Mark("our side");
        Run b = RunSide(ctx, false, seed);
        ++ctx.cases;
        if (a.notes.size() != b.notes.size()) {
            ctx.Fail("seed %u: %zu answers from the original, %zu from ours", seed, a.notes.size(), b.notes.size());
        }
        for (size_t i = 0; i < a.notes.size() && i < b.notes.size(); ++i) {
            if (!(a.notes[i] == b.notes[i])) {
                ctx.Fail("seed %u step %zu: original %s, ours %s", seed, i, Show(a.notes[i]).c_str(), Show(b.notes[i]).c_str());
                break;
            }
        }
    }
}

// A fresh object from each factory.
DIFF_TEST(SG_MaterialAttributes_Create, kFactory)
{
    auto original = ctx.Original<cIMaterialAttributes *(__cdecl *)()>(kFactory);
    Run a, b;
    long long blocksA, blocksB, bytesA, bytesB;
    cIMaterialAttributes *game, *mine;
    {
        difftest::AllocationCounter c;
        game = original();
        blocksA = c.Blocks();
        bytesA = c.Bytes();
    }
    {
        difftest::AllocationCounter c;
        mine = CreateMaterialAttributes();
        blocksB = c.Blocks();
        bytesB = c.Bytes();
    }
    ++ctx.cases;
    if (Rd<uint32_t>(game, 0) != uint32_t(ctx.image.Base() + kVtable)) ctx.Fail("the game object's vtable is not 0xe76188");
    if (blocksA != blocksB || bytesA != bytesB)
        ctx.Fail("allocations: original %lld blocks %lld bytes, ours %lld blocks %lld bytes", blocksA, bytesA, blocksB, bytesB);
    DumpGame(a, game);
    DumpOurs(b, static_cast<cMaterialAttributes *>(mine));
    if (a.notes.size() != b.notes.size() || !(a.notes[0] == b.notes[0]))
        ctx.Fail("fresh state: original %s, ours %s", Show(a.notes[0]).c_str(), Show(b.notes[0]).c_str());
    difftest::FreeRecorder rec;
    game->AddRef();
    game->Release();
    mine->AddRef();
    mine->Release();
    rec.Stop();
    ++ctx.cases;
    if (rec.Count() < 2) ctx.Fail("the final releases freed %zu blocks", rec.Count());
}

// ---- 0xae87d0 -----------------------------------------------------------------------------------------------------------
namespace {

std::string gCellLog;
uint32_t __fastcall SvcOpen(void *, void *, uint32_t a, uint32_t b)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "open %x %x\n", a, b);
    gCellLog += buf;
    return a * 3 + 0x100;
}
uint32_t __fastcall SvcUse(void *, void *, uint32_t v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "use %x\n", v);
    gCellLog += buf;
    return 0;
}
void *gSvcVt[9];
void *gSvcObject[1] = {gSvcVt};

} // namespace

DIFF_TEST(SG_OpenNameCell, 0xae87d0)
{
    auto original = ctx.Original<ServiceCell *(__cdecl *)(ServiceCell *, uint32_t)>(0xae87d0);
    void **cache = ctx.image.At<void *>(0x10f9070);
    void *saved = *cache;
    std::memset(gSvcVt, 0, sizeof(gSvcVt));
    gSvcVt[5] = reinterpret_cast<void *>(&SvcOpen);
    gSvcVt[6] = reinterpret_cast<void *>(&SvcUse);
    *cache = gSvcObject;
    std::mt19937 rng(0xae87d0);
    for (int i = 0; i < 400; ++i) {
        const uint32_t name = i % 4 == 0 ? 0 : i % 4 == 1 ? 1 + rng() % 5 : rng();
        const uint32_t junk = rng();
        ServiceCell a{junk}, b{junk};
        gCellLog.clear();
        ServiceCell *ra = original(&a, name);
        const std::string logA = gCellLog;
        gCellLog.clear();
        ServiceCell *rb = OpenNameCell(&b, name);
        const std::string logB = gCellLog;
        ++ctx.cases;
        if ((ra == &a) != (rb == &b) || a.mValue != b.mValue || logA != logB)
            ctx.Fail("name %x: original cell %x returns self %d log '%s', ours cell %x self %d log '%s'", name, a.mValue,
                     ra == &a, logA.c_str(), b.mValue, rb == &b, logB.c_str());
    }
    *cache = saved;
}
