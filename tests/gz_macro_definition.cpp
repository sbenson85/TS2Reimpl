// Differential tests for the command parser's macro definitions (src/gz/MacroDefinition.h, lane NV): the definition
// searches and their comparisons (0xafd790, 0xafd520, 0xafea90, 0xafeb10), the held-definition vector's insert, its
// reallocating emplace and its uninitialized copy (0xafd3c0, 0xafc950, 0x29a2e0), the definition's destructors
// (0xafe6c0, 0xaff0d0) and the parameter substitution (0xb06920).
// define's Execute and HandleLine are in command_parser_define.cpp.
//
// The definitions are fakes with the definition's layout up to the name: a FakeVTable (slot 1 release and slot 2
// reference recorded, the calls compared in order) and a std::string name at +8. Vectors the code may reallocate or free
// are rebuilt on fresh heap blocks before each side runs and read afterwards (sizes, capacities, elements as pool
// indexes) into the data the fakes saw, then freed and their words cleared, so the compared memory holds no heap address.
// The image's operator new / delete are patched to the CRT's (as in gz_parser_run_text.cpp) and the RZ allocator stood
// in on both sides (as in command_parser_create.cpp), so either side's blocks can be freed by the other's code.
#define NOMINMAX
#include "code_patch.h"
#include "command_parser.h"
#include "gz/MacroDefinition.h"
#include "gz/cBaseParser.h"
#include "rz/RZMemory.h"

#include <algorithm>
#include <cstdlib>
#include <random>
#include <string>

namespace cp = nGZCommandParser;
using namespace cptest;

namespace {

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void *__cdecl TestRzMalloc(uint32_t bytes) { return std::malloc(bytes ? bytes : 1); }
void __cdecl TestRzFree(void *p) { std::free(p); }

struct Allocators {
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    shape::StandIn rzMalloc{0x1be3b0, &RZMemory::Malloc, &TestRzMalloc};
    shape::StandIn rzFree{0x1be3a1, &RZMemory::Free, &TestRzFree};
};

void PushWord(uint32_t w)
{
    difftest::NotCounted bookkeeping;
    gExtra.insert(gExtra.end(), reinterpret_cast<uint8_t *>(&w), reinterpret_cast<uint8_t *>(&w) + 4);
}

void PushText(const char *s, size_t n)
{
    difftest::NotCounted bookkeeping;
    PushWord(static_cast<uint32_t>(n));
    gExtra.insert(gExtra.end(), s, s + n);
}

// ---- the definitions --------------------------------------------------------------------------------------------------

struct FakeDefinition {
    void *vtable;     // +0
    uint32_t count;   // +4
    std::string name; // +8
};

// In the name-table order (length first, then the bytes from the last one as signed chars).
const char *const kPool[] = {"x", "\xe9", "ba", "ab", "set", "alpha", "longer1", "averyveryverylongname"};
constexpr int kPoolSize = 8;
// Keys: the pool's names, near misses, the empty name, bytes above 0x7f.
const char *const kKeys[] = {"x",     "\xe9",    "ba", "ab", "set", "alpha", "longer1", "averyveryverylongname", "y", "aa", "sex", "",
                             "alphA", "longer2", "bb", "b\xe9", "\xe9\xe9", "zzzzzzzzzzzzzzzzzzzzz", "a"};
constexpr int kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

FakeVTable gItemVt{5};
FakeDefinition gDefs[kPoolSize];

void SetUpPool()
{
    gItemVt.Set<1>(0);
    gItemVt.Set<2>(0);
    for (int i = 0; i < kPoolSize; ++i) {
        gDefs[i].vtable = gItemVt.slots;
        gDefs[i].count = 0x100 + i;
        gDefs[i].name = kPool[i];
    }
}

uint8_t PoolIndex(void *p)
{
    if (!p) return 0xfe;
    for (int k = 0; k < kPoolSize; ++k)
        if (p == &gDefs[k]) return static_cast<uint8_t>(k);
    return 0xff;
}

// A sorted run of pool definitions (as pointers): a random subset, sometimes with repeats.
std::vector<void *> SortedRun(std::mt19937 &g)
{
    std::vector<void *> run;
    for (int i = 0; i < kPoolSize; ++i) {
        if (g() % 3 == 0) continue;
        run.push_back(&gDefs[i]);
        if (g() % 6 == 0) run.push_back(&gDefs[i]);
    }
    return run;
}

} // namespace

