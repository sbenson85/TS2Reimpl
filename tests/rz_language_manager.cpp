// Differential tests for cRZLanguageManager in the standalone shape (docs/standalone-shape.md "Tests"). The original
// side is an object built by the original constructor (0xc6ed1c: the game's vtables and the game's cRZLanguageUtility);
// our side one built by our constructor. Every slot is called through each object's own vtable.
//
// What is compared after each call: the result, the log and the object's state. The log holds the writes to the
// recording strings (tests/rz_recording_string.h), the imports (GetUserDefaultLCID, CompareStringA: impfx fakes), the
// CRT locale calls (setlocale / _setmbcp: the image's code patched on the original side, LanguageManagerCrt() on ours)
// and, where a test asks for it, the manager's calls to its own slots (9, 12, 13, 14, 16, 17, 24, 26, 34 and 35: the
// object's vtable word swapped for a copy whose entries log and forward to the side's own slot). The state is every
// field: the service part, the allowed set (read as the std::set both sides lay out alike), the language and country,
// the utility's fields and separator, and the system code page cache (game global 0xff23a4, which the locale helper
// resets).
//
// RVAs covered: the constructor 0xc6ed1c, the creator 0xc6f338, the destructor 0xc6edcf and the deleting destructor
// 0xc6ee2a (through the +8 face's thunk 0xc6ee22), QueryInterface 0xc747ac (and its thunks 0xc747e9 / 0xc747f1), slots
// 5 0xc6ee68 (the set insert 0xc6ec8a), 6 0xc748f0, 8 0xc74960 (the locale helper 0xc74831 and its setlocale wrapper
// 0x1b18b5), 9 0xc6eeab, 10 0xc70b25, 11 0xc731c6, 12 0xc71bd8, 13 0xc7225f, 14 0xc7301d, 15 0xc7193d, 16 0xc71964,
// 17 0xc7214a, 18 0xc7207e, 19 0xc720e4, 20 0xc7202b, 21 0x55e17e, 22 0x96de02, 23 0xc71fc4, 24 0xc716bb,
// 25 0xc718d7, 26 0xc71342, 27 0xc71656, 29 0xc7491f, 30 0xc7304b, 31 0xc711c2, 34 0xc705fb, 35 0xc709e7,
// 36 0xc70917, 37 0xc7097f, 38 0xc724ab, 39 0xc72fe8, 40 0x20d940, 42 0xc6ef7b, 43 0xc6ef6c, 44 0xc6efa0,
// 45 0xc6ef91, 46 0xc7047f, 47 0xc70433.
#include "code_patch.h"
#include "difftest.h"
#include "free_scope.h"
#include "impfx.h"
#include "rz_recording_string.h"
#include "rz/cRZLanguageManager.h"
#include "rz/cRZString.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <list>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace recstr;

constexpr uint32_t kCtor = 0xc6ed1c;
constexpr uint32_t kDtor = 0xc6edcf;
constexpr uint32_t kCreator = 0xc6f338;
constexpr uint32_t kSetLocale = 0x837dcc; // the image CRT's setlocale (library)
constexpr uint32_t kSetMbcp = 0x84a8d7;   // the image CRT's _setmbcp (library)
constexpr uint32_t kCodePageGlobal = 0xff23a4;
constexpr int32_t kSeededCodePage = 8;
constexpr uint32_t kSize = cRZLanguageManager::kSize;

std::string Hex(uint32_t value)
{
    char text[16];
    std::snprintf(text, sizeof(text), "0x%x", value);
    return text;
}

uint32_t Word(const void *object, uint32_t offset)
{
    uint32_t value;
    std::memcpy(&value, static_cast<const uint8_t *>(object) + offset, 4);
    return value;
}
void SetWord(void *object, uint32_t offset, uint32_t value)
{
    std::memcpy(static_cast<uint8_t *>(object) + offset, &value, 4);
}

template <class Fn> Fn SlotOf(const void *object, unsigned slot, uint32_t face = 0)
{
    const void *at = static_cast<const uint8_t *>(object) + face;
    return reinterpret_cast<Fn>((*static_cast<void *const *const *>(at))[slot]);
}

// ---- the imports and the CRT ---------------------------------------------------------------------------------------

uint32_t gUserLcid = 0x409;
uint32_t OnGetUserDefaultLCID(drawfx::Fake &, const uint32_t *)
{
    Note("GetUserDefaultLCID -> 0x%x", gUserLcid);
    return gUserLcid;
}

// CompareStringA: lexical, folding ASCII case when the flags ask (NORM_IGNORECASE), 1 / 2 / 3 as the API.
uint32_t OnCompareString(drawfx::Fake &, const uint32_t *args)
{
    const char *a = reinterpret_cast<const char *>(static_cast<uintptr_t>(args[2]));
    const char *b = reinterpret_cast<const char *>(static_cast<uintptr_t>(args[4]));
    const int32_t aLength = static_cast<int32_t>(args[3]);
    const int32_t bLength = static_cast<int32_t>(args[5]);
    Note("CompareStringA(0x%x, 0x%x, [%s], [%s])", args[0], args[1], Shown(a, aLength < 0 ? 0 : aLength).c_str(),
         Shown(b, bLength < 0 ? 0 : bLength).c_str());
    const bool fold = (args[1] & 1) != 0;
    const int32_t n = aLength < bLength ? aLength : bLength;
    for (int32_t i = 0; i < n; ++i) {
        int ca = static_cast<unsigned char>(a[i]);
        int cb = static_cast<unsigned char>(b[i]);
        if (fold && ca >= 'A' && ca <= 'Z') ca += 0x20;
        if (fold && cb >= 'A' && cb <= 'Z') cb += 0x20;
        if (ca != cb) return ca < cb ? 1 : 3;
    }
    return aLength == bLength ? 2 : aLength < bLength ? 1 : 3;
}

