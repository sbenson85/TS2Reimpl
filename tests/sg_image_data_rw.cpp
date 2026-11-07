// Differential tests for nGZSceneGraph::cImageData as a whole class (lane SZ, scenegraph M4b part 2; src/sg/cImageData.*).
//
// SG_ImageData_Construct: the constructor 0xaafd60 (ours: cImageData_Constructor) on a filled buffer, the words it
// leaves (vtables and the vbptr aside), a probe through the object's own vtables (QueryInterface 0xab2330, GetResource,
// the records, GetTypeInfo 0xab3980, PixelFormat 0xab2070, Init 0xab1890 and SetName so the destructor has work), then the
// destructor three ways: the deleting destructor in place (flag 0), the RVA 0xab0070 against our adapter on each side's
// object, and our adapter on a game-built object.
//
// SG_ImageData_MipChainBytes: 0xab0250 directly over level counts, every format word and sizes (powers of two, not, 0).
//
// SG_ImageData_Init: objects from each side's factory (0xab1590, ours Create), random Init 0xab1890 (slot 6) and
// BuildLevels 0xab1960 on the side's own entries; the object after each, Clone 0xab1390 (slot 3), PixelFormat (slot 33),
// QueryInterface; then the last Release.
//
// SG_ImageData_ReadWrite: Read 0xab23d0 (with ReadLevels 0xab2da0) through the persist reader on generated streams: every
// version 0-12 and 0xffffffff, sizes and formats, level counts, entry counts, the config's size reduction (found or not,
// at or past the level count, ignored), pixel records, external records (resolved, missing, a null resource, past the
// levels), negative record counts, and truncated streams (every read past the cut gives 0 or ""); then Write 0xab3b20
// (GetTypeInfo 0xab3980) of the image and sometimes of its clone, with colours and scales set past the 0-1 range. Some
// seeds run our side through the adapters on a game-built object. The resource manager, config, string pool, allocator,
// graphics managers (pixel formats, the image manipulator) are fakes; DrawMissingTextureName 0xae8310 is a stand-in on
// both sides (the image's and ours; its own test is tests/sg_missing_texture_name.cpp). VirtualAlloc / VirtualFree come from one arena.
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/cIGZPropertySet.h"
#include "impfx.h"
#include "rz/cRZString.h"
#include "sg/DrawMissingTextureName.h"
#include "sg/cImageData.h"
#include "sg/cLevelInfo.h"

#include <windows.h>

#include <algorithm>
#include <climits>
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

constexpr uint32_t kImageFactory = 0xab1590;
constexpr uint32_t kImageCtor = 0xaafd60;
constexpr uint32_t kImageDtor = 0xab0070;
constexpr uint32_t kMipChainBytes = 0xab0250;
constexpr uint32_t kBuildLevels = 0xab1960;
constexpr uint32_t kLevelFactory = 0xab15f0;
constexpr uint32_t kDrawMissing = 0xae8310;
constexpr uint32_t kStringPoolCell = 0x10f9070, kFrameworkCell = 0x10c9378, kAllocatorCell = 0x10c95d4,
                   kAllocatorGuard = 0x10c95d8, kFormatsCell = 0x10ca830, kManipCell = 0x10ca990, kKeysCell = 0x10f8f2c;
constexpr uint32_t kFace = 0x6c;       // the image's virtual base
constexpr uint32_t kLevelVbase = 0x50; // the level's

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
template <class T> T Rd(const void *base, uint32_t off) { return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + off); }
uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
float FromBits(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

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
void Log(const std::string &what, const std::vector<uint32_t> &words, const std::string &text = {})
{
    if (gRun) gRun->Add(what, words, text);
}

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

template <unsigned N> uint32_t __fastcall Trap(void *, void *)
{
    Log("fake: an unexpected slot", {N});
    return 0;
}
template <unsigned... N> void FillTraps(void **vt, std::integer_sequence<unsigned, N...>)
{
    ((vt[N] = reinterpret_cast<void *>(&Trap<N>)), ...);
}
template <size_t N> void Traps(void *(&vt)[N]) { FillTraps(vt, std::make_integer_sequence<unsigned, N>()); }

// ---- the string pool (0x10f9070): slot 5 interns, 6 / 7 count, 8 the text ------------------------------------------------
constexpr uint32_t kFirstHandle = 0x1000;
struct StringPool {
    std::deque<std::string> texts;
    std::vector<int> counts;
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
    Log("pool open", {flag}, s);
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
std::string HandleText(uint32_t h) { return Text(h >= kFirstHandle ? reinterpret_cast<const char *>(PoolText(nullptr, nullptr, h)) : nullptr); }
void *gPoolVt[10];
void *gPoolObject[1] = {gPoolVt};

// ---- counted fakes: the type-info record names, the pixel formats ------------------------------------------------------
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
void *gCfVt[12];
CountedFake gRecordNames[2];
CountedFake gFormats[4]; // by the manager's slot: 5 default, 6 masks, 7 luminance, 10 DXT
uint32_t FormatId(const void *p)
{
    for (uint32_t k = 0; k < 4; ++k)
        if (p == &gFormats[k]) return k + 1;
    return p ? 0xf0f0f0f0u : 0;
}

// ---- the persist reader: a typed stream -------------------------------------------------------------------------------
struct Item {
    char kind; // 'w' word, 'f' float, 'b' bool, 't' text
    uint32_t word;
    std::string text;
};
struct Stream {
    uint32_t versions[2] = {};
    std::vector<Item> items;
    size_t at = 0, versionAt = 0, cut = SIZE_MAX;
    uint32_t fill = 0, voids = 0;
    unsigned misaligned = 0;
} gStream;
std::string gLastText;
unsigned gSeen[5]; // the fakes reached: skip, get resource, save resource, copy texture, draw missing

const Item *Next(char kind)
{
    const size_t i = gStream.at++;
    if (i >= gStream.cut) return nullptr; // truncated: 0 / ""
    if (i >= gStream.items.size() || gStream.items[i].kind != kind) {
        ++gStream.misaligned; // the generator's model and the reads disagree
        return nullptr;
    }
    return &gStream.items[i];
}
uint32_t __fastcall RdTypeInfo(void *, void *, uint32_t *record)
{
    const size_t i = gStream.versionAt++;
    CountedFake *name = i < 2 ? &gRecordNames[i] : nullptr;
    if (name) ++name->refs;
    record[0] = P(name);
    record[1] = 0;
    record[2] = i < 2 ? gStream.versions[i] : 0;
    Log("read record", {static_cast<uint32_t>(i)});
    return P(record);
}
bool __fastcall RdBool(void *, void *)
{
    const Item *it = Next('b');
    Log("read bool", {static_cast<uint32_t>(gStream.at)});
    return it && it->word != 0;
}
uint32_t __fastcall RdUint32(void *, void *)
{
    const Item *it = Next('w');
    Log("read word", {static_cast<uint32_t>(gStream.at)});
    return it ? it->word : 0;
}
float __fastcall RdFloat(void *, void *)
{
    const Item *it = Next('f');
    Log("read float", {static_cast<uint32_t>(gStream.at)});
    return it ? FromBits(it->word) : 0.0f;
}
uint32_t __fastcall RdString(void *, void *, void *s)
{
    const Item *it = Next('t');
    {
        difftest::NotCounted quiet;
        gLastText = it ? it->text : "";
    }
    Log("read text", {static_cast<uint32_t>(gStream.at)});
    using FromCharFn = void(__thiscall *)(void *, const char *, uint32_t);
    reinterpret_cast<FromCharFn>((*static_cast<void ***>(s))[3])(s, gLastText.c_str(), static_cast<uint32_t>(gLastText.size()));
    return 1;
}
uint32_t Where(const void *p);
uint32_t __fastcall RdVoid(void *, void *, uint8_t *data, uint32_t size)
{
    Log("read bytes", {Where(data), size});
    const uint32_t base = gStream.fill + 13 * gStream.voids++;
    for (uint32_t i = 0; i < size; ++i) data[i] = static_cast<uint8_t>(base + i * 7);
    return 1;
}
uint32_t __fastcall RdSkip(void *, void *, uint32_t n)
{
    ++gSeen[0];
    Log("skip", {n});
    return 1;
}
void *gReaderVt[29];
void *gReaderObject[1] = {gReaderVt};

// ---- the persist writer -------------------------------------------------------------------------------------------------
uint32_t Sum(const uint8_t *data, uint32_t size)
{
    uint32_t h = 0x811c9dc5u;
    for (uint32_t i = 0; i < size; ++i) h = (h ^ data[i]) * 0x01000193u;
    return h;
}
std::string GzText(void *s)
{
    if (!s) return "<null>";
    using DataFn = const char *(__thiscall *)(void *);
    using LenFn = uint32_t(__thiscall *)(void *);
    void **vt = *static_cast<void ***>(s);
    const char *data = reinterpret_cast<DataFn>(vt[6])(s);
    return data ? std::string(data, reinterpret_cast<LenFn>(vt[7])(s)) : "<no data>";
}
uint32_t __fastcall WrUint8(void *, void *, uint8_t v)
{
    Log("write byte", {v});
    return 1;
}
uint32_t __fastcall WrUint32(void *, void *, uint32_t v)
{
    Log("write word", {v});
    return 1;
}
uint32_t __fastcall WrFloat(void *, void *, float v)
{
    Log("write float", {Bits(v)});
    return 1;
}
uint32_t __fastcall WrVoid(void *, void *, const uint8_t *data, uint32_t size)
{
    Log("write bytes", {Where(data), size, data ? Sum(data, size) : 0});
    return 1;
}
uint32_t __fastcall WrText(void *, void *, const char *text)
{
    Log("write text", {}, Text(text));
    return 1;
}
uint32_t __fastcall WrGzText(void *, void *, void *s)
{
    Log("write gz text", {}, GzText(s));
    return 1;
}
uint32_t __fastcall WrRecord(void *, void *, const uint32_t *record)
{
    Log("write record", {record[1], record[2]}, GzText(reinterpret_cast<void *>(static_cast<uintptr_t>(record[0]))));
    return 1;
}
void *gWriterVt[29];
void *gWriterObject[1] = {gWriterVt};

// ---- the levels: each side's own factory, for the resource manager's resources ----------------------------------------
using FactoryFn = void *(__cdecl *)();
FactoryFn gLevelFactory = nullptr;
template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void ***>(face))[slot])(face, a...);
}
void *__cdecl OurLevelFactory() { return cLevelInfo::Create(); }

