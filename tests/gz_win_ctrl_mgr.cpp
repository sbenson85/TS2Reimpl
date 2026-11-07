// Differential test for cGZWinCtrlMgr (lane KO, src/gz/cGZWinCtrlMgr.h): the whole class in the standalone shape.
//
// WinCtrlMgr_Run: a manager built by the original constructor (0x94ac21, game vtables 0xe56030 / 0xe56068 / 0xe56084) or
// factory (0x92f732) against one built by ours (placement new / Create), each driven through its own vtables by the same
// random operations, then destroyed by the last Release (cRZSystemService's, reaching the +4 face's deleting destructor
// thunk 0x94ad77, the deleting destructor 0x94ad7f and the destructor 0x94acd4, which runs Shutdown) or by that thunk.
// Operations: QueryInterface 0x94c1a3 on every face (thunks 0x94c1cd / 0x94c1d5), Init 0x94bf56 and Shutdown 0x94c581
// through both faces (thunks 0x94c19b / 0x94c66b), the tables (GetFont 0x94c69e, SetFont 0x94c4d7, GetImage 0x94c673,
// SetImage 0x94c455, GetObject8C 0x94beaf, SetObject8C 0x94c335, GetObjectAC 0x94beda, FindObjectAC 0x94bf0b,
// SetObjectAC 0x94c3cd), the colours (GetColorRGB 0x94be2a, GetColor 0x94bde5, SetColor 0x94c286, SetColorFromValue
// 0x94c2e8), the sound manager (0x94c572, 0x94bf47, 0x94c6c9, 0x94c559), GetWordD4 (folded 0x254680) / SetWordD4
// 0x94c3c0, the creators list (AddCreator 0x94c1dd, RemoveCreator 0x94c235, CreateControl 0x94aed0), every control
// creator (CreateButton 0x94adbe, the kinds 0x94ada1 / 0x94b791 / 0x94aeb3 through slots 9, 11, 12 and 20 to 23,
// CreateText 0x94b7ae, CreateOptionGroup 0x94b95e, CreateBitmap 0x94bd40, CreateSlider 0x94bb54, CreateScrollbar
// 0x94ba91, CreateSpinner 0x94bc1b, CreateLineInput 0x94b895) and CreateDefaultControl 0x94af1f on every type it knows
// (GetFontOrDefault 0x94c88a through the buttons) and unknown ones.
//
// Every object reached is a recording fake: the framework (GetServiceObject's cell 0x10c9378) with the colour service
// (0xc416025c) and the sound manager (0x1af85bfb), the COM object (the director's slot 12; GetClassObject answers by class
// id, a byte result with noise above), fonts, images, table objects, creators, a matcher, one fake per control kind with
// its window face (slot 3) and the scroll bar's bar (slot 4). The controls' constructors (cGZWinBtn::Construct 0x93536f
// and cGZWinText::Construct 0x949ed1 on both sides, the originals' 0x95b137, 0x93741a, 0x957b0e, 0x9586a8, 0x95c6d1,
// 0x95d9dd, 0x937b4c, 0x94cb2e, 0x953f2f) are stand-ins that log their argument, free the block and hand back the kind's
// fake. The game's operator new / delete (0x824632 / 0x824624) are ours. Compared after every operation: the result and
// out words, the vtable words, the service fields, the init byte, the two held services, every table and the colour map
// by content in list order (and bucket counts), the creators list, the word +0xd4, the event log and every fake's
// reference count and values. Not compared: the padding bytes +0x21..+0x23 (the constructor writes the byte +0x20 only).
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "stack_out.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZWinCtrlMgr.h"
#include "ui/cGZWinBtn.h"
#include "ui/cGZWinText.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <utility>
#include <vector>

