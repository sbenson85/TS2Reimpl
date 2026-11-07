// Differential tests for cGZLegacyControl (lane MC, src/gz/cGZLegacyControl.h): the class in the standalone shape, and
// FindUint32Attribute.
//
// LegacyControl_Run: a control made by the original creator (0x85b2b3: the constructor 0x85d62d, game vtables 0xe482a4 /
// 0xe482d4 / 0xe482f4) against one made by ours, each driven through its own vtables by the same random operations,
// then destroyed by the last Release (0x86dec9, thunks 0x86dee0 / 0x86dee8; the deleting destructor 0x85da06, the
// destructor 0x85d82b). Operations: QueryInterface on the three faces (0x86dd98, thunks 0x86ddfb / 0x86de03), AddRef
// (folded 0x15e520, thunks 0x1f4daf / 0x1f4db7), Create (0x86dfdc, with FindUint32Attribute 0x86401e and the window
// procedure hand-over 0x863aec), GetVocab (0x863dee), GetId (0x863df4), SetId (0x86dfc2), SetOwnsWindow (folded
// 0x2281f0, legacy thunk 0x86dfd4), Bind (folded 0x1ac160), SetParent (0x86e211), Render (folded 0x148260), SetRect
// (0x86def0), GetRect (0x863b87), HitTest (folded 0x1477b0), GetWindow (folded 0x46390), SetWindow (0x863b5b),
// SetWinProc (0x86e29c), GetScript (0x86e758).
//
// The world: windows are winfx fakes (tests/winfx.h: state-backed ids, areas, flags, faces), the window manager a winfx
// fake whose main window is window 0; GZCOM's GetClassObject (the director framework's slot 3, a recording fake) hands
// out the next window for the classes the seed makes creatable, the shared vocabulary (a fake) for class 0xba2e7954, or
// refuses. The vocabulary cell (0x10e6890) starts empty or holding the fake. The types and handlers tables (0x10e689c /
// 0x10e6894) are built per seed in place (std::map, as the game's); their handlers are recorders. Stood in for on both
// sides (the same recorder): the 22 registrations, ApplyCommonProperties 0x86549d (ours since F5b, tested by gz_legacy_properties) and
// the script writer 0x87a9a9. The windows' SetProperty (slot 127) logs the variant it gets by content (type, value)
// and keeps it counted until the side ends. Attribute lists are ours (cGZArray), their values recording fakes typed as
// uint32 variants or not. Compared after every operation: the result, both event logs, every field (the window, the
// parent window and the procedure by identity, the counts, the flag), the vocabulary cell, every fake's count, the
// windows' state and counts. Data words (class ids, iids, property ids, flags, the attribute values) are compared as they
// are, never through Norm (docs/lane/test.md, "Normalised words"). A TS2_FAILURE_NOTES=1 sweep still names the windows'
// QueryInterface iids and SetProperty ids and variants: drawfx logs (and audits) them through Norm before the handlers
// put the words back as they are.
//
// LegacyControl_FindUint32: FindUint32Attribute 0x86401e against ours on generated lists (ids, null values, value types,
// duplicates): the result, the value written (or the untouched marker), the event log.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"
#include "winfx.h"

#include "game/game.h"
#include "gz/cGZLegacyControl.h"
#include "gz/cGZLegacyControlRegistrations.h"
#include "gz/cGZLegacyScriptWriter.h"
#include "gz/cIGZString.h"
#include "rz/cIGZVariant.h"
#include "rz/cRZString.h"

#include <cstring>
#include <iterator>
#include <new>
#include <random>
#include <vector>