// ---- the searches -----------------------------------------------------------------------------------------------------

DIFF_TEST(MacroDefinition_Searches, 0xafd790)
{
    // Also 0xafd520 (equal_range), 0xafea90 and 0xafeb10 (the two comparisons): results as indexes, the comparisons'
    // low byte (a bool), the key word handed in unused.
    SetUpPool();
    auto lower = ctx.Original<void **(__cdecl *)(void **, void **, const void *, const cp::DefinitionKey *)>(0xafd790);
    auto range = ctx.Original<cp::DefinitionRange *(__cdecl *)(cp::DefinitionRange *, void **, void **, const void *, cp::DefinitionKey)>(0xafd520);
    auto before = ctx.Original<bool(__thiscall *)(const cp::DefinitionKey *, const void *, uint32_t)>(0xafea90);
    auto after = ctx.Original<bool(__thiscall *)(const cp::DefinitionKey *, uint32_t, const void *)>(0xafeb10);
    std::mt19937 g(0xafd790);
    for (int i = 0; i < 3000; ++i) {
        std::vector<void *> run = SortedRun(g);
        const char *name = kKeys[g() % kKeyCount];
        const cp::DefinitionKey key{name + std::strlen(name), name};
        void **first = run.data(), **last = run.data() + run.size();
        const uint32_t word = g();
        auto *ourFirst = reinterpret_cast<cp::DefinitionRef *>(first);
        auto *ourLast = reinterpret_cast<cp::DefinitionRef *>(last);
        gLog.clear();
        void **la = lower(first, last, &word, &key);
        cp::DefinitionRef *lb = cp::LowerBoundDefinition(ourFirst, ourLast, &word, &key);
        cp::DefinitionRange ra{}, rb{};
        cp::DefinitionRange *pa = range(&ra, first, last, &word, key);
        cp::DefinitionRange *pb = cp::EqualRangeDefinitions(&rb, ourFirst, ourLast, &word, key);
        ++ctx.cases;
        auto at = [&](const void *p) { return static_cast<int>(static_cast<void *const *>(p) - first); };
        if (at(la) != at(lb) || pa != &ra || pb != &rb || at(ra.first) != at(rb.first) || at(ra.last) != at(rb.last))
            ctx.Fail("search '%s' over %zu: lower %d/%d, range %d..%d / %d..%d", Printable(name).c_str(), run.size(), at(la), at(lb),
                     at(ra.first), at(ra.last), at(rb.first), at(rb.last));
        if (!gLog.empty()) ctx.Fail("search '%s': the searches called the definitions", Printable(name).c_str());
        for (int k = 0; k < kPoolSize; ++k) {
            const void *entry = &gDefs[k];
            const uint32_t junk = g();
            const bool ba = before(&key, entry, junk), bb = key.DefinitionBefore(static_cast<const cp::DefinitionRef *>(entry), junk);
            const bool aa = after(&key, junk, entry), ab = key.BeforeDefinition(junk, static_cast<const cp::DefinitionRef *>(entry));
            ctx.cases += 2;
            if (ba != bb || aa != ab)
                ctx.Fail("compare '%s' with '%s': definition before %d/%d, key before %d/%d", Printable(name).c_str(),
                         Printable(kPool[k]).c_str(), ba, bb, aa, ab);
        }
    }
}

// ---- the held-definition vector ---------------------------------------------------------------------------------------

namespace {

// A C++ exception shaped like a cError for RunGuarded's reader (vtable word, text inline, size, capacity).
struct FakeThrow {
    uint32_t vtable;
    char text[16];
    uint32_t size, capacity;
};

int gReferences = 0, gThrowAt = -1;
uint32_t __fastcall ThrowingReference(void *self, void *)
{
    Record(self, 2);
    if (gReferences++ == gThrowAt) throw FakeThrow{0x5eed, "reference", 9, 15};
    return 0;
}

struct VectorWorld {
    alignas(8) uint8_t vec[12] = {};
    std::vector<void *> initial;
    size_t capacity = 0;

    void **&Begin() { return reinterpret_cast<void ***>(vec)[0]; }
    void **&End() { return reinterpret_cast<void ***>(vec)[1]; }
    void **&Cap() { return reinterpret_cast<void ***>(vec)[2]; }