// ---- the framework, the resource manager, the config --------------------------------------------------------------------
int32_t gManagerRefs = 0, gConfigRefs = 0;
uint32_t __fastcall SvcQi(void *, void *, uint32_t, void **out)
{
    if (out) *out = nullptr;
    return 0;
}
uint32_t __fastcall MgrAddRef(void *, void *) { return static_cast<uint32_t>(++gManagerRefs); }
uint32_t __fastcall MgrRelease(void *, void *) { return static_cast<uint32_t>(--gManagerRefs); }
uint32_t __fastcall MgrFindSegment(void *, void *, const uint32_t *key, void **out)
{
    Log("find segment", {key[0], key[1], key[2]});
    *out = nullptr;
    return 0;
}
uint32_t __fastcall MgrGetResource(void *, void *, const uint32_t *key, uint32_t iid, void **out, uint32_t a, uint32_t b)
{
    ++gSeen[1];
    Log("get resource", {key[0], key[1], key[2], key[3], iid, a, b}, gLastText);
    if (gLastText.rfind("missing", 0) == 0) return 0;
    if (gLastText.rfind("nullout", 0) == 0) {
        *out = nullptr;
        return 1;
    }
    uint8_t *const level = static_cast<uint8_t *>(gLevelFactory()) - kLevelVbase;
    Slot<uint32_t>(level, 1);
    Slot<void>(level, 15, static_cast<uint8_t>(gLastText.find("tex_b") == std::string::npos ? 1 : 0));
    const uint32_t old = Rd<uint32_t>(level, 0x20);
    const uint32_t handle = PoolOpen(nullptr, nullptr, P(gLastText.c_str()), 1);
    PoolUse(nullptr, nullptr, handle);
    if (old >= kFirstHandle) PoolClose(nullptr, nullptr, old);
    *reinterpret_cast<uint32_t *>(level + 0x20) = handle;
    *out = level;
    return 1;
}
uint8_t __fastcall MgrSave(void *, void *, uint8_t *level, const uint32_t *key, void *segment)
{
    ++gSeen[2];
    Log("save resource", {level ? Rd<uint32_t>(level, 0x2c) : 0xffffffffu, key[0], key[1], key[2], key[3], P(segment)},
        level ? HandleText(Rd<uint32_t>(level, 0x20)) : "");
    return 1;
}
void *gManagerVt[46];
void *gManagerObject[1] = {gManagerVt};

struct Config {
    uint32_t reduction = 0;
    bool reductionFound = false;
    uint8_t ignore = 0;
    bool ignoreFound = false;
} gConfig;
uint32_t __fastcall CfgAddRef(void *, void *) { return static_cast<uint32_t>(++gConfigRefs); }
uint32_t __fastcall CfgRelease(void *, void *) { return static_cast<uint32_t>(--gConfigRefs); }
bool __fastcall CfgGet(void *, void *, const GZServiceString *key, uint8_t *any)
{
    const std::string k = key ? key->c_str() : "<null>";
    Log("config", {}, k);
    if (k == "imageDataSizeReductionOnLoad" && gConfig.reductionFound) {
        std::memcpy(any + 4, &gConfig.reduction, 4);
        return true;
    }
    if (k == "ignoreImageDataSizeReductionOnLoad" && gConfig.ignoreFound) {
        any[4] = gConfig.ignore;
        return true;
    }
    return false;
}
void *gConfigVt[16];
void *gConfigObject[1] = {gConfigVt};

uint32_t __fastcall FwGetService(void *, void *, uint32_t service, uint32_t iid, void **out)
{
    Log("get service", {service, iid});
    if (service == 0x056b906e && iid == 0x656b8efc) {
        *out = gManagerObject;
        ++gManagerRefs;
        return 1;
    }
    if (service == 0x0ac1887e && iid == 0x4bb0d269) {
        *out = gConfigObject;
        ++gConfigRefs;
        return 1;
    }
    *out = nullptr; // StringKeyParser's key service among them: refused, so the keys are hashed
    return 0;
}
void *gFrameworkVt[11];
void *gFrameworkObject[1] = {gFrameworkVt};

// ---- the allocator service (GZServiceString) ---------------------------------------------------------------------------
int32_t gAllocated = 0;
void *__fastcall AlAlloc(void *, void *, uint32_t size)
{
    ++gAllocated;
    return HeapAlloc(GetProcessHeap(), 0, size);
}
void __fastcall AlFree(void *, void *, void *p)
{
    if (!p) return;
    --gAllocated;
    HeapFree(GetProcessHeap(), 0, p);
}
void *gAllocatorVt[8];
void *gAllocatorObject[1] = {gAllocatorVt};