int gLocaleCalls = 0;
int gLocaleFailAt = -1; // the setlocale call (0 first) that answers null; -1: none
char gLocaleAnswer[] = "fake locale";

char *__cdecl FakeSetLocale(int category, const char *locale)
{
    Note("setlocale(%d, [%s]) code page %d", category, locale ? locale : "(null)", *GameAddress<int32_t>(kCodePageGlobal));
    return gLocaleCalls++ == gLocaleFailAt ? nullptr : gLocaleAnswer;
}
int __cdecl FakeSetMbcp(int codePage)
{
    Note("_setmbcp(%d)", codePage);
    return 0;
}

// ---- recording the manager's calls to itself -----------------------------------------------------------------------

void *gReal[48];      // the side's own vtable
void *gRecording[48]; // a copy whose entries below log and forward

template <unsigned S> bool __fastcall NameSlot(void *self, void *, cIGZString *out, uint32_t id)
{
    Note("self s%u(0x%x)", S, id);
    const bool r = reinterpret_cast<bool(__thiscall *)(void *, cIGZString *, uint32_t)>(gReal[S])(self, out, id);
    Note("self s%u -> %d", S, r ? 1 : 0);
    return r;
}
template <unsigned S> bool __fastcall BoolSlot(void *self, void *, uint32_t id)
{
    const bool r = reinterpret_cast<bool(__thiscall *)(void *, uint32_t)>(gReal[S])(self, id);
    Note("self s%u(0x%x) -> %d", S, id, r ? 1 : 0);
    return r;
}
template <unsigned S> uint32_t __fastcall WordSlot(void *self, void *, uint32_t id)
{
    const uint32_t r = reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t)>(gReal[S])(self, id);
    Note("self s%u(0x%x) -> 0x%x", S, id, r);
    return r;
}
bool __fastcall StepSlot(void *self, void *, uint32_t *id)
{
    const uint32_t before = *id;
    const bool r = reinterpret_cast<bool(__thiscall *)(void *, uint32_t *)>(gReal[14])(self, id);
    Note("self s14(0x%x) -> %d, 0x%x", before, r ? 1 : 0, *id);
    return r;
}

// A recording string's deleting destructor (slot 33): logged, then the game's.
void *__fastcall S33(void *self, void *, uint32_t flags)
{
    Note("s33 destroy(%u) [%s]", flags, TextOf(*static_cast<cRZString *>(self)).c_str());
    return reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>(gGameString[33])(self, flags);
}

// Swaps the object's vtable word for the recording copy (built from the side's own vtable).
void RecordSelf(void *object)
{
    std::memcpy(gReal, *static_cast<void ***>(object), sizeof(gReal));
    std::memcpy(gRecording, gReal, sizeof(gRecording));
    gRecording[9] = reinterpret_cast<void *>(&BoolSlot<9>);
    gRecording[12] = reinterpret_cast<void *>(&WordSlot<12>);
    gRecording[13] = reinterpret_cast<void *>(&NameSlot<13>);
    gRecording[14] = reinterpret_cast<void *>(&StepSlot);
    gRecording[16] = reinterpret_cast<void *>(&NameSlot<16>);
    gRecording[17] = reinterpret_cast<void *>(&NameSlot<17>);
    gRecording[24] = reinterpret_cast<void *>(&NameSlot<24>);
    gRecording[26] = reinterpret_cast<void *>(&NameSlot<26>);
    gRecording[34] = reinterpret_cast<void *>(&NameSlot<34>);
    gRecording[35] = reinterpret_cast<void *>(&NameSlot<35>);
    *static_cast<void ***>(object) = gRecording;
}
void StopRecordingSelf(void *object) { *static_cast<void ***>(object) = gReal; }

// ---- the two objects -----------------------------------------------------------------------------------------------

void *NewOriginal(difftest::Context &ctx)
{
    void *memory = ::operator new(kSize);
    std::memset(memory, 0xcd, kSize);
    return ctx.Original<void *(__thiscall *)(void *)>(kCtor)(memory);
}
void *NewOurs()
{
    void *memory = ::operator new(kSize);
    std::memset(memory, 0xcd, kSize);
    return new (memory) cRZLanguageManager;
}
void FreeOriginal(difftest::Context &ctx, void *object)
{
    ctx.Original<void(__thiscall *)(void *)>(kDtor)(object);
    ::operator delete(object);
}
void FreeOurs(void *object)
{
    static_cast<cRZLanguageManager *>(object)->~cRZLanguageManager();
    ::operator delete(object);
}

// The vtable words each side's objects carry (+0, +4, +8 and the utility's at +0x34), for the codes in StateOf.
uint32_t gVTables[2][4];

void LearnVTables(difftest::Context &ctx)
{
    void *objects[2] = {NewOriginal(ctx), NewOurs()};
    const uint32_t offsets[4] = {0, 4, 8, 0x34};
    for (int side = 0; side < 2; ++side)
        for (int i = 0; i < 4; ++i) gVTables[side][i] = Word(objects[side], offsets[i]);
    FreeOriginal(ctx, objects[0]);
    FreeOurs(objects[1]);
}

std::string VTableCode(int side, uint32_t word)
{
    for (int i = 0; i < 4; ++i)
        if (word == gVTables[side][i]) return "vt" + std::to_string(i);
    if (word == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gRecording))) return "recording";
    return Hex(word);
}