namespace winctrlmgr_test {

using svcfx::Blob;
using svcfx::Ev;
using svcfx::P;
using Mgr = cGZWinCtrlMgr;

constexpr uint32_t kTag = 0x7d000000; // the manager under test
constexpr unsigned kSlots = 128;

// ---- fakes with 128 slots (the controls' window faces reach slot 120) ----

struct Fake;
using Custom = uint32_t (*)(Fake *self, uint32_t *args);
struct Fake {
    void **vt;
    uint32_t id;
    int32_t refs;
    uint32_t value[8];
    uint32_t ret[kSlots];
    Custom custom[kSlots];
    void *table[kSlots];
    char kinds[kSlots][8]; // each argument's kind: 'p' pointer, 'd' data, 0 undeclared (tests/norm_words.h)
    uint32_t line[kSlots]; // the Set's line (the undeclared-argument message names it)
    void Reset(uint32_t newId);
    void Set(unsigned slot, unsigned args, uint32_t result = 0, Custom fn = nullptr,
             std::source_location where = std::source_location::current());
    // Set with the arguments' kinds, one character each ('p', 'd', '-' undeclared): the count is the string's length.
    template <size_t N>
    void Set(unsigned slot, const char (&argKinds)[N], uint32_t result = 0, Custom fn = nullptr,
             std::source_location where = std::source_location::current())
    {
        static_assert(N - 1 <= 7, "seven arguments");
        Set(slot, unsigned(N - 1), result, fn, where);
        for (unsigned i = 0; i < N - 1; ++i) kinds[slot][i] = svcfx::Obj::Kind(argKinds[i]);
    }
};

enum : unsigned {
    kFramework,
    kColorSvc,
    kSound,
    kFont0, kFonts = 3,
    kImage0 = kFont0 + kFonts, kImages = 3,
    kObj0 = kImage0 + kImages, kObjs = 3,
    kCreator0 = kObj0 + kObjs, kCreators = 3,
    kMatcher = kCreator0 + kCreators,
    kBtn, kText, kOptGrp, kBmp, kSlider, kScroll, kSpinner, kLine, kWindowCtl, kC598, kC231a,
    kBar, kWin, kParent,
    kFakeCount
};
Fake gFakes[kFakeCount];
Fake &F(unsigned i) { return gFakes[i]; }

uint32_t Norm(uint32_t v, std::source_location where = std::source_location::current())
{
    for (unsigned i = 0; i < kFakeCount; ++i)
        if (v == P(&gFakes[i])) return 300 + i;
    return svcfx::Norm(v, where);
}
// An out word the call may leave as the test set it (0x5e5e5e5e): left alone, or a pointer.
uint32_t NormOut(uint32_t v, std::source_location where = std::source_location::current())
{
    return v == 0x5e5e5e5e ? normwords::kUntouched : Norm(v, where);
}
// A logged argument by its declared kind.
uint32_t Arg(const Fake *self, unsigned slot, unsigned i, uint32_t v)
{
    const char kind = self->kinds[slot][i];
    if (kind == 'd') return v;
    if (kind == 'p') return Norm(v);
    if (!v) return 0;
    char what[128];
    std::snprintf(what, sizeof(what), "fake #%u slot %u (Set at gz_win_ctrl_mgr.cpp:%u) argument %u", self->id, slot,
                  unsigned(self->line[slot]), i);
    return normwords::Undeclared(v, what, "in its Set: Set(slot, \"...\") with 'p' or 'd' per argument", svcfx::gRanges);
}

uint32_t Dispatch(Fake *self, unsigned slot, uint32_t *args, unsigned count)
{
    Ev(0x60);
    Ev(self->id);
    Ev(slot);
    Ev(count);
    for (unsigned i = 0; i < count; ++i) Ev(Arg(self, slot, i, args[i]));
    if (self->custom[slot]) return self->custom[slot](self, args);
    if (slot == 1) return uint32_t(++self->refs);
    if (slot == 2) return uint32_t(--self->refs);
    return self->ret[slot];
}

template <size_t> using Word = uint32_t;
template <unsigned S, class Seq> struct Gen;
template <unsigned S, size_t... I> struct Gen<S, std::index_sequence<I...>> {
    static uint32_t __fastcall Fn(Fake *self, void *, Word<I>... a)
    {
        uint32_t args[] = {a..., 0u};
        return Dispatch(self, S, args, unsigned(sizeof...(I)));
    }
};
void *gTable[kSlots][8];
template <unsigned S> void FillSlot()
{
    gTable[S][0] = reinterpret_cast<void *>(&Gen<S, std::make_index_sequence<0>>::Fn);
    gTable[S][1] = reinterpret_cast<void *>(&Gen<S, std::make_index_sequence<1>>::Fn);
    gTable[S][2] = reinterpret_cast<void *>(&Gen<S, std::make_index_sequence<2>>::Fn);
    gTable[S][3] = reinterpret_cast<void *>(&Gen<S, std::make_index_sequence<3>>::Fn);
    gTable[S][4] = reinterpret_cast<void *>(&Gen<S, std::make_index_sequence<4>>::Fn);
    gTable[S][5] = reinterpret_cast<void *>(&Gen<S, std::make_index_sequence<5>>::Fn);
    gTable[S][6] = reinterpret_cast<void *>(&Gen<S, std::make_index_sequence<6>>::Fn);
    gTable[S][7] = reinterpret_cast<void *>(&Gen<S, std::make_index_sequence<7>>::Fn);
}
template <size_t... S> void FillAll(std::index_sequence<S...>) { (FillSlot<unsigned(S)>(), ...); }
bool gFilled = false;

void Fake::Reset(uint32_t newId)
{
    if (!gFilled) FillAll(std::make_index_sequence<kSlots>()), gFilled = true;
    vt = table;
    id = newId;
    refs = 1000;
    for (auto &v : value) v = 0;
    for (unsigned i = 0; i < kSlots; ++i) ret[i] = 0, custom[i] = nullptr, table[i] = nullptr;
    Set(1, 0);
    Set(2, 0);
}
void Fake::Set(unsigned slot, unsigned args, uint32_t result, Custom fn, std::source_location where)
{
    line[slot] = where.line();
    table[slot] = gTable[slot][args];
    std::memset(kinds[slot], 0, sizeof(kinds[slot]));
    ret[slot] = result;
    custom[slot] = fn;
}

template <class T> T *As(uintptr_t word) { return reinterpret_cast<T *>(word); }

// ---- the world ----

constexpr uint32_t kIids[4] = {0x11111111, 0x22222222, 0x231a1c57, 1};
constexpr uint32_t kCreatorIid = Mgr::kCreatorIid;

struct World {
    bool framework, colorSvc, sound;
    uint32_t comMask; // bit per control kind: the COM object answers its class id
};
World gWorld;

// The framework's slot 5 (id, iid, out).
uint32_t GetSystemService(Fake *, uint32_t *a)
{
    unsigned which = kFakeCount;
    if (a[0] == Mgr::kColorServiceId && a[1] == Mgr::kColorServiceIid && gWorld.colorSvc) which = kColorSvc;
    if (a[0] == cGZSndMgr::kServiceId && a[1] == cIGZSndMgr::kIid && gWorld.sound) which = kSound;
    if (which == kFakeCount) return 0x100;
    ++F(which).refs;
    *As<void *>(a[2]) = &F(which);
    return 0x101;
}

// The COM object's slot 3 (class id, iid, out): the kind's fake when its bit is set. A byte result, noise above.
struct ClassOf {
    uint32_t classId, iid;
    unsigned kind;
};
constexpr ClassOf kClasses[] = {
    {0x22ecfc47, 0x8810, kBtn},       {0x592, 0x212cdc1f, kText},       {0xa1336cc1, 0xa1336cc0, kOptGrp},
    {0x82fe68c4, 0xc12cea13, kBmp},   {0x21325208, 0x21325207, kSlider}, {0x61325a2e, 0x61325a2d, kScroll},
    {0x612ce0c4, 0x612ce0c3, kSpinner}, {0x21335c5a, 0x21335c59, kLine}, {0x4386d516, 0x5386d516, kWindowCtl},
    {0x598, 0x4132242b, kC598},       {0x231a1c58, 0x231a1c57, kC231a},
};
uint32_t GetClassObject(svcfx::Obj *, uint32_t *a)
{
    for (const ClassOf &c : kClasses) {
        if (a[0] != c.classId) continue;
        if (a[1] != c.iid) return 0x9900;
        if (!(gWorld.comMask >> (c.kind - kBtn) & 1)) return 0x5500;
        ++F(c.kind).refs;
        *As<void *>(a[2]) = &F(c.kind);
        return 0x7701;
    }
    return 0x8800;
}

// Controls' QueryInterface by mask (value[0] over kIids).
uint32_t Query(Fake *self, uint32_t *a)
{
    for (unsigned i = 0; i < 4; ++i) {
        if (a[0] == kIids[i] && (self->value[0] >> i & 1)) {
            ++self->refs;
            *As<void *>(a[1]) = self;
            return 0x301;
        }
    }
    return 0x300;
}
// Creators: the creator interface by value[0] bit 0; slot 4 answers value[1]; slot 6 (type, iid, out) answers for the type
// in value[2] with the control fake value[3].
uint32_t CreatorQuery(Fake *self, uint32_t *a)
{
    if (a[0] == kCreatorIid && (self->value[0] & 1)) {
        ++self->refs;
        *As<void *>(a[1]) = self;
        return 0x401;
    }
    return 0x400;
}
uint32_t CreatorCreate(Fake *self, uint32_t *a)
{
    if (a[0] != self->value[2]) return 0x500;
    Fake &made = F(self->value[3]);
    ++made.refs;
    *As<void *>(a[2]) = &made;
    return 0x501;
}
// The matcher's slot 12 (object, 1): true for the fake in value[0].
uint32_t Match(Fake *self, uint32_t *a) { return (Norm(a[0]) == 300 + self->value[0] ? 1u : 0u) | 0x600u; }
// An image's slot 11: fills the rectangle (four words).
uint32_t ImageRect(Fake *self, uint32_t *a)
{
    int32_t *rect = As<int32_t>(a[0]);
    for (int i = 0; i < 4; ++i) rect[i] = int32_t(self->value[4] + uint32_t(i) * 7);
    difftest::FillOut(rect, 16);
    return 0;
}
// A button's slot 12 (kind, image, rect): the rectangle's words when given (the default one CreateDefaultControl builds).
uint32_t BtnImage(Fake *, uint32_t *a)
{
    if (const int32_t *rect = As<int32_t>(a[2]))
        for (int i = 0; i < 4; ++i) Ev(uint32_t(rect[i]));
    return 0;
}
// The colour service's slot 36 (value, r, g, b): the bytes of the value's mix.
uint32_t ColorToRGB(Fake *, uint32_t *a)
{
    const uint32_t v = a[0] * 2654435761u;
    *As<uint8_t>(a[1]) = uint8_t(v >> 24);
    *As<uint8_t>(a[2]) = uint8_t(v >> 16);
    *As<uint8_t>(a[3]) = uint8_t(v >> 8);
    return 0;
}
// The colour service's slot 34 (r, g, b): a value of the low bytes (the callee's view of its byte arguments).
uint32_t ColorFromRGB(Fake *, uint32_t *a) { return 0xc0000000u | (a[0] & 0xff) << 16 | (a[1] & 0xff) << 8 | (a[2] & 0xff); }
// The window's slot 120 (r, g, b): the callee reads only the low bytes (gz/cGZWin.h: "from the low bytes of"), and the
// original passes byte locals whose upper bytes it never wrote, so only the low bytes are logged.
uint32_t WinRGB(Fake *self, uint32_t *a)
{
    Ev(0x6120);
    Ev(a[0] & 0xff), Ev(a[1] & 0xff), Ev(a[2] & 0xff);
    self->value[7] = (a[0] & 0xff) << 16 | (a[1] & 0xff) << 8 | (a[2] & 0xff);
    return 0;
}

// Slot 120's own thunk (not Dispatch, which logs the raw words).
uint32_t __fastcall WinRGBThunk(Fake *self, void *, uint32_t r, uint32_t g, uint32_t b)
{
    uint32_t a[3] = {r, g, b};
    return WinRGB(self, a);
}

// ---- the stand-in constructors ----

template <unsigned Kind, uint32_t Offset> void *__fastcall Ctor0(void *self, void *)
{
    Ev(0x77000000 | Kind);
    ::operator delete(self);
    return reinterpret_cast<uint8_t *>(&F(Kind)) - Offset;
}
template <unsigned Kind, uint32_t Offset> void *__fastcall Ctor1(void *self, void *, uint32_t arg)
{
    Ev(0x77000000 | Kind);
    Ev(arg);
    ::operator delete(self);
    return reinterpret_cast<uint8_t *>(&F(Kind)) - Offset;
}

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

void SetControl(Fake &c, std::mt19937 &rng)
{
    c.Set(0, "dp", 0, &Query);
    c.Set(3, 0, P(&F(kWin)));
    c.value[0] = rng() % 16;
}

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 11);
    svcfx::ResetAll();
    for (unsigned i = 0; i < kFakeCount; ++i) F(i).Reset(300 + i);
    gWorld.framework = rng() % 6 != 0;
    gWorld.colorSvc = rng() % 4 != 0;
    gWorld.sound = rng() % 4 != 0;
    gWorld.comMask = rng() & 0x7ff;
    F(kFramework).Set(5, "ddp", 0, &GetSystemService);
    F(kColorSvc).Set(34, "ddd", 0, &ColorFromRGB);
    F(kColorSvc).Set(36, "dppp", 0, &ColorToRGB);
    F(kSound).Set(4, "d");
    F(kSound).Set(5, 0, 0x5000 + rng() % 9);
    F(kSound).Set(6, "d", 0x6000 + rng() % 9);
    F(kSound).Set(7, "dd-");
    static const uint32_t kHeights[] = {0, 1, 7, 0x10, 11, uint32_t(-3), 0x7fffffff, 0x80000000u, 0x1000001};
    for (unsigned i = 0; i < kFonts; ++i) {
        F(kFont0 + i).Set(12, 0, 0x1200 + i);
        F(kFont0 + i).Set(37, 2, kHeights[rng() % 9]);
    }
    for (unsigned i = 0; i < kImages; ++i) {
        Fake &img = F(kImage0 + i);
        img.Set(9, 0, 0x90 + rng() % 50);
        img.Set(10, 0, 0xa0 + rng() % 50);
        img.Set(11, "p", 0, &ImageRect);
        img.value[4] = rng() % 100;
    }
    for (unsigned i = 0; i < kCreators; ++i) {
        Fake &c = F(kCreator0 + i);
        c.Set(0, "dp", 0, &CreatorQuery);
        c.Set(4, 0);
        c.Set(5, 0);
        c.Set(6, "ddp", 0, &CreatorCreate);
        c.value[0] = rng() % 4 != 0;
        c.ret[4] = (rng() % 4 != 0 ? 1u : 0u) | 0x4400u;
        static const uint32_t kTypes[] = {0xe8963e41, 0x88963e39, 0x68963e48, 0x12345};
        c.value[2] = kTypes[rng() % 4];
        c.value[3] = c.value[2] == 0xe8963e41 ? kC231a : kWindowCtl;
    }
    F(kMatcher).Set(12, "pd", 0, &Match);
    F(kMatcher).value[0] = kObj0 + rng() % kObjs;

    // The controls and their slots (arities from the creators' call sites).
    for (unsigned k = kBtn; k <= kC231a; ++k) SetControl(F(k), rng);
    F(kC231a).value[0] |= 4; // answers its own class iid (CreateControl's 0x231a1c57) most of the time
    if (rng() % 5 == 0) F(kC231a).value[0] &= ~4u;
    Fake &btn = F(kBtn);
    btn.Set(12, "dpp", 0, &BtnImage), btn.Set(19, "p"), btn.Set(23, "dd"), btn.Set(27, "d"), btn.Set(30, "dd");
    Fake &text = F(kText);
    text.Set(18, "p"), text.Set(10, "d"), text.Set(21, "d"), text.Set(16, 1), text.Set(7, "d-"), text.Set(5, 2);
    Fake &grp = F(kOptGrp);
    grp.Set(5, "dd"), grp.Set(29, "dddd"), grp.Set(36, "-d");
    F(kBmp).Set(4, "p");
    Fake &slider = F(kSlider);
    slider.Set(10, "d"), slider.Set(12, "p-"), slider.Set(6, "d"), slider.Set(8, "d"), slider.Set(5, "d");
    Fake &scroll = F(kScroll);
    scroll.Set(4, 0, P(&F(kBar))), scroll.Set(5, "d");
    Fake &bar = F(kBar);
    bar.Set(10, "d"), bar.Set(12, "p-"), bar.Set(6, "d"), bar.Set(8, "d");
    Fake &spin = F(kSpinner);
    spin.Set(14, "p-"), spin.Set(10, "dd"), spin.Set(9, "d"), spin.Set(12, "d"), spin.Set(4, "d"), spin.Set(31, "dddd"), spin.Set(23, "dd");
    Fake &line = F(kLine);
    line.Set(28, "d"), line.Set(11, "d"), line.Set(14, "dd");
    Fake &c598 = F(kC598);
    c598.Set(41, "p"), c598.Set(42, "dd"), c598.Set(68, "d"), c598.Set(47, "d"), c598.Set(52, "d"), c598.Set(54, "d"), c598.Set(70, "dd");
    Fake &c231 = F(kC231a);
    c231.Set(9, "p"), c231.Set(84, "d"), c231.Set(21, "d"), c231.Set(23, "d"), c231.Set(12, "dd"), c231.Set(13, "d"), c231.Set(18, "d");
    c231.Set(39, "dd"), c231.Set(82, "d"), c231.Set(6, "--dd"), c231.Set(45, 0);
    Fake &win = F(kWin);
    win.Set(62, "dd"), win.Set(64, "dddd"), win.Set(79, "p"), win.Set(90, "p"), win.Set(92, "d"), win.Set(119, "d");
    win.table[120] = reinterpret_cast<void *>(&WinRGBThunk);

    svcfx::F(0).Set(3, "ddp", 0, reinterpret_cast<svcfx::Custom>(&GetClassObject));
    *GameAddress<void *>(0x10c9378) = gWorld.framework ? &F(kFramework) : nullptr;
}

