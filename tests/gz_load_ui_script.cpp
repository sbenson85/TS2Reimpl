// Difftests for LoadUIScript and its window helpers (lane MD, foundations F5b): src/gz/LoadUIScript.cpp.
//
// One world per seed, built again for each side: winfx windows (a random tree) whose GetProperty (slot 126) answers the
// script's condition properties 0x30000ef..0x30000f3 from a per-seed table of variants (ours, cRZVariant: texts of
// generated conditions, and uint32 values), and faces the script code asks for (a win proc host 0x5386d516, a button
// 0x8810 whose slot 3 is a window, a text edit 0x231a1c57 whose slot 5 is its window and whose slots 95 / 96 take
// scrollbars, a scrollbar 0x4b42da53 with an orientation); slot 43 (GetChildInterface) finds a direct child by id and
// asks it for the face. The services are svcfx fakes: the framework (cell 0x10c9378) hands out the UI script service,
// the resource manager, the window manager and the settings face (or refuses); the cheat console (cell 0x10faf68)
// gives a converter whose slots 30-33 parse a text (bool, float in ST0, sint32, uint32); the settings face (cell
// 0x10fb348, guard 0x10fb34c) gives a string-keyed property set over a per-seed table of settings (fake cIGZAny values
// of every type the code compares, and one it cannot: a string set); the message server (cell 0x10f519c) logs each
// sent / posted message by content (type, values, its strings' texts, its object) and answers a condition message's
// data 1 from the name and value. The variants and settings are built once per seed and handed to both sides.
//
// Compared: the result and outs, the window log (every call on a window or face, with notes for the converter,
// settings and message calls in order), the services' event log and counts, the variants' counts, the screen size
// cells, and the settings cell and whether the guard was set.
//
// Operator new / delete (0x824632 / 0x824624) are ours on the original side too: the message strings our side makes
// are freed by the game's string code. The settings static is mostly given (guard INT_MIN, the cell the fake or null);
// some cases start it unbuilt (guard 0, the thread's statics epoch -1), so its framework query runs.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"
#include "winfx.h"

#include "game/game.h"
#include "gz/LoadUIScript.h"
#include "gz/cGZLegacyControl.h"
#include "gz/cGZPersistResourceManager.h"
#include "gz/cGZPropertySet.h"
#include "gz/cIGZPropertySet.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"

#include <intrin.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace load_ui_script_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{4, "d"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;

constexpr uint32_t kServiceCell = 0x10c9378;
constexpr uint32_t kConsoleCell = 0x10faf68;
constexpr uint32_t kSettingsCell = 0x10fb348;
constexpr uint32_t kSettingsGuard = 0x10fb34c;
constexpr uint32_t kWidthCell = 0x10fb340;
constexpr uint32_t kHeightCell = 0x10fb344;
enum : unsigned { kFramework = 0, kUIService, kLegacy, kSet, kResMgr, kConsole, kConverter, kSettings, kPropSet, kServer };

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
template <size_t N> const char *Pick(const char *const (&pool)[N]) { return pool[Draw(uint32_t(N))]; }

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

void NoteText(const char *label, const char *text, std::initializer_list<uint32_t> words = {})
{
    drawfx::Call c;
    c.note = label;
    c.words.assign(words.begin(), words.end());
    c.text = text ? text : "(null)";
    drawfx::gLog.push_back(std::move(c));
}

// ---- the per-seed tables (built once, shared by both sides) ------------------------------------------------------------

// The settings: fake cIGZAny values (vtable, payload, type).
struct FakeAny {
    void *const *vt;
    uint32_t payload;
    uint32_t type;
};
template <unsigned Slot> uint32_t __fastcall AnySlot(FakeAny *self, void *)
{
    if constexpr (Slot == 4) {
        return self->type;
    } else { // no other slot is expected: logged, so a call shows in the comparison
        drawfx::Note("any slot", Slot);
        return 0;
    }
}
void *const kAnyVTable[] = {reinterpret_cast<void *>(&AnySlot<0>), reinterpret_cast<void *>(&AnySlot<1>),
                            reinterpret_cast<void *>(&AnySlot<2>), reinterpret_cast<void *>(&AnySlot<3>),
                            reinterpret_cast<void *>(&AnySlot<4>), reinterpret_cast<void *>(&AnySlot<5>),
                            reinterpret_cast<void *>(&AnySlot<6>), reinterpret_cast<void *>(&AnySlot<7>)};

