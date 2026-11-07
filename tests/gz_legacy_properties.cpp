// Difftests for the legacy controls' attribute readers and common window properties (lane MD, foundations F5b):
// src/gz/cGZLegacyControlProperties.size.cpp.
//
// The attribute lists are ours (cGZArray, the game's layout) and their values real variants (ours: uint32, sint32 and
// text variants, the texts from per-attribute pools of well-formed and malformed texts; and a variant class of the
// test's that reports a chosen type and answers GetValString false or true). Lists and values are built once
// per seed and handed to both sides; neither side changes them.
//
// LegacyProperties_Readers: FindTextAttribute 0x863fae, FindPairAttribute 0x863e87, FindColorAttribute 0x863e06 and
// HasAttribute 0x864077 against ours on generated lists: the result, every word / byte around the outs (outs inside
// marker-filled buffers, so a write past an out shows), the text left in the out string.
//
// LegacyProperties_LoadText: LoadLocalizedText 0x86dceb against ours. The framework (the service object cell 0x10c9378)
// is a recording fake whose GetSystemService hands out the UI script service fake (or refuses) and the resource manager
// fake; the service's GetContext answers a context fake or null; the context's slot 4 and the manager's slot 5 log the
// key's four words, write a text / hand out a counted string (or fail, a result with high garbage bits). Compared: the
// result, the out text, the event log, every fake's count, the resource string's count.
//
// LegacyProperties_ResourceKey: SetResourceKeyProperty 0x87b35c against ours with a winfx window (or null), random ids and
// keys: the window's log, its SetProperty logging the variant by content (type, count, bytes).
//
// LegacyProperties_FileBrowserKey: AppendFileBrowserKey 0x87b143 against ours (lane NH), see the test.
//
// LegacyProperties_Apply: ApplyCommonProperties 0x86549d against ours, apply true and false, on generated lists over the
// window properties' attributes (flags f01a..f02e, id, area, position, size, colour, caption, caption key, hidden, the
// eight tooltips, typed properties "id,type,value" of every type name, malformed ones included) with a winfx window: the
// window's log (SetFlag, SetID, SetSize state-backed; SetAnchorArea logs the rectangle, SetBackgroundColor the three low
// bytes, SetCaption the text; SetProperty the variant by content: tag, count, payload or array bytes, and for the
// tooltip variant the list it holds: its vtable (the game's 0xe486ac or ours), count and texts), the window's state, the
// framework fakes' log (the caption key's LoadLocalizedText) and counts. Operator new / delete (0x824632 / 0x824624) are
// the harness's on the original side. The variants the window keeps are released after the side, which frees the
// tooltip list (its Release 0x86de0b on the original side, ours on ours) and its strings.
//
// Not compared: an area text (0x101) that parses partially. The original passes its rectangle words uninitialised
// (0x865857..0x86589f: sscanf's result is not checked and the four stack words are never set before), so the pools only
// hold area texts with all four numbers. A null value of a typed-property attribute (0x110): the original calls its
// GetType without a null test (0x865e16..0x865e1c), as ours, so the lists never hold one.
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "shape.h"
#include "ts_services_fixture.h"
#include "winfx.h"

#include "game/game.h"
#include "gz/cGZLegacyControl.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZWin.h"
#include "rz/cIGZVariant.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace legacy_properties_test {

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;

constexpr uint32_t kServiceCell = 0x10c9378;
constexpr uint32_t kGameTextListVTable = 0xe486ac; // the game's tooltip list (cGZLegacyTextList)
constexpr uint32_t kUIServiceId = 0x5a356e15;
constexpr uint32_t kUIServiceIid = 0xfa3562fa;
constexpr uint32_t kManagerId = 0x056b906e;
constexpr uint32_t kManagerIid = 0x656b8efc;
enum : unsigned { kFramework = 0, kService, kContext, kManager };

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// ---- values ------------------------------------------------------------------------------------------------------------

std::vector<cRZVariant *> gValues; // every value made for the seed (released after it)