const cRZString &Separator(const void *object)
{
    return *reinterpret_cast<const cRZString *>(static_cast<const uint8_t *>(object) + 0x48);
}

// Every field of the object: the vtable words as codes, the service part, +0x20, the allowed set, the language and
// country, the utility (count, language, country, flag word, separator), and the code page cache.
std::string StateOf(int side, const void *object)
{
    std::string s = VTableCode(side, Word(object, 0)) + " " + VTableCode(side, Word(object, 4)) + " " +
                    VTableCode(side, Word(object, 8)) + " refs " + Hex(Word(object, 0xc)) + " id " +
                    Hex(Word(object, 0x10)) + " prio " + Hex(Word(object, 0x14)) + "/" + Hex(Word(object, 0x18)) +
                    // +0x1c is the service base's one-byte running flag; its three padding bytes are never written (whatever the
                    // allocator left there: the full-suite flake of 2026-10-23), so only the flag is compared.
                    " run " + Hex(Word(object, 0x1c) & 0xff) + " +20 " + Hex(Word(object, 0x20)) + " set {";
    const auto *set = reinterpret_cast<const std::set<uint32_t> *>(static_cast<const uint8_t *>(object) + 0x24);
    for (uint32_t id : *set) s += Hex(id) + ",";
    s += "} n " + std::to_string(set->size()) + " lang " + Hex(Word(object, 0x2c)) + " country " + Hex(Word(object, 0x30));
    s += " | util " + VTableCode(side, Word(object, 0x34)) + " refs " + Hex(Word(object, 0x38)) + " lang " +
         Hex(Word(object, 0x3c)) + " country " + Hex(Word(object, 0x40)) + " flag " + Hex(Word(object, 0x44) & 0xff) +
         " sep " + (Separator(object).mVTable == GameAddress(cRZString::kVTable) ? "game" : "other") + " [" +
         TextOf(Separator(object)) + "] " + std::to_string(Separator(object).mRefCount);
    s += " | cp " + std::to_string(*GameAddress<int32_t>(kCodePageGlobal));
    return s;
}

struct Setup {
    uint32_t language = 1;
    uint32_t country = 0x36;
    std::vector<uint32_t> allowed; // through slot 5, in this order
    bool recordSelf = false;
};

void Configure(void *object, const Setup &setup)
{
    SetWord(object, 0x2c, setup.language);
    SetWord(object, 0x30, setup.country);
    for (uint32_t id : setup.allowed) SlotOf<void(__thiscall *)(void *, uint32_t)>(object, 5)(object, id);
}

struct SideResult {
    std::string result;
    std::vector<std::string> log;
    std::string state;
};

std::string Describe(const Setup &setup)
{
    std::string s = "lang " + Hex(setup.language) + " country " + Hex(setup.country) + " allowed {";
    for (uint32_t id : setup.allowed) s += Hex(id) + ",";
    return s + "}";
}

// Runs `body` on a fresh object of each side configured by `setup` and compares the result text, the log and the
// object's state afterwards.
void Compare(difftest::Context &ctx, impfx::Imports &imp, const Setup &setup, const std::string &what,
             const std::function<std::string(void *)> &body)
{
    SideResult sides[2];
    for (int side = 0; side < 2; ++side) {
        void *object = side == 0 ? NewOriginal(ctx) : NewOurs();
        Configure(object, setup);
        if (setup.recordSelf) RecordSelf(object);
        imp.BeginSide();
        drawfx::ResetAll();
        gLog.clear();
        gLocaleCalls = 0;
        *GameAddress<int32_t>(kCodePageGlobal) = kSeededCodePage;
        sides[side].result = body(object);
        sides[side].log = gLog;
        sides[side].state = StateOf(side, object);
        if (setup.recordSelf) StopRecordingSelf(object);
        if (side == 0)
            FreeOriginal(ctx, object);
        else
            FreeOurs(object);
    }
    ++ctx.cases;
    const SideResult &a = sides[0];
    const SideResult &b = sides[1];
    if (a.result != b.result)
        ctx.Fail("%s (%s): result [%s] / [%s]", what.c_str(), Describe(setup).c_str(), a.result.c_str(),
                 b.result.c_str());
    if (a.state != b.state)
        ctx.Fail("%s (%s): state\n  [%s]\n  [%s]", what.c_str(), Describe(setup).c_str(), a.state.c_str(),
                 b.state.c_str());
    if (a.log != b.log) {
        size_t i = 0;
        while (i < a.log.size() && i < b.log.size() && a.log[i] == b.log[i]) ++i;
        ctx.Fail("%s (%s): log %zu / %zu entries, first difference at %zu: [%s] / [%s]", what.c_str(),
                 Describe(setup).c_str(), a.log.size(), b.log.size(), i, i < a.log.size() ? a.log[i].c_str() : "(end)",
                 i < b.log.size() ? b.log[i].c_str() : "(end)");
    }
}