    void Before()
    {
        void **block = static_cast<void **>(::operator new(capacity * sizeof(void *) + 4));
        for (size_t i = 0; i < initial.size(); ++i) block[i] = initial[i];
        Begin() = block;
        End() = block + initial.size();
        Cap() = block + capacity;
    }
    void After()
    {
        PushWord(static_cast<uint32_t>(End() - Begin()));
        PushWord(static_cast<uint32_t>(Cap() - Begin()));
        for (void **p = Begin(); p != End(); ++p) gExtra.push_back(PoolIndex(*p));
        ::operator delete(Begin());
        Begin() = End() = Cap() = nullptr;
    }
};

} // namespace

DIFF_TEST(DefinitionVector_Insert, 0xafd3c0)
{
    // Also 0xafc950 (reached when the vector is full). The value inserted is a held pointer outside the vector (enddef's
    // recording): a pool definition or, rarely, null. Compared: the references and releases in order, the returned
    // position (an index), the vector's size, capacity and elements.
    Allocators allocators;
    SetUpPool();
    auto orig = ctx.Original<void ***(__thiscall *)(void *, void ***, void **, void *const *)>(0xafd3c0);
    std::mt19937 g(0xafd3c0);
    static VectorWorld w;
    for (int i = 0; i < 1500; ++i) {
        w.initial.clear();
        const size_t n = g() % 7;
        for (size_t k = 0; k < n; ++k) w.initial.push_back(g() % 9 == 0 ? nullptr : &gDefs[g() % kPoolSize]);
        w.capacity = n + (g() % 2 ? 0 : 1 + g() % 3);
        if (w.capacity == 0 && g() % 2) w.capacity = 1;
        const size_t where = n ? g() % (n + 1) : 0;
        void *value = g() % 10 == 0 ? nullptr : &gDefs[g() % kPoolSize];
        void **out = nullptr;
        auto run = [&](bool original) {
            return RunGuarded([&] {
                void **at = w.Begin() + where;
                void ***r = original ? orig(w.vec, &out, at, &value)
                                     : reinterpret_cast<void ***>(reinterpret_cast<cp::DefinitionVector *>(w.vec)->Insert(
                                           reinterpret_cast<cp::DefinitionRef **>(&out), reinterpret_cast<cp::DefinitionRef *>(at),
                                           reinterpret_cast<const cp::DefinitionRef *>(&value)));
                PushWord(r == &out);
                PushWord(static_cast<uint32_t>(out - w.Begin()));
            });
        };
        char what[96];
        std::snprintf(what, sizeof(what), "Insert case %d (size %zu, capacity %zu, at %zu)", i, n, w.capacity, where);
        Differential(ctx, what, {{w.vec, sizeof(w.vec)}}, [&] { return run(true); }, [&] { return run(false); }, [] { w.After(); },
                     [] { w.Before(); });
    }
}

DIFF_TEST(DefinitionVector_EmplaceReallocate, 0xafc950)
{
    // Called on a full vector only (Insert's precondition): the new element's index, the references and releases.
    Allocators allocators;
    SetUpPool();
    auto orig = ctx.Original<void **(__thiscall *)(void *, void **, void *const *)>(0xafc950);
    std::mt19937 g(0xafc950);
    static VectorWorld w;
    for (int i = 0; i < 800; ++i) {
        w.initial.clear();
        const size_t n = g() % 9;
        for (size_t k = 0; k < n; ++k) w.initial.push_back(g() % 9 == 0 ? nullptr : &gDefs[g() % kPoolSize]);
        w.capacity = n;
        const size_t where = n ? g() % (n + 1) : 0;
        void *value = g() % 10 == 0 ? nullptr : &gDefs[g() % kPoolSize];
        auto run = [&](bool original) {
            return RunGuarded([&] {
                void **at = w.Begin() + where;
                void **r = original ? orig(w.vec, at, &value)
                                    : reinterpret_cast<void **>(reinterpret_cast<cp::DefinitionVector *>(w.vec)->EmplaceReallocate(
                                          reinterpret_cast<cp::DefinitionRef *>(at), reinterpret_cast<const cp::DefinitionRef *>(&value)));
                return static_cast<uint32_t>(r - w.Begin());
            });
        };
        char what[96];
        std::snprintf(what, sizeof(what), "EmplaceReallocate case %d (size %zu, at %zu)", i, n, where);
        Differential(ctx, what, {{w.vec, sizeof(w.vec)}}, [&] { return run(true); }, [&] { return run(false); }, [] { w.After(); },
                     [] { w.Before(); });
    }
}