// ---- graphics: the pixel formats manager, the image manipulator, the missing-texture drawing ---------------------------
void *__fastcall FmDefault(void *, void *)
{
    Log("format default", {});
    return &gFormats[0];
}
void *__fastcall FmMasks(void *, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    Log("format masks", {a, b, c, d, e});
    return &gFormats[1];
}
void *__fastcall FmLuminance(void *, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Log("format luminance", {a, b, c});
    return &gFormats[2];
}
void *__fastcall FmDxt(void *, void *, uint32_t a)
{
    Log("format dxt", {a});
    return &gFormats[3];
}
void *gFormatsVt[16];
void *gFormatsObject[1] = {gFormatsVt};

std::vector<uint32_t> TextureWords(void *t)
{
    if (!t) return {0};
    return {1, Slot<uint32_t>(t, 17, 0u), Slot<uint32_t>(t, 18, 0u), FormatId(Slot<void *>(t, 19))};
}
uint32_t __fastcall ManipCopy(void *, void *, void *from, void *to)
{
    std::vector<uint32_t> w = TextureWords(from);
    const std::vector<uint32_t> t = TextureWords(to);
    w.insert(w.end(), t.begin(), t.end());
    ++gSeen[3];
    Log("copy texture", w);
    return 1;
}
void *gManipVt[16];
void *gManipObject[1] = {gManipVt};

void __cdecl StandDrawMissing(void *surface, const cRZString *name)
{
    ++gSeen[4];
    Log("draw missing", {surface != nullptr}, name ? name->ToChar() : "<null>");
}

// ---- VirtualAlloc / VirtualFree: blocks from one arena (the same address on both sides) ---------------------------------
constexpr uint32_t kArenaBytes = 48u << 20;
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
    Log("VirtualAlloc", {a[0], size, a[2], a[3]});
    if (gArenaNext + size > kArenaBytes) return 0;
    uint8_t *block = gArena + gArenaNext;
    std::memset(block, 0, size);
    gArenaNext = (gArenaNext + size + 0xfff) & ~0xfffu;
    return P(block);
}
uint32_t OnVirtualFree(drawfx::Fake &, const uint32_t *a)
{
    Log("VirtualFree", {Where(reinterpret_cast<void *>(static_cast<uintptr_t>(a[0]))), a[1], a[2]});
    return 1;
}

// Heap blocks start filled (operator new's level pixels are not cleared: their bytes would be the heap's).
void FillAllocated(void *p, size_t bytes) { std::memset(p, 0x5c, bytes); }
void Ignore(void *, size_t) {}
void IgnoreFree(void *) {}
const difftest::HeapWatcher kFiller{&FillAllocated, &Ignore, &IgnoreFree};
struct HeapFill {
    HeapFill() { difftest::WatchHeap(&kFiller); }
    ~HeapFill() { difftest::WatchHeap(nullptr); }
    HeapFill(const HeapFill &) = delete;
};

// A pointer an out parameter was given: null, untouched, or its distance from the object.
uint32_t Rel(const void *out, const void *o)
{
    if (!out) return 0;
    if (P(out) == 0x5a5a5a5au) return 1;
    return 0x10000000u + (P(out) - P(o));
}