// The fixture of every test: the recording strings, the imports, the CRT locale fakes, the code page cache.
struct Fixture {
    impfx::Imports imp;
    shape::CodePatch setLocale;
    shape::CodePatch setMbcp;
    cRZLanguageManagerCrt savedCrt;
    int32_t savedCodePage;
    explicit Fixture(difftest::Context &ctx)
        : imp(ctx), setLocale(kSetLocale, reinterpret_cast<const void *>(&FakeSetLocale)),
          setMbcp(kSetMbcp, reinterpret_cast<const void *>(&FakeSetMbcp))
    {
        SetUpStrings(ctx);
        gRecordingString[33] = reinterpret_cast<void *>(&S33);
        IMPFX_ADD(imp, GetUserDefaultLCID).On(&OnGetUserDefaultLCID);
        IMPFX_ADD(imp, CompareStringA).On(&OnCompareString);
        savedCrt = LanguageManagerCrt();
        LanguageManagerCrt() = {&FakeSetLocale, &FakeSetMbcp};
        savedCodePage = *GameAddress<int32_t>(kCodePageGlobal);
        *GameAddress<int32_t>(kCodePageGlobal) = kSeededCodePage;
        gLocaleFailAt = -1;
        gUserLcid = 0x409;
        LearnVTables(ctx);
    }
    ~Fixture()
    {
        LanguageManagerCrt() = savedCrt;
        *GameAddress<int32_t>(kCodePageGlobal) = savedCodePage;
    }
};

// The result of a slot writing a string: its return value, then the string's text.
std::string WithText(uint32_t value, const RecString &out) { return Hex(value) + " [" + TextOf(out.s) + "]"; }

// Ids 0 to 0x45, the edges, and a few random values.
std::vector<uint32_t> Ids(std::mt19937 &rng, int randoms = 4)
{
    std::vector<uint32_t> out;
    for (uint32_t id = 0; id <= 0x45; ++id) out.push_back(id);
    const uint32_t extra[] = {0x7f, 0x80, 0xff, 0x100, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu};
    for (uint32_t id : extra) out.push_back(id);
    for (int i = 0; i < randoms; ++i) out.push_back(rng());
    return out;
}

// Current languages and countries the setups draw from (0 and ids past the tables included: the fields are words).
constexpr uint32_t kLanguages[] = {1, 2, 3, 0xa, 0xd, 0x17, 0x23, 0x2c, 0x2d, 0, 0xffffffffu};
constexpr uint32_t kCountries[] = {0x36, 1, 2, 0x15, 0x33, 0x38, 0x39, 0, 0xffffffffu};

template <class T, size_t N> T Pick(std::mt19937 &rng, const T (&values)[N]) { return values[rng() % N]; }

using NameFn = bool(__thiscall *)(void *, cIGZString *, uint32_t);
using FindFn = bool(__thiscall *)(void *, const cIGZString *, uint32_t *);
using IdFn = uint32_t(__thiscall *)(void *, uint32_t);
using BoolIdFn = bool(__thiscall *)(void *, uint32_t);
using VoidIdFn = void(__thiscall *)(void *, uint32_t);
using WordFn = uint32_t(__thiscall *)(void *);
using StepFn = bool(__thiscall *)(void *, uint32_t *);
using QueryFn = bool(__thiscall *)(void *, uint32_t, void **);

std::string Offset(const void *object, const void *p)
{
    if (!p) return "null";
    return "+" + Hex(static_cast<uint32_t>(static_cast<const uint8_t *>(p) - static_cast<const uint8_t *>(object)));
}

// The allowed sets the tests draw from.
std::vector<std::vector<uint32_t>> AllowedSets(std::mt19937 &rng)
{
    std::vector<std::vector<uint32_t>> sets = {{},   {1},       {2},          {1, 2, 3},  {0x2c}, {0x2d, 0x2c},
                                               {0},  {0, 5, 6}, {3, 3, 0x10}, {0xffffffffu, 7}};
    for (int i = 0; i < 6; ++i) {
        std::vector<uint32_t> s;
        const int n = 1 + static_cast<int>(rng() % 12);
        for (int k = 0; k < n; ++k) s.push_back(rng() % 0x30);
        sets.push_back(s);
    }
    return sets;
}

} // namespace

DIFF_TEST(cRZLanguageManager_Construct, 0xc6ed1c)
{
    // The constructor (ours through the creator hook): every field after construction, and the calls it makes.
    Fixture fx(ctx);
    Setup setup;
    setup.language = 1;
    setup.country = 0x36;
    Compare(ctx, fx.imp, setup, "construction", [&](void *) { return std::string(); });
}

DIFF_TEST(cRZLanguageManager_Create, 0xc6f338)
{
    // The creator: the face it returns (+4, cIGZSystemService) and the object behind it; freed through its +8 face's
    // deleting destructor.
    Fixture fx(ctx);
    std::string states[2];
    for (int side = 0; side < 2; ++side) {
        *GameAddress<int32_t>(kCodePageGlobal) = kSeededCodePage;
        cIGZSystemService *face =
            side == 0 ? ctx.Original<cIGZSystemService *(__cdecl *)()>(kCreator)() : cRZLanguageManager::Create();
        void *object = reinterpret_cast<uint8_t *>(face) - 4;
        states[side] = StateOf(side, object);
        SlotOf<void *(__thiscall *)(void *, uint32_t)>(object, 3, 8)(static_cast<uint8_t *>(object) + 8, 1u);
    }
    ++ctx.cases;
    if (states[0] != states[1]) ctx.Fail("Create: state\n  [%s]\n  [%s]", states[0].c_str(), states[1].c_str());
}