cRZVariant *Keep(cRZVariant *v)
{
    v->AddRef();
    gValues.push_back(v);
    return v;
}
cRZVariant *TextValue(const char *text)
{
    LegacyText s;
    s.mText.FromChar(text);
    return Keep(new cRZVariant(*s.mText.AsInterface())); // an RZChar array: tag 0x800e
}
// A value of another variant class (the attribute values are any cIGZVariant): it reports `type` and answers
// GetValString with `text` and `result`. The game's own text variants always answer true; another class need not,
// and a non-text value can still give a text with commas.
class OtherVariant : public cRZVariant {
public:
    OtherVariant(uint32_t type, const char *text, bool result) : mFakeType(type), mFakeText(text), mFakeResult(result) {}
    uint32_t GetType() const override { return mFakeType; }
    bool GetValString(cIGZString &text, uint32_t, char) override
    {
        text.FromChar(mFakeText);
        return mFakeResult;
    }

private:
    uint32_t mFakeType;
    const char *mFakeText;
    bool mFakeResult;
};
void ReleaseValues()
{
    for (cRZVariant *v : gValues) v->Release();
    gValues.clear();
}

const char *const kAreaTexts[] = {"( 1, 2, 30, 40)", "(-5,0,7,8)", "( 100, -20, 3, 4)", "(0,0,0,0)"};
const char *const kPairTexts[] = {"(3,4)", "(-7,12)", "(5)", "abc", "", "(1, 2)", "(100,200,300)", "( 9 , 8 )", "(x,1)"};
const char *const kColorTexts[] = {"(10,20,30)", "(256,-1,511)", "(1,2)", "x", "", "( 7, 8, 9)", "(1,2,3,4)", "(0x1,2,3)"};
const char *const kCaptionTexts[] = {"Caption", "", "Hello world", "a b c"};
const char *const kKeyTexts[] = {"{1a,2b}", "{0,0}", "{ff}", "junk", "{abc,def}", "", "{FFFFFFFF,12345678}"};
const char *const kTipTexts[] = {"tip one", "", "second tip", "x"};
const char *const kTypedTexts[] = {
    "7,uint32,99", "0x20,bool,1", "5,bool,7", "3,sint8,-5", "4,uint16,65535", "9,sint16,-300", "10,sint32,-1",
    "11,uint64,123456789012", "12,sint64,-42", "13,float32,1.5", "14,float64,2.25", "15,string,hello", "0,uint32,5",
    "6,,5", "6,uint32,", "6,unknown,1", "6,UINT32,5", "8,string,a,b", "nonum", "", "16,uint8,300", "17,sint32,abc",
    "0x7fffffff,float32,-0.5", "18,bool,0", "19,uint8,12"};
const char *const kOtherTexts[] = {"(1,2)", "(1,2,3)", "( 1, 2, 3, 4)", "{1,2}", "text", ""};
template <size_t N> const char *Pick(const char *const (&pool)[N]) { return pool[Draw(uint32_t(N))]; }

const char *PoolText(uint32_t id)
{
    switch (id) {
    case 0x101: return Pick(kAreaTexts);
    case 0x102:
    case 0x103: return Pick(kPairTexts);
    case 0x104: return Pick(kColorTexts);
    case 0x105: return Pick(kCaptionTexts);
    case 0x106: return Pick(kKeyTexts);
    case 0x110: return Pick(kTypedTexts);
    default: return id >= 0x108 && id <= 0x10f ? Pick(kTipTexts) : Pick(kOtherTexts);
    }
}

// A value for attribute `id`: mostly a text from its pool, sometimes a uint32 / sint32 variant, a variant of another
// class (a text one that answers false, or a non-text one whose text is from the pool), or null (never for the typed
// properties: see the file comment).
cIGZUnknown *ValueFor(uint32_t id)
{
    const uint32_t pick = Draw(11);
    if (pick == 0 && id != 0x110) return nullptr;
    if (pick == 1) return Keep(new cRZVariant(uint32_t(Draw(3) ? Draw(3) : gRng())));
    if (pick == 2) return Keep(new cRZVariant(int32_t(Draw(5)) - 2));
    if (pick == 3) {
        const uint32_t type = Draw(2) ? 0x800e : 0x8003;
        const bool result = Draw(2) != 0;
        return Keep(new OtherVariant(type, PoolText(id), result));
    }
    if (id >= 0xf01a && id <= 0xf02e) return Keep(new cRZVariant(uint32_t(Draw(3)))); // the flags are uint32s
    if (id == 0x100) return Keep(new cRZVariant(uint32_t(0x1000 + Draw(8))));
    return TextValue(PoolText(id));
}