void CaptureFakes(Blob &b)
{
    for (const Fake &o : gFakes) {
        b.push_back(uint32_t(o.refs));
        for (uint32_t v : o.value) b.push_back(v); // data all: masks, answers, types, fake indices, a rectangle seed
    }
    svcfx::CaptureFakes(b);
}

// ---- driving ----

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }
Mgr &View(void *object) { return *static_cast<Mgr *>(static_cast<rz::cRZSystemService *>(object)); }
uint32_t Word32(const uint8_t *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(m + off); }

bool gFactory;

void *Build(difftest::Context &ctx, bool ours)
{
    if (gFactory) {
        void *object = ours ? static_cast<void *>(Mgr::Create()) : ctx.Original<void *(__cdecl *)()>(0x92f732)();
        return object;
    }
    void *memory = ::operator new(Mgr::kSize);
    std::memset(memory, 0xc5, Mgr::kSize);
    if (ours) ::new (memory) Mgr;
    else ctx.Original<void *(__thiscall *)(void *)>(0x94ac21)(memory);
    return memory;
}

void DumpTable(Blob &b, const Mgr::ObjectTable &t)
{
    b.push_back(uint32_t(t.size()));
    b.push_back(uint32_t(t.bucket_count()));
    for (const auto &e : t) b.push_back(e.first), b.push_back(Norm(P(e.second)));
}