struct Setting {
    const char *name;
    FakeAny any;
};
const char *const kSettingNames[] = {"alpha", "Beta", "gamma", "delta", "eps", "zeta"};
const char *const kServerNames[] = {"omega", "msgA", "x", "Alpha"};
const char *const kValues[] = {"1", "0", "-3", "42", "4294967295", "1.5", "-0.5", "abc", "Hello", "hello", "",
                               "true", "3", "0x10"};
const char *const kStringValues[] = {"hello", "Hello", "abc", "", "1", "a b"};
const uint32_t kTypes[] = {gzany::kTypeString, gzany::kTypeSint32, gzany::kTypeUint32, gzany::kTypeBoolean,
                           gzany::kTypeFloat32, gzany::kTypeStringSet};
std::vector<Setting> gSettings;
std::vector<cRZString *> gStrings; // the string settings' payloads (counted)

void BuildSettings()
{
    gSettings.clear();
    for (const char *name : kSettingNames) {
        if (!Draw(4)) continue;
        Setting s{name, {kAnyVTable, 0, kTypes[Draw(6)]}};
        switch (s.any.type) {
        case gzany::kTypeString: {
            auto *text = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
            text->Construct(Pick(kStringValues));
            text->AddRef();
            gStrings.push_back(text);
            s.any.payload = P(text);
            break;
        }
        case gzany::kTypeSint32: s.any.payload = uint32_t(int32_t(Draw(5)) - 3); break;
        case gzany::kTypeUint32: s.any.payload = Draw(3) ? Draw(4) : 42u; break;
        case gzany::kTypeBoolean: s.any.payload = Draw(2) | (Draw(2) ? 0xabcd00u : 0u); break; // a byte: the rest is noise
        case gzany::kTypeFloat32: {
            const float values[] = {1.5f, -0.5f, 0.0f, 3.0f};
            float f = values[Draw(4)];
            std::memcpy(&s.any.payload, &f, 4);
            break;
        }
        default: s.any.payload = 0x5e75e7; break;
        }
        gSettings.push_back(s);
    }
}
const Setting *FindSetting(const char *name)
{
    for (const Setting &s : gSettings)
        if (std::strcmp(s.name, name) == 0) return &s;
    return nullptr;
}

// A condition text: tokens separated by one (sometimes two) spaces.
std::string Term()
{
    const uint32_t kind = Draw(14);
    const char *name = Draw(3) ? Pick(kSettingNames) : Pick(kServerNames);
    const char *value = Pick(kValues);
    if (Draw(3) == 0) value = Pick(kStringValues);
    switch (kind) {
    case 0: return "and";
    case 1: return "or";
    case 2: return Draw(2) ? "AND" : "Or";
    case 3: return Draw(2) ? "xor" : "&&";
    case 4: return Draw(2) ? "=" : "!=";
    case 5: return std::string(name) + "=" + value + "=" + Pick(kValues);
    case 6: return std::string("=") + value;
    case 7: return std::string(name) + (Draw(2) ? "=" : "!=");
    case 8: return std::string(name) + "!" + "x=" + value;
    case 9: return std::string(name) + "!=" + value;
    default: return std::string(name) + "=" + value;
    }
}
// Tabs (the other blank the trims remove; a space would split the token): before or after the token, or around its '='.
std::string Tabbed(std::string token)
{
    const size_t at = token.find('=');
    switch (Draw(8)) {
    case 0: return "\t" + token;
    case 1: return token + "\t";
    case 2:
        if (at != std::string::npos) token.insert(at, "\t");
        return token;
    case 3:
        if (at != std::string::npos) token.insert(at + 1, "\t");
        return token;
    default: return token;
    }
}
std::string Condition()
{
    std::string text;
    if (Draw(10) == 0) text += ' ';
    const uint32_t n = Draw(6);
    for (uint32_t i = 0; i < n; ++i) {
        if (i) text += Draw(8) ? " " : "  ";
        // Mostly alternating terms and joiners, as real conditions are.
        if (i % 2 && Draw(4)) text += Tabbed(Draw(2) ? "and" : "or");
        else text += Tabbed(Term());
    }
    if (Draw(10) == 0) text += ' ';
    return text;
}

// The windows' properties: per window, per property 0x30000ef + p, a variant or null.
constexpr unsigned kProps = 5;
constexpr uint32_t kFirstProperty = 0x30000ef;
std::vector<cRZVariant *> gValues; // every value made for the seed (released after it)
cRZVariant *gProp[winfx::World::kMax][kProps];