const uint32_t kApplyIds[] = {0xf01a, 0xf01b, 0xf01c, 0xf01e, 0xf01f, 0xf020, 0xf021, 0xf023, 0xf024, 0xf025, 0xf026,
                              0xf027, 0xf028, 0xf029, 0xf02a, 0xf02b, 0xf02c, 0xf02d, 0xf02e, 0x100, 0x101, 0x102,
                              0x103, 0x104, 0x105, 0x106, 0x107, 0x108, 0x109, 0x10a, 0x10b, 0x10c, 0x10d, 0x10e,
                              0x10f, 0x110, 0x110, 0x110, 0x111, 0x5};
const uint32_t kReaderIds[] = {0x101, 0x102, 0x104, 0x105, 0x106, 0x110, 0x7};

cGZScriptAttributes *MakeList(const uint32_t *ids, uint32_t idCount, uint32_t maxLength)
{
    auto *list = new cGZScriptAttributes;
    const uint32_t n = Draw(maxLength + 1);
    for (uint32_t i = 0; i < n; ++i) {
        cGZScriptAttribute a;
        a.mId = ids[Draw(idCount)];
        a.mValue = ValueFor(a.mId);
        list->PushBack(a);
    }
    return list;
}

// ---- the window --------------------------------------------------------------------------------------------------------

winfx::World gWorld;
std::vector<cIGZVariant *> gProps; // what the window's SetProperty kept
const void *gOurListVTable;
int gSide;

const uint8_t *Bytes(uint32_t p) { return reinterpret_cast<const uint8_t *>(uintptr_t(p)); }
template <class T> T At(const uint8_t *b, uint32_t off)
{
    T v;
    std::memcpy(&v, b + off, sizeof(T));
    return v;
}

uint32_t ElementSize(uint16_t type)
{
    static const uint8_t kSizes[] = {1, 1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 1, 2, 1, 4, 4};
    const uint32_t kind = type & 0x7fff;
    return kind < sizeof(kSizes) ? kSizes[kind] : 1;
}

// A string element of a list by content: its vtable (the game's cRZString's?), text, count.
void DescribeString(drawfx::Call &c, const cRZString &s)
{
    c.words.push_back(s.mVTable == GameAddress<void>(cRZString::kVTable));
    c.words.push_back(s.mRefCount);
    c.text += s.mString;
    c.text += '\x1f';
}

// A variant (the original's cRZVariant layout: tag +0xe, count +0xc, payload +4, array count +0x10) by content.
void DescribeVariant(drawfx::Call &c, const uint8_t *v)
{
    const uint16_t type = At<uint16_t>(v, 0xe);
    c.words.push_back(type);
    c.words.push_back(At<uint16_t>(v, 0xc));
    if (type & 0x8000) {
        const uint32_t count = At<uint32_t>(v, 0x10);
        c.words.push_back(count);
        const uint8_t *data = Bytes(At<uint32_t>(v, 4));
        const uint32_t size = count * ElementSize(type);
        c.words.push_back(data != nullptr);
        for (uint32_t i = 0; data && i < size; ++i) c.words.push_back(data[i]);
    } else if (type == cRZVariant::kTypeUnknown) {
        // The tooltip list: vtable, count, the vector's strings.
        const uint8_t *list = Bytes(At<uint32_t>(v, 4));
        c.words.push_back(list != nullptr);
        if (!list) return;
        const uint32_t vt = At<uint32_t>(list, 0);
        // The side's own vtable: the game's on the original's list, ours on ours.
        c.words.push_back(vt == P(gSide ? gOurListVTable : GameAddress<void>(kGameTextListVTable)));
        c.words.push_back(At<uint32_t>(list, 0x10));
        const auto *first = reinterpret_cast<const cRZString *>(uintptr_t(At<uint32_t>(list, 4)));
        const auto *last = reinterpret_cast<const cRZString *>(uintptr_t(At<uint32_t>(list, 8)));
        c.words.push_back(uint32_t(last - first));
        for (const cRZString *s = first; s != last; ++s) DescribeString(c, *s);
    } else {
        c.words.push_back(At<uint32_t>(v, 4));
        c.words.push_back(At<uint32_t>(v, 8));
    }
}