DIFF_TEST(DefinitionVector_CopyDefinitions, 0x29a2e0)
{
    // The uninitialized copy: every copy referenced (null ones not), the end returned; a reference that throws (the
    // fake's k-th call) releases the copies made so far and the exception goes on. Compared: the calls in order, the
    // returned index, what was thrown, the destination words.
    SetUpPool();
    gItemVt.slots[2] = reinterpret_cast<void *>(&ThrowingReference);
    auto orig = ctx.Original<void **(__cdecl *)(void *const *, void *const *, void **, void *)>(0x29a2e0);
    std::mt19937 g(0x29a2e0);
    for (int i = 0; i < 1500; ++i) {
        void *source[8];
        const size_t n = g() % 9;
        for (size_t k = 0; k < n; ++k) source[k] = g() % 7 == 0 ? nullptr : &gDefs[g() % kPoolSize];
        alignas(8) void *dest[8];
        for (auto &d : dest) d = reinterpret_cast<void *>(0xcdcdcd00u + static_cast<uint32_t>(g() % 0x100));
        const int throwAt = g() % 3 == 0 ? static_cast<int>(g() % (n + 1)) : -1;
        uint8_t allocator = 0x5a;
        auto run = [&](bool original) {
            gReferences = 0;
            gThrowAt = throwAt;
            return RunGuarded([&] {
                void **r = original ? orig(source, source + n, dest, &allocator)
                                    : reinterpret_cast<void **>(cp::CopyDefinitions(
                                          reinterpret_cast<const cp::DefinitionRef *>(source), reinterpret_cast<const cp::DefinitionRef *>(source + n),
                                          reinterpret_cast<cp::DefinitionRef *>(dest), &allocator));
                return static_cast<uint32_t>(r - dest);
            });
        };
        char what[96];
        std::snprintf(what, sizeof(what), "CopyDefinitions case %d (%zu, throw at %d)", i, n, throwAt);
        Differential(ctx, what, {{dest, sizeof(dest)}}, [&] { return run(true); }, [&] { return run(false); });
    }
    gThrowAt = -1;
    gItemVt.Set<2>(0);
}

// ---- the destructors --------------------------------------------------------------------------------------------------

namespace {

const char *const kTexts[] = {"", "x", "short name", "a name longer than fifteen", "path/to/some/file/far/away.txt"};
const char *const kParameterTexts[] = {"", "a", "a b c", "first second third fourth fifth", "(x y) -s z"};

// A definition as define builds it, with every member in use (sizes from the seed).
cp::cMacroDefinition *BuildDefinition(uint32_t seed)
{
    std::mt19937 g(seed);
    auto *d = new (::operator new(cp::cMacroDefinition::kSize)) cp::cMacroDefinition();
    d->mRefCount = static_cast<int32_t>(g() % 3);
    d->mName = kTexts[g() % 5];
    d->mParameters.Parse(kParameterTexts[g() % 5], 0, 1);
    const size_t body = g() % 3 == 0 ? 0 : g() % 200;
    for (size_t k = 0; k < body; ++k) d->mBody.push_back(static_cast<char>('a' + k % 26));
    const size_t lines = g() % 3 == 0 ? 0 : g() % 20;
    for (size_t k = 0; k < lines; ++k) d->mLines.push_back({static_cast<int32_t>(k), -1000});
    d->mLevel = static_cast<int32_t>(g() % 4);
    d->mFile = kTexts[g() % 5];
    d->mFirstLine = static_cast<int32_t>(g() % 100);
    return d;
}

struct Freed {
    std::vector<size_t> sizes;
    bool self = false;
    uint32_t words[2] = {};
    uint32_t ret = 0;
};

} // namespace