void Dump(Blob &b, void *object, bool ours)
{
    const auto *m = static_cast<const uint8_t *>(object);
    // The game's vtables of each face (kept here, not in src/: our code must not hold them, tools/callers.py).
    static const uint32_t kFaces[3][2] = {{0, 0xe56030}, {4, 0xe56068}, {0x1c, 0xe56084}};
    for (const auto &f : kFaces) {
        const bool game = Word32(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    for (uint32_t off = 8; off < 0x18; off += 4) b.push_back(Word32(m, off));
    b.push_back(m[0x18]); // the running flag (its padding is the allocator's or the fill)
    b.push_back(m[0x20]); // the init byte
    b.push_back(Norm(Word32(m, 0x24)));
    b.push_back(Norm(Word32(m, 0x28)));
    const Mgr &mgr = View(object);
    b.push_back(uint32_t(mgr.mColors.size()));
    b.push_back(uint32_t(mgr.mColors.bucket_count()));
    for (const auto &e : mgr.mColors) b.push_back(e.first), b.push_back(e.second);
    DumpTable(b, mgr.mFonts);
    DumpTable(b, mgr.mImages);
    DumpTable(b, mgr.mObjects8C);
    DumpTable(b, mgr.mObjectsAC);
    b.push_back(uint32_t(mgr.mCreators.size()));
    for (cIGZUnknown *c : mgr.mCreators) b.push_back(Norm(P(c)));
    b.push_back(Word32(m, 0xd4));
}

struct Op {
    unsigned kind;
    uint32_t a, c;
};

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 7);
    std::vector<Op> ops;
    const unsigned n = 6 + rng() % 34;
    for (unsigned i = 0; i < n; ++i) ops.push_back({rng() % 20, rng(), rng()});
    return ops;
}

