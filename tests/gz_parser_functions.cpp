// Differential tests for the command parser's own expression functions (src/gz/ParserFunctions.h, lane NV): the slot-0
// bodies (Evaluate) of minVersion, maxVersion, varExists, commandExists, definitionExists, match and propExists, and
// ReadName (0xb0a600). The name and help getters are byte matches (tools/matchcheck.py).
//
// Every Evaluate case runs both sides from the same state on a function object (the parser function's 0x10 bytes or
// propExists' 0x14) and compares: the result buffer (all 0x14 bytes, pre-filled with the same random bytes, so a partial
// write before an error shows), the cursor's offset, the returned pointer, what was thrown (RunGuarded: the cError's
// vtable word, message and type list), the calls the fakes logged and the names they were given (by content).
//
// The parser is a block of random bytes: its versions (+0x60, +0x64) and, at +8, a vbtable whose third entry leads to
// the parser face (a fake object: slots 27 FindVariable, 48 HasDefinition, 49 HasCommand log the name and answer the
// case's word, sometimes with only bits above the low byte set). propExists' property set is a fake whose slot 10 logs
// the key (a std::string layout: its size, capacity and bytes). The texts after the function's name are made of the
// grammar's pieces (parentheses, blanks, names of letters, digits and '_', quoted texts, a comma) and sometimes broken.
//
// Allocation: the names are std::strings on both sides (the image's through the game's operator new / delete, patched
// to the CRT's here as in gz_parser_run_text.cpp; propExists' key through the allocator service, a counting fake that
// must end each side with nothing live).
#define NOMINMAX
#include "code_patch.h"
#include "command_parser.h"
#include "gz/ExpressionEvaluator.h"
#include "gz/ParserFunctions.h"

#include <cstdlib>
#include <random>
#include <string>

namespace cp = nGZCommandParser;
using namespace cptest;

namespace {

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

struct Allocators {
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
};

// The allocator service (cache 0x10c95d4, guard 0x10c95d8 as after its first use): slot 3 allocates, slot 4 frees.
int gServiceLive = 0;
uint32_t __fastcall ServiceNop(void *, void *) { return 0; }
void *__fastcall ServiceAlloc(void *, void *, uint32_t n)
{
    ++gServiceLive;
    return std::malloc(n ? n : 1);
}
void __fastcall ServiceFree(void *, void *, void *p)
{
    if (p) --gServiceLive;
    std::free(p);
}
void *gServiceVTable[5] = {reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceNop),
                           reinterpret_cast<void *>(&ServiceAlloc), reinterpret_cast<void *>(&ServiceFree)};
void *gServiceObject = gServiceVTable;

struct ServiceFake {
    void **cache;
    int32_t *guard;
    void *savedCache;
    int32_t savedGuard;
    explicit ServiceFake(difftest::Context &ctx)
        : cache(ctx.image.At<void *>(0x10c95d4)), guard(ctx.image.At<int32_t>(0x10c95d8)), savedCache(*cache), savedGuard(*guard)
    {
        *cache = &gServiceObject;
        *guard = INT32_MIN;
    }
    ~ServiceFake()
    {
        *cache = savedCache;
        *guard = savedGuard;
    }
};

void PushWord(uint32_t w) { gExtra.insert(gExtra.end(), reinterpret_cast<uint8_t *>(&w), reinterpret_cast<uint8_t *>(&w) + 4); }

void PushText(const char *s, size_t n)
{
    PushWord(static_cast<uint32_t>(n));
    gExtra.insert(gExtra.end(), s, s + n);
}

// The parser face's answers, per slot.
uint32_t gFaceRet[64];
constexpr int kFaceId = 20, kSetId = 21;

template <int S> uint32_t __fastcall FaceTakesName(void *self, void *, const char *name)
{
    difftest::NotCounted bookkeeping;
    gLog.push_back({kFaceId, S, reinterpret_cast<uint32_t>(self), {0, 0, 0, 0}});
    PushText(name, std::strlen(name));
    return gFaceRet[S];
}

void *gFaceVTable[64];

// The property set: slot 10 (HasProperty) reads the key as a std::string (size +0x10, capacity +0x14, the bytes inline
// below 16 or through +0).
uint32_t gSetRet = 0;
uint32_t __fastcall SetHasProperty(void *self, void *, const uint8_t *key)
{
    difftest::NotCounted bookkeeping;
    gLog.push_back({kSetId, 10, reinterpret_cast<uint32_t>(self), {0, 0, 0, 0}});
    const uint32_t size = *reinterpret_cast<const uint32_t *>(key + 0x10);
    const uint32_t capacity = *reinterpret_cast<const uint32_t *>(key + 0x14);
    const char *text = capacity >= 16 ? *reinterpret_cast<const char *const *>(key) : reinterpret_cast<const char *>(key);
    PushWord(capacity);
    PushText(text, size);
    return gSetRet;
}
void *gSetVTable[16];

// ---- texts ------------------------------------------------------------------------------------------------------------

const char *const kNames[] = {"x", "abc", "Name1", "a_b", "_x", "9lives", "camelCase", "averyveryverylongname0123", "q", ""};
const char *const kBlanks[] = {"", " ", "\t", " \r\n ", "  "};

