// Differential tests for nGZSceneGraph::cLevelInfo (lane SX, scenegraph M4b; src/sg/cLevelInfo.*).
//
// SG_LevelInfo_Construct: both constructors (0xaaff70 default, 0xaafe10 sized) on a buffer with flag 1, every format word
// from -1 to 10 and widths / heights at 0, 1, the DXT block edges, large and negative; the object's words after the
// constructor (vtable words aside), then the slots through its own vtables: QueryInterface 0xab2380 (the self iid, the
// cSGResource iids, others, a null out), slots 13 / 14 0xab0350, 15 / 16 (the flag), the virtual base's records 18 / 19
// (0xab1c10 / 0xab1bf0), GetTypeInfo 0xab3a30, GetOwnerInfo; then the deleting destructor 0xab01a0 with flag 0 (in place).
//
// SG_LevelInfo_ReadWrite: a level from each side's factory (the game's 0xab15f0, ours Create), then three rounds of Read
// 0xab2cf0 and Write 0xab3f10 on generated streams (byte counts 0, small, the threshold 0x1000 and either side of it,
// large; the same count twice so the resize 0xab41b0 keeps the block), and the copy 0xab0120 from a second level into the
// first (sizes either side of the threshold, the same size, none); at the end the last Release (the deleting destructor
// 0xab01a0 with flag 1). VirtualAlloc / VirtualFree are faked (impfx): the blocks come from one arena at the same address
// on both sides, so their offsets compare; operator new[] blocks are counted and compared by content.
#include "difftest.h"
#include "game/game.h"
#include "impfx.h"
#include "rz/cRZString.h"
#include "sg/cLevelInfo.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace nGZSceneGraph;