constexpr uint32_t kTypes[] = {0x28963e2c, 0x28963e36, 0x48963e28, 0x48963e30, 0x48963e33, 0x68963e21,
                               0x68963e45, 0x68963e48, 0x88963e15, 0x88963e39, 0xa8963e4b, 0xc8963e1d,
                               0xc8963e3c, 0xe8963e24, 0xe8963e41, 0xe8963e4d, 0x88963e14, 0x48963e34,
                               0x28963e2b, 0xffffffff, 0x68963e46, 0xe8963e4e, 0x12345};
uint32_t PickType(uint32_t a) { return kTypes[a % (sizeof(kTypes) / sizeof(kTypes[0]))]; }
// Table ids: the ids the creators read, plus a few others.
constexpr uint32_t kIds[] = {0x68963c4c, 0x68963c54, 0xc8963c5c, 0xe8963ef1, 0x88963ec1, 0xa8963ec4,
                             0xe8963ec7, 0x68963ed6, 0xa8963ef3, 0xc8963ef6, 7, 0};
uint32_t PickId(uint32_t a) { return kIds[a % (sizeof(kIds) / sizeof(kIds[0]))]; }
// Colour ids: Init's and a few others.
constexpr uint32_t kColorIds[] = {0x28963f93, 0xa8963fad, 0x08963faf, 0x08963fb9, 0x28963fbc, 0x08963fb7,
                                  0x88963fb4, 0xa8963fb2, 0x68963f7d, 3, 0x12};