namespace legacy_control_test {

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Ctl = cGZLegacyControl;

constexpr uint32_t kTag = 0x7d000000;     // the control
constexpr uint32_t kListTag = 0x7c000000; // an attribute list
enum : unsigned { kFramework = 0, kVocab, kParent, kParentFace, kProc, kProcB, kValue };
constexpr unsigned kValues = 6; // fakes kValue.. : attribute values
constexpr uint32_t kValueTypes[] = {6, 6, 6, 4, 7, 0}; // GetType: uint32 (6) or not
constexpr unsigned kWindows = 10;
constexpr uint32_t kMgrCell = 0x10f51ac; // GetCachedServiceObjectD's
constexpr uint32_t kClasses[] = {0x11110001, 0x11110002, 0x11110003, Ctl::kDefaultWindowClass};
constexpr unsigned kClassCount = sizeof(kClasses) / sizeof(kClasses[0]);
constexpr uint32_t kTypes[] = {0x5501, 0x5502, 0x5503};
constexpr unsigned kTypeCount = sizeof(kTypes) / sizeof(kTypes[0]);
constexpr uint32_t kAttributeIds[] = {Ctl::kClassIdAttribute, Ctl::kTypeIdAttribute, 0x100, 0xf01a};
constexpr uint32_t kQueryIids[] = {1, 0x1a356083, 0x5a356159, 0x9a4527cb, 0x1a35608a};

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

winfx::World gWorld;
winfx::Mgr gMgr;
drawfx::Fake gHost; // the windows' 0x5386d516 face
unsigned gNextWindow;
uint32_t gCreatable; // a bit per kClasses entry
bool gScriptOk;
std::vector<cIGZVariant *> gProps; // what the windows' SetProperty kept

uint32_t D(const void *p) { return drawfx::Norm(P(p)); }

// GZCOM's GetClassObject.
uint32_t ClassObject(Obj *, uint32_t *a)
{
    Ev(0xc1a55);
    Ev(a[0]);
    Ev(a[1]);
    void **out = reinterpret_cast<void **>(uintptr_t(a[2]));
    if (a[0] == Ctl::kVocabClass) {
        if (a[1] != cIGZScriptVocab::kIID || Draw(4) == 0) return 0x100;
        ++F(kVocab).refs;
        *out = &F(kVocab);
        return 0x101;
    }
    for (unsigned i = 0; i < kClassCount; ++i) {
        if (a[0] != kClasses[i] || !(gCreatable >> i & 1) || a[1] != Ctl::kWinIid) continue;
        winfx::Win &w = gWorld[1 + gNextWindow++ % (kWindows - 1)];
        ++w.f.refs;
        *out = &w.f;
        return 0x101 | Draw(2) << 8;
    }
    return Draw(2) << 8; // refused: the low byte is what counts
}

// The parent control's QueryInterface: its legacy face now and then.
uint32_t ParentQuery(Obj *, uint32_t *a)
{
    if (a[0] != cIGZLegacyControlWindow::kIID || Draw(4) == 0) return 0x100;
    ++F(kParentFace).refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &F(kParentFace);
    return 0x101;
}

// An attribute value: slot 3 the variant type, slot 78 the address of its uint32. The uint32s (class ids, types, small
// values: data) live outside the fakes' value words, which CaptureFakes passes through Norm, and Capture compares them as
// they are.
uint32_t gValueWords[kValues];
uint32_t ValueRef(Obj *self, uint32_t *) { return P(&gValueWords[self - &F(kValue)]); }

// Data arguments of the windows' slots are logged as they are, not through drawfx::Norm: QueryInterface's iid (slot 0),
// IsFlagSet's flag (82) and SetProperty's id (127). A property id or flag that happened to fall inside one side's control
// or list block was rewritten on that side only (lane NL: LegacyControl_Run seed 2968 failed once in a full run; ids
// such as 0x08d6e462 lie in the heap's range). The wrapper calls the handler it replaced (winfx's, or the test's), the
// same for every window.
template <unsigned Slot, unsigned Mask> struct RawArgs {
    static inline drawfx::Handler next = nullptr;
    static uint32_t Fn(drawfx::Fake &self, const uint32_t *a)
    {
        drawfx::Call &call = drawfx::gLog.back();
        for (unsigned i = 0; i < 8 && i < call.words.size(); ++i)
            if (Mask >> i & 1) call.words[i] = a[i];
        return next(self, a);
    }
    static void Wrap(drawfx::Fake &f)
    {
        if (f.handler[Slot] == &Fn) return;
        next = f.handler[Slot];
        f.On(Slot, &Fn);
    }
};

// The window's SetProperty: the variant described in place of its address, kept counted.
uint32_t SetProperty(drawfx::Fake &, const uint32_t *a)
{
    auto *value = reinterpret_cast<cIGZVariant *>(uintptr_t(a[1]));
    drawfx::Call &call = drawfx::gLog.back();
    call.words[0] = a[0]; // the property id: data, as it is (RawArgs)
    call.words[1] = value ? 1 : 0;
    if (value) {
        call.words.push_back(value->GetType());
        call.words.push_back(*value->AsUint32Ref());
        value->AddRef();
        gProps.push_back(value);
    }
    return 1;
}

template <unsigned I> void __cdecl Registration() { drawfx::Note("registration", I); }

void __cdecl FormatStandIn(void *window, void *attributes, uint32_t apply)
{
    drawfx::Note("format", {D(window), D(attributes), apply & 0xff});
}
bool __cdecl HandlerA(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    drawfx::Note("handlerA", {D(window), D(attributes), uint32_t(apply)});
    return true;
}
bool __cdecl HandlerB(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    drawfx::Note("handlerB", {D(window), D(attributes), uint32_t(apply)});
    return false;
}
bool __cdecl WriteScript(void *window, cIGZString *text)
{
    drawfx::Note("write", {D(window), text->Strlen()});
    text->FromChar("legacy");
    return gScriptOk;
}

// The game's tables, built in place for a side and destroyed after it.
struct Tables {
    explicit Tables(uint32_t seed)
    {
        std::mt19937 rng(seed * 31 + 7);
        types = ::new (GameAddress<void>(Ctl::kTypesCell)) LegacyTypeMap;
        handlers = ::new (GameAddress<void>(Ctl::kHandlersCell)) LegacyHandlerMap;
        for (unsigned i = 0; i < kClassCount; ++i)
            if (rng() % 3) (*types)[kClasses[i]] = kTypes[rng() % kTypeCount];
        if (rng() % 2) (*types)[0x22220001] = kTypes[rng() % kTypeCount]; // a class GZCOM never makes
        const LegacyHandler choices[] = {&HandlerA, &HandlerB, nullptr};
        for (unsigned i = 0; i < kTypeCount; ++i)
            if (rng() % 4) (*handlers)[kTypes[i]] = choices[rng() % 3];
    }
    ~Tables()
    {
        types->~LegacyTypeMap();
        handlers->~LegacyHandlerMap();
        std::memset(GameAddress<void>(Ctl::kTypesCell), 0, sizeof(LegacyTypeMap));
        std::memset(GameAddress<void>(Ctl::kHandlersCell), 0, sizeof(LegacyHandlerMap));
    }
    Tables(const Tables &) = delete;
    Tables &operator=(const Tables &) = delete;
    LegacyTypeMap *types;
    LegacyHandlerMap *handlers;
};

void BuildFakes(uint32_t seed)
{
    svcfx::ResetAll();
    drawfx::ResetAll();
    std::mt19937 rng(seed * 13 + 5);
    F(kFramework).Set(3, "--p", 0, &ClassObject);
    F(kFramework).Data(3, 0x3); // the class id and the iid
    F(kParent).Set(0, "-p", 0, &ParentQuery);
    F(kParent).Data(0, 0x1); // the iid
    for (unsigned i = 0; i < kValues; ++i) {
        Obj &v = F(kValue + i);
        v.Set(3, 0, kValueTypes[rng() % 6]);
        v.Set(78, 0, 0, &ValueRef);
        const unsigned pick = rng() % 3;
        gValueWords[i] = pick == 0 ? kClasses[rng() % kClassCount] : pick == 1 ? kTypes[rng() % kTypeCount] : rng() % 3;
    }
    F(kVocab).refs = rng() % 2 ? 0 : 1000;
    gWorld.Build(seed, kWindows);
    for (unsigned i = 0; i < kWindows; ++i) {
        gWorld[i].f.On(127, &SetProperty);
        RawArgs<0, 0x1>::Wrap(gWorld[i].f);  // QueryInterface: the iid
        RawArgs<82, 0x1>::Wrap(gWorld[i].f); // IsFlagSet: the flag
        gWorld[i].flags |= (rng() % 3 == 0) ? 0x800000 : 0;
    }
    gHost.Reset<fx_slots::cIGZWin>(400);
    for (unsigned i = 1; i < kWindows; ++i)
        if (rng() % 2) gWorld[i].f.AddFace(cIGZWinProcHost::kIID, gHost);
    gMgr.Reset(401);
    gMgr.f.Returns(3, &gWorld[0].f);
    F(kParentFace).Set(3, 0, svcfx::P(&gWorld[1 + rng() % (kWindows - 1)].f));
    gCreatable = rng() & 0xf;
    gNextWindow = 0;
    gScriptOk = rng() % 2 != 0;
    gProps.clear();
}

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }

void Dump(Blob &b, void *c, const void *const tables[3], bool ours)
{
    for (unsigned i = 0; i < 3; ++i) {
        const void *game = GameAddress<void>(i == 0 ? Ctl::kVTable : i == 1 ? Ctl::kRenderVTable : Ctl::kLegacyVTable);
        b.push_back(Word(c, 4 * i) == P(ours ? tables[i] : game));
    }
    b.push_back(Word(c, 0xc));
    b.push_back(D(reinterpret_cast<void *>(uintptr_t(Word(c, 0x10)))));
    b.push_back(D(reinterpret_cast<void *>(uintptr_t(Word(c, 0x14)))));
    b.push_back(svcfx::Norm(Word(c, 0x18)));
    b.push_back(Word(c, 0x1c) & 0xff);
    b.push_back(svcfx::Norm(*GameAddress<uint32_t>(Ctl::kVocabCell)));
}

// A random attribute list (ours: the game's cGZArray layout), registered with both normalisers.
cGZScriptAttributes *MakeList()
{
    auto *list = new cGZScriptAttributes;
    const unsigned n = Draw(6);
    for (unsigned i = 0; i < n; ++i) {
        cGZScriptAttribute a;
        a.mId = kAttributeIds[Draw(4)];
        a.mValue = Draw(6) ? reinterpret_cast<cIGZUnknown *>(&F(kValue + Draw(kValues))) : nullptr;
        list->PushBack(a);
    }
    svcfx::AddRange(list, sizeof(*list), kListTag);
    drawfx::AddRange(list, sizeof(*list), kListTag);
    return list;
}