// Everything the tests put in the image's globals, put back when they end.
struct Fakes {
    HeapFill fill;
    struct Cell {
        void **at;
        void *saved;
    };
    std::vector<Cell> cells;
    impfx::Imports imp;
    shape::StandIn draw;
    void Set(difftest::Context &ctx, uint32_t rva, void *value)
    {
        void **at = ctx.image.At<void *>(rva);
        cells.push_back({at, *at});
        *at = value;
    }
    explicit Fakes(difftest::Context &ctx) : imp(ctx), draw(kDrawMissing, &nGZSceneGraph::DrawMissingTextureName, &StandDrawMissing)
    {
        Traps(gPoolVt);
        gPoolVt[5] = reinterpret_cast<void *>(&PoolOpen);
        gPoolVt[6] = reinterpret_cast<void *>(&PoolUse);
        gPoolVt[7] = reinterpret_cast<void *>(&PoolClose);
        gPoolVt[8] = reinterpret_cast<void *>(&PoolText);
        Traps(gCfVt);
        gCfVt[0] = reinterpret_cast<void *>(&CfQi);
        gCfVt[1] = reinterpret_cast<void *>(&CfAddRef);
        gCfVt[2] = reinterpret_cast<void *>(&CfRelease);
        Traps(gReaderVt);
        gReaderVt[4] = reinterpret_cast<void *>(&RdBool);
        gReaderVt[10] = reinterpret_cast<void *>(&RdUint32);
        gReaderVt[11] = reinterpret_cast<void *>(&RdFloat);
        gReaderVt[15] = reinterpret_cast<void *>(&RdVoid);
        gReaderVt[22] = reinterpret_cast<void *>(&RdString);
        gReaderVt[23] = reinterpret_cast<void *>(&RdSkip);
        gReaderVt[26] = reinterpret_cast<void *>(&RdTypeInfo);
        Traps(gWriterVt);
        gWriterVt[4] = reinterpret_cast<void *>(&WrUint8);
        gWriterVt[10] = reinterpret_cast<void *>(&WrUint32);
        gWriterVt[11] = reinterpret_cast<void *>(&WrFloat);
        gWriterVt[15] = reinterpret_cast<void *>(&WrVoid);
        gWriterVt[23] = reinterpret_cast<void *>(&WrText);
        gWriterVt[24] = reinterpret_cast<void *>(&WrGzText);
        gWriterVt[28] = reinterpret_cast<void *>(&WrRecord);
        Traps(gManagerVt);
        gManagerVt[0] = reinterpret_cast<void *>(&SvcQi);
        gManagerVt[1] = reinterpret_cast<void *>(&MgrAddRef);
        gManagerVt[2] = reinterpret_cast<void *>(&MgrRelease);
        gManagerVt[3] = reinterpret_cast<void *>(&MgrGetResource);
        gManagerVt[14] = reinterpret_cast<void *>(&MgrSave);
        gManagerVt[34] = reinterpret_cast<void *>(&MgrFindSegment);
        Traps(gConfigVt);
        gConfigVt[0] = reinterpret_cast<void *>(&SvcQi);
        gConfigVt[1] = reinterpret_cast<void *>(&CfgAddRef);
        gConfigVt[2] = reinterpret_cast<void *>(&CfgRelease);
        gConfigVt[9] = reinterpret_cast<void *>(&CfgGet);
        Traps(gFrameworkVt);
        gFrameworkVt[5] = reinterpret_cast<void *>(&FwGetService);
        Traps(gAllocatorVt);
        gAllocatorVt[3] = reinterpret_cast<void *>(&AlAlloc);
        gAllocatorVt[4] = reinterpret_cast<void *>(&AlFree);
        Traps(gFormatsVt);
        gFormatsVt[5] = reinterpret_cast<void *>(&FmDefault);
        gFormatsVt[6] = reinterpret_cast<void *>(&FmMasks);
        gFormatsVt[7] = reinterpret_cast<void *>(&FmLuminance);
        gFormatsVt[10] = reinterpret_cast<void *>(&FmDxt);
        Traps(gManipVt);
        gManipVt[8] = reinterpret_cast<void *>(&ManipCopy);
        Set(ctx, kStringPoolCell, gPoolObject);
        Set(ctx, kFrameworkCell, gFrameworkObject);
        Set(ctx, kAllocatorCell, gAllocatorObject);
        Set(ctx, kAllocatorGuard, reinterpret_cast<void *>(static_cast<uintptr_t>(0x80000000u))); // initialised
        Set(ctx, kFormatsCell, gFormatsObject);
        Set(ctx, kManipCell, gManipObject);
        Set(ctx, kKeysCell, nullptr);
        IMPFX_ADD(imp, VirtualAlloc).On(&OnVirtualAlloc);
        IMPFX_ADD(imp, VirtualFree).On(&OnVirtualFree);
        gArena = static_cast<uint8_t *>(VirtualAlloc(nullptr, kArenaBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    }
    ~Fakes()
    {
        VirtualFree(gArena, 0, MEM_RELEASE);
        gArena = nullptr;
        for (size_t i = cells.size(); i-- > 0;) *cells[i].at = cells[i].saved;
    }
    Fakes(const Fakes &) = delete;
    void BeginSide(difftest::Context &ctx, bool game)
    {
        imp.BeginSide();
        gStrings.texts.clear();
        gStrings.counts.clear();
        gManagerRefs = gConfigRefs = gAllocated = 0;
        gArenaNext = 0;
        for (CountedFake &n : gRecordNames) n = {gCfVt, 0};
        for (CountedFake &n : gFormats) n = {gCfVt, 0};
        gLevelFactory = game ? ctx.Original<FactoryFn>(kLevelFactory) : &OurLevelFactory;
        *ctx.image.At<void *>(kKeysCell) = nullptr; // the key parser's service: none, each side (restored at the end)
    }
};

void EndCounts(Run &run)
{
    std::vector<uint32_t> counts;
    for (int c : gStrings.counts) counts.push_back(static_cast<uint32_t>(c));
    run.Add("pool counts", counts);
    std::vector<uint32_t> refs = {static_cast<uint32_t>(gManagerRefs), static_cast<uint32_t>(gConfigRefs),
                                  static_cast<uint32_t>(gAllocated)};
    for (const CountedFake &n : gRecordNames) refs.push_back(static_cast<uint32_t>(n.refs));
    for (const CountedFake &n : gFormats) refs.push_back(static_cast<uint32_t>(n.refs));
    run.Add("service, record and format refs", refs);
}

// ---- the object -------------------------------------------------------------------------------------------------------
// A level: its words (the vtables and the vbptr aside), its pixels by where they are and their bytes, its name.
void DumpLevel(Run &run, const uint8_t *o)
{
    if (!o) {
        run.Add("level", {0});
        return;
    }
    std::vector<uint32_t> w;
    for (uint32_t off = 4; off < 0x1c; off += 4) w.push_back(Rd<uint32_t>(o, off));
    for (uint32_t off = 0x20; off < 0x44; off += 4) w.push_back(off == 0x28 || off == 0x3c ? Rd<uint8_t>(o, off) : Rd<uint32_t>(o, off));
    const uint8_t *data = Rd<const uint8_t *>(o, 0x44);
    const uint32_t size = Rd<uint32_t>(o, 0x48);
    w.insert(w.end(), {Where(data), size, Rd<uint32_t>(o, 0x4c), data ? Sum(data, size) : 0});
    run.Add("level", w, HandleText(Rd<uint32_t>(o, 0x20)));
}

uint32_t Entries(const uint8_t *o)
{
    return static_cast<uint32_t>(Rd<uint32_t>(o, 0x3c) - Rd<uint32_t>(o, 0x38)) / static_cast<uint32_t>(sizeof(ImageEntry));
}

// The image: its words (+0x2c is a byte), the name, each entry's colour, scale and levels.
void Dump(Run &run, const uint8_t *o, const char *what)
{
    const uint32_t entries = Entries(o);
    const cRZString *name = reinterpret_cast<const cRZString *>(o + 0x48);
    run.Add(what, {Rd<uint32_t>(o, 8), Rd<uint32_t>(o, 0x24), Rd<uint32_t>(o, 0x28), Rd<uint8_t>(o, 0x2c), Rd<uint32_t>(o, 0x30),
                   Rd<uint32_t>(o, 0x34), entries, Rd<uint32_t>(o, 0x44), static_cast<uint32_t>(name->mString.size()), Rd<uint32_t>(o, 0x68)},
            Text(name->ToChar()) + " " + HandleText(Rd<uint32_t>(o, 0x24)));
    const ImageEntry *e = Rd<const ImageEntry *>(o, 0x38);
    for (uint32_t k = 0; k < entries && k < 8; ++k) {
        const uint32_t levels = static_cast<uint32_t>(e[k].mLevels.size());
        run.Add("entry", {k, Bits(e[k].mColour[0]), Bits(e[k].mColour[1]), Bits(e[k].mColour[2]), Bits(e[k].mColour[3]),
                          Bits(e[k].mScale), levels});
        for (uint32_t j = 0; j < levels && j < 16; ++j) DumpLevel(run, reinterpret_cast<const uint8_t *>(e[k].mLevels[j].Get()));
    }
}

// The slots through the object's own vtables (+0 cIImageData, +0x6c the virtual base).
void Probe(Run &run, uint8_t *o, std::mt19937 &rng, bool adapters)
{
    void *vb = o + kFace;
    run.Add("held", {Slot<uint32_t>(o, 1)}); // a count of our own, so the probe's releases never reach 0
    const uint32_t iids[] = {cIImageData::kIid, cISGResource::kIid, cLevelInfo::kIidSelf, 0x5b0aaa74, 0xbb11a53e, 0x12345678, 0};
    for (uint32_t iid : iids) {
        void *out = reinterpret_cast<void *>(0x5a5a5a5a);
        const bool nullOut = rng() % 6 == 0;
        const uint32_t before = Rd<uint32_t>(o, 8);
        const bool r = adapters ? cImageDataAdapters::QueryInterface(reinterpret_cast<cImageData *>(o), nullptr, iid,
                                                                     nullOut ? nullptr : &out)
                                : Slot<bool>(o, 0, iid, nullOut ? nullptr : &out);
        const uint32_t after = Rd<uint32_t>(o, 8);
        run.Add("qi", {iid, r, nullOut ? 2u : Rel(out, o), before, after});
        if (r && !nullOut)
            for (uint32_t k = before; k < after; ++k) static_cast<cIGZUnknown *>(out)->Release();
    }
    run.Add("resource", {Slot<uint32_t>(o, 4) - P(o), Slot<uint32_t>(o, 5) - P(o)});
    run.Add("records", {Slot<uint32_t>(vb, 18), Slot<uint32_t>(vb, 19)});
    cPersistTypeInfo info;
    std::memset(&info, 0xa5, sizeof(info));
    if (adapters)
        cImageDataAdapters::GetTypeInfo(vb, nullptr, &info);
    else
        Slot<cPersistTypeInfo *>(vb, 4, &info);
    const std::string name = info.mName ? reinterpret_cast<const cRZString *>(info.mName)->ToChar() : "<null>";
    const uint32_t count = info.mName ? info.mName->AddRef() : 0;
    if (info.mName) {
        info.mName->Release();
        info.mName->Release();
    }
    run.Add("type info", {info.mWord4, info.mVersion, count}, name);
    run.Add("owner info", {Slot<uint32_t>(vb, 6) - P(o)});
    if (Entries(o) != 0)
        run.Add("pixel format",
                {FormatId(adapters ? cImageDataAdapters::PixelFormat(reinterpret_cast<cImageData *>(o), nullptr) : Slot<void *>(o, 33))});
}

using CtorFn = void *(__fastcall *)(void *, void *, uint32_t);
using DtorFn = void(__fastcall *)(void *, void *);
using MipFn = uint32_t(__cdecl *)(uint32_t, int32_t, uint32_t *, uint32_t *);
using BuildFn = void(__cdecl *)(void *, int32_t, int32_t, uint32_t, int32_t);

const int32_t kSizes[] = {0, 1, 2, 3, 4, 5, 6, 8, 16, 32, 33, 64, 100, 128};
int32_t PickSize(std::mt19937 &rng, int32_t most)
{
    for (;;) {
        const int32_t s = kSizes[rng() % (sizeof(kSizes) / sizeof(kSizes[0]))];
        if (s <= most) return s;
    }
}
const uint32_t kLevelCounts[] = {0, 1, 2, 3, 4, 5, 7, 9, 12, 0xffffffffu};
uint32_t PickLevels(std::mt19937 &rng) { return kLevelCounts[rng() % (sizeof(kLevelCounts) / sizeof(kLevelCounts[0]))]; }
const uint32_t kFloats[] = {0x3f800000u, 0x00000000u, 0x3f000000u, 0xc0000000u, 0x7149f2cau, 0xbf800000u, 0x40000000u,
                            0x3b800000u, 0x437f0000u, 0x7fc00000u};
float PickFloat(std::mt19937 &rng) { return FromBits(kFloats[rng() % (sizeof(kFloats) / sizeof(kFloats[0]))]); }

} // namespace

DIFF_TEST(SG_ImageData_Construct, 0xaafd60)
{
    Fakes fakes(ctx);
    unsigned failed = 0;
    for (uint32_t seed = 1; seed <= 60 && failed < 3; ++seed) {
        Run runs[2];
        for (int side = 0; side < 2; ++side) {
            const bool game = side == 0;
            fakes.BeginSide(ctx, game);
            gRun = &runs[side];
            std::mt19937 rng(seed);
            const int mode = static_cast<int>(seed % 3); // 0 deleting in place, 1 the RVA / our adapter, 2 adapter on a game object
            alignas(8) uint8_t buffer[cImageData::kSize];
            std::memset(buffer, 0xcd, sizeof(buffer));
            difftest::AllocationCounter counter;
            const CtorFn ctor = game || mode == 2 ? ctx.Original<CtorFn>(kImageCtor)
                                                  : reinterpret_cast<CtorFn>(&cImageData_Constructor);
            void *const r = ctor(buffer, nullptr, 1);
            std::vector<uint32_t> w = {r == buffer};
            for (uint32_t off = 8; off < cImageData::kSize; off += 4)
                if (off != 0x20 && off != 0x48 && off != 0x6c) w.push_back(off == 0x2c ? Rd<uint8_t>(buffer, off) : Rd<uint32_t>(buffer, off));
            runs[side].Add("constructed", w);
            const bool adapters = !game && mode == 2;
            Probe(runs[side], buffer, rng, adapters);
            if (rng() % 3 != 0) {
                Slot<void>(buffer, 6, PickSize(rng, 16), PickSize(rng, 16), PickLevels(rng), static_cast<int32_t>(rng() % 12) - 1,
                           PickFloat(rng), static_cast<uint32_t>(rng() % 4));
                Slot<void>(buffer, 13, rng() % 2 ? "an_image_name_well_past_fifteen_bytes" : "img");
                Dump(runs[side], buffer, "after init");
                Probe(runs[side], buffer, rng, adapters);
            }
            if (mode == 0)
                Slot<void *>(buffer + 4, 12, 0u); // the deleting destructor, in place
            else if (game)
                ctx.Original<DtorFn>(kImageDtor)(buffer, nullptr);
            else
                cImageDataAdapters::Destruct(reinterpret_cast<cImageData *>(buffer), nullptr);
            runs[side].Add("destroyed", {static_cast<uint32_t>(counter.Blocks())});
            EndCounts(runs[side]);
            gRun = nullptr;
        }
        if (!Compare(ctx, "seed", seed, runs[0], runs[1])) ++failed;
    }
}

DIFF_TEST(SG_ImageData_MipChainBytes, 0xab0250)
{
    Fakes fakes(ctx);
    const uint32_t sizes[] = {0, 1, 2, 3, 4, 5, 6, 8, 12, 16, 64, 100, 256, 1024, 0x10000};
    const uint32_t levels[] = {0, 1, 2, 3, 5, 8, 17, 0xffffffffu};
    unsigned failed = 0;
    for (int32_t format = -1; format <= 10 && failed < 3; ++format)
        for (uint32_t n : levels)
            for (uint32_t wi = 0; wi < 15 && failed < 3; ++wi)
                for (uint32_t hi = 0; hi < 15 && failed < 3; hi += 2) {
                    uint32_t w[2] = {sizes[wi], sizes[wi]}, h[2] = {sizes[(hi + wi) % 15], sizes[(hi + wi) % 15]};
                    fakes.BeginSide(ctx, true);
                    const uint32_t a = ctx.Original<MipFn>(kMipChainBytes)(n, format, &w[0], &h[0]);
                    fakes.BeginSide(ctx, false);
                    const uint32_t b = cImageData::MipChainBytes(n, format, &w[1], &h[1]);
                    ++ctx.cases;
                    if (a != b || w[0] != w[1] || h[0] != h[1]) {
                        ctx.Fail("levels %u format %d %ux%u: original %x (%ux%u), ours %x (%ux%u)", n, format, sizes[wi],
                                 sizes[(hi + wi) % 15], a, w[0], h[0], b, w[1], h[1]);
                        ++failed;
                    }
                }
}

namespace {

Run RunInit(difftest::Context &ctx, Fakes &fakes, bool game, uint32_t seed)
{
    Run run;
    fakes.BeginSide(ctx, game);
    gRun = &run;
    std::mt19937 rng(seed);
    difftest::AllocationCounter counter;
    uint8_t *const o = static_cast<uint8_t *>(game ? ctx.Original<FactoryFn>(kImageFactory)() : static_cast<void *>(cImageData::Create()));
    run.Add("made", {Rd<uint32_t>(o, 8)});
    Slot<uint32_t>(o, 1);
    char mark[64];
    for (int round = 0; round < 3; ++round) {
        std::snprintf(mark, sizeof(mark), "%s side, seed %u, round %d", game ? "game" : "our", seed, round);
        ctx.Mark(mark);
        const int32_t w = PickSize(rng, 128), h = PickSize(rng, 128), format = static_cast<int32_t>(rng() % 12) - 1;
        const uint32_t levels = PickLevels(rng), entries = rng() % 4;
        const float scale = PickFloat(rng);
        Slot<void>(o, 6, w, h, levels, format, scale, entries);
        run.Add("init", {static_cast<uint32_t>(w), static_cast<uint32_t>(h), levels, static_cast<uint32_t>(format), entries});
        Dump(run, o, "after init");
        if (Entries(o) != 0) {
            const uint32_t k = rng() % Entries(o);
            const int32_t w2 = PickSize(rng, 64), h2 = PickSize(rng, 64), format2 = static_cast<int32_t>(rng() % 12) - 1;
            const uint32_t levels2 = PickLevels(rng);
            ImageEntry *const e = Rd<ImageEntry *>(o, 0x38) + k;
            if (game)
                ctx.Original<BuildFn>(kBuildLevels)(e, w2, h2, levels2, format2);
            else
                cImageData::BuildLevels(e, w2, h2, levels2, format2);
            run.Add("build levels", {k, static_cast<uint32_t>(w2), static_cast<uint32_t>(h2), levels2, static_cast<uint32_t>(format2)});
            Dump(run, o, "after build");
            run.Add("pixel format", {FormatId(Slot<void *>(o, 33))});
        }
        if (rng() % 2) {
            uint8_t *clone = nullptr;
            Slot<void>(o, 3, &clone);
            run.Add("clone", {clone != nullptr, clone != o});
            if (clone) {
                Dump(run, clone, "clone");
                run.Add("clone released", {Slot<uint32_t>(clone, 2)});
            }
        }
        Probe(run, o, rng, false);
    }
    run.Add("released", {Slot<uint32_t>(o, 2)});
    counter.Stop();
    run.Add("heap blocks left", {static_cast<uint32_t>(counter.Blocks())});
    EndCounts(run);
    gRun = nullptr;
    return run;
}

} // namespace

DIFF_TEST(SG_ImageData_Init, 0xab1890)
{
    Fakes fakes(ctx);
    unsigned failed = 0;
    for (uint32_t seed = 1; seed <= 120 && failed < 3; ++seed) {
        const Run a = RunInit(ctx, fakes, true, seed);
        const Run b = RunInit(ctx, fakes, false, seed);
        if (!Compare(ctx, "seed", seed, a, b)) ++failed;
    }
}

namespace {

// The test's model of the reads (MipChainBytes' halving of the size, BuildLevels' count), so the generated stream holds
// what Read takes in order: the misalignment count catches any disagreement.
bool PowerOfTwoOrZero(uint32_t x) { return (x & (x - 1)) == 0; }
void ModelHalve(uint32_t levels, uint32_t &w, uint32_t &h)
{
    for (uint32_t n = 0; n < levels; ++n) {
        if (!PowerOfTwoOrZero(w) || !PowerOfTwoOrZero(h) || (w < 2 && h < 2)) return;
        w = w >> 1 ? w >> 1 : 1;
        h = h >> 1 ? h >> 1 : 1;
    }
}
uint32_t ModelCount(uint32_t w, uint32_t h, uint32_t levels)
{
    for (uint32_t made = 1;; ++made) {
        if (made >= levels || !PowerOfTwoOrZero(w) || !PowerOfTwoOrZero(h) || (w < 2 && h < 2)) return made;
        w = w >> 1 ? w >> 1 : 1;
        h = h >> 1 ? h >> 1 : 1;
    }
}

struct Coverage {
    unsigned path1 = 0, path2 = 0, failed = 0, old = 0, external = 0, past = 0, negative = 0, truncated = 0, ignored = 0,
             oldSkip = 0;
} gCover;

void MakeStream(std::mt19937 &rng)
{
    difftest::NotCounted quiet;
    gStream = {};
    auto word = [](uint32_t v) { gStream.items.push_back({'w', v, {}}); };
    auto text = [](const char *t) { gStream.items.push_back({'t', 0, t}); };
    const uint32_t versionPicks[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0xffffffffu, 9, 9, 10};
    const uint32_t v = versionPicks[rng() % (sizeof(versionPicks) / sizeof(versionPicks[0]))];
    gStream.versions[0] = v;
    gStream.versions[1] = rng() % 4;
    gStream.fill = rng();
    uint32_t levels = PickLevels(rng);
    const uint32_t entries = rng() % 4;
    // The config.
    gConfig = {};
    const uint32_t reductions[] = {0, 1, 2, 3, 4, 5, 8, 33, 0xffffffffu, levels, levels - 1, levels + 1};
    gConfig.reduction = reductions[rng() % (sizeof(reductions) / sizeof(reductions[0]))];
    gConfig.reductionFound = rng() % 5 != 0;
    gConfig.ignoreFound = rng() % 4 == 0;
    gConfig.ignore = static_cast<uint8_t>(rng() % 3);
    const bool ignored = gConfig.ignoreFound && gConfig.ignore != 0;
    const uint32_t reduction = ignored || !gConfig.reductionFound ? 0 : gConfig.reduction;
    uint32_t w = static_cast<uint32_t>(PickSize(rng, v < 9 ? 64 : 128)), h = static_cast<uint32_t>(PickSize(rng, v < 9 ? 64 : 128));
    int32_t format = static_cast<int32_t>(rng() % 12) - 1;
    // ORIGINAL-BUG (src/sg/cImageData.cpp, ReadLevels): an old stream (version < 9) whose single level is read at a
    // skipped size and then copied down reads past the block; the bytes past it are the heap's, so keep such images small.
    // The exception: a DXT image whose skipped-to level is 4x4 or 2x2 is one block at every smaller size too, so the
    // block is exactly the level and the read is sound (this is the only way to reach the one-level path's resize with
    // levels skipped).
    const bool dxtSkip = v < 9 && reduction >= 6 && rng() % 2;
    if (dxtSkip) {
        const int32_t dxt[] = {cLevelInfo::kFormatDxt1, cLevelInfo::kFormatDxt3, cLevelInfo::kFormatDxt5};
        format = dxt[rng() % 3];
        levels = 5 + rng() % 2; // 64 -> 4 or 2
        w = h = 64;
        ++gCover.oldSkip;
    }
    const bool path2 = !(reduction < levels);
    if (v < 9 && path2 && !dxtSkip) {
        if (w > 32) w = 32;
        if (h > 32) h = 32;
    }
    text(rng() % 2 ? "image_resource" : "Image_Resource_well_past_fifteen");
    word(w);
    word(h);
    word(static_cast<uint32_t>(format));
    word(levels);
    gStream.items.push_back({'f', Bits(PickFloat(rng)), {}});
    word(entries);
    word(rng() % 5 == 0 ? rng() : rng() % 4);
    const uint32_t pixels = w * h;
    uint32_t per = 1;
    if (!path2) {
        const uint32_t skip = pixels > 0x400 ? reduction : 0;
        ModelHalve(skip, w, h);
        per = ModelCount(w, h, levels - skip);
        ++gCover.path1;
    } else {
        ++gCover.path2;
    }
    if (ignored) ++gCover.ignored;
    if (v > 6) text(rng() % 3 ? "the_image" : "");
    if (v < 9) ++gCover.old;
    const char *names[] = {"tex_a", "tex_b", "grp!tex_d", "missing_c", "nullout_e", ""};
    bool failed = false;
    for (uint32_t e = 0; e < entries && !failed; ++e) {
        if (v < 9) {
            word(rng());
        } else {
            int32_t records = static_cast<int32_t>(rng() % (per + 3));
            if (rng() % 8 == 0) {
                records = rng() % 2 ? -1 : INT_MIN;
                ++gCover.negative;
            }
            word(static_cast<uint32_t>(records));
            for (int32_t i = 0; i < records && !failed; ++i) {
                const bool external = rng() % 4 == 0;
                gStream.items.push_back({'b', external ? 1u : 0u, {}});
                if (static_cast<uint32_t>(i) >= per) ++gCover.past;
                if (!external) {
                    word(rng() % 3 ? rng() % 0x100 : rng());
                    continue;
                }
                ++gCover.external;
                const char *name = names[rng() % 6];
                text(name);
                if (static_cast<uint32_t>(i) < per && (name[0] == 'm' || name[0] == 'n')) failed = true;
            }
        }
        if (v >= 6) word(rng() % 3 ? rng() : 0xffffffffu);
        if (v > 7) gStream.items.push_back({'f', Bits(PickFloat(rng)), {}});
    }
    if (failed) ++gCover.failed;
    if (rng() % 5 == 0) {
        gStream.cut = rng() % (gStream.items.size() + 1);
        ++gCover.truncated;
    }
}

void SetColours(Run &run, uint8_t *o, std::mt19937 &rng)
{
    if (Entries(o) == 0 || rng() % 3 != 0) return;
    float colour[4];
    for (float &c : colour) c = rng() % 2 ? static_cast<float>(rng() % 1000) / 999.0f : PickFloat(rng);
    Slot<void>(o, 11, static_cast<const float *>(colour));
    Slot<void>(o, 12, PickFloat(rng));
    Slot<void>(o, 10, PickFloat(rng));
    run.Add("colours set", {Bits(colour[0]), Bits(colour[1]), Bits(colour[2]), Bits(colour[3])});
}

void Write(uint8_t *o, bool adapters)
{
    if (adapters)
        cImageDataAdapters::Write(o + kFace, nullptr, reinterpret_cast<ifaces::cIGZPersistWriter *>(gWriterObject));
    else
        Slot<void>(o + kFace, 5, static_cast<void *>(gWriterObject));
}

Run RunReadWrite(difftest::Context &ctx, Fakes &fakes, bool game, uint32_t seed, unsigned &misaligned)
{
    Run run;
    fakes.BeginSide(ctx, game);
    gRun = &run;
    std::mt19937 rng(seed);
    const bool adapters = !game && seed % 3 == 0; // ours through the adapters on a game-built object
    difftest::AllocationCounter counter;
    uint8_t *const o = static_cast<uint8_t *>(game || adapters ? ctx.Original<FactoryFn>(kImageFactory)()
                                                               : static_cast<void *>(cImageData::Create()));
    Slot<uint32_t>(o, 1);
    char mark[64];
    for (int round = 0; round < 3; ++round) {
        std::snprintf(mark, sizeof(mark), "%s side, seed %u, round %d", game ? "game" : "our", seed, round);
        ctx.Mark(mark);
        MakeStream(rng);
        if (adapters)
            cImageDataAdapters::Read(o + kFace, nullptr, reinterpret_cast<ifaces::cIGZPersistReader *>(gReaderObject));
        else
            Slot<void>(o + kFace, 3, static_cast<void *>(gReaderObject));
        if (gStream.cut == SIZE_MAX) misaligned += gStream.misaligned;
        run.Add("read taken", {static_cast<uint32_t>(gStream.at), static_cast<uint32_t>(gStream.versionAt),
                               static_cast<uint32_t>(gRecordNames[0].refs), static_cast<uint32_t>(gRecordNames[1].refs),
                               static_cast<uint32_t>(counter.Blocks())});
        Dump(run, o, "after read");
        if (Entries(o) > 1 && rng() % 2) { // Read leaves entry 0 current (its Init); Write writes the index
            const uint32_t current = rng() % Entries(o);
            Slot<void>(o, 7, current);
            run.Add("current", {current});
            // The current entry one level shorter than the rest (DropTopLevels, slot 15), so Write's per-entry level
            // count (the current entry's: ORIGINAL-BUG) differs from the levels that follow it.
            if (Slot<uint32_t>(o, 31) >= 2 && rng() % 2) {
                Slot<void>(o, 15, 1u);
                run.Add("dropped", {Slot<uint32_t>(o, 31)});
            }
        }
        SetColours(run, o, rng);
        if (Entries(o) != 0) Write(o, adapters);
        if (Entries(o) != 0 && rng() % 3 == 0) {
            uint8_t *clone = nullptr;
            Slot<void>(o, 3, &clone);
            Write(clone, false);
            run.Add("clone released", {Slot<uint32_t>(clone, 2)});
        }
    }
    Probe(run, o, rng, adapters);
    run.Add("released", {Slot<uint32_t>(o, 2)});
    counter.Stop();
    run.Add("heap blocks left", {static_cast<uint32_t>(counter.Blocks())});
    EndCounts(run);
    gRun = nullptr;
    return run;
}

} // namespace

DIFF_TEST(SG_ImageData_ReadWrite, 0xab23d0)
{
    Fakes fakes(ctx);
    unsigned failed = 0, misaligned = 0;
    gCover = {};
    for (unsigned &n : gSeen) n = 0;
    for (uint32_t seed = 1; seed <= 240 && failed < 3; ++seed) {
        const Run a = RunReadWrite(ctx, fakes, true, seed, misaligned);
        const Run b = RunReadWrite(ctx, fakes, false, seed, misaligned);
        if (!Compare(ctx, "seed", seed, a, b)) ++failed;
    }
    if (misaligned != 0) ctx.Fail("%u reads disagreed with the generated streams (the test's model is wrong)", misaligned);
    // Every guard reached (counted on both sides, so twice the streams).
    const unsigned *const counts[] = {&gCover.path1, &gCover.path2, &gCover.failed, &gCover.old, &gCover.external,
                                      &gCover.past, &gCover.negative, &gCover.truncated, &gCover.ignored,
                                      &gCover.oldSkip};
    for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i)
        if (*counts[i] == 0) ctx.Fail("coverage: case %zu never generated", i);
    for (size_t i = 0; i < 5; ++i)
        if (gSeen[i] == 0) ctx.Fail("coverage: fake %zu (skip, get / save resource, copy, draw missing) never reached", i);
}