namespace {

constexpr uint32_t kFactory = 0xab15f0;
constexpr uint32_t kCtorDefault = 0xaaff70;
constexpr uint32_t kCtorSized = 0xaafe10;
constexpr uint32_t kAssign = 0xab0120;
constexpr uint32_t kStringPoolCell = 0x10f9070;
constexpr uint32_t kFrameworkCell = 0x10c9378;
constexpr uint32_t kVbase = 0x50;

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
Run *gRun = nullptr;

bool Compare(difftest::Context &ctx, const char *what, uint32_t seed, const Run &a, const Run &b)
{
    ++ctx.cases;
    for (size_t i = 0; i < a.notes.size() && i < b.notes.size(); ++i) {
        if (!(a.notes[i] == b.notes[i])) {
            ctx.Fail("%s %u note %zu: original %s, ours %s", what, seed, i, Show(a.notes[i]).c_str(), Show(b.notes[i]).c_str());
            return false;
        }
    }
    if (a.notes.size() != b.notes.size()) {
        ctx.Fail("%s %u: %zu notes from the original, %zu from ours", what, seed, a.notes.size(), b.notes.size());
        return false;
    }
    return true;
}

// ---- the string pool (0x10f9070): slot 5 interns, 6 / 7 count, 8 the text ------------------------------------------------
constexpr uint32_t kFirstHandle = 0x1000;
struct StringPool {
    std::deque<std::string> texts;
    std::vector<int> counts;
    void Reset()
    {
        texts.clear();
        counts.clear();
    }
} gStrings;
uint32_t __fastcall PoolOpen(void *, void *, uint32_t a, uint32_t flag)
{
    difftest::NotCounted quiet;
    const char *text = reinterpret_cast<const char *>(static_cast<uintptr_t>(a));
    const std::string s = text ? text : "";
    for (size_t i = 0; i < gStrings.texts.size(); ++i)
        if (gStrings.texts[i] == s) return kFirstHandle + static_cast<uint32_t>(i);
    gStrings.texts.push_back(s);
    gStrings.counts.push_back(0);
    if (gRun) gRun->Add("pool open", {flag}, s);
    return kFirstHandle + static_cast<uint32_t>(gStrings.texts.size() - 1);
}
int *Count(uint32_t h) { return h >= kFirstHandle && h - kFirstHandle < gStrings.counts.size() ? &gStrings.counts[h - kFirstHandle] : nullptr; }
uint32_t __fastcall PoolUse(void *, void *, uint32_t h)
{
    if (int *c = Count(h)) ++*c;
    return 0;
}
uint32_t __fastcall PoolClose(void *, void *, uint32_t h)
{
    if (int *c = Count(h)) --*c;
    return 0;
}
uint32_t __fastcall PoolText(void *, void *, uint32_t h)
{
    return h >= kFirstHandle && h - kFirstHandle < gStrings.texts.size() ? P(gStrings.texts[h - kFirstHandle].c_str()) : P("");
}
uint32_t __fastcall PoolTrap(void *, void *)
{
    if (gRun) gRun->Add("string pool: an unexpected slot", {});
    return 0;
}
void *gPoolVt[10];
void *gPoolObject[1] = {gPoolVt};

// ---- the persist reader and writer -------------------------------------------------------------------------------------
struct Stream {
    std::vector<uint32_t> versions;
    std::vector<std::string> texts;
    std::vector<uint32_t> words;
    uint32_t fill = 0; // GetVoid's bytes: fill + index
    size_t versionAt = 0, textAt = 0, wordAt = 0;
} gStream;

struct CountedFake {
    void **vt;
    int32_t refs;
};
uint32_t __fastcall CfQi(CountedFake *, void *, uint32_t, void **out)
{
    if (out) *out = nullptr;
    return 0;
}
uint32_t __fastcall CfAddRef(CountedFake *self, void *) { return static_cast<uint32_t>(++self->refs); }
uint32_t __fastcall CfRelease(CountedFake *self, void *) { return static_cast<uint32_t>(--self->refs); }
void *gCfVt[3] = {reinterpret_cast<void *>(&CfQi), reinterpret_cast<void *>(&CfAddRef), reinterpret_cast<void *>(&CfRelease)};
CountedFake gRecordNames[2];

uint32_t __fastcall RdTypeInfo(void *, void *, uint32_t *record)
{
    const size_t i = gStream.versionAt++;
    CountedFake *name = i < 2 ? &gRecordNames[i] : nullptr;
    if (name) ++name->refs;
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
uint32_t Where(const void *p); // the block's arena offset, or a heap / null mark
uint32_t __fastcall RdVoid(void *, void *, uint8_t *data, uint32_t size)
{
    if (gRun) gRun->Add("read bytes", {Where(data), size});
    for (uint32_t i = 0; i < size; ++i) data[i] = static_cast<uint8_t>(gStream.fill + i * 7);
    return 1;
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

uint32_t Sum(const uint8_t *data, uint32_t size)
{
    uint32_t h = 0x811c9dc5u;
    for (uint32_t i = 0; i < size; ++i) h = (h ^ data[i]) * 0x01000193u;
    return h;
}
uint32_t __fastcall WrUint32(void *, void *, uint32_t v)
{
    if (gRun) gRun->Add("write word", {v});
    return 1;
}
uint32_t __fastcall WrText(void *, void *, const char *text)
{
    if (gRun) gRun->Add("write text", {}, Text(text));
    return 1;
}
uint32_t __fastcall WrVoid(void *, void *, const uint8_t *data, uint32_t size)
{
    if (gRun) gRun->Add("write bytes", {Where(data), size, data ? Sum(data, size) : 0});
    return 1;
}
uint32_t __fastcall WrRecord(void *, void *, const uint32_t *record)
{
    std::string text = "<null>";
    if (void *s = reinterpret_cast<void *>(static_cast<uintptr_t>(record[0]))) {
        using DataFn = const char *(__thiscall *)(void *);
        using LenFn = uint32_t(__thiscall *)(void *);
        void **vt = *static_cast<void ***>(s);
        const char *data = reinterpret_cast<DataFn>(vt[6])(s);
        text = data ? std::string(data, reinterpret_cast<LenFn>(vt[7])(s)) : "<no data>";
    }
    if (gRun) gRun->Add("write record", {record[1], record[2]}, text);
    return 1;
}
void *gWriterVt[29];
void *gWriterObject[1] = {gWriterVt};

// ---- the framework and its resource manager -----------------------------------------------------------------------------
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

// ---- VirtualAlloc / VirtualFree: blocks from one arena (the same address on both sides) ---------------------------------
constexpr uint32_t kArenaBytes = 0x100000;
uint8_t *gArena = nullptr;
uint32_t gArenaNext = 0;
uint32_t Where(const void *p)
{
    if (!p) return 0;
    const uint8_t *b = static_cast<const uint8_t *>(p);
    if (b >= gArena && b < gArena + kArenaBytes) return 0x70000000u + static_cast<uint32_t>(b - gArena);
    return 0xeeee0000u; // a heap block
}
uint32_t OnVirtualAlloc(drawfx::Fake &, const uint32_t *a)
{
    const uint32_t size = a[1];
    if (gRun) gRun->Add("VirtualAlloc", {a[0], size, a[2], a[3]});
    if (gArenaNext + size > kArenaBytes) return 0;
    uint8_t *block = gArena + gArenaNext;
    std::memset(block, 0, size);
    gArenaNext = (gArenaNext + size + 0xfff) & ~0xfffu;
    return P(block);
}
uint32_t OnVirtualFree(drawfx::Fake &, const uint32_t *a)
{
    if (gRun) gRun->Add("VirtualFree", {Where(reinterpret_cast<void *>(static_cast<uintptr_t>(a[0]))), a[1], a[2]});
    return 1;
}

// Everything the tests put in the image's globals, put back when they end.
struct Fakes {
    void **poolCell, **framework;
    void *savedPool, *savedFramework;
    impfx::Imports imp;
    explicit Fakes(difftest::Context &ctx)
        : poolCell(ctx.image.At<void *>(kStringPoolCell)), framework(ctx.image.At<void *>(kFrameworkCell)),
          savedPool(*poolCell), savedFramework(*framework), imp(ctx)
    {
        for (void *&s : gPoolVt) s = reinterpret_cast<void *>(&PoolTrap);
        gPoolVt[5] = reinterpret_cast<void *>(&PoolOpen);
        gPoolVt[6] = reinterpret_cast<void *>(&PoolUse);
        gPoolVt[7] = reinterpret_cast<void *>(&PoolClose);
        gPoolVt[8] = reinterpret_cast<void *>(&PoolText);
        *poolCell = gPoolObject;
        FillTraps(gReaderVt, std::make_integer_sequence<unsigned, 29>());
        gReaderVt[10] = reinterpret_cast<void *>(&RdUint32);
        gReaderVt[15] = reinterpret_cast<void *>(&RdVoid);
        gReaderVt[22] = reinterpret_cast<void *>(&RdString);
        gReaderVt[26] = reinterpret_cast<void *>(&RdTypeInfo);
        FillTraps(gWriterVt, std::make_integer_sequence<unsigned, 29>());
        gWriterVt[10] = reinterpret_cast<void *>(&WrUint32);
        gWriterVt[15] = reinterpret_cast<void *>(&WrVoid);
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
        IMPFX_ADD(imp, VirtualAlloc).On(&OnVirtualAlloc);
        IMPFX_ADD(imp, VirtualFree).On(&OnVirtualFree);
        gArena = static_cast<uint8_t *>(VirtualAlloc(nullptr, kArenaBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    }
    ~Fakes()
    {
        VirtualFree(gArena, 0, MEM_RELEASE);
        gArena = nullptr;
        *poolCell = savedPool;
        *framework = savedFramework;
    }
    Fakes(const Fakes &) = delete;
    void BeginSide()
    {
        imp.BeginSide();
        gStrings.Reset();
        gManagerRefs = 0;
        gArenaNext = 0;
        for (CountedFake &n : gRecordNames) n = {gCfVt, 0};
    }
};

// The object's words (vtables and the vbptr aside: each side's own), the pixel block by where it is and its bytes.
void Dump(Run &run, const uint8_t *o, const char *what)
{
    std::vector<uint32_t> w;
    for (uint32_t off = 4; off < 0x1c; off += 4) w.push_back(Rd<uint32_t>(o, off));
    for (uint32_t off = 0x20; off < 0x44; off += 4) // +0x28 and +0x3c are bytes: their padding is the heap's, unset
        w.push_back(off == 0x28 || off == 0x3c ? Rd<uint8_t>(o, off) : Rd<uint32_t>(o, off));
    const uint8_t *data = Rd<const uint8_t *>(o, 0x44);
    const uint32_t size = Rd<uint32_t>(o, 0x48);
    w.insert(w.end(), {Where(data), size, Rd<uint32_t>(o, 0x4c), data ? Sum(data, size) : 0});
    const uint32_t name = Rd<uint32_t>(o, 0x20);
    run.Add(what, w, Text(name >= kFirstHandle ? reinterpret_cast<const char *>(PoolText(nullptr, nullptr, name)) : nullptr));
}

// The slots through the object's own vtables (primary at +0, the virtual base at +0x50).
template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void ***>(face))[slot])(face, a...);
}

void Probe(Run &run, uint8_t *o, std::mt19937 &rng)
{
    void *vb = o + kVbase;
    run.Add("held", {Slot<uint32_t>(o, 1)}); // a count of our own, so the probe's releases never reach 0 (a stack object)
    const uint32_t iids[] = {cLevelInfo::kIidSelf, cISGResource::kIid, 0x5b0aaa74, 0xbb11a53e, 0x9b11bf70, 0x12345678, 0};
    for (uint32_t iid : iids) {
        void *out = reinterpret_cast<void *>(0x5a5a5a5a);
        const bool nullOut = rng() % 6 == 0;
        const uint32_t before = Rd<uint32_t>(o, 4);
        // Through vb: the vtordisp thunk 0xab236d.
        const bool r = Slot<bool>(rng() % 2 ? static_cast<void *>(o) : vb, 0, iid, nullOut ? nullptr : &out);
        const uint32_t after = Rd<uint32_t>(o, 4);
        run.Add("qi", {iid, r, nullOut ? 1u : P(out) - P(o), before, after});
        if (r && !nullOut)
            for (uint32_t k = before; k < after; ++k) static_cast<cIGZUnknown *>(out)->Release();
    }
    run.Add("slots 13 / 14", {Slot<uint32_t>(o, 13) - P(o), Slot<uint32_t>(o, 14) - P(o)});
    const uint8_t flag = static_cast<uint8_t>(rng());
    Slot<void>(o, 15, flag);
    run.Add("flag", {Slot<uint8_t>(o, 16), Rd<uint32_t>(o, 0x3c)});
    // The vtordisp thunks 0xab1c06 (slot 18) and 0xab1be6 (slot 19).
    run.Add("records", {}, Text(Slot<const char *>(vb, 18)) + " " + Text(Slot<const char *>(vb, 19)));
    cPersistTypeInfo info;
    std::memset(&info, 0xa5, sizeof(info));
    Slot<cPersistTypeInfo *>(vb, 4, &info); // the vtordisp thunk 0xab3a26
    const std::string name = info.mName ? reinterpret_cast<const cRZString *>(info.mName)->ToChar() : "<null>";
    const uint32_t count = info.mName ? info.mName->AddRef() : 0;
    if (info.mName) {
        info.mName->Release();
        info.mName->Release();
    }
    run.Add("type info", {info.mWord4, info.mVersion, count}, name);
    run.Add("owner info", {Slot<uint32_t>(vb, 6) - P(o)});
}

using CtorDefault = void *(__fastcall *)(void *, void *, uint32_t);
using CtorSized = void *(__fastcall *)(void *, void *, int32_t, int32_t, int32_t, uint32_t);
using AssignFn = void *(__fastcall *)(void *, void *, const void *);

} // namespace

DIFF_TEST(SG_LevelInfo_Construct, 0xaafe10)
{
    Fakes fakes(ctx);
    const int32_t sizes[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 255, 256, 1023, 1024, -1, -4, -5, 0x10001};
    uint32_t seed = 0;
    unsigned failed = 0;
    for (int32_t format = -1; format <= 10 && failed < 3; ++format) {
        for (int32_t w : sizes) {
            for (int k = 0; k < 3 && failed < 3; ++k) {
                const int32_t h = sizes[(seed * 7 + k * 5) % (sizeof(sizes) / sizeof(sizes[0]))];
                const bool sized = format != -1 || k != 0;
                Run runs[2];
                for (int side = 0; side < 2; ++side) {
                    fakes.BeginSide();
                    gRun = &runs[side];
                    std::mt19937 rng(seed);
                    alignas(8) uint8_t buffer[cLevelInfo::kSize];
                    std::memset(buffer, 0xcd, sizeof(buffer));
                    difftest::AllocationCounter counter;
                    void *r;
                    if (sized) {
                        const CtorSized ctor = side == 0 ? ctx.Original<CtorSized>(kCtorSized)
                                                         : reinterpret_cast<CtorSized>(&cLevelInfo_SizedConstructor);
                        r = ctor(buffer, nullptr, w, h, format, 1);
                    } else {
                        const CtorDefault ctor = side == 0 ? ctx.Original<CtorDefault>(kCtorDefault)
                                                           : reinterpret_cast<CtorDefault>(&cLevelInfo_Constructor);
                        r = ctor(buffer, nullptr, 1);
                    }
                    runs[side].Add("constructed", {r == buffer, Rd<uint32_t>(buffer, 0x1c) != 0xcdcdcdcd});
                    Dump(runs[side], buffer, "built");
                    Probe(runs[side], buffer, rng);
                    Slot<void *>(buffer, 12, 0u); // the deleting destructor, in place
                    runs[side].Add("destroyed", {static_cast<uint32_t>(counter.Blocks())});
                    gRun = nullptr;
                }
                if (!Compare(ctx, "seed", seed, runs[0], runs[1])) ++failed;
                ++seed;
            }
        }
    }
}

namespace {

// A stream for a level's Read: its record (version 0-12), the resource's record and name, the four words, the bytes.
void MakeStream(std::mt19937 &rng, uint32_t bytes)
{
    difftest::NotCounted quiet;
    gStream = {};
    gStream.versions = {rng() % 13, rng() % 4};
    const char *names[] = {"", "level_0", "Level_0", "a_level_name_well_past_fifteen_bytes"};
    gStream.texts = {names[rng() % 4]};
    gStream.words = {rng() % 2048, rng() % 2048, rng() % 8192, bytes};
    gStream.fill = rng();
}

uint32_t Bytes(std::mt19937 &rng)
{
    const uint32_t picks[] = {0, 1, 16, 0xfff, 0x1000, 0x1001, 0x4000, 0x2b00, 64};
    return rng() % 4 == 0 ? rng() % 0x3000 : picks[rng() % 9];
}

Run RunReadWrite(difftest::Context &ctx, Fakes &fakes, bool game, uint32_t seed)
{
    Run run;
    fakes.BeginSide();
    gRun = &run;
    std::mt19937 rng(seed);
    difftest::AllocationCounter counter;
    using FactoryFn = void *(__cdecl *)();
    void *const faces[2] = {game ? ctx.Original<FactoryFn>(kFactory)() : cLevelInfo::Create(),
                            game ? ctx.Original<FactoryFn>(kFactory)() : cLevelInfo::Create()};
    uint8_t *objs[2] = {static_cast<uint8_t *>(faces[0]) - kVbase, static_cast<uint8_t *>(faces[1]) - kVbase};
    char mark[64];
    for (int round = 0; round < 3; ++round) {
        for (int which = 0; which < 2; ++which) {
            std::snprintf(mark, sizeof(mark), "%s side, seed %u, round %d read %d", game ? "game" : "our", seed, round, which);
            ctx.Mark(mark);
            const uint32_t bytes = round == 1 && which == 0 && rng() % 2 ? Rd<uint32_t>(objs[0], 0x48) : Bytes(rng);
            MakeStream(rng, bytes);
            Slot<void>(faces[which], 3, reinterpret_cast<void *>(gReaderObject)); // the vtordisp thunk 0xab2ce5
            run.Add("read taken", {static_cast<uint32_t>(gStream.versionAt), static_cast<uint32_t>(gStream.textAt),
                                   static_cast<uint32_t>(gStream.wordAt), static_cast<uint32_t>(gRecordNames[0].refs),
                                   static_cast<uint32_t>(gRecordNames[1].refs), static_cast<uint32_t>(counter.Blocks())});
            Dump(run, objs[which], "after read");
            Slot<void>(faces[which], 5, reinterpret_cast<void *>(gWriterObject)); // the vtordisp thunk 0xab3f03
        }
        std::snprintf(mark, sizeof(mark), "%s side, seed %u, round %d assign", game ? "game" : "our", seed, round);
        ctx.Mark(mark);
        for (uint8_t *o : objs) Slot<void>(o, 15, static_cast<uint8_t>(rng())); // flags of their own, for the copy
        const int to = static_cast<int>(rng() % 2);
        const AssignFn assign = game ? ctx.Original<AssignFn>(kAssign) : reinterpret_cast<AssignFn>(&cLevelInfoAdapters::Assign);
        void *const r = assign(objs[to], nullptr, rng() % 8 ? objs[1 - to] : objs[to]);
        run.Add("assigned", {static_cast<uint32_t>(to), r == objs[to], static_cast<uint32_t>(counter.Blocks())});
        Dump(run, objs[0], "after assign 0");
        Dump(run, objs[1], "after assign 1");
        Slot<void>(faces[to], 5, reinterpret_cast<void *>(gWriterObject));
    }
    for (void *face : faces) run.Add("release", {static_cast<cIGZUnknown *>(face)->Release()});
    run.Add("manager refs", {static_cast<uint32_t>(gManagerRefs)});
    std::vector<uint32_t> counts;
    for (int c : gStrings.counts) counts.push_back(static_cast<uint32_t>(c));
    run.Add("pool counts", counts);
    counter.Stop();
    run.Add("heap blocks left", {static_cast<uint32_t>(counter.Blocks())});
    gRun = nullptr;
    return run;
}

} // namespace

DIFF_TEST(SG_LevelInfo_ReadWrite, 0xab2cf0)
{
    Fakes fakes(ctx);
    unsigned failed = 0;
    for (uint32_t seed = 1; seed <= 200 && failed < 3; ++seed) {
        const Run a = RunReadWrite(ctx, fakes, true, seed);
        const Run b = RunReadWrite(ctx, fakes, false, seed);
        if (!Compare(ctx, "seed", seed, a, b)) ++failed;
    }
}