DIFF_TEST(cRZLanguageManager_Destruct, 0xc6edcf)
{
    // The destructor (called directly) and the deleting destructor (0xc6ee2a through the +8 face's slot 3, the thunk
    // 0xc6ee22) with flags 0 and 1: the set's nodes and head, the separator's heap text and (with bit 0) the object
    // freed; the result.
    Fixture fx(ctx);
    const std::vector<uint32_t> sets[] = {{}, {1}, {4, 2, 9}};
    for (const std::vector<uint32_t> &allowed : sets) {
        for (int route = 0; route < 3; ++route) {
            std::string outcome[2];
            for (int side = 0; side < 2; ++side) {
                void *object = side == 0 ? NewOriginal(ctx) : NewOurs();
                Setup setup;
                setup.allowed = allowed;
                Configure(object, setup);
                const_cast<cRZString &>(Separator(object)).mString = "a text long enough to live on the heap";
                const void *text = Separator(object).mString.data();
                const void *head = reinterpret_cast<const void *>(static_cast<uintptr_t>(Word(object, 0x24)));
                std::vector<const void *> nodes;
                const auto *set = reinterpret_cast<const std::set<uint32_t> *>(static_cast<const uint8_t *>(object) + 0x24);
                for (auto it = set->begin(); it != set->end(); ++it) nodes.push_back(&*it);
                difftest::BeginFrees();
                std::string r = "-";
                if (route == 0) {
                    if (side == 0)
                        ctx.Original<void(__thiscall *)(void *)>(kDtor)(object);
                    else
                        static_cast<cRZLanguageManager *>(object)->~cRZLanguageManager();
                } else {
                    void *face = static_cast<uint8_t *>(object) + 8;
                    void *returned = SlotOf<void *(__thiscall *)(void *, uint32_t)>(object, 3, 8)(face, route == 1 ? 0u : 1u);
                    r = Offset(object, returned);
                }
                const bool freedObject = difftest::FreedSinceBegin(object);
                outcome[side] = r + " object " + std::to_string(freedObject) + " text " +
                                std::to_string(difftest::CurrentFrees()->Freed(text)) + " head " +
                                std::to_string(difftest::CurrentFrees()->Freed(head)) + " nodes";
                for (const void *node : nodes) {
                    // The value sits 0x10 into its node (left, parent, right, colour bytes).
                    outcome[side] += " " + std::to_string(difftest::CurrentFrees()->Freed(static_cast<const uint8_t *>(node) - 0x10));
                }
                if (!freedObject) ::operator delete(object);
            }
            ++ctx.cases;
            if (outcome[0] != outcome[1])
                ctx.Fail("destruction route %d, %zu allowed: [%s] / [%s]", route, allowed.size(), outcome[0].c_str(),
                         outcome[1].c_str());
        }
    }
}

DIFF_TEST(cRZLanguageManager_QueryInterface, 0xc747ac)
{
    // QueryInterface through each face (0xc747ac; the thunks 0xc747e9 at +4 and 0xc747f1 at +8): the result, the face
    // handed out (as an offset) or the out pointer untouched, the count after. AddRef / Release (slots 1 / 2) through the
    // primary face: the counts; a release to 0 frees the object.
    Fixture fx(ctx);
    std::vector<uint32_t> ids = {0, 1, 2, cIGZLanguageManager::kIid, cIGZSystemService::kIID, cIGZLanguageUtility::kIid,
                                 0x441e5070, 0xffffffffu};
    for (int bit = 0; bit < 32; ++bit) ids.push_back(cIGZLanguageManager::kIid ^ (1u << bit));
    for (int bit = 0; bit < 32; bit += 3) ids.push_back(cIGZSystemService::kIID ^ (1u << bit));
    for (uint32_t id : ids) {
        for (uint32_t face : {0u, 4u, 8u}) {
            Setup setup;
            Compare(ctx, fx.imp, setup, "QueryInterface " + Hex(id) + " face " + Hex(face), [&](void *object) {
                void *out = reinterpret_cast<void *>(0x5a5a5a5a);
                void *self = static_cast<uint8_t *>(object) + face;
                const bool r = SlotOf<QueryFn>(object, 0, face)(self, id, &out);
                return Hex(r) + " " + (out == reinterpret_cast<void *>(0x5a5a5a5a) ? "untouched" : Offset(object, out));
            });
        }
    }
    for (uint32_t count : {1u, 2u, 5u}) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            void *object = side == 0 ? NewOriginal(ctx) : NewOurs();
            SetWord(object, 0xc, count);
            const uint32_t added = SlotOf<WordFn>(object, 1)(object);
            const uint32_t after = Word(object, 0xc);
            difftest::BeginFrees();
            const uint32_t released = SlotOf<WordFn>(object, 2)(object);
            const uint32_t released2 = SlotOf<WordFn>(object, 2)(object);
            const bool freed = difftest::FreedSinceBegin(object);
            results[side] = Hex(added) + " " + Hex(after) + " " + Hex(released) + " " + Hex(released2) + " freed " +
                            std::to_string(freed);
            if (!freed) {
                if (side == 0)
                    FreeOriginal(ctx, object);
                else
                    FreeOurs(object);
            }
        }
        ++ctx.cases;
        if (results[0] != results[1])
            ctx.Fail("AddRef / Release from %u: [%s] / [%s]", count, results[0].c_str(), results[1].c_str());
    }
}