DIFF_TEST(cMacroDefinition_Destruct, 0xafe6c0)
{
    // Also the deleting destructor 0xaff0d0 (flags 0 and 1). Compared: the blocks freed (their sizes, sorted, and
    // whether the object itself was), the returned pointer, and the first two words afterwards (the cRZRefCount vtable
    // the destructor leaves, the count). Not compared: the rest of a destroyed object. The original's member destructors
    // (0xafe6c0: the strings' tidy at +0x6c and +8, the vectors' at +0x5c and +0x50, cArguments' 0xb0bdd0 at +0x20)
    // leave their own bookkeeping in the dead members (the strings' empty-buffer state, the image's cArguments vtable),
    // which nothing reads: the object is freed or its memory reused by its owner.
    Allocators allocators;
    auto destruct = ctx.Original<void(__thiscall *)(void *)>(0xafe6c0);
    auto deleting = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xaff0d0);
    for (uint32_t i = 0; i < 400; ++i) {
        const int mode = static_cast<int>(i % 3); // 0: Destruct, 1: deleting with flags 0, 2: with flags 1 (and junk bits)
        const uint32_t flags = mode == 2 ? (1u | (i & 0x10u ? 0xfff0u : 0u)) : (i & 0x20u ? 0x2u : 0u);
        Freed sides[2];
        for (int side = 0; side < 2; ++side) {
            cp::cMacroDefinition *d = BuildDefinition(0xafe6c0 + i);
            difftest::FreeRecorder freed;
            if (mode == 0) {
                if (side == 0) destruct(d);
                else d->Destruct();
            } else {
                void *r = side == 0 ? deleting(d, flags) : d->DeletingDestructor(flags);
                sides[side].ret = r == d;
            }
            freed.Stop();
            Freed &f = sides[side];
            for (size_t k = 0; k < freed.Count(); ++k) {
                if (freed.Pointer(k) == d) f.self = true;
                else f.sizes.push_back(freed.Size(k));
            }
            std::sort(f.sizes.begin(), f.sizes.end());
            if (!f.self) {
                f.words[0] = reinterpret_cast<uint32_t *>(d)[0];
                f.words[1] = reinterpret_cast<uint32_t *>(d)[1];
                ::operator delete(d);
            }
        }
        ++ctx.cases;
        if (sides[0].sizes != sides[1].sizes || sides[0].self != sides[1].self || sides[0].ret != sides[1].ret ||
            sides[0].words[0] != sides[1].words[0] || sides[0].words[1] != sides[1].words[1])
            ctx.Fail("destructor case %u (mode %d, flags %x): freed %zu/%zu blocks, self %d/%d, ret %u/%u, vtable %08x/%08x, count %u/%u", i,
                     mode, flags, sides[0].sizes.size(), sides[1].sizes.size(), sides[0].self, sides[1].self, sides[0].ret, sides[1].ret,
                     sides[0].words[0], sides[1].words[0], sides[0].words[1], sides[1].words[1]);
    }
}

// ---- parameter substitution -------------------------------------------------------------------------------------------

namespace {

// The innermost pending call's definition parameters and the call's arguments: slot 7 (Count) and slot 4 (Argument)
// of each, logged and answered from the case's lists.
constexpr int kNamesId = 30, kValuesId = 31;
std::vector<std::string> gNames, gValues;
int32_t gNameCount = 0;

uint32_t __fastcall NamesCount(void *self, void *)
{
    difftest::NotCounted bookkeeping;
    gLog.push_back({kNamesId, 7, reinterpret_cast<uint32_t>(self), {0, 0, 0, 0}});
    return static_cast<uint32_t>(gNameCount);
}
const char *__fastcall NamesArgument(void *self, void *, uint32_t index)
{
    difftest::NotCounted bookkeeping;
    gLog.push_back({kNamesId, 4, reinterpret_cast<uint32_t>(self), {index, 0, 0, 0}});
    return index < gNames.size() ? gNames[index].c_str() : "?";
}
const char *__fastcall ValuesArgument(void *self, void *, uint32_t index)
{
    difftest::NotCounted bookkeeping;
    gLog.push_back({kValuesId, 4, reinterpret_cast<uint32_t>(self), {index, 0, 0, 0}});
    return index < gValues.size() ? gValues[index].c_str() : "?";
}
uint32_t __fastcall Unexpected(void *self, void *)
{
    difftest::NotCounted bookkeeping;
    gLog.push_back({99, 99, reinterpret_cast<uint32_t>(self), {0, 0, 0, 0}});
    return 0;
}

void *gNamesVt[17], *gValuesVt[17];

const char *const kParameterNames[] = {"p", "pp1", "_q", "Q", "long_parameter_name_x", "a9", "x"};
const char *const kValues[] = {"", "1", "hello world", "a value longer than sixteen bytes", "&p", "(x)"};
const char *const kPieces[] = {"abc ", "x=", "&p", "&{p}", "&&", "&", "&{", "&{p", "&9", "&_q", "&pp1", "(", "&P", "&unknown",
                               "&{x}", "&x", "&a9b", "&{a9}", "}", " ", "&{_q}z", "&{}", "&-", "&{ p}", "&long_parameter_name_x",
                               "&\xe9", "&&&", "&{&}"};

} // namespace