uint32_t PickColorId(uint32_t a) { return kColorIds[a % (sizeof(kColorIds) / sizeof(kColorIds[0]))]; }
void *PickHeld(unsigned base, unsigned count, uint32_t a) { return a % 5 == 0 ? nullptr : &F(base + a % count); }
void *PickAny(uint32_t a)
{
    if (a % 6 == 0) return nullptr;
    static const unsigned kAny[] = {kFont0, kImage0 + 1, kObj0 + 2, kParent, kCreator0};
    return &F(kAny[a % 5]);
}

// A control a creator returned: its out word, then released by the test (the caller's reference).
void TakeControl(Blob &b, uint32_t made)
{
    b.push_back(Norm(made));
    if (made) Slot<uint32_t>(As<void>(made), 2);
}

void RunOp(Blob &b, void *object, const Op &op)
{
    void *const service = object;
    void *const face = Face(object, 0x1c);
    const uint32_t a = op.a, c = op.c;
    switch (op.kind) {
    case 0: // Init through either face
        b.push_back(Slot<uint32_t>(a % 2 ? service : face, a % 2 ? 8u : 3u) & 0xff);
        break;
    case 1: // Shutdown through either face (rarely: the tables would empty too often)
        if (a % 4) return;
        b.push_back(Slot<uint32_t>((a >> 2) % 2 ? service : face, (a >> 2) % 2 ? 9u : 4u) & 0xff);
        break;
    case 2: { // a table: get / set
        static const unsigned kGet[4] = {5, 7, 35, 37}, kSet[4] = {6, 8, 36, 39};
        static const unsigned kBase[4] = {kFont0, kImage0, kObj0, kObj0};
        const unsigned t = a % 4;
        if ((a >> 2) % 3) Slot<void>(face, kSet[t], PickId(c), PickHeld(kBase[t], 3, c >> 8));
        b.push_back(Norm(Slot<uint32_t>(face, kGet[t], PickId(c >> 16))));
        break;
    }
    case 3: // FindObjectAC
        F(kMatcher).value[0] = kObj0 + a % kObjs;
        b.push_back(Slot<uint32_t>(face, 38, &F(kMatcher)));
        break;
    case 4: { // colours
        const uint32_t id = PickColorId(a);
        switch ((a >> 8) % 4) {
        case 0: // bytes with noise above
            Slot<void>(face, 33, id, (c & 0xff) | 0xab00, (c >> 8 & 0xff) | 0xcd0000, (c >> 16 & 0xff) | 0x1200);
            break;
        case 1: {
            uint32_t out[3] = {0xa5a5a5a5, 0xa5a5a5a5, 0xa5a5a5a5};
            Slot<void>(face, 31, id, reinterpret_cast<uint8_t *>(&out[0]), reinterpret_cast<uint8_t *>(&out[1]) + 1,
                       reinterpret_cast<uint8_t *>(&out[2]) + 3);
            b.push_back(out[0]), b.push_back(out[1]), b.push_back(out[2]);
            break;
        }
        case 2: b.push_back(Slot<uint32_t>(face, 32, id)); break;
        default: Slot<void>(face, 34, id, c); break;
        }
        break;
    }
    case 5: // the sound manager
        switch (a % 4) {
        case 0: Slot<void>(face, 27, c); break;
        case 1: b.push_back(Slot<uint32_t>(face, 28)); break;
        case 2: b.push_back(Slot<uint32_t>(face, 29, c)); break;
        default: Slot<void>(face, 30, c, a); break;
        }
        break;
    case 6: // the word +0xd4
        if (a % 2) Slot<void>(face, 41, c);
        b.push_back(Slot<uint32_t>(face, 40));
        break;
    case 7: { // creators
        void *creator = a % 7 == 0 ? nullptr : &F(kCreator0 + a % kCreators);
        b.push_back(Slot<uint32_t>(face, (a >> 4) % 3 ? 24u : 25u, creator) & 0xff);
        break;
    }
    case 8: case 9: { // CreateControl (creators first) or CreateDefaultControl
        uint32_t out = 0x5e5e5e5e;
        const uint32_t type = PickType(a);
        const uint32_t iid = kIids[(a >> 8) % 4];
        b.push_back(Slot<uint32_t>(face, op.kind == 8 ? 26u : 42u, type, iid, &out) & 0xff);
        b.push_back(NormOut(out));
        if (out != 0x5e5e5e5e) Slot<uint32_t>(As<void>(out), 2);
        break;
    }
    case 10: { // the button creators
        static const unsigned kKinds[7] = {9, 11, 12, 20, 21, 22, 23};
        int32_t rect[4] = {1, 2, 3, 4};
        void *image = a % 3 ? &F(kImage0 + a % kImages) : nullptr;
        if ((a >> 4) % 4 == 0)
            TakeControl(b, Slot<uint32_t>(face, 10, c % 5, PickAny(c >> 3), PickAny(c >> 6), image, rect, c >> 12));
        else
            TakeControl(b, Slot<uint32_t>(face, kKinds[(a >> 4) % 7], PickAny(c >> 3), PickAny(c >> 6), image,
                                          (a >> 9) % 2 ? rect : nullptr, c >> 12));
        break;
    }
    case 11: { // the other creators
        int32_t rect[4] = {int32_t(c % 50), int32_t(c >> 8 & 0x3f), int32_t(c >> 16 & 0xff), int32_t(c >> 24)};
        void *parent = PickAny(a >> 4);
        switch (a % 7) {
        case 0: TakeControl(b, Slot<uint32_t>(face, 13, parent, PickAny(c))); break;
        case 1: TakeControl(b, Slot<uint32_t>(face, 14, parent, PickAny(c), (a >> 8) % 3 ? rect : nullptr)); break;
        case 2:
            TakeControl(b, Slot<uint32_t>(face, 15, parent, (a >> 8) % 4 ? &F(kImage0 + c % kImages) : nullptr,
                                          (a >> 12) % 3 == 0 ? 0u : ((a >> 12) % 3 == 1 ? 1u : 0x100u)));
            break;
        case 3: TakeControl(b, Slot<uint32_t>(face, 16, parent, (c >> 4) % 3, c, c >> 1, c >> 2)); break;
        case 4: TakeControl(b, Slot<uint32_t>(face, 17, parent, (c >> 4) % 3, c, c >> 1, c >> 2)); break;
        case 5: TakeControl(b, Slot<uint32_t>(face, 18, parent, c, c >> 1, c >> 2, c >> 3, c >> 4)); break;
        default: TakeControl(b, Slot<uint32_t>(face, 19, parent, c)); break;
        }
        break;
    }
    case 12: { // QueryInterface on a face, AddRef / Release through others
        static const uint32_t kAsk[] = {1, cIGZWinCtrlMgr::kIid, 0x287fb697, Mgr::kServiceId, 0x12345678};
        void *faces[] = {service, Face(object, 4), face};
        uint32_t q = 0x5e5e5e5e;
        const uint32_t ok = Slot<uint32_t>(faces[a % 3], 0, kAsk[(a >> 4) % 5], &q) & 0xff;
        b.push_back(ok);
        b.push_back(NormOut(q));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 3], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 3], 2));
        if (ok) b.push_back(Slot<uint32_t>(As<void>(q), 2));
        break;
    }
    case 13: // the cIGZSystemService accessors
        b.push_back(Slot<uint32_t>(service, 3));
        b.push_back(Slot<uint32_t>(service, 5));
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        b.push_back(Slot<uint32_t>(service, 10, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 11, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 12));
        Slot<void>(service, 7, a % 2);
        b.push_back(Slot<uint32_t>(Face(object, 4), 5));
        break;
    case 14: { // the world changes
        gWorld.comMask ^= 1u << (a % 11);
        gWorld.colorSvc = (c >> 3) % 4 != 0;
        gWorld.sound = (c >> 5) % 4 != 0;
        Fake &cr = F(kCreator0 + c % kCreators);
        cr.ret[4] ^= 1;
        static const uint32_t kTypes2[] = {0xe8963e41, 0x88963e39, 0x68963e48, 0x12345};
        cr.value[2] = kTypes2[(c >> 8) % 4];
        cr.value[0] ^= (c >> 12) % 3 == 0;
        F(kBtn + (a >> 8) % (kC231a - kBtn + 1)).value[0] = c >> 16 & 15;
        break;
    }
    default: // more of the creating operations
        RunOp(b, object, {8 + (op.kind - 15) % 4, a, c});
        break;
    }
}

} // namespace winctrlmgr_test