DIFF_TEST(cRZLanguageManager_Allowed, 0xc6ee68)
{
    // The allowed set: AllowLanguage (slot 5, 0xc6ee68) and DisallowLanguage (6, 0xc748f0) over edges, then
    // IsLanguageAllowed (9, 0xc6eeab), NextAllowedLanguage (14, 0xc7301d), GetAllowedLanguages (47, 0xc70433) and
    // GetLanguageNames (46, 0xc7047f; the self calls recorded; the list starts with two recording strings, whose
    // destruction through slot 33 is logged).
    Fixture fx(ctx);
    std::mt19937 rng(9101);
    for (const std::vector<uint32_t> &allowed : AllowedSets(rng)) {
        Setup setup;
        setup.allowed = allowed;
        setup.language = Pick(rng, kLanguages);
        const std::vector<uint32_t> ids = Ids(rng, 2);
        Compare(ctx, fx.imp, setup, "IsLanguageAllowed", [&](void *object) {
            std::string r;
            for (uint32_t id : ids) r += Hex(SlotOf<BoolIdFn>(object, 9)(object, id)) + ",";
            return r;
        });
        Compare(ctx, fx.imp, setup, "Disallow", [&](void *object) {
            for (uint32_t id : {0u, 2u, 3u, 0x2cu, 0x2du, 0xffffffffu, 3u})
                SlotOf<VoidIdFn>(object, 6)(object, id);
            return std::string();
        });
        Compare(ctx, fx.imp, setup, "NextAllowedLanguage", [&](void *object) {
            std::string r;
            for (uint32_t start : {0u, 1u, 2u, 0x10u, 0x2bu, 0x2cu, 0x2du, 0x7fffffffu, 0xfffffffeu, 0xffffffffu}) {
                uint32_t id = start;
                const bool found = SlotOf<StepFn>(object, 14)(object, &id);
                r += Hex(found) + ":" + Hex(id) + ",";
            }
            return r;
        });
        Compare(ctx, fx.imp, setup, "GetAllowedLanguages", [&](void *object) {
            std::list<uint32_t> out = {7, 8};
            SlotOf<void(__thiscall *)(void *, std::list<uint32_t> *)>(object, 47)(object, &out);
            std::string r;
            for (uint32_t id : out) r += Hex(id) + ",";
            return r + " n " + std::to_string(out.size());
        });
        setup.recordSelf = true;
        Compare(ctx, fx.imp, setup, "GetLanguageNames", [&](void *object) {
            std::string r;
            {
                std::list<cRZLanguageNameEntry> out;
                for (const char *seed : {"first", "a second text long enough for the heap"}) {
                    {
                        PlainString text(seed);
                        out.emplace_back(text.s);
                    }
                    out.back().String()->mVTable = gRecordingString;
                }
                SlotOf<void(__thiscall *)(void *, std::list<cRZLanguageNameEntry> *)>(object, 46)(object, &out);
                for (cRZLanguageNameEntry &entry : out) {
                    cRZString *s = entry.String();
                    r += "[" + TextOf(*s) + "] " + (s->mVTable == GameAddress(cRZString::kVTable) ? "game" : "other") +
                         " " + std::to_string(s->mRefCount) + ",";
                }
                r += " n " + std::to_string(out.size());
            }
            return r;
        });
    }
}

DIFF_TEST(cRZLanguageManager_SetLanguage, 0xc74960)
{
    // SetLanguage (slot 8, 0xc74960) with its locale helper (0xc74831) and RZText::SetLocale (0x1b18b5): every target
    // id and edges, from several current languages, with allowed sets, and with each setlocale call failing in turn.
    // The self calls (9, 13), the CRT calls, the code page cache and the utility's language are compared.
    Fixture fx(ctx);
    std::mt19937 rng(9102);
    const std::vector<std::vector<uint32_t>> sets = {{}, {1, 3, 5}, {0x2c, 0}, {2}};
    for (const std::vector<uint32_t> &allowed : sets) {
        for (uint32_t target : Ids(rng, 2)) {
            for (int failAt = -1; failAt < 4; ++failAt) {
                if (failAt >= 0 && (target == 0 || target > 0x2c || (rng() & 3) != 0)) continue;
                Setup setup;
                setup.allowed = allowed;
                setup.language = Pick(rng, kLanguages);
                setup.recordSelf = true;
                gLocaleFailAt = failAt;
                Compare(ctx, fx.imp, setup, "SetLanguage " + Hex(target) + " fail " + std::to_string(failAt),
                        [&](void *object) { return Hex(SlotOf<BoolIdFn>(object, 8)(object, target)); });
            }
        }
    }
    gLocaleFailAt = -1;
}

DIFF_TEST(cRZLanguageManager_Lcid, 0xc731c6)
{
    // GetLanguageLcid (slot 11, 0xc731c6) over ids; LanguageFromLcid (12, 0xc71bd8) over every 16-bit LCID and some
    // with high bits; GetUserDefaultLanguage (10, 0xc70b25) through the GetUserDefaultLCID fake (the self call to 12
    // recorded).
    Fixture fx(ctx);
    std::mt19937 rng(9103);
    for (uint32_t language : kLanguages) {
        Setup setup;
        setup.language = language;
        const std::vector<uint32_t> ids = Ids(rng, 4);
        Compare(ctx, fx.imp, setup, "GetLanguageLcid", [&](void *object) {
            std::string r;
            for (uint32_t id : ids) r += Hex(SlotOf<IdFn>(object, 11)(object, id)) + ",";
            return r;
        });
    }
    Setup setup;
    Compare(ctx, fx.imp, setup, "LanguageFromLcid", [&](void *object) {
        std::string r;
        for (uint32_t lcid = 0; lcid <= 0xffff; ++lcid) r += Hex(SlotOf<IdFn>(object, 12)(object, lcid)) + ",";
        for (uint32_t lcid : {0x10409u, 0xffff0409u, 0x80000c0au, 0x12345678u})
            r += Hex(SlotOf<IdFn>(object, 12)(object, lcid)) + ",";
        return r;
    });
    setup.recordSelf = true;
    for (uint32_t lcid : {0x409u, 0x40cu, 0x10409u, 0x804u, 0x1234u, 0u, 0xffffffffu}) {
        gUserLcid = lcid;
        Compare(ctx, fx.imp, setup, "GetUserDefaultLanguage " + Hex(lcid),
                [&](void *object) { return Hex(SlotOf<WordFn>(object, 10)(object)); });
    }
    gUserLcid = 0x409;
}