cRZVariant *Keep(cRZVariant *v)
{
    v->AddRef();
    gValues.push_back(v);
    return v;
}
cRZVariant *TextValue(const std::string &text)
{
    LegacyText s;
    s.mText.FromChar(text.c_str());
    return Keep(new cRZVariant(*s.mText.AsInterface()));
}
void BuildProps(unsigned windows)
{
    for (unsigned w = 0; w < winfx::World::kMax; ++w)
        for (unsigned p = 0; p < kProps; ++p) {
            gProp[w][p] = nullptr;
            if (w >= windows || Draw(5) < 2) continue;
            if (p == 0) // the announce flag: a uint32 or a text
                gProp[w][p] = Draw(2) ? Keep(new cRZVariant(uint32_t(Draw(3)))) : TextValue(Draw(2) ? "1" : "0");
            else if (p == 4) // the notify text
                gProp[w][p] = TextValue(Draw(2) ? "notify me" : "");
            else
                gProp[w][p] = Draw(8) ? TextValue(Condition()) : Keep(new cRZVariant(uint32_t(Draw(3))));
        }
}
void ReleaseSeed()
{
    for (cRZVariant *v : gValues) v->Release();
    gValues.clear();
    for (cRZString *s : gStrings) s->Release();
    gStrings.clear();
}

// ---- the windows and faces -----------------------------------------------------------------------------------------

// The faces' own tables (the script code calls only these slots; the rest are unknown and undeclared).
constexpr drawfx::Slot kUnknownSlot{0, drawfx::Ret::Int, false, "", ""};
template <unsigned N> constexpr std::array<drawfx::Slot, N> FaceSlots()
{
    std::array<drawfx::Slot, N> s{};
    for (drawfx::Slot &x : s) x = kUnknownSlot;
    s[0] = {2, drawfx::Ret::Int, true, "QueryInterface", "ua"};
    s[1] = {0, drawfx::Ret::Int, true, "AddRef", ""};
    s[2] = {0, drawfx::Ret::Int, true, "Release", ""};
    return s;
}
// cIGZWinProcHost (0x5386d516, src/gz/cGZLegacyControl.h): slot 6 takes the mode and its argument (4, 0), 7 the proc.
constexpr std::array<drawfx::Slot, 8> MakeHostSlots()
{
    std::array<drawfx::Slot, 8> s = FaceSlots<8>();
    s[6] = {2, drawfx::Ret::Int, true, "SetMode", "uu"};
    s[7] = {1, drawfx::Ret::Int, true, "SetWinProc", "a"};
    return s;
}
constexpr std::array<drawfx::Slot, 8> kHostSlots = MakeHostSlots();
constexpr drawfx::Iface kHost{"cIGZWinProcHost", 8, kHostSlots.data(), false, drawfx::kTestTable};
// cGZWinTextEdit's face 0x231a1c57: slot 5 its window, 95 / 96 attach a scrollbar (vertical / other).
constexpr std::array<drawfx::Slot, 97> MakeEditSlots()
{
    std::array<drawfx::Slot, 97> s = FaceSlots<97>();
    s[5] = {0, drawfx::Ret::Int, true, "GetWindow", ""};
    s[95] = {1, drawfx::Ret::Int, true, "AttachVerticalScrollbar", "a"};
    s[96] = {1, drawfx::Ret::Int, true, "AttachScrollbar", "a"};
    return s;
}
constexpr std::array<drawfx::Slot, 97> kEditSlots = MakeEditSlots();
constexpr drawfx::Iface kEdit{"TextEditFace", 97, kEditSlots.data(), false, drawfx::kTestTable};

winfx::World gWorld;
unsigned gWindows;
drawfx::Fake gHost[winfx::World::kMax], gButton[winfx::World::kMax], gEdit[winfx::World::kMax],
    gScroll[winfx::World::kMax];
drawfx::Fake gWinProc;
winfx::Mgr gMgr;

unsigned IndexOf(drawfx::Fake &f) { return unsigned(static_cast<winfx::Win *>(f.owner) - gWorld.wins); }

uint32_t GetProperty(drawfx::Fake &f, const uint32_t *a)
{
    const uint32_t p = a[0] - kFirstProperty;
    if (p >= kProps) return 0;
    cRZVariant *v = gProp[IndexOf(f)][p];
    if (!v) return 0x7700; // false in al, noise above
    v->AddRef();
    *reinterpret_cast<void **>(uintptr_t(a[1])) = static_cast<cIGZVariant *>(v);
    return 1;
}
uint32_t GetChildInterface(drawfx::Fake &f, const uint32_t *a)
{
    winfx::Win *child = static_cast<winfx::Win *>(f.owner)->Child(a[0]);
    return child ? child->Query(a[1], a[2]) : 0;
}
uint32_t EditWindow(drawfx::Fake &f, const uint32_t *) { return f.value[0]; }
uint32_t ButtonWindow(drawfx::Fake &f, const uint32_t *) { return f.value[0]; }
uint32_t GetOrientation(drawfx::Fake &f, const uint32_t *a)
{
    if (!(f.value[1] & 1)) return f.value[1];
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = f.value[0];
    return f.value[1];
}