uint32_t SetProperty(drawfx::Fake &, const uint32_t *a)
{
    drawfx::Call &call = drawfx::gLog.back();
    call.words[1] = a[1] != 0;
    if (a[1]) {
        DescribeVariant(call, Bytes(a[1]));
        auto *value = reinterpret_cast<cIGZVariant *>(uintptr_t(a[1]));
        value->AddRef();
        gProps.push_back(value);
    }
    return 1;
}
uint32_t SetCaption(drawfx::Fake &, const uint32_t *a)
{
    drawfx::Call &call = drawfx::gLog.back();
    call.words[0] = a[0] != 0;
    if (a[0]) DescribeString(call, *reinterpret_cast<const cRZString *>(uintptr_t(a[0])));
    return 1;
}
uint32_t SetAnchorArea(drawfx::Fake &, const uint32_t *a)
{
    drawfx::Call &call = drawfx::gLog.back();
    call.words[0] = a[0] != 0;
    for (unsigned i = 0; i < 4; ++i) call.words.push_back(At<uint32_t>(Bytes(a[0]), 4 * i));
    return 0;
}
uint32_t SetBackgroundColor(drawfx::Fake &, const uint32_t *a)
{
    // Byte arguments: the original pushes words whose high bytes are whatever its locals held.
    drawfx::Call &call = drawfx::gLog.back();
    for (unsigned i = 0; i < 3; ++i) call.words[i] = a[i] & 0xff;
    return 0;
}

ifaces::cIGZWin *BuildWindow(uint32_t seed)
{
    gWorld.Build(seed, 2);
    winfx::Win &w = gWorld[1];
    w.f.On(127, &SetProperty);
    w.f.On(90, &SetCaption);
    w.f.On(65, &SetAnchorArea);
    w.f.On(120, &SetBackgroundColor);
    return reinterpret_cast<ifaces::cIGZWin *>(&w.f);
}

// The kept variants' counts after the call (ours and the window's), and the tooltip list's (held by its variant).
void KeptCounts(Blob &b)
{
    for (cIGZVariant *v : gProps) {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(v);
        b.push_back(At<uint16_t>(bytes, 0xc));
        if (At<uint16_t>(bytes, 0xe) == cRZVariant::kTypeUnknown)
            if (const uint8_t *list = Bytes(At<uint32_t>(bytes, 4))) b.push_back(At<uint32_t>(list, 0x10));
    }
}

void ReleaseProps()
{
    for (cIGZVariant *v : gProps) v->Release();
    gProps.clear();
}

// ---- the framework -----------------------------------------------------------------------------------------------------

bool gGiveService, gGiveContext, gContextOk, gManagerOk;
const char *gContextText;
cRZString *gResource; // the manager's string resource (counted)

uint32_t GetSystemService(Obj *, uint32_t *a)
{
    void **out = reinterpret_cast<void **>(uintptr_t(a[2]));
    if (a[0] == kUIServiceId && a[1] == kUIServiceIid && gGiveService) {
        ++F(kService).refs;
        *out = &F(kService);
        return 1;
    }
    if (a[0] == kManagerId && a[1] == kManagerIid) {
        ++F(kManager).refs;
        *out = &F(kManager);
        return 1;
    }
    return 0;
}
void LogKey(uint32_t key)
{
    for (unsigned i = 0; i < 4; ++i) Ev(At<uint32_t>(Bytes(key), 4 * i));
}
uint32_t ContextLoad(Obj *, uint32_t *a)
{
    LogKey(a[0]);
    reinterpret_cast<cIGZString *>(uintptr_t(a[1]))->FromChar(gContextText);
    return gContextOk ? 0x5a01 : 0x5a00;
}
uint32_t ManagerGet(Obj *, uint32_t *a)
{
    LogKey(a[0]);
    if (!gManagerOk) return 0x7700;
    gResource->AddRef();
    *reinterpret_cast<void **>(uintptr_t(a[2])) = gResource;
    return 0x7701;
}