std::string Pick(std::mt19937 &g, const char *const *list, size_t n) { return list[g() % n]; }
#define PICK(g, list) Pick(g, list, sizeof(list) / sizeof(list[0]))

// Breaks a text now and then: a character replaced, inserted or removed.
void Break(std::mt19937 &g, std::string &s)
{
    static const char kChars[] = "() ,\"x_9\xe9\t&";
    if (g() % 4 != 0) return;
    const size_t at = s.empty() ? 0 : g() % (s.size() + 1);
    const char c = kChars[g() % (sizeof(kChars) - 1)];
    switch (g() % 3) {
    case 0:
        if (at < s.size()) s[at] = c;
        break;
    case 1: s.insert(s.begin() + at, c); break;
    default:
        if (at < s.size()) s.erase(at, 1);
        break;
    }
}

// "(name)" with blanks around the pieces; sometimes a name with a character outside the name class inside it.
std::string NameCall(std::mt19937 &g)
{
    std::string s = PICK(g, kBlanks) + "(" + PICK(g, kBlanks) + PICK(g, kNames);
    if (g() % 6 == 0) s += g() % 2 ? "_tail" : "-x";
    s += PICK(g, kBlanks) + ")" + (g() % 2 ? " + 1" : "");
    Break(g, s);
    return s;
}

std::string EmptyCall(std::mt19937 &g)
{
    std::string s = PICK(g, kBlanks) + "(" + PICK(g, kBlanks) + ")" + (g() % 3 == 0 ? "*2" : "");
    Break(g, s);
    return s;
}

const char *const kMatchTexts[] = {"abc", "ABC", "a*", "*", "?b?", "", "hello world", "h*o w?rld", "x*y*z", "xyz", "*c", "a\xe9", "A\xc9", "[ab]"};

std::string MatchCall(std::mt19937 &g)
{
    std::string s = PICK(g, kBlanks) + "(" + PICK(g, kBlanks) + "\"" + PICK(g, kMatchTexts) + "\"" + PICK(g, kBlanks) + "," +
                    PICK(g, kBlanks) + "\"" + PICK(g, kMatchTexts) + "\"" + PICK(g, kBlanks) + ")";
    Break(g, s);
    return s;
}

// ---- the cases --------------------------------------------------------------------------------------------------------

constexpr size_t kParserSize = 0x100;

struct World {
    alignas(8) uint8_t object[0x14] = {};
    alignas(8) uint8_t parser[kParserSize] = {};
    alignas(8) uint8_t face[16] = {};
    alignas(8) uint8_t set[16] = {};
    alignas(8) uint8_t evaluator[16] = {};
    int32_t vbtable[4] = {};
};

uint32_t RandomAnswer(std::mt19937 &g)
{
    switch (g() % 4) {
    case 0: return 0;
    case 1: return g() & 0xffffff00u;
    default: return g();
    }
}

void SetUpWorld(World &w, std::mt19937 &g, bool propertySet)
{
    for (auto &b : w.parser) b = static_cast<uint8_t>(g());
    for (auto &b : w.object) b = static_cast<uint8_t>(g());
    *reinterpret_cast<int32_t *>(w.parser + 0x60) = g() % 3 == 0 ? -1 : static_cast<int32_t>(g() % 5 == 0 ? g() : g() % 40);
    *reinterpret_cast<int32_t *>(w.parser + 0x64) = g() % 3 == 0 ? -1 : static_cast<int32_t>(g() % 5 == 0 ? g() : g() % 40);
    w.vbtable[0] = 0;
    w.vbtable[1] = static_cast<int32_t>(g() % 0x40);
    w.vbtable[2] = static_cast<int32_t>(w.face - (w.parser + 8));
    w.vbtable[3] = 0;
    *reinterpret_cast<int32_t **>(w.parser + 8) = w.vbtable;
    *reinterpret_cast<void **>(w.face) = gFaceVTable;
    *reinterpret_cast<void **>(w.set) = gSetVTable;
    if (propertySet) *reinterpret_cast<void **>(w.object + 0x10) = g() % 6 == 0 ? nullptr : w.set;
    else *reinterpret_cast<void **>(w.object + 0xc) = w.parser;
    for (int s : {27, 48, 49}) gFaceRet[s] = RandomAnswer(g);
    gSetRet = RandomAnswer(g);
}