// Rebuilds the windows and their faces for the seed (the same on both sides).
void BuildWindows(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 7);
    gWindows = 3 + rng() % 8;
    gWorld.Build(seed, gWindows);
    for (unsigned i = 0; i < gWindows; ++i) {
        winfx::Win &w = gWorld[i];
        w.f.On(126, &GetProperty);
        w.f.On(43, &GetChildInterface);
        w.f.Settle<21, 1>("a"); // the parent's slot 21 (a forwarding thunk, no fact) takes the held-back child window
        if (rng() % 4 == 0) w.id = rng() % 2; // a scrollbar's id under its text edit
        const uint32_t kind = rng() % 8;
        gHost[i].Reset<kHost>(400 + i);
        gButton[i].Reset<fx_slots::cIGZWinBtn>(420 + i);
        gButton[i].On(3, &ButtonWindow);
        gButton[i].value[0] = drawfx::P(&gWorld[rng() % 3 ? i : rng() % gWindows].f);
        gEdit[i].Reset<kEdit>(440 + i);
        gEdit[i].On(5, &EditWindow);
        gEdit[i].value[0] = drawfx::P(&gWorld[rng() % 4 ? i : rng() % gWindows].f);
        gScroll[i].Reset<fx_slots::cIGZWinScrollbar2>(460 + i);
        gScroll[i].On(5, &GetOrientation);
        gScroll[i].value[0] = rng() % 3 ? 1 : rng() % 3;
        gScroll[i].value[1] = rng() % 4 ? 1 : 0x500;
        if (kind == 1 || kind == 5) w.f.AddFace(0x5386d516, gHost[i]);
        if (kind == 2 || kind == 5 || kind == 6) w.f.AddFace(0x8810, gButton[i]);
        if (kind == 3 || kind == 4 || kind == 6) w.f.AddFace(0x231a1c57, gEdit[i]);
        if (w.id < 2 && rng() % 4) w.f.AddFace(0x4b42da53, gScroll[i]);
    }
    gWinProc.Reset<fx_slots::cIGZWin>(501);
}

// The windows' state and their faces' counts.
void WindowState(Blob &b)
{
    const std::vector<uint32_t> state = gWorld.State();
    b.insert(b.end(), state.begin(), state.end());
    for (unsigned i = 0; i < gWindows; ++i) {
        b.push_back(uint32_t(gHost[i].refs));
        b.push_back(uint32_t(gButton[i].refs));
        b.push_back(uint32_t(gEdit[i].refs));
        b.push_back(uint32_t(gScroll[i].refs));
    }
    b.push_back(uint32_t(gWinProc.refs));
    b.push_back(uint32_t(gMgr.f.refs));
}

// ---- the services ----------------------------------------------------------------------------------------------------

bool gGiveUIService, gGiveLegacy, gGiveResMgr, gGiveMgr, gGiveSettings;
std::vector<drawfx::Fake *> gSetWindows; // GetWindowAt's answers
uint32_t gCompileResult;                 // 0: no set

uint32_t GetSystemService(Obj *, uint32_t *a)
{
    void **out = reinterpret_cast<void **>(uintptr_t(a[2]));
    Obj *given = nullptr;
    if (a[0] == 0x5a356e15 && a[1] == 0xfa3562fa && gGiveUIService) given = &F(kUIService);
    if (a[0] == 0x056b906e && a[1] == 0x656b8efc && gGiveResMgr) given = &F(kResMgr);
    if (a[0] == 0x0ac1887e && a[1] == 0x6bb22f38 && gGiveSettings) given = &F(kSettings);
    if (a[0] == 0xa417445e && a[1] == 0x5a4 && gGiveMgr) {
        ++gMgr.f.refs;
        *out = &gMgr.f;
        return 1;
    }
    if (!given) return 0x3300;
    ++given->refs;
    *out = given;
    return 1;
}
uint32_t UIServiceQuery(Obj *, uint32_t *a)
{
    if (a[0] != 0x7a4a48ed || !gGiveLegacy) return 0x100;
    ++F(kLegacy).refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &F(kLegacy);
    return 1;
}
const uint32_t *Words(uint32_t p) { return reinterpret_cast<const uint32_t *>(uintptr_t(p)); }
uint32_t CompileNode(Obj *, uint32_t *a)
{
    for (unsigned i = 0; i < 4; ++i) Ev(Words(a[0])[i]);
    drawfx::Note("compile", {Words(a[0])[0], Words(a[0])[1], Words(a[0])[2], Words(a[0])[3]});
    return gCompileResult;
}
uint32_t TestForKey(Obj *self, uint32_t *a)
{
    for (unsigned i = 0; i < 4; ++i) Ev(Words(a[0])[i]);
    return self->value[0];
}
uint32_t GetWindowAt(Obj *, uint32_t *a)
{
    return a[0] < gSetWindows.size() && gSetWindows[a[0]] ? drawfx::P(gSetWindows[a[0]]) : 0;
}
const char *KeyText(uint32_t key) { return reinterpret_cast<const GZServiceString *>(uintptr_t(key))->c_str(); }
uint32_t HasProperty(Obj *, uint32_t *a)
{
    NoteText("has", KeyText(a[0]));
    return FindSetting(KeyText(a[0])) ? 0x4401 : 0x4400;
}
uint32_t GetSettingProperty(Obj *, uint32_t *a)
{
    NoteText("get", KeyText(a[0]));
    const Setting *s = FindSetting(KeyText(a[0]));
    return s ? P(&s->any) : 0;
}