void BuildFakes(uint32_t seed)
{
    svcfx::ResetAll();
    drawfx::ResetAll();
    std::mt19937 rng(seed * 17 + 3);
    F(kFramework).Set(5, "ddp", 0, &GetSystemService);
    F(kService).Set(9, 0, 0u);
    gGiveService = rng() % 4 != 0;
    gGiveContext = rng() % 2 != 0;
    if (gGiveContext) F(kService).Set(9, 0, &F(kContext));
    F(kContext).Set(4, "pp", 0, &ContextLoad);
    F(kManager).Set(5, "ppp--", 0, &ManagerGet);
    gContextOk = rng() % 3 != 0;
    gManagerOk = rng() % 3 != 0;
    static const char *const kTexts[] = {"localized", "", "Another caption"};
    gContextText = kTexts[rng() % 3];
    gResource = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    gResource->Construct(kTexts[rng() % 3]);
    gResource->AddRef();
    gProps.clear();
}

void EndSide(Blob &b)
{
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    svcfx::CaptureFakes(b);
    b.push_back(gResource->mRefCount);
    b.push_back(uint32_t(gResource->mString.size()));
    gResource->Release();
    gResource = nullptr;
}

} // namespace legacy_properties_test

using namespace legacy_properties_test;

DIFF_TEST(LegacyProperties_Readers, 0x863fae)
{
    // Also reached: 0x863e87, 0x863e06, 0x864077.
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    using TextFn = bool(__cdecl *)(const void *, uint32_t, cRZString *);
    using PairFn = bool(__cdecl *)(const void *, uint32_t, int32_t *, int32_t *);
    using ColorFn = bool(__cdecl *)(const void *, uint32_t, uint8_t *, uint8_t *, uint8_t *);
    using HasFn = bool(__cdecl *)(const void *, uint32_t);
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        gRng.seed(seed);
        cGZScriptAttributes *list = MakeList(kReaderIds, sizeof(kReaderIds) / 4, 6);
        const unsigned which = seed % 4;
        const uint32_t id = kReaderIds[Draw(sizeof(kReaderIds) / 4)];
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            svcfx::ResetAll();
            Blob &b = results[side];
            switch (which) {
            case 0: {
                LegacyText out;
                out.mText.FromChar("previous text");
                const bool r = side ? FindTextAttribute(list, id, &out.mText) : ctx.Original<TextFn>(0x863fae)(list, id, &out.mText);
                b.push_back(r);
                b.push_back(uint32_t(out.mText.mString.size()));
                for (char ch : out.mText.mString) b.push_back(uint8_t(ch));
                break;
            }
            case 1: {
                int32_t words[4] = {-0x11111112, -0x22222223, -0x33333334, -0x44444445};
                const bool r = side ? FindPairAttribute(list, id, &words[1], &words[2])
                                    : ctx.Original<PairFn>(0x863e87)(list, id, &words[1], &words[2]);
                b.push_back(r);
                for (int32_t w : words) b.push_back(uint32_t(w));
                break;
            }
            case 2: {
                uint8_t bytes[12];
                std::memset(bytes, 0xcc, sizeof(bytes));
                const bool r = side ? FindColorAttribute(list, id, &bytes[1], &bytes[5], &bytes[9])
                                    : ctx.Original<ColorFn>(0x863e06)(list, id, &bytes[1], &bytes[5], &bytes[9]);
                b.push_back(r);
                for (uint8_t v : bytes) b.push_back(v);
                break;
            }
            default: {
                const bool r = side ? HasAttribute(list, id) : ctx.Original<HasFn>(0x864077)(list, id);
                b.push_back(r);
                break;
            }
            }
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
        }
        delete list;
        ReleaseValues();
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyProperties_Readers", seed)) break;
    }
}