DIFF_TEST(cRZLanguageManager_Names, 0xc71964)
{
    // The slots that write a name, over every id and edges, into empty and non-empty recording strings, from several
    // current languages and countries: GetLocaleName (13, 0xc7225f), HasLanguageName (15, 0xc7193d), GetLanguageName
    // (16, 0xc71964), GetNativeLanguageName (17, 0xc7214a), its copies 21 (0x55e17e) and 22 (the shared 0x96de02),
    // GetLanguageCode (24, 0xc716bb), GetCountryCode (26, 0xc71342), GetCountryName (34, 0xc705fb) and its copy 35
    // (0xc709e7). Self calls recorded.
    Fixture fx(ctx);
    std::mt19937 rng(9104);
    const unsigned slots[] = {13, 15, 16, 17, 21, 22, 24, 26, 34, 35};
    for (unsigned slot : slots) {
        for (int round = 0; round < 3; ++round) {
            Setup setup;
            setup.language = round == 0 ? 1 : Pick(rng, kLanguages);
            setup.country = round == 0 ? 0x36 : Pick(rng, kCountries);
            setup.recordSelf = true;
            const std::vector<uint32_t> ids = Ids(rng, 2);
            for (const char *initial : {"", "old text"}) {
                Compare(ctx, fx.imp, setup, "slot " + std::to_string(slot) + " [" + initial + "]", [&](void *object) {
                    std::string r;
                    for (uint32_t id : ids) {
                        RecString out(initial);
                        const bool ok = SlotOf<NameFn>(object, slot)(object, out.I(), id);
                        r += WithText(ok, out) + ",";
                    }
                    return r;
                });
            }
        }
    }
}

DIFF_TEST(cRZLanguageManager_FromName, 0xc7207e)
{
    // The slots that look a name up: LanguageFromName (18, 0xc7207e), LanguageFromNativeName (19, 0xc720e4),
    // LanguageFromCode (25, 0xc718d7), CountryFromCode (27, 0xc71656), CountryFromName (36, 0xc70917) and its copy
    // (37, 0xc7097f), over every text the name slots write (as written and upper-cased), the empty text and junk; the
    // utility's Compare through the CompareStringA fake; self calls recorded.
    Fixture fx(ctx);
    std::vector<std::string> names = {"", "zz", "Untranslated", "english", "ENGLISH", "en", "EN", "UnitedStates"};
    {
        void *object = NewOriginal(ctx);
        for (unsigned slot : {16u, 17u, 24u, 26u, 34u}) {
            for (uint32_t id = 1; id <= 0x39; ++id) {
                PlainString out;
                SlotOf<NameFn>(object, slot)(object, out.s.AsInterface(), id);
                names.push_back(out.s.mString);
                std::string upper = out.s.mString;
                for (char &c : upper)
                    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 0x20);
                names.push_back(upper);
            }
        }
        FreeOriginal(ctx, object);
    }
    std::mt19937 rng(9105);
    const unsigned slots[] = {18, 19, 25, 27, 36, 37};
    for (unsigned slot : slots) {
        Setup setup;
        setup.language = Pick(rng, kLanguages);
        setup.country = Pick(rng, kCountries);
        setup.recordSelf = true;
        Compare(ctx, fx.imp, setup, "slot " + std::to_string(slot), [&](void *object) {
            std::string r;
            for (const std::string &name : names) {
                RecString text(name.c_str());
                uint32_t id = 0xdeadbeef;
                const bool ok = SlotOf<FindFn>(object, slot)(object, text.I(), &id);
                r += Hex(ok) + ":" + Hex(id) + ",";
            }
            return r;
        });
    }
}

// TranslateLanguageName's slot 20 as a fake on a copy of the side's vtable (the real one calls itself for ever).
bool gTranslateResult = false;
bool gTranslateWrites = false;
bool __fastcall FakeTranslate(void *, void *, cIGZString *name, uint32_t language)
{
    Note("s20(0x%x) [%s] on %s", language, TextOf(*static_cast<cRZString *>(static_cast<void *>(name))).c_str(),
         static_cast<cRZString *>(static_cast<void *>(name))->mVTable == GameAddress(cRZString::kVTable) ? "a game string"
                                                                                                         : "another");
    if (gTranslateWrites) name->FromChar("translated");
    return gTranslateResult;
}

DIFF_TEST(cRZLanguageManager_Translate, 0xc7202b)
{
    // TranslateLanguageName (slot 20, 0xc7202b) called on an object whose vtable copy's slot 20 is the fake: the copy
    // it hands the fake, what it writes back (a recording string) and its result. ORIGINAL-BUG kept: the slot calls
    // itself.
    Fixture fx(ctx);
    static void *patched[48];
    for (const char *initial : {"", "Deutsch", "a name long enough to live on the heap"}) {
        for (int mode = 0; mode < 4; ++mode) {
            for (uint32_t language : {0u, 1u, 5u}) {
                gTranslateResult = (mode & 1) != 0;
                gTranslateWrites = (mode & 2) != 0;
                Setup setup;
                Compare(ctx, fx.imp, setup, std::string("Translate [") + initial + "] mode " + std::to_string(mode),
                        [&](void *object) {
                            void **own = *static_cast<void ***>(object);
                            std::memcpy(patched, own, sizeof(patched));
                            patched[20] = reinterpret_cast<void *>(&FakeTranslate);
                            *static_cast<void ***>(object) = patched;
                            RecString name(initial);
                            const bool r = reinterpret_cast<NameFn>(own[20])(object, name.I(), language);
                            *static_cast<void ***>(object) = own;
                            return WithText(r, name);
                        });
            }
        }
    }
}