// ---- the pixel slots, fills, RegenerateMips and SetFormat (lane TN) ---------------------------------------------------------
//
// SG_ImageData_Slot*: slots 14 (0xab1680), 16 (0xab03c0), 17 (0xab0e50), 18 (0xab08b0), 19 (0xab36d0), 20 (0xab3390), 21
// (0xab3530), 38 (0xab2150), 39 (0xab1ca0), 40 (0xab1ec0) through the vtable of an object from each side's factory: random
// images (format, size, level count, entries, current entry) with random bytes in every level, then the slot over random
// arguments with colours from the edge values (the clamp's INT_MIN case, NaN, huge, negative); after each the object
// (every level's bytes), what the read slots answered and the image manipulator fake's calls (CopySurface slot 4 and
// ComputeNormalMap slot 13 log their surfaces: the bytes, pitch, size, format).
namespace {

std::vector<uint32_t> SurfaceWords(const void *s)
{
    const uint8_t *impl = s ? Rd<const uint8_t *>(s, 8) : nullptr;
    if (!impl) return {0};
    const uint8_t *bits = Rd<const uint8_t *>(impl, 0x14);
    return {1, Where(bits), Rd<uint32_t>(impl, 0x18), Rd<uint32_t>(impl, 0x1c), Rd<uint32_t>(impl, 0x20),
            Rd<uint32_t>(impl, 0x24), FormatId(Rd<const void *>(impl, 0x28)),
            0};
}
bool __fastcall ManipCopySurface(void *, void *, void *from, void *to)
{
    std::vector<uint32_t> w = SurfaceWords(from);
    const std::vector<uint32_t> t = SurfaceWords(to);
    w.insert(w.end(), t.begin(), t.end());
    Log("copy surface", w);
    return true;
}
bool __fastcall ManipNormalMap(void *, void *, void *from, void *to, float amplitude, bool occlusion)
{
    std::vector<uint32_t> w = {Bits(amplitude), occlusion};
    for (const void *tex : {from, to}) {
        const uint8_t *impl = tex ? Rd<const uint8_t *>(tex, 8) : nullptr;
        if (!impl) {
            w.push_back(0);
            continue;
        }
        const uint32_t *begin = Rd<const uint32_t *>(impl, 8), *end = Rd<const uint32_t *>(impl, 0xc);
        w.push_back(static_cast<uint32_t>(end - begin));
        for (const uint32_t *l = begin; l != end; ++l) {
            const std::vector<uint32_t> s = SurfaceWords(reinterpret_cast<const void *>(static_cast<uintptr_t>(*l)));
            w.insert(w.end(), s.begin(), s.end());
        }
    }
    Log("normal map", w);
    return true;
}

const uint32_t kColourBits[] = {0x3f800000u, 0x00000000u, 0x3f000000u, 0xc7c00000u /* -98304 */, 0xc7bfffffu, 0xc7c00001u,
                                0x7fc00000u, 0x7f800000u, 0xff800000u, 0x437f0000u, 0x43800000u, 0x3b800000u,
                                0xbf800000u, 0x477fff00u, 0x80000000u, 0x3f7fffffu, 0x3efffffeu, 0x7149f2cau};
float PickColour(std::mt19937 &rng)
{
    if (rng() % 3 == 0) return static_cast<float>(rng() % 1000) / 640.0f - 0.2f;
    return FromBits(kColourBits[rng() % (sizeof(kColourBits) / sizeof(kColourBits[0]))]);
}

const int32_t kSlotFormats[] = {1, 1, 1, 2, 2, 3, 3, 4, 5, 6, 7, 8, 9, 0, 10, -1};
const uint32_t kSlotLevels[] = {1, 2, 3, 4, 0xffffffffu};

// One image, ten operations of slot `slot`.
Run RunSlots(difftest::Context &ctx, Fakes &fakes, bool game, uint32_t seed, unsigned slot)
{
    Run run;
    fakes.BeginSide(ctx, game);
    gManipVt[4] = reinterpret_cast<void *>(&ManipCopySurface);
    gManipVt[13] = reinterpret_cast<void *>(&ManipNormalMap);
    gRun = &run;
    std::mt19937 rng(seed * 7919u + slot);
    difftest::AllocationCounter counter;
    uint8_t *const o = static_cast<uint8_t *>(game ? ctx.Original<FactoryFn>(kImageFactory)() : static_cast<void *>(cImageData::Create()));
    Slot<uint32_t>(o, 1);
    const int32_t size = 1 << (rng() % 6);
    const int32_t w = size, h = rng() % 3 == 0 ? 1 << (rng() % 5) : size;
    const uint32_t levels = kSlotLevels[rng() % 5];
    // The pixel slots want a format they act on most of the time; SetFormat wants any.
    const int32_t format = slot == 16 ? kSlotFormats[rng() % 16] : kSlotFormats[rng() % 7];
    const uint32_t entries = 1 + rng() % 3;
    Slot<void>(o, 6, w, h, levels, format, PickFloat(rng), entries);
    {
        difftest::NotCounted quiet;
        const ImageEntry *e = Rd<const ImageEntry *>(o, 0x38);
        for (uint32_t k = 0; k < Entries(o); ++k)
            for (size_t j = 0; j < e[k].mLevels.size(); ++j) {
                uint8_t *data = Rd<uint8_t *>(e[k].mLevels[j].Get(), 0x44);
                const uint32_t bytes = Rd<uint32_t>(e[k].mLevels[j].Get(), 0x48);
                for (uint32_t b = 0; b < bytes; ++b) data[b] = static_cast<uint8_t>(rng());
            }
    }
    Dump(run, o, "start");
    bool unsafe = false; // a failed SetFormat leaves the format changed over the old levels
    char mark[64];
    for (int step = 0; step < 10; ++step) {
        std::snprintf(mark, sizeof(mark), "%s side, seed %u, slot %u, step %d", game ? "game" : "our", seed, slot, step);
        ctx.Mark(mark);
        *reinterpret_cast<uint32_t *>(o + 0x44) = rng() % Entries(o);
        const ImageEntry &cur = Rd<const ImageEntry *>(o, 0x38)[Rd<uint32_t>(o, 0x44)];
        const int32_t fmt = static_cast<int32_t>(Rd<uint32_t>(o, 0x30));
        const bool pixels = !(unsafe && fmt >= 1 && fmt <= 3);
        const uint32_t count = static_cast<uint32_t>(cur.mLevels.size());
        const uint32_t lv = count ? rng() % count : 0;
        int32_t x = 0, y = 0;
        if (count) {
            const uint8_t *level = reinterpret_cast<const uint8_t *>(cur.mLevels[lv].Get());
            x = static_cast<int32_t>(rng() % (std::max)(1u, Rd<uint32_t>(level, 0x2c)));
            y = static_cast<int32_t>(rng() % (std::max)(1u, Rd<uint32_t>(level, 0x30)));
        }
        const float colour[4] = {PickColour(rng), PickColour(rng), PickColour(rng), PickColour(rng)};
        float out[4];
        for (float &f : out) f = FromBits(0x5a5a5a5au);
        // a fill over a size that is not a whole number of pixels would run on
        bool whole = true;
        for (size_t j = 0; j < cur.mLevels.size(); ++j) {
            const uint32_t bytes = Rd<uint32_t>(cur.mLevels[j].Get(), 0x48);
            if ((fmt == 1 && bytes % 4) || (fmt == 2 && bytes % 3)) whole = false;
        }
        const int32_t fillLevel = rng() % 3 == 0 ? -1 : static_cast<int32_t>(lv);
        const int32_t level = static_cast<int32_t>(lv);
        const bool ok = pixels && count;
        switch (slot) {
        case 14:
            if (ok) Slot<void>(o, 14);
            break;
        case 16: {
            const int32_t nf = kSlotFormats[rng() % 16];
            const bool r = Slot<bool>(o, 16, nf);
            run.Add("set format", {static_cast<uint32_t>(nf), r});
            if (!r && nf != fmt) unsafe = true;
            if (r && nf != fmt) unsafe = false;
            break;
        }
        case 17:
            if (ok && whole) Slot<void>(o, 17, colour, fillLevel);
            break;
        case 18:
            if (ok && whole) Slot<void>(o, 18, colour, fillLevel);
            break;
        case 19:
            if (ok) Slot<void>(o, 19, x, y, colour, level);
            break;
        case 20:
            if (ok) Slot<void>(o, 20, x, y, colour, level);
            break;
        case 21:
            if (ok) Slot<void>(o, 21, x, y, colour[0], level);
            break;
        case 38:
            if (ok) {
                float *r = Slot<float *>(o, 38, out, x, y, level);
                run.Add("rgb", {Bits(out[0]), Bits(out[1]), Bits(out[2]), Bits(out[3]), r == out});
            }
            break;
        case 39:
            if (ok) {
                float *r = Slot<float *>(o, 39, out, x, y, level);
                run.Add("pixel", {Bits(out[0]), Bits(out[1]), Bits(out[2]), Bits(out[3]), r == out});
            }
            break;
        default:
            if (ok) run.Add("channel", {Bits(Slot<float>(o, 40, x, y, level))});
            break;
        }
        run.Add("step", {fmt < 0 ? 0xffffffffu : static_cast<uint32_t>(fmt), lv, static_cast<uint32_t>(x), static_cast<uint32_t>(y), pixels});
        Dump(run, o, "after");
    }
    run.Add("released", {Slot<uint32_t>(o, 2)});
    counter.Stop();
    run.Add("heap blocks left", {static_cast<uint32_t>(counter.Blocks())});
    EndCounts(run);
    gRun = nullptr;
    return run;
}

void CheckSlot(difftest::Context &ctx, unsigned slot)
{
    Fakes fakes(ctx);
    unsigned failed = 0;
    for (uint32_t seed = 1; seed <= 300 && failed < 3; ++seed) {
        const Run a = RunSlots(ctx, fakes, true, seed, slot);
        const Run b = RunSlots(ctx, fakes, false, seed, slot);
        if (!Compare(ctx, "seed", seed, a, b)) ++failed;
    }
}

} // namespace

DIFF_TEST(SG_ImageData_RegenerateMips, 0xab1680) { CheckSlot(ctx, 14); }
DIFF_TEST(SG_ImageData_SetFormat, 0xab03c0) { CheckSlot(ctx, 16); }
DIFF_TEST(SG_ImageData_FillOpaque, 0xab0e50) { CheckSlot(ctx, 17); }
DIFF_TEST(SG_ImageData_Fill, 0xab08b0) { CheckSlot(ctx, 18); }
DIFF_TEST(SG_ImageData_SetPixelOpaque, 0xab36d0) { CheckSlot(ctx, 19); }
DIFF_TEST(SG_ImageData_SetPixel, 0xab3390) { CheckSlot(ctx, 20); }
DIFF_TEST(SG_ImageData_SetPixelChannel, 0xab3530) { CheckSlot(ctx, 21); }
DIFF_TEST(SG_ImageData_GetPixelRGB, 0xab2150) { CheckSlot(ctx, 38); }
DIFF_TEST(SG_ImageData_GetPixel, 0xab1ca0) { CheckSlot(ctx, 39); }
DIFF_TEST(SG_ImageData_GetPixelChannel, 0xab1ec0) { CheckSlot(ctx, 40); }