enum : unsigned {
    oQuery, oAddRef, oCreate, oGetVocab, oGetId, oSetId, oSetOwns, oBind, oSetParent, oRender, oSetRect, oGetRect,
    oHitTest, oGetWindow, oLegacyOwns, oSetWindow, oSetWinProc, oGetScript, oCount
};

void Op(Blob &b, void *c, unsigned op)
{
    void *const render = static_cast<uint8_t *>(c) + 4;
    void *const legacy = static_cast<uint8_t *>(c) + 8;
    void *const faces[] = {c, render, legacy};
    switch (op) {
    case oQuery: {
        void *out = reinterpret_cast<void *>(uintptr_t(0xffff5a5a)); // left alone, or the face
        void *face = faces[Draw(3)];
        const uint32_t r = Slot<uint32_t>(face, 0, kQueryIids[Draw(5)], &out) & 0xff;
        b.push_back(r);
        b.push_back(svcfx::NormOut(P(out), 0xffff5a5a));
        if (r) Slot<uint32_t>(out, 2);
        break;
    }
    case oAddRef: b.push_back(Slot<uint32_t>(faces[Draw(3)], 1)); break;
    case oCreate: {
        cGZScriptAttributes *list = MakeList();
        b.push_back(Slot<uint32_t>(c, 3, list) & 0xff);
        delete list;
        break;
    }
    case oGetVocab: b.push_back(svcfx::Norm(Slot<uint32_t>(c, 4))); break;
    case oGetId: b.push_back(Slot<uint32_t>(c, 5)); break;
    case oSetId: Slot<uint32_t>(c, 6, 0x1000 + Draw(16)); break;
    case oSetOwns: Slot<uint32_t>(c, 7, uint32_t(Draw(2) | 0x5a00)); break;
    case oBind: b.push_back(Slot<uint32_t>(c, 8, P(&F(kParent)), Draw(4), 0x1234u, Draw(9)) & 0xff); break;
    case oSetParent: b.push_back(Slot<uint32_t>(c, 9, Draw(4) ? P(&F(kParent)) : 0u) & 0xff); break;
    case oRender: b.push_back(Slot<uint32_t>(render, 3, P(&F(kProc))) & 0xff); break;
    case oSetRect: {
        int32_t rect[4] = {int32_t(Draw(50)), int32_t(Draw(50)), int32_t(50 + Draw(50)), int32_t(50 + Draw(50))};
        b.push_back(Slot<uint32_t>(render, 4, rect) & 0xff);
        break;
    }
    case oGetRect: {
        int32_t rect[4] = {-7, -7, -7, -7};
        b.push_back(Slot<uint32_t>(render, 5, rect) & 0xff);
        for (int32_t v : rect) b.push_back(uint32_t(v));
        break;
    }
    case oHitTest: {
        int32_t point[2] = {1, 2};
        b.push_back(Slot<uint32_t>(render, 6, point, P(&F(kProc))) & 0xff);
        break;
    }
    case oGetWindow: b.push_back(D(reinterpret_cast<void *>(uintptr_t(Slot<uint32_t>(legacy, 3))))); break;
    case oLegacyOwns: Slot<uint32_t>(legacy, 4, uint32_t(Draw(2) | 0x7700)); break;
    case oSetWindow: Slot<uint32_t>(legacy, 5, Draw(4) ? svcfx::P(&gWorld[Draw(kWindows)].f) : 0u); break;
    case oSetWinProc: {
        const unsigned pick = Draw(3);
        Slot<uint32_t>(legacy, 6, pick == 2 ? 0u : P(&F(pick ? kProcB : kProc)));
        break;
    }
    case oGetScript: {
        cIGZString *out = reinterpret_cast<cIGZString *>(uintptr_t(0x5a5a5a5a));
        const uint32_t r = Slot<uint32_t>(legacy, 7, &out) & 0xff;
        b.push_back(r);
        if (r) {
            b.push_back(out->Strlen());
            for (uint32_t i = 0; i < out->Strlen(); ++i) b.push_back(uint8_t(out->Data()[i]));
            out->Release();
        } else {
            b.push_back(P(out));
        }
        break;
    }
    }
}