DIFF_TEST(LegacyProperties_LoadText, 0x86dceb)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    globals.Set(kServiceCell, &F(kFramework));
    using LoadFn = bool(__cdecl *)(uint32_t, uint32_t, cIGZString *);
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            gRng.seed(seed);
            const uint32_t group = Draw(2) ? Draw(4) : gRng();
            const uint32_t instance = Draw(2) ? Draw(4) : gRng();
            LegacyText out;
            out.mText.FromChar("old");
            const bool r = side ? LoadLocalizedText(group, instance, out.mText.AsInterface())
                                : ctx.Original<LoadFn>(0x86dceb)(group, instance, out.mText.AsInterface());
            Blob &b = results[side];
            b.push_back(r);
            for (char ch : out.mText.mString) b.push_back(uint8_t(ch));
            b.push_back(out.mText.mRefCount);
            EndSide(b);
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyProperties_LoadText", seed)) break;
    }
}

DIFF_TEST(LegacyProperties_ResourceKey, 0x87b35c)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    using SetKeyFn = void(__cdecl *)(void *, uint32_t, const void *);
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        drawfx::Log logs[2];
        for (int side = 0; side < 2; ++side) {
            drawfx::ResetAll();
            gRng.seed(seed);
            ifaces::cIGZWin *window = Draw(5) ? BuildWindow(seed) : nullptr;
            const uint32_t id = Draw(2) ? 0xb31771a : gRng();
            uint32_t key[4];
            for (uint32_t &w : key) w = Draw(2) ? Draw(5) : gRng();
            if (side)
                SetResourceKeyProperty(window, id, key);
            else
                ctx.Original<SetKeyFn>(0x87b35c)(window, id, key);
            ReleaseProps();
            logs[side] = drawfx::gLog;
        }
        if (!drawfx::Same(ctx, logs[0], logs[1], "LegacyProperties_ResourceKey", seed)) break;
    }
}

// ---- the file browser's key list ---------------------------------------------------------------------------------------

uint32_t gStoreId;
cIGZVariant *gStored; // the window's one stored property (what GetProperty hands out, AddRef'd)

uint32_t GetStoredProperty(drawfx::Fake &, const uint32_t *a)
{
    drawfx::Call &call = drawfx::gLog.back();
    call.words[1] = 1; // the out pointer is a stack address
    if (a[0] != gStoreId || !gStored) return 0;
    gStored->AddRef();
    *reinterpret_cast<void **>(uintptr_t(a[1])) = gStored;
    return 1;
}
uint32_t SetStoredProperty(drawfx::Fake &f, const uint32_t *a)
{
    SetProperty(f, a);
    if (a[0] == gStoreId) gStored = reinterpret_cast<cIGZVariant *>(uintptr_t(a[1]));
    return 1;
}