DIFF_TEST(cRZLanguageManager_Mapping, 0xc71fc4)
{
    // The id mappings and the small slots: GetBaseLanguage (23, 0xc71fc4), GetCountryLanguage (30, 0xc7304b),
    // GetLanguageCountry (31, 0xc711c2), GetLanguage (7) / GetCountry (28), SetCountry (29, 0xc7491f), vf32, vf33,
    // GetLanguageUtility (38, 0xc724ab), vf40, vf41, vf42 to vf45 (0xc6ef7b, 0xc6ef6c, 0xc6efa0, 0xc6ef91: canaries
    // around the out words).
    Fixture fx(ctx);
    std::mt19937 rng(9106);
    for (int round = 0; round < 6; ++round) {
        Setup setup;
        setup.language = round == 0 ? 1 : Pick(rng, kLanguages);
        setup.country = round == 0 ? 0x36 : Pick(rng, kCountries);
        const std::vector<uint32_t> ids = Ids(rng, 4);
        const uint32_t country = Pick(rng, kCountries);
        const uint32_t a = rng();
        const uint32_t b = rng();
        Compare(ctx, fx.imp, setup, "mappings", [&](void *object) {
            std::string r;
            for (uint32_t id : ids) {
                r += Hex(SlotOf<IdFn>(object, 23)(object, id)) + "/" + Hex(SlotOf<IdFn>(object, 30)(object, id)) + "/" +
                     Hex(SlotOf<IdFn>(object, 31)(object, id)) + "/" +
                     Offset(object, SlotOf<void *(__thiscall *)(void *, uint32_t)>(object, 38)(object, id)) + "/" +
                     Hex(SlotOf<BoolIdFn>(object, 33)(object, id));
                const float f = SlotOf<float(__thiscall *)(void *, uint32_t)>(object, 40)(object, id);
                uint32_t bits;
                std::memcpy(&bits, &f, 4);
                r += "/" + Hex(bits) + ",";
                SlotOf<void(__thiscall *)(void *, uint32_t, uint32_t)>(object, 41)(object, id, ~id);
            }
            r += " get " + Hex(SlotOf<WordFn>(object, 7)(object)) + " " + Hex(SlotOf<WordFn>(object, 28)(object)) +
                 " vf32 " + Hex(SlotOf<WordFn>(object, 32)(object));
            return r;
        });
        Compare(ctx, fx.imp, setup, "SetCountry " + Hex(country), [&](void *object) {
            return Hex(SlotOf<BoolIdFn>(object, 29)(object, country));
        });
        Compare(ctx, fx.imp, setup, "vf42-45", [&](void *object) {
            uint32_t out[4] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
            std::string r;
            r += Hex(SlotOf<bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t *, uint32_t)>(object, 42)(
                object, a, b, out + 1, 0x99));
            r += Hex(SlotOf<bool(__thiscall *)(void *, uint32_t, uint32_t *, uint32_t)>(object, 43)(object, b, out + 3, 0x98));
            for (uint32_t w : out) r += " " + Hex(w);
            uint32_t out2[4] = {0x55555555, 0x66666666, 0x77777777, 0x88888888};
            r += Hex(SlotOf<bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t *, uint32_t)>(object, 44)(
                object, b, a, 0x97, out2 + 1, 0x96));
            r += Hex(SlotOf<bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t *, uint32_t)>(object, 45)(
                object, a, 0x95, out2 + 3, 0x94));
            for (uint32_t w : out2) r += " " + Hex(w);
            return r;
        });
    }
}

DIFF_TEST(cRZLanguageManager_CreateUtility, 0xc72fe8)
{
    // CreateLanguageUtility (slot 39, 0xc72fe8): the new utility's vtable (the game's / ours), count, language, country,
    // flag and separator; released through its slot 2 (freed).
    Fixture fx(ctx);
    for (uint32_t language : {0u, 1u, 3u, 0x2cu, 0x2du, 0xffffffffu}) {
        Setup setup;
        setup.language = 5;
        Compare(ctx, fx.imp, setup, "CreateLanguageUtility " + Hex(language), [&](void *object) {
            void *utility = SlotOf<void *(__thiscall *)(void *, uint32_t)>(object, 39)(object, language);
            // The vtable: the one the manager's own utility carries (the game's / ours), or another.
            const bool same = Word(utility, 0) == Word(object, 0x34);
            const cRZString &sep = *reinterpret_cast<const cRZString *>(static_cast<uint8_t *>(utility) + 0x14);
            std::string r = std::string(same ? "utility vtable" : "another vtable") + " refs " + Hex(Word(utility, 4)) +
                            " lang " + Hex(Word(utility, 8)) + " country " + Hex(Word(utility, 0xc)) + " flag " +
                            Hex(Word(utility, 0x10) & 0xff) + " sep [" + TextOf(sep) + "]";
            difftest::BeginFrees();
            const uint32_t left = SlotOf<WordFn>(utility, 2)(utility);
            r += " release " + Hex(left) + " freed " + std::to_string(difftest::FreedSinceBegin(utility));
            return r;
        });
    }
}