// The converter's slots (a text in, a value out; logged as notes with the text).
const char *Arg(uint32_t p) { return reinterpret_cast<const char *>(uintptr_t(p)); }
uint32_t __fastcall ToBool(Obj *, void *, uint32_t text)
{
    NoteText("bool", Arg(text));
    const bool v = Arg(text)[0] == '1' || _stricmp(Arg(text), "true") == 0;
    return 0x6600 | uint32_t(v);
}
float __fastcall ToFloat(Obj *, void *, uint32_t text)
{
    NoteText("float", Arg(text));
    return std::strtof(Arg(text), nullptr);
}
uint32_t __fastcall ToSint32(Obj *, void *, uint32_t text)
{
    NoteText("sint32", Arg(text));
    return uint32_t(std::strtol(Arg(text), nullptr, 10));
}
uint32_t __fastcall ToUint32(Obj *, void *, uint32_t text)
{
    NoteText("uint32", Arg(text));
    return uint32_t(std::strtoul(Arg(text), nullptr, 10));
}

// A message by content: type, flags, values, and its three pointer fields (a string's text, a fake's id).
void LogMessage(const char *label, uint32_t pointer)
{
    const auto *m = reinterpret_cast<const cRZMessage2Standard *>(uintptr_t(pointer));
    drawfx::Call c;
    c.note = label;
    c.words = {m->mRefCount, m->mFlags, m->mId, m->mValue[0], m->mValue[1], m->mValue[2], m->mValue[3]};
    for (void *field : {m->mPtr20, m->mPtr24, m->mPtr28}) {
        const auto *s = static_cast<const cRZString *>(field);
        if (!field) {
            c.words.push_back(0);
        } else if (s->mVTable == GameAddress<void>(cRZString::kVTable)) {
            c.words.push_back(1);
            c.words.push_back(s->mRefCount);
            c.text += s->mString;
            c.text += '\x1f';
        } else {
            c.words.push_back(2);
            c.words.push_back(drawfx::Norm(P(field)));
        }
    }
    drawfx::gLog.push_back(std::move(c));
}
// The server's answer to a condition message: refused for some names and values, else data 1 from them.
uint32_t __fastcall MessageSend(Obj *, void *, uint32_t message)
{
    LogMessage("send", message);
    const auto *m = reinterpret_cast<const cRZMessage2Standard *>(uintptr_t(message));
    if (m->mId != 0x8e9dce8e) return 1;
    const auto *name = static_cast<const cRZString *>(m->mPtr20);
    const auto *value = static_cast<const cRZString *>(m->mPtr24);
    const uint32_t h = uint32_t(name ? name->mString.size() * 7 : 3) + uint32_t(value ? value->mString.size() * 3 : 5) +
                       uint32_t(value && !value->mString.empty() ? uint8_t(value->mString[0]) : 0);
    if (h % 9 == 0) return 0x2200;
    const uint32_t answers[] = {0, 1, 0x100, 2};
    reinterpret_cast<ifaces::cIGZMessage2Standard *>(uintptr_t(message))->SetData1(answers[h % 4]);
    return 0x2201;
}
uint32_t __fastcall MessagePost(Obj *, void *, uint32_t message, uint32_t flag)
{
    LogMessage("post", message);
    drawfx::Note("post flag", flag & 0xff);
    return 1;
}

// The settings static's state for a case.
struct Statics {
    uint32_t guard; // INT_MIN: built
    bool cellSet;
};