DIFF_TEST(cMacroParser_ExpandParameters, 0xb06920)
{
    // The parser is a block of random bytes with the pending calls (+0x11c: first, last, capacity) over a static array of
    // 0x34-byte frames: each a definition pointer (a block whose +0x20 is the parameter list) and the call's arguments at
    // +4. Only the innermost frame is read. The text is a std::string rebuilt before each side and read afterwards (size,
    // capacity, bytes). Compared: the calls (Count / Argument on the parameter list, Argument on the call's
    // arguments) in order, what was thrown, the text, the parser's bytes.
    Allocators allocators;
    for (auto &s : gNamesVt) s = reinterpret_cast<void *>(&Unexpected);
    for (auto &s : gValuesVt) s = reinterpret_cast<void *>(&Unexpected);
    gNamesVt[7] = reinterpret_cast<void *>(&NamesCount);
    gNamesVt[4] = reinterpret_cast<void *>(&NamesArgument);
    gValuesVt[4] = reinterpret_cast<void *>(&ValuesArgument);
    auto orig = ctx.Original<void(__thiscall *)(void *, std::string *)>(0xb06920);
    std::mt19937 g(0xb06920);
    alignas(8) static uint8_t parser[0x200];
    alignas(8) static uint8_t frames[3][0x34];
    alignas(8) static uint8_t definitions[3][0x40];
    alignas(8) static uint8_t textBox[sizeof(std::string)];
    static std::string input;
    for (int i = 0; i < 6000; ++i) {
        for (auto &b : parser) b = static_cast<uint8_t>(g());
        for (int f = 0; f < 3; ++f) {
            for (auto &b : frames[f]) b = static_cast<uint8_t>(g());
            for (auto &b : definitions[f]) b = static_cast<uint8_t>(g());
            *reinterpret_cast<void **>(frames[f]) = definitions[f];
            *reinterpret_cast<void **>(definitions[f] + 0x20) = gNamesVt;
            *reinterpret_cast<void **>(frames[f] + 4) = gValuesVt;
        }
        const size_t depth = g() % 8 == 0 ? 0 : 1 + g() % 3;
        uint8_t **calls = reinterpret_cast<uint8_t **>(parser + 0x11c);
        calls[0] = frames[0];
        calls[1] = frames[0] + 0x34 * depth;
        calls[2] = frames[0] + 0x34 * 3;
        gNames.clear();
        gValues.clear();
        const int names = static_cast<int>(g() % 5);
        for (int k = 0; k < names; ++k) {
            gNames.push_back(kParameterNames[g() % (sizeof(kParameterNames) / sizeof(kParameterNames[0]))]);
            gValues.push_back(kValues[g() % (sizeof(kValues) / sizeof(kValues[0]))]);
        }
        gNameCount = names;
        input.clear();
        const int pieces = static_cast<int>(g() % 6);
        for (int k = 0; k < pieces; ++k) input += kPieces[g() % (sizeof(kPieces) / sizeof(kPieces[0]))];
        if (g() % 8 == 0) input = "no ampersand here, a longer line of text";
        auto before = [] {
            std::string *s = new (textBox) std::string(input);
            if (input.size() % 3 == 0) s->reserve(s->size() + 20);
        };
        auto after = [] {
            std::string *s = reinterpret_cast<std::string *>(textBox);
            PushWord(static_cast<uint32_t>(s->capacity()));
            PushText(s->data(), s->size());
            s->~basic_string();
            std::memset(textBox, 0, sizeof(textBox));
        };
        auto run = [&](bool original) {
            return RunGuarded([&] {
                std::string *s = reinterpret_cast<std::string *>(textBox);
                if (original) orig(parser, s);
                else cp::cMacroParser::Of(parser)->ExpandParameters(s);
            });
        };
        char what[200];
        std::snprintf(what, sizeof(what), "ExpandParameters case %d (depth %zu, %d names) '%s'", i, depth, names, Printable(input).c_str());
        Differential(ctx, what, {{parser, sizeof(parser)}, {frames, sizeof(frames)}, {definitions, sizeof(definitions)}},
                     [&] { return run(true); }, [&] { return run(false); }, after, before);
    }
}