DIFF_TEST(LegacyProperties_FileBrowserKey, 0x87b143)
{
    // 0x87b143 against ours on a window that stores one property: absent, an empty variant, a plain number, or a byte list
    // of 0..33 keys (the rebuilt list is on the stack for fewer than 32 keys, else a heap block: both sides of that line)
    // and sometimes a length that is not a whole number of keys; index from -2 to past the end. Compared: the result, the
    // window's log (the new variant by content), the stored variant afterwards (tag, count, bytes), the sizes of the big
    // blocks freed (the heap buffer, the variant's old bytes).
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    using AppendFn = bool(__cdecl *)(void *, uint32_t, int32_t, const void *);
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        drawfx::Log logs[2];
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            drawfx::ResetAll();
            gRng.seed(seed);
            ifaces::cIGZWin *window = BuildWindow(seed);
            winfx::Win &w = gWorld[1];
            w.f.On(126, &GetStoredProperty);
            w.f.On(127, &SetStoredProperty);
            gStoreId = Draw(2) ? 0x1342 : gRng();
            gStored = nullptr;
            uint32_t entries = 0;
            std::vector<uint8_t> bytes;
            const uint32_t kind = Draw(10);
            if (kind >= 2) {
                entries = Draw(4) == 0 ? 29 + Draw(5) : Draw(10);
                bytes.resize(entries * 16 + (kind == 2 ? 1 + Draw(15) : 0));
                for (uint8_t &b : bytes) b = uint8_t(gRng());
            }
            const bool owned = kind != 0;
            if (kind == 1) {
                gStored = new cRZVariant;
            } else if (kind == 3) {
                gStored = new cRZVariant(uint32_t(gRng()));
            } else if (kind >= 2) {
                gStored = new cRZVariant;
                gStored->SetVoidArray(bytes.data(), uint32_t(bytes.size()));
            }
            if (owned) gStored->AddRef();
            uint8_t key[16];
            for (uint8_t &b : key) b = uint8_t(gRng());
            const int32_t index = int32_t(Draw(entries + 5)) - 2;
            {
                difftest::FreeRecorder frees;
                bool result;
                if (side)
                    result = AppendFileBrowserKey(window, gStoreId, index, key);
                else
                    result = ctx.Original<AppendFn>(0x87b143)(window, gStoreId, index, key);
                frees.Stop();
                results[side].push_back(result);
                for (size_t i = 0; i < frees.Count(); ++i)
                    if (frees.Size(i) >= 256) results[side].push_back(uint32_t(frees.Size(i)));
            }
            drawfx::Note("stored", {gStored != nullptr});
            if (gStored) DescribeVariant(drawfx::gLog.back(), reinterpret_cast<const uint8_t *>(gStored));
            ReleaseProps();
            if (owned) gStored->Release();
            gStored = nullptr;
            logs[side] = drawfx::gLog;
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyProperties_FileBrowserKey", seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], "LegacyProperties_FileBrowserKey log", seed)) break;
    }
}

DIFF_TEST(LegacyProperties_Apply, 0x86549d)
{
    // Also reached: 0x86de0b (the tooltip list's Release, when the kept variants are released).
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    globals.Set(kServiceCell, &F(kFramework));
    {
        cGZLegacyTextList probe;
        gOurListVTable = *reinterpret_cast<void **>(&probe);
    }
    using ApplyFn = void(__cdecl *)(void *, void *, bool);
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        gRng.seed(seed);
        cGZScriptAttributes *list = MakeList(kApplyIds, sizeof(kApplyIds) / 4, 16);
        const bool apply = seed % 3 != 0;
        Blob results[2];
        drawfx::Log logs[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            gSide = side;
            ifaces::cIGZWin *window = BuildWindow(seed);
            if (side)
                ApplyCommonProperties(window, list, apply);
            else
                ctx.Original<ApplyFn>(0x86549d)(window, list, apply);
            Blob &b = results[side];
            const std::vector<uint32_t> state = gWorld.State();
            b.insert(b.end(), state.begin(), state.end());
            KeptCounts(b);
            ReleaseProps();
            EndSide(b);
            logs[side] = drawfx::gLog;
        }
        delete list;
        ReleaseValues();
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyProperties_Apply", seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], "LegacyProperties_Apply log", seed)) break;
    }
}

DIFF_TEST(LegacyProperties_TextListRelease, 0x86de0b)
{
    // The tooltip list's Release on a list built by hand (the game's vtable on the original side's object): the result,
    // the count and strings left when it was not the last count (the last frees the block and its strings).
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    using ReleaseFn = uint32_t(__thiscall *)(void *);
    for (uint32_t seed = 1; seed <= 500; ++seed) {
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            gRng.seed(seed);
            auto *list = new cGZLegacyTextList;
            const uint32_t strings = Draw(4);
            for (uint32_t i = 0; i < strings; ++i) {
                LegacyText s;
                s.mText.FromChar(Pick(kTipTexts));
                list->mTexts.PushBackAny(s.mText);
            }
            list->mRefCount = Draw(4);
            if (!side) *reinterpret_cast<const void **>(list) = GameAddress<void>(kGameTextListVTable);
            const uint32_t before = list->mRefCount;
            const uint32_t r = side ? list->Release() : ctx.Original<ReleaseFn>(0x86de0b)(list);
            Blob &b = results[side];
            b.push_back(r);
            if (before > 1) {
                b.push_back(list->mRefCount);
                b.push_back(uint32_t(list->mTexts.Count()));
                list->mRefCount = 1;
                list->Release();
            }
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyProperties_TextListRelease", seed)) break;
    }
}