// Builds the services for one side (the framework, console, settings, message server; the script service and its
// set), from the seed.
void BuildServices(uint32_t seed, svcfx::Globals &globals, const Statics &statics, bool withServer)
{
    svcfx::ResetAll();
    std::mt19937 rng(seed * 13 + 5);
    F(kFramework).Set(5, "ddp", 0, &GetSystemService);
    gGiveUIService = rng() % 8 != 0;
    gGiveLegacy = rng() % 8 != 0;
    gGiveResMgr = rng() % 4 != 0;
    gGiveMgr = rng() % 4 != 0;
    gGiveSettings = rng() % 4 != 0;
    F(kUIService).Set(0, "dp", 0, &UIServiceQuery);
    F(kUIService).Set(7, "p", 0, &CompileNode);
    F(kLegacy).Set(3, 1, 0x500 | (rng() % 2));
    F(kLegacy).Data(3, 1);
    F(kLegacy).Set(4, 1, 1);
    F(kResMgr).Set(15, "p", 0, &TestForKey);
    F(kResMgr).value[0] = rng() % 2 ? 1 : 0x900;
    F(kSet).Set(8, "d", 0, &GetWindowAt);
    F(kSet).Set(9, 1);
    F(kSet).Set(10, 0, uint32_t(gSetWindows.size()));
    F(kConsole).Set(3, 0, &F(kConverter));
    for (unsigned s = 30; s <= 33; ++s) F(kConverter).Set(s, 1);
    F(kConverter).table[30] = reinterpret_cast<void *>(&ToBool);
    F(kConverter).table[31] = reinterpret_cast<void *>(&ToFloat);
    F(kConverter).table[32] = reinterpret_cast<void *>(&ToSint32);
    F(kConverter).table[33] = reinterpret_cast<void *>(&ToUint32);
    F(kSettings).Set(3, 0, &F(kPropSet));
    F(kPropSet).Set(8, "p", 0, &GetSettingProperty);
    F(kPropSet).Set(10, "p", 0, &HasProperty);
    F(kServer).Set(3, 1);
    F(kServer).table[3] = reinterpret_cast<void *>(&MessageSend);
    F(kServer).Set(4, 2);
    F(kServer).table[4] = reinterpret_cast<void *>(&MessagePost);
    gMgr.Reset(500);
    globals.Set(kConsoleCell, rng() % 10 ? &F(kConsole) : nullptr);
    globals.Set(svcfx::kMessageServerCell, withServer ? &F(kServer) : nullptr);
    globals.SetWord(kSettingsGuard, statics.guard);
    globals.Set(kSettingsCell, statics.cellSet ? &F(kSettings) : nullptr);
}

// The thread's statics epoch (word 1 of the image's TLS block): -1 for the unbuilt cases, restored afterwards.
struct Epoch {
    int *tls;
    int saved;
    Epoch()
    {
        auto **array = reinterpret_cast<int **>(__readfsdword(0x2c));
        tls = array[*GameAddress<uint32_t>(0x10df814)];
        saved = tls[1];
    }
    void Unbuilt() { tls[1] = -1; }
    ~Epoch() { tls[1] = saved; }
};

Statics DrawStatics()
{
    const uint32_t pick = Draw(10);
    if (pick == 0) return {0, false};
    return {0x80000000u, pick != 1};
}

// What a side leaves for the comparison besides the window log.
void EndSide(Blob &b)
{
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    svcfx::CaptureFakes(b, kValueKinds);
    WindowState(b);
    for (cRZVariant *v : gValues) b.push_back(reinterpret_cast<const uint16_t *>(v)[6]); // the count at +0xc
    for (cRZString *s : gStrings) b.push_back(s->mRefCount);
    b.push_back(svcfx::Norm(*GameAddress<uint32_t>(kSettingsCell)));
    b.push_back(*GameAddress<int32_t>(kSettingsGuard) != 0);
    b.push_back(*GameAddress<uint32_t>(kWidthCell));
    b.push_back(*GameAddress<uint32_t>(kHeightCell));
    b.push_back(svcfx::Norm(*GameAddress<uint32_t>(kConsoleCell)));
    b.push_back(svcfx::Norm(*GameAddress<uint32_t>(svcfx::kMessageServerCell)));
}

struct Scope {
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew{0x824632, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624, shape::CodeOf(&GameDelete)};
    Epoch epoch;
    Scope()
    {
        globals.Set(kServiceCell, &F(kFramework));
        globals.SetWord(kWidthCell, 0);
        globals.SetWord(kHeightCell, 0);
    }
};

} // namespace load_ui_script_test

using namespace load_ui_script_test;