void Capture(Blob &b)
{
    svcfx::CaptureFakes(b);
    b.insert(b.end(), std::begin(gValueWords), std::end(gValueWords));
    for (unsigned i = 0; i < kWindows; ++i) b.push_back(uint32_t(gWorld[i].f.refs));
    b.push_back(uint32_t(gHost.refs));
    const std::vector<uint32_t> state = gWorld.State();
    b.insert(b.end(), state.begin(), state.end());
}

} // namespace legacy_control_test

using namespace legacy_control_test;

DIFF_TEST(LegacyControl_Run, 0x85b2b3)
{
    // Also reached: every RVA in the file comment's LegacyControl_Run paragraph.
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::StandIn format(0x86549d, &ApplyCommonProperties, &FormatStandIn);
    shape::StandIn write(0x87a9a9, &WriteLegacyScript, &WriteScript);
    shape::StandIn r0(0x85e431, &RegisterCommonNames, &Registration<0>);
    shape::StandIn r1(0x86024e, &RegisterFlatRect, &Registration<1>);
    shape::StandIn r2(0x86249d, &RegisterText, &Registration<2>);
    shape::StandIn r3(0x86288d, &RegisterTextEdit, &Registration<3>);
    shape::StandIn r4(0x861263, &RegisterListBox, &Registration<4>);
    shape::StandIn r5(0x85e28d, &RegisterCombo, &Registration<5>);
    shape::StandIn r6(0x85dba9, &RegisterButton, &Registration<6>);
    shape::StandIn r7(0x85da3d, &RegisterBitmap, &Registration<7>);
    shape::StandIn r8(0x861fd1, &RegisterSlider, &Registration<8>);
    shape::StandIn r9(0x861e67, &RegisterScrollbar, &Registration<9>);
    shape::StandIn r10(0x860f01, &RegisterLineInput, &Registration<10>);
    shape::StandIn r11(0x8614af, &RegisterOptionGroup, &Registration<11>);
    shape::StandIn r12(0x862173, &RegisterSpinner, &Registration<12>);
    shape::StandIn r13(0x861999, &RegisterOutline, &Registration<13>);
    shape::StandIn r14(0x862fa9, &RegisterTextTicker, &Registration<14>);
    shape::StandIn r15(0x85f31a, &RegisterFileBrowser, &Registration<15>);
    shape::StandIn r16(0x863115, &RegisterTreeView, &Registration<16>);
    shape::StandIn r17(0x860508, &RegisterFolders, &Registration<17>);
    shape::StandIn r18(0x8605b7, &RegisterGrid, &Registration<18>);
    shape::StandIn r19(0x85f26b, &RegisterCustom, &Registration<19>);
    shape::StandIn r20(0x861b05, &RegisterScrollbar2, &Registration<20>);
    shape::StandIn r21(0x8635c7, &RegisterGen, &Registration<21>);
    globals.Set(kMgrCell, &gMgr.f);
    globals.SetWord(Ctl::kVocabCell, 0);

    const void *tables[3] = {};
    {
        BuildFakes(1);
        Tables t(1);
        globals.SetWord(Ctl::kVocabCell, P(&F(kVocab)));
        auto *probe = static_cast<Ctl *>(Ctl::Create());
        for (unsigned i = 0; i < 3; ++i) tables[i] = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(probe) + 4 * i);
        probe->AddRef();
        probe->Release();
    }

    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        std::mt19937 opRng(seed * 7919);
        std::vector<unsigned> ops(1 + opRng() % 14);
        for (unsigned &op : ops) op = opRng() % oCount;
        if (opRng() % 2) ops.insert(ops.begin() + opRng() % ops.size(), oCreate);
        Blob results[2];
        drawfx::Log logs[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            gRng.seed(seed);
            Tables t(seed);
            const bool shared = Draw(3) != 0;
            *GameAddress<uint32_t>(Ctl::kVocabCell) = shared ? P(&F(kVocab)) : 0;
            Blob &b = results[side];
            void *c = side ? static_cast<void *>(Ctl::Create()) : ctx.Original<void *(__cdecl *)()>(0x85b2b3)();
            svcfx::AddRange(c, Ctl::kSize, kTag);
            drawfx::AddRange(c, Ctl::kSize, kTag);
            Dump(b, c, tables, side == 1);
            Slot<uint32_t>(c, 1); // the owner's reference
            for (size_t i = 0; i < ops.size(); ++i) {
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                b.push_back(0x0b000000 | ops[i]);
                Op(b, c, ops[i]);
                Dump(b, c, tables, side == 1);
            }
            b.push_back(Slot<uint32_t>(c, 2)); // the last release destroys it
            b.push_back(svcfx::Norm(*GameAddress<uint32_t>(Ctl::kVocabCell)));
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            Capture(b);
            for (cIGZVariant *v : gProps) v->Release();
            gProps.clear();
            logs[side] = drawfx::gLog;
            svcfx::gRanges.clear();
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyControl_Run", seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], "LegacyControl_Run log", seed)) break;
    }
    *GameAddress<uint32_t>(Ctl::kVocabCell) = 0;
}

DIFF_TEST(LegacyControl_FindUint32, 0x86401e)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        Blob results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            gRng.seed(seed);
            cGZScriptAttributes *list = MakeList();
            const uint32_t id = kAttributeIds[Draw(4)];
            uint32_t out = 0xdddddddd;
            const bool r = side ? FindUint32Attribute(list, id, &out)
                                : ctx.Original<bool(__cdecl *)(const void *, uint32_t, uint32_t *)>(0x86401e)(list, id, &out);
            Blob &b = results[side];
            b.push_back(r);
            b.push_back(out);
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            delete list;
            svcfx::gRanges.clear();
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyControl_FindUint32", seed)) break;
    }
}