template <class C>
void EvaluateTest(difftest::Context &ctx, uint32_t rva, const char *name, cp::Value *(C::*ours)(cp::Value *, const char **, cp::ExpressionEvaluator *),
                  std::string (*text)(std::mt19937 &), bool propertySet, int cases)
{
    Allocators allocators;
    ServiceFake service(ctx);
    for (auto &s : gFaceVTable) s = nullptr;
    gFaceVTable[27] = reinterpret_cast<void *>(&FaceTakesName<27>);
    gFaceVTable[48] = reinterpret_cast<void *>(&FaceTakesName<48>);
    gFaceVTable[49] = reinterpret_cast<void *>(&FaceTakesName<49>);
    for (auto &s : gSetVTable) s = nullptr;
    gSetVTable[10] = reinterpret_cast<void *>(&SetHasProperty);
    auto orig = ctx.Original<cp::Value *(__thiscall *)(void *, cp::Value *, const char **, void *)>(rva);
    std::mt19937 g(rva);
    static World w;
    for (int i = 0; i < cases; ++i) {
        SetUpWorld(w, g, propertySet);
        const std::string input = text(g);
        alignas(8) uint8_t out[sizeof(cp::Value)];
        for (auto &b : out) b = static_cast<uint8_t>(g());
        const char *cursor = input.c_str();
        int liveA = 0, liveB = 0;
        auto run = [&](bool original) {
            cursor = input.c_str();
            gServiceLive = 0;
            Outcome o = RunGuarded([&] {
                cp::Value *r = original ? orig(w.object, reinterpret_cast<cp::Value *>(out), &cursor, w.evaluator)
                                    : (reinterpret_cast<C *>(w.object)->*ours)(reinterpret_cast<cp::Value *>(out), &cursor,
                                                                               reinterpret_cast<cp::ExpressionEvaluator *>(w.evaluator));
                return reinterpret_cast<uint32_t>(r) - reinterpret_cast<uint32_t>(out);
            });
            PushWord(static_cast<uint32_t>(cursor - input.c_str()));
            (original ? liveA : liveB) = gServiceLive;
            return o;
        };
        char what[160];
        std::snprintf(what, sizeof(what), "%s case %d text '%s'", name, i, Printable(input).c_str());
        Differential(ctx, what, {{out, sizeof(out)}, {w.object, sizeof(w.object)}, {w.parser, kParserSize}},
                     [&] { return run(true); }, [&] { return run(false); });
        if (liveA != 0 || liveB != 0) ctx.Fail("%s: allocator service blocks left live: %d vs %d", what, liveA, liveB);
    }
}

} // namespace

DIFF_TEST(cMinVersionFunction_Evaluate, 0xafeeb0)
{
    EvaluateTest(ctx, 0xafeeb0, "minVersion", &cp::cMinVersionFunction::Evaluate, &EmptyCall, false, 600);
}

DIFF_TEST(cMaxVersionFunction_Evaluate, 0xafee50)
{
    EvaluateTest(ctx, 0xafee50, "maxVersion", &cp::cMaxVersionFunction::Evaluate, &EmptyCall, false, 600);
}

DIFF_TEST(cVarExistsFunction_Evaluate, 0xafef10)
{
    EvaluateTest(ctx, 0xafef10, "varExists", &cp::cVarExistsFunction::Evaluate, &NameCall, false, 1000);
}

DIFF_TEST(cCommandExistsFunction_Evaluate, 0xafe850)
{
    EvaluateTest(ctx, 0xafe850, "commandExists", &cp::cCommandExistsFunction::Evaluate, &NameCall, false, 1000);
}

DIFF_TEST(cDefinitionExistsFunction_Evaluate, 0xafe970)
{
    EvaluateTest(ctx, 0xafe970, "definitionExists", &cp::cDefinitionExistsFunction::Evaluate, &NameCall, false, 1000);
}

DIFF_TEST(cMatchFunction_Evaluate, 0xafeb90)
{
    EvaluateTest(ctx, 0xafeb90, "match", &cp::cMatchFunction::Evaluate, &MatchCall, false, 3000);
}

DIFF_TEST(cPropExistsFunction_Evaluate, 0xb3b1e0)
{
    EvaluateTest(ctx, 0xb3b1e0, "propExists", &cp::cPropExistsFunction::Evaluate, &NameCall, true, 1000);
}

DIFF_TEST(ParserFunctions_ReadName, 0xb0a600)
{
    auto orig = ctx.Original<cp::NameToken *(__thiscall *)(void *, cp::NameToken *, const char **)>(0xb0a600);
    std::mt19937 g(0xb0a600);
    for (int i = 0; i < 4000; ++i) {
        std::string s = PICK(g, kBlanks) + PICK(g, kNames) + (g() % 2 ? ")" : " x");
        Break(g, s);
        if (g() % 10 == 0) s = std::string(1, static_cast<char>(g() % 256)) + s;
        cp::NameToken a{reinterpret_cast<const char *>(0x11111111), 0x22222222}, b = a;
        const char *ca = s.c_str(), *cb = s.c_str();
        uint8_t evaluator[8] = {};
        cp::NameToken *ra = orig(evaluator, &a, &ca);
        cp::NameToken *rb = cp::ReadName(reinterpret_cast<cp::ExpressionEvaluator *>(evaluator), nullptr, &b, &cb);
        ++ctx.cases;
        if (ra != &a || rb != &b || a.text != b.text || a.length != b.length || ca != cb)
            ctx.Fail("ReadName '%s': returned %d/%d, text +%d/+%d, length %u/%u, cursor +%d/+%d", Printable(s).c_str(), ra == &a, rb == &b,
                     static_cast<int>(a.text - s.c_str()), static_cast<int>(b.text - s.c_str()), a.length, b.length,
                     static_cast<int>(ca - s.c_str()), static_cast<int>(cb - s.c_str()));
    }
}