DIFF_TEST(LoadUIScript_Condition, 0xb40d20)
{
    Scope scope;
    using EvalFn = bool(__cdecl *)(uint32_t, void *, bool *);
    for (uint32_t seed = 1; seed <= 6000; ++seed) {
        gRng.seed(seed);
        BuildSettings();
        gSetWindows.clear();
        Blob results[2];
        drawfx::Log logs[2];
        std::vector<std::string> errors[2];
        uint32_t property = 0, window = 0;
        uint8_t initial = 0;
        bool server = false;
        Statics statics{};
        for (int side = 0; side < 2; ++side) {
            drawfx::ResetAll();
            BuildWindows(seed);
            if (!side) {
                BuildProps(gWindows);
                property = kFirstProperty + 1 + Draw(2);
                if (Draw(6) == 0) property = kFirstProperty + Draw(kProps);
                window = Draw(20) ? 1 + Draw(gWindows - 1) : 0xffffffffu;
                const uint8_t initials[] = {0, 1, 0x5a};
                initial = initials[Draw(3)];
                server = Draw(10) != 0;
                statics = DrawStatics();
            }
            BuildServices(seed, scope.globals, statics, server);
            if (!statics.guard) scope.epoch.Unbuilt();
            void *const target = window == 0xffffffffu ? nullptr : &gWorld[window].f;
            uint8_t out[4] = {0xcc, initial, 0xcc, 0xcc};
            const bool r = side ? EvaluateWindowCondition(property, static_cast<cIGZUnknown *>(target), reinterpret_cast<bool *>(&out[1]))
                                : ctx.Original<EvalFn>(0xb40d20)(property, target, reinterpret_cast<bool *>(&out[1]));
            Blob &b = results[side];
            b.push_back(r);
            for (uint8_t v : out) b.push_back(v);
            EndSide(b);
            logs[side] = drawfx::gLog;
            errors[side] = drawfx::gErrors;
        }
        ReleaseSeed();
        errors[0].insert(errors[0].end(), errors[1].begin(), errors[1].end());
        if (!svcfx::Same(ctx, results[0], results[1], "LoadUIScript_Condition", seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], "LoadUIScript_Condition log", seed, errors[0])) break;
    }
}

DIFF_TEST(LoadUIScript_Apply, 0xb42250)
{
    // Also reached: 0xb40d20 (the original's own call on the original side; ours on ours).
    Scope scope;
    using ApplyFn = bool(__cdecl *)(void *);
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        gRng.seed(seed);
        BuildSettings();
        gSetWindows.clear();
        Blob results[2];
        drawfx::Log logs[2];
        std::vector<std::string> errors[2];
        uint32_t window = 0;
        Statics statics{};
        for (int side = 0; side < 2; ++side) {
            drawfx::ResetAll();
            BuildWindows(seed);
            if (!side) {
                BuildProps(gWindows);
                window = Draw(30) ? Draw(gWindows) : 0xffffffffu;
                statics = DrawStatics();
            }
            BuildServices(seed, scope.globals, statics, true);
            if (!statics.guard) scope.epoch.Unbuilt();
            void *const target = window == 0xffffffffu ? nullptr : &gWorld[window].f;
            const bool r = side ? ApplyScriptConditions(static_cast<cIGZUnknown *>(target)) : ctx.Original<ApplyFn>(0xb42250)(target);
            Blob &b = results[side];
            b.push_back(r);
            EndSide(b);
            logs[side] = drawfx::gLog;
            errors[side] = drawfx::gErrors;
        }
        ReleaseSeed();
        errors[0].insert(errors[0].end(), errors[1].begin(), errors[1].end());
        if (!svcfx::Same(ctx, results[0], results[1], "LoadUIScript_Apply", seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], "LoadUIScript_Apply log", seed, errors[0])) break;
    }
}