using namespace winctrlmgr_test;

namespace winctrlmgr_test {
void RunSeeds(difftest::Context &ctx, const char *name, uint32_t seeds);
}

DIFF_TEST(WinCtrlMgr_Run, 0x94ac21) { RunSeeds(ctx, "WinCtrlMgr_Run", 3000); }

// The same run, fewer seeds, registered at CreateDefaultControl so difftest::FillOut can measure the image rectangle
// (int32_t[4]) in its frame (tests/stack_out.h measures only in frames of functions the harness knows). The other stack
// objects the fakes write are single void* outs (GetSystemService, GetClassObject, QueryInterface, a creator's slot 6):
// the callee writes exactly one pointer by the interface, and FillOut over-measures one of them (the original's list
// local in 0x94af1f measures 8: the word after it is never written), so they get no FillOut.
DIFF_TEST(WinCtrlMgr_DefaultControl, 0x94af1f) { RunSeeds(ctx, "WinCtrlMgr_DefaultControl", 300); }

void winctrlmgr_test::RunSeeds(difftest::Context &ctx, const char *name, uint32_t seeds)
{
    // Also reached (see the file comment): 0x92f732 (factory), 0x94acd4, 0x94ad7f, 0x94ad77, 0x94c1a3, 0x94c1cd,
    // 0x94c1d5, 0x94bf56, 0x94c19b, 0x94c581, 0x94c66b, 0x94c69e, 0x94c4d7, 0x94c673, 0x94c455, 0x94ada1, 0x94adbe,
    // 0x94b791, 0x94aeb3, 0x94b7ae, 0x94b95e, 0x94bd40, 0x94bb54, 0x94ba91, 0x94bc1b, 0x94b895, 0x94c1dd, 0x94c235,
    // 0x94aed0, 0x94c572, 0x94bf47, 0x94c6c9, 0x94c559, 0x94be2a, 0x94bde5, 0x94c286, 0x94c2e8, 0x94beaf, 0x94c335,
    // 0x94beda, 0x94bf0b, 0x94c3cd, 0x254680, 0x94c3c0, 0x94af1f, 0x94c88a.
    svcfx::Globals globals;
    globals.SetWord(0x10c9378, 0);
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::StandIn btnCtor(0x93536f, &cGZWinBtn::Construct, &Ctor0<kBtn, 0>);
    shape::StandIn textCtor(0x949ed1, &cGZWinText::Construct, &Ctor0<kText, 0xf8>);
    shape::CodePatch grpCtor(0x95b137, shape::CodeOf(&Ctor0<kOptGrp, 0xf8>));
    shape::CodePatch bmpCtor(0x93741a, shape::CodeOf(&Ctor0<kBmp, 0xf8>));
    shape::CodePatch sliderCtor(0x957b0e, shape::CodeOf(&Ctor0<kSlider, 0xf8>));
    shape::CodePatch scrollCtor(0x9586a8, shape::CodeOf(&Ctor0<kScroll, 0x144>));
    shape::CodePatch spinCtor(0x95c6d1, shape::CodeOf(&Ctor0<kSpinner, 0xf8>));
    shape::CodePatch lineCtor(0x95d9dd, shape::CodeOf(&Ctor1<kLine, 0xf8>));
    shape::CodePatch windowCtor(0x937b4c, shape::CodeOf(&Ctor0<kWindowCtl, 0xf8>));
    shape::CodePatch c231Ctor(0x94cb2e, shape::CodeOf(&Ctor1<kC231a, 0xf8>));
    shape::CodePatch c598Ctor(0x953f2f, shape::CodeOf(&Ctor1<kC598, 0xf8>));
    svcfx::gFrameworkIndex = 0; // the COM object: svcfx fake 0

    bool failed = false;
    for (uint32_t seed = 1; seed <= seeds && !failed; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 7 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            void *object = Build(ctx, side == 1);
            svcfx::AddRange(object, Mgr::kSize, kTag);
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else {
                    // The last release destroys it; every fifth seed the +4 face's deleting destructor (flags 1) instead.
                    if (seed % 5) b.push_back(Slot<uint32_t>(object, 2));
                    else b.push_back(Norm(Slot<uint32_t>(Face(object, 4), 3, 1u)));
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                CaptureFakes(b);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], name, seed)) {
                ctx.Fail("  at op %u of %u (kind %u)", unsigned(i), unsigned(ops.size()), i < ops.size() ? ops[i].kind : 99u);
                failed = true;
                break;
            }
        }
    }
    *GameAddress<void *>(0x10c9378) = nullptr;
}