DIFF_TEST(LoadUIScript_ScreenSize, 0xb429b0)
{
    Scope scope;
    using SizeFn = void(__cdecl *)();
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        gRng.seed(seed);
        gSetWindows.clear();
        Blob results[2];
        drawfx::Log logs[2];
        std::vector<std::string> errors[2];
        uint32_t width = 0, height = 0, main = 0;
        int32_t size[2] = {};
        for (int side = 0; side < 2; ++side) {
            drawfx::ResetAll();
            BuildWindows(seed);
            if (!side) {
                width = Draw(2) ? Draw(3) * 700 : gRng();
                height = Draw(2) ? Draw(3) * 500 : gRng();
                main = Draw(6) ? Draw(gWindows) : 0xffffffffu;
                const int32_t sizes[] = {1024, 1023, 768, 767, 800, 1600, 0, -5, 0x7fffffff};
                size[0] = sizes[Draw(9)];
                size[1] = sizes[Draw(9)];
            }
            BuildServices(seed, scope.globals, {0x80000000u, true}, true);
            *GameAddress<uint32_t>(kWidthCell) = width;
            *GameAddress<uint32_t>(kHeightCell) = height;
            if (main != 0xffffffffu) {
                winfx::Win &w = gWorld[main];
                w.area[2] = w.area[0] + size[0];
                w.area[3] = w.area[1] + size[1];
                gMgr.f.Returns(3, &w.f);
            }
            if (side)
                UpdateScriptScreenSize();
            else
                ctx.Original<SizeFn>(0xb429b0)();
            Blob &b = results[side];
            EndSide(b);
            logs[side] = drawfx::gLog;
            errors[side] = drawfx::gErrors;
        }
        errors[0].insert(errors[0].end(), errors[1].begin(), errors[1].end());
        if (!svcfx::Same(ctx, results[0], results[1], "LoadUIScript_ScreenSize", seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], "LoadUIScript_ScreenSize log", seed, errors[0])) break;
    }
}

DIFF_TEST(LoadUIScript_Load, 0xb41b50)
{
    // Also reached: 0xb40d20, 0xb42250, 0xb429b0 (each side's own).
    Scope scope;
    using LoadFn = bool(__cdecl *)(uint32_t, uint32_t, void *, void **, bool);
    for (uint32_t seed = 1; seed <= 5000; ++seed) {
        gRng.seed(seed);
        BuildSettings();
        Blob results[2];
        drawfx::Log logs[2];
        std::vector<std::string> errors[2];
        uint32_t instance = 0, scriptArg = 0, width = 0, height = 0, main = 0;
        bool withProc = false, withOut = false, sized = false, compiles = false;
        std::vector<int> setIndex;
        Statics statics{};
        for (int side = 0; side < 2; ++side) {
            drawfx::ResetAll();
            BuildWindows(seed);
            if (!side) {
                BuildProps(gWindows);
                instance = Draw(2) ? Draw(4) : gRng();
                scriptArg = gRng();
                withOut = Draw(2) != 0;
                sized = Draw(2) != 0;
                compiles = Draw(10) != 0;
                const uint32_t sizes[] = {0, 800, 1024, 1280, 5, 123456, 9999};
                width = Draw(3) ? sizes[Draw(7)] : 0;
                height = Draw(3) ? sizes[Draw(7)] : 0;
                main = Draw(gWindows);
                setIndex.clear();
                const uint32_t count = Draw(7);
                for (uint32_t i = 0; i < count; ++i) setIndex.push_back(Draw(12) ? int(Draw(gWindows)) : -1);
                withProc = !setIndex.empty() && setIndex[0] >= 0 && Draw(2);
                statics = DrawStatics();
            }
            gSetWindows.clear();
            for (int i : setIndex) gSetWindows.push_back(i < 0 ? nullptr : &gWorld[unsigned(i)].f);
            BuildServices(seed, scope.globals, statics, true);
            if (!statics.guard) scope.epoch.Unbuilt();
            gCompileResult = compiles ? P(&F(kSet)) : 0;
            *GameAddress<uint32_t>(kWidthCell) = width;
            *GameAddress<uint32_t>(kHeightCell) = height;
            gWorld[main].area[2] = gWorld[main].area[0] + 1280;
            gWorld[main].area[3] = gWorld[main].area[1] + 1024;
            gMgr.f.Returns(3, &gWorld[main].f);
            uint32_t out[3] = {0xc0c0c0c0, 0xc1c1c1c1, 0xc2c2c2c2};
            void **const outArg = withOut ? reinterpret_cast<void **>(&out[1]) : nullptr;
            void *const proc = withProc ? &gWinProc : nullptr;
            const bool r = side ? LoadUIScript(instance, scriptArg, static_cast<cIGZUnknown *>(proc),
                                               reinterpret_cast<cIGZScriptableControlSet **>(outArg), sized)
                                : ctx.Original<LoadFn>(0xb41b50)(instance, scriptArg, proc, outArg, sized);
            Blob &b = results[side];
            b.push_back(r);
            for (unsigned i = 0; i < 3; ++i) b.push_back(svcfx::NormOut(out[i], 0xc0c0c0c0u + 0x01010101u * i));
            EndSide(b);
            logs[side] = drawfx::gLog;
            errors[side] = drawfx::gErrors;
        }
        ReleaseSeed();
        errors[0].insert(errors[0].end(), errors[1].begin(), errors[1].end());
        if (!svcfx::Same(ctx, results[0], results[1], "LoadUIScript_Load", seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], "LoadUIScript_Load log", seed, errors[0])) break;
    }
}
