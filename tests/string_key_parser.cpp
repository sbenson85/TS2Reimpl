// Differential tests for the string key parser (0xae6e60) and the functions it uses: the CRC tables
// and hashes, the strtoul wrapper, the bsearch and its comparator, the cached service object, and
// the three cRZString helpers (0x1ad167, 0x1acd5b, 0x1acd83) that only the parser uses so far.
//
// Each case runs the original and ours on the same inputs (and the same game globals, restored in
// between) and compares return values, the objects, the game globals and the calls the fakes saw.
//
// Not compared: bsearch's invalid-parameter path (base null with num > 0, width 0, compare null):
// the original calls the CRT invalid-parameter handler, which the harness can't run. The parser
// never passes those arguments.
#include "difftest.h"
#include "rz/StringKeyParser.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kCrc24Table = 0x10f51d0;
constexpr uint32_t kCrc32Table = 0x10f55d0;
constexpr uint32_t kCrcReady = 0x10f59d0;
constexpr uint32_t kServiceCache = 0x10f8f2c;
constexpr uint32_t kServiceGlobal = 0x10c9378;
constexpr uint32_t kEntryTable = 0x10718c8; // 14 entries of {uint32 hash, const char *text}

struct Gen {
    std::mt19937 rng;
    explicit Gen(uint32_t seed) : rng(seed) {}
    uint32_t Below(uint32_t n) { return n ? rng() % n : 0; }
    bool Coin() { return rng() % 2 != 0; }
    std::string Text(const char *alphabet, uint32_t alphabetLength, uint32_t maxLen)
    {
        uint32_t len = Below(maxLen + 1);
        if (rng() % 5 == 0) len = 14 + Below(6); // around the 15-character inline buffer
        std::string s;
        for (uint32_t i = 0; i < len; ++i) s += alphabet[Below(alphabetLength)];
        return s;
    }
    // Mixed-case letters, digits and high bytes (the signed-char and tolower edges).
    std::string Mixed(uint32_t maxLen)
    {
        static const char alphabet[] = {'a', 'A', 'b', 'B', 'z', 'Z', '0', '9', '_', '\x80', '\xc3', '\xff', '@', '['};
        return Text(alphabet, sizeof(alphabet), maxLen);
    }
};

std::string Show(const std::string &s)
{
    std::string out;
    for (char c : s) out += c ? c : '~';
    return out;
}

// Flips the case of some letters, so a suffix matches only case-insensitively.
std::string FlipCase(Gen &g, std::string s)
{
    for (auto &c : s) {
        if (g.Below(3) == 0 && std::isalpha(static_cast<unsigned char>(c))) c = static_cast<char>(c ^ 0x20);
    }
    return s;
}

// A text that is a (possibly case-flipped) suffix of `self`, or an unrelated one.
std::string SuffixCase(Gen &g, const std::string &self)
{
    if (g.Below(5) == 0) return g.Mixed(12);
    uint32_t length = g.Below(static_cast<uint32_t>(self.size()) + 1);
    return FlipCase(g, self.substr(self.size() - length));
}

cRZString *NewString(const difftest::Context &ctx, const std::string &text)
{
    auto *r = new cRZString{};
    r->mVTable = ctx.image.At(cRZString::kVTable);
    r->mString = text;
    r->mRefCount = 1;
    return r;
}

// The CRC tables and their flag, as game memory.
struct TableState {
    uint32_t t24[256], t32[256];
    uint8_t ready;
};

TableState Capture(const difftest::Context &ctx)
{
    TableState s{};
    std::memcpy(s.t24, ctx.image.At<uint32_t>(kCrc24Table), sizeof(s.t24));
    std::memcpy(s.t32, ctx.image.At<uint32_t>(kCrc32Table), sizeof(s.t32));
    s.ready = *ctx.image.At<uint8_t>(kCrcReady);
    return s;
}

void Restore(const difftest::Context &ctx, const TableState &s)
{
    std::memcpy(ctx.image.At<uint32_t>(kCrc24Table), s.t24, sizeof(s.t24));
    std::memcpy(ctx.image.At<uint32_t>(kCrc32Table), s.t32, sizeof(s.t32));
    *ctx.image.At<uint8_t>(kCrcReady) = s.ready;
}

bool SameState(const TableState &a, const TableState &b)
{
    return a.ready == b.ready && std::memcmp(a.t24, b.t24, sizeof(a.t24)) == 0 && std::memcmp(a.t32, b.t32, sizeof(a.t32)) == 0;
}

// A start state for the table tests: half the time the tables are ready, sometimes with garbage in them.
TableState StartState(const difftest::Context &ctx, Gen &g)
{
    TableState s = Capture(ctx);
    uint32_t mode = g.Below(4);
    if (mode == 0) {
        s.ready = 0; // the function must build the tables itself
        for (auto &v : s.t24) v = g.rng();
        for (auto &v : s.t32) v = g.rng();
    } else if (mode == 1) {
        s.ready = 1;
        for (auto &v : s.t24) v = g.rng();
    }
    return s;
}

// ---- fakes for the service object and the parser's cached object --------------------------------

struct Log {
    int slot;
    const void *self;
    uint32_t a, b;
    const void *wrote;
    std::string text;
    bool operator==(const Log &o) const
    {
        return slot == o.slot && self == o.self && a == o.a && b == o.b && wrote == o.wrote && text == o.text;
    }
};

std::vector<Log> gLog;
void *gServiceStore = nullptr; // what the fake service's slot 5 stores through its output pointer
uint32_t gQueryResult = 0;     // what it returns (only al counts)
uint32_t gLookupResult = 0;    // what the cached object's slot 0 returns

// Fakes are __thiscall: `this` in ecx, the stack arguments popped by the callee. A __fastcall function
// with a dummy edx takes the same registers and stack layout.
uint32_t __fastcall QueryFake(void *self, void * /*edx*/, uint32_t a, uint32_t b, void **out)
{
    gLog.push_back({5, self, a, b, gServiceStore, {}});
    *out = gServiceStore;
    return gQueryResult;
}

uint32_t __fastcall LookupFake(void *self, void * /*edx*/, const char *text)
{
    gLog.push_back({0, self, 0, 0, nullptr, text ? std::string(text) : std::string("<null>")});
    return gLookupResult;
}

uint32_t __fastcall AddRefFake(void *self, void * /*edx*/)
{
    gLog.push_back({1, self, 0, 0, nullptr, {}});
    return 2;
}

void *gServiceVTable[8];
void *gObjectVTable[8];
struct FakeObject {
    void *vtable;
    uint32_t pad;
};
FakeObject gService{}, gObject{};

void SetupFakes()
{
    std::memset(gServiceVTable, 0, sizeof(gServiceVTable));
    std::memset(gObjectVTable, 0, sizeof(gObjectVTable));
    gServiceVTable[5] = reinterpret_cast<void *>(&QueryFake);
    gObjectVTable[0] = reinterpret_cast<void *>(&LookupFake);
    gObjectVTable[1] = reinterpret_cast<void *>(&AddRefFake);
    gService.vtable = gServiceVTable;
    gObject.vtable = gObjectVTable;
}

// Installs the service and sets the cache global; the service's query stores gServiceStore.
void SetServiceState(difftest::Context &ctx, void *cache)
{
    *ctx.image.At<void *>(kServiceGlobal) = &gService;
    *ctx.image.At<void *>(kServiceCache) = cache;
}

// ---- cRZString helper fixtures -----------------------------------------------------------------

template <typename Fn> Fn Orig(const difftest::Context &ctx, uint32_t rva) { return ctx.Original<Fn>(rva); }

} // namespace

DIFF_TEST(StringKeyParser_CrcTables, 0x9cbb60)
{
    auto init = Orig<void *(__thiscall *)(void *)>(ctx, 0x9cbb60);
    Gen g(1);
    for (int i = 0; i < 300; ++i) {
        TableState start = StartState(ctx, g);
        void *self = reinterpret_cast<void *>(static_cast<uintptr_t>(0x1000 + i * 8));
        Restore(ctx, start);
        void *ret1 = init(self);
        TableState a = Capture(ctx);
        Restore(ctx, start);
        auto *tables = reinterpret_cast<StringKeyParser::CrcTables *>(self);
        void *ret2 = tables->Initialize();
        TableState b = Capture(ctx);
        ++ctx.cases;
        if (ret1 != self || ret2 != self) ctx.Fail("Initialize returned %p / %p, this %p", ret1, ret2, self);
        if (!SameState(a, b)) ctx.Fail("tables differ (start ready %u)", start.ready);
    }
    // Leave the tables built, as the game would have them.
    Restore(ctx, Capture(ctx));
    *ctx.image.At<uint8_t>(kCrcReady) = 0;
    auto done = Orig<void *(__thiscall *)(void *)>(ctx, 0x9cbb60);
    done(nullptr);
}

DIFF_TEST(StringKeyParser_Crc24, 0x9cbbdd)
{
    auto orig = Orig<uint32_t(__cdecl *)(const char *, uint32_t)>(ctx, 0x9cbbdd);
    Gen g(2);
    for (int i = 0; i < 20000; ++i) {
        std::string text = g.Mixed(40);
        // The flag is read as a byte: sometimes pass a value whose low byte is 0 or 1 with high bits set.
        uint32_t lower = g.Coin() ? 1u : 0u;
        if (g.Below(8) == 0) lower |= g.rng() & 0xffffff00u;
        TableState start = StartState(ctx, g);
        Restore(ctx, start);
        uint32_t a = orig(text.c_str(), lower);
        TableState sa = Capture(ctx);
        Restore(ctx, start);
        uint32_t b = StringKeyParser::Crc24(text.c_str(), static_cast<uint8_t>(lower));
        TableState sb = Capture(ctx);
        ctx.cases += 2;
        if (a != b) ctx.Fail("Crc24('%s', %u): original %08x, ours %08x", Show(text).c_str(), lower, a, b);
        if (!SameState(sa, sb)) ctx.Fail("Crc24('%s'): table state differs", Show(text).c_str());
    }
}

DIFF_TEST(StringKeyParser_Crc32, 0x9cbc3e)
{
    auto orig = Orig<uint32_t(__cdecl *)(const char *, uint32_t)>(ctx, 0x9cbc3e);
    Gen g(3);
    for (int i = 0; i < 20000; ++i) {
        std::string text = g.Mixed(40);
        uint32_t lower = g.Coin() ? 1u : 0u;
        if (g.Below(8) == 0) lower |= g.rng() & 0xffffff00u;
        TableState start = StartState(ctx, g);
        Restore(ctx, start);
        uint32_t a = orig(text.c_str(), lower);
        TableState sa = Capture(ctx);
        Restore(ctx, start);
        uint32_t b = StringKeyParser::Crc32(text.c_str(), static_cast<uint8_t>(lower));
        TableState sb = Capture(ctx);
        ctx.cases += 2;
        if (a != b) ctx.Fail("Crc32('%s', %u): original %08x, ours %08x", Show(text).c_str(), lower, a, b);
        if (!SameState(sa, sb)) ctx.Fail("Crc32('%s'): table state differs", Show(text).c_str());
    }
}

DIFF_TEST(StringKeyParser_ParseNumber, 0x1b10f3)
{
    auto orig = Orig<uint32_t(__cdecl *)(const char *)>(ctx, 0x1b10f3);
    Gen g(4);
    static const char digits[] = "0123456789abcdefABCDEFxXgz-+ ";
    const char *prefixes[] = {"", "0", "0x", "0X", "00x", "x", "0xx", "-0x", " 0x"};
    for (int i = 0; i < 20000; ++i) {
        std::string text = prefixes[g.Below(9)] + g.Text(digits, sizeof(digits) - 1, 12);
        if (g.Below(10) == 0) text += "!tail";
        if (g.Below(20) == 0) text = "4294967296";
        uint32_t a = orig(text.c_str());
        uint32_t b = StringKeyParser::ParseNumber(text.c_str());
        ++ctx.cases;
        if (a != b) ctx.Fail("ParseNumber('%s'): original %u, ours %u", Show(text).c_str(), a, b);
    }
}

namespace {

// Two comparators: one returns the sign only, the other the wrapped 32-bit difference of unsigned values.
std::vector<uintptr_t> gProbes;
int32_t __cdecl CompareSign(const void *key, const void *entry)
{
    gProbes.push_back(reinterpret_cast<uintptr_t>(entry));
    uint32_t a = *static_cast<const uint32_t *>(key), b = *static_cast<const uint32_t *>(entry);
    return a < b ? -1 : (a > b ? 1 : 0);
}
int32_t __cdecl CompareWrap(const void *key, const void *entry)
{
    gProbes.push_back(reinterpret_cast<uintptr_t>(entry));
    return static_cast<int32_t>(*static_cast<const uint32_t *>(key) - *static_cast<const uint32_t *>(entry));
}

} // namespace

// 0xc4ad97 is the CRT's bsearch: this checks ours is equivalent (results and probe order), which is
// the evidence for its db/crt-equivalents.jsonl entry.
DIFF_TEST(StringKeyParser_Bsearch, 0xc4ad97)
{
    auto orig = Orig<void *(__cdecl *)(const void *, const void *, uint32_t, uint32_t, int32_t(__cdecl *)(const void *, const void *))>(ctx, 0xc4ad97);
    Gen g(5);
    for (int i = 0; i < 20000; ++i) {
        uint32_t num = g.Below(41);
        uint32_t width = 4 + 4 * g.Below(3);
        std::vector<uint8_t> buffer(static_cast<size_t>(num) * width + 16, 0);
        std::vector<uint32_t> keys(num);
        uint32_t spread = g.Coin() ? 6 : 0x7fffffff;
        for (auto &k : keys) k = g.Below(spread) + (g.Coin() ? 0 : 0x80000000u * g.Below(2));
        std::sort(keys.begin(), keys.end());
        for (uint32_t j = 0; j < num; ++j) std::memcpy(buffer.data() + j * width, &keys[j], 4);
        // The rest of each entry is filler the comparator never reads.
        for (uint32_t j = 0; j < num; ++j) std::memset(buffer.data() + j * width + 4, 0xa5, width - 4);

        uint32_t key = (num && g.Coin()) ? keys[g.Below(num)] : g.rng();
        auto compare = g.Coin() ? &CompareSign : &CompareWrap;
        const void *base = (num == 0 && g.Coin()) ? nullptr : buffer.data();

        gProbes.clear();
        void *a = orig(&key, base, num, width, compare);
        std::vector<uintptr_t> probesA = gProbes;
        gProbes.clear();
        void *b = ::bsearch(&key, base, num, width, compare);
        ++ctx.cases;
        if (a != b) ctx.Fail("bsearch num %u width %u key %08x: original %p, ours %p", num, width, key, a, b);
        if (probesA != gProbes) ctx.Fail("bsearch num %u width %u key %08x: probe sequences differ (%zu vs %zu)", num, width, key, probesA.size(), gProbes.size());
    }
}

DIFF_TEST(StringKeyParser_EnsureServiceObject, 0xae7300)
{
    SetupFakes();
    auto orig = Orig<void(__cdecl *)()>(ctx, 0xae7300);
    Gen g(6);
    static const uint32_t results[] = {0, 1, 0x100, 0xff, 0x101};
    for (int i = 0; i < 2000; ++i) {
        bool cached = g.Below(4) == 0;
        gServiceStore = g.Coin() ? &gObject : nullptr;
        gQueryResult = results[g.Below(5)];
        if (gQueryResult & 0xff) gServiceStore = &gObject; // a successful query always hands out an object

        gLog.clear();
        SetServiceState(ctx, cached ? &gObject : nullptr);
        orig();
        void *cacheA = *ctx.image.At<void *>(kServiceCache);
        std::vector<Log> logA = gLog;

        gLog.clear();
        SetServiceState(ctx, cached ? &gObject : nullptr);
        StringKeyParser::EnsureServiceObject();
        void *cacheB = *ctx.image.At<void *>(kServiceCache);
        ++ctx.cases;
        if (cacheA != cacheB) ctx.Fail("EnsureServiceObject: cache %p (original) vs %p (ours), cached %d result %x", cacheA, cacheB, cached, gQueryResult);
        if (logA.size() != gLog.size() || !std::equal(logA.begin(), logA.end(), gLog.begin()))
            ctx.Fail("EnsureServiceObject: calls differ (cached %d, result %x): original %zu, ours %zu", cached, gQueryResult, logA.size(), gLog.size());
    }
    SetServiceState(ctx, nullptr);
}

DIFF_TEST(StringKeyParser_CompareEntry, 0xae7460)
{
    auto orig = Orig<int32_t(__cdecl *)(const void *, const void *)>(ctx, 0xae7460);
    Gen g(7);
    static const char *texts[] = {"", "abc", "ABC", "AbC", "abd", "zz", "Z", "ab\x80", "AB\x80", "abc\xff"};
    for (int i = 0; i < 20000; ++i) {
        // Entries are {hash, text}; key and table entries share the layout.
        struct Entry {
            uint32_t hash;
            const char *text;
        } key{g.Coin() ? 7u : g.rng(), texts[g.Below(10)]}, entry{g.Coin() ? 7u : g.rng(), texts[g.Below(10)]};
        if (g.Below(4) == 0) entry.hash = key.hash;
        int32_t a = orig(&key, &entry);
        int32_t b = StringKeyParser::CompareEntry(&key, &entry);
        ++ctx.cases;
        if (a != b) ctx.Fail("CompareEntry(%08x '%s', %08x '%s'): original %d, ours %d", key.hash, key.text, entry.hash, entry.text, a, b);
    }
}

DIFF_TEST(cRZString_LowerCase, 0x1ad167)
{
    auto orig = Orig<void(__thiscall *)(void *)>(ctx, 0x1ad167);
    Gen g(8);
    for (int i = 0; i < 20000; ++i) {
        std::string text = g.Mixed(40);
        cRZString *a = NewString(ctx, text), *b = NewString(ctx, text);
        orig(a);
        reinterpret_cast<cRZStringHelpers *>(b)->LowerCase();
        ++ctx.cases;
        if (a->mString != b->mString || a->mVTable != b->mVTable || a->mRefCount != b->mRefCount)
            ctx.Fail("LowerCase('%s'): original '%s', ours '%s'", Show(text).c_str(), Show(a->mString).c_str(), Show(b->mString).c_str());
        delete a;
        delete b;
    }
}

DIFF_TEST(cRZString_EndsWithObject, 0x1acd5b)
{
    auto orig = Orig<uint8_t(__thiscall *)(void *, void *, uint32_t)>(ctx, 0x1acd5b);
    Gen g(9);
    for (int i = 0; i < 20000; ++i) {
        std::string self = g.Mixed(40);
        std::string other = SuffixCase(g, self);
        uint32_t cs = g.Coin() ? 1u : 0u;
        if (g.Below(8) == 0) cs |= g.rng() & 0xffffff00u;
        cRZString *a = NewString(ctx, self), *b = NewString(ctx, self);
        cRZString *o = NewString(ctx, other);
        uint8_t ra = orig(a, o, cs);
        uint8_t rb = reinterpret_cast<cRZStringHelpers *>(b)->EndsWithObject(o, static_cast<uint8_t>(cs));
        ++ctx.cases;
        if (ra != rb) ctx.Fail("EndsWithObject('%s', '%s', %u): original %u, ours %u", Show(self).c_str(), Show(other).c_str(), cs, ra, rb);
        if (a->mString != b->mString || o->mString != other) ctx.Fail("EndsWithObject changed a string");
        delete a;
        delete b;
        delete o;
    }
}

DIFF_TEST(cRZString_EndsWith, 0x1acd83)
{
    auto orig = Orig<uint8_t(__thiscall *)(void *, const char *, uint32_t, uint32_t)>(ctx, 0x1acd83);
    Gen g(10);
    for (int i = 0; i < 20000; ++i) {
        std::string self = g.Mixed(40);
        std::string text = SuffixCase(g, self);
        if (g.Below(5) == 0) text.push_back(static_cast<char>(g.Below(2) ? 'a' : 'A'));
        // Usually the whole text is compared; sometimes a shorter or longer length (the text is padded below).
        uint32_t length = g.Coin() ? static_cast<uint32_t>(text.size()) : g.Below(static_cast<uint32_t>(text.size()) + 4);
        if (g.Below(10) == 0) length = g.Below(static_cast<uint32_t>(self.size()) + 4);
        uint32_t cs = g.Coin() ? 1u : 0u;
        if (g.Below(8) == 0) cs |= g.rng() & 0xffffff00u;

        cRZString *a = NewString(ctx, self), *b = NewString(ctx, self);
        // Only the first `length` characters of text are read, so the buffer must be at least that long.
        std::string padded = text;
        padded.resize(length > padded.size() ? length : padded.size(), 'x');
        uint8_t ra = orig(a, padded.c_str(), length, cs);
        uint8_t rb = reinterpret_cast<cRZStringHelpers *>(b)->EndsWith(padded.c_str(), length, static_cast<uint8_t>(cs));
        ++ctx.cases;
        if (ra != rb) ctx.Fail("EndsWith('%s', '%s', %u, %u): original %u, ours %u", Show(self).c_str(), Show(padded).c_str(), length, cs, ra, rb);
        if (a->mString != b->mString) ctx.Fail("EndsWith changed the string");
        delete a;
        delete b;
    }
}

namespace {

// The keyword literals: the test reads them from the game image (their text is not in this repo).
const uint32_t kLiteralRvas[] = {0xe7ce3c, 0xdbde70, 0xe7ce44, 0xdbd570, 0xe71dc4, 0xe696b0,
                                 0xe730f0, 0xdb0810, 0xdb0818, 0xe7ce4c, 0xe7ce54, 0xe76390};

std::string ParseKeyCase(Gen &g, const difftest::Context &ctx)
{
    static const char alphabet[] = "ab!*#xX01_Zz";
    // Often the text is the game's own table text (in any case): its CRC then matches the table entry.
    if (g.Below(4) == 0) {
        uint32_t index = g.Below(14);
        const char *entry = *ctx.image.At<const char *>(kEntryTable + index * 8 + 4);
        return std::string(1, static_cast<char>('a' + g.Below(20))) + "!" + FlipCase(g, entry);
    }
    uint32_t mode = g.Below(6);
    std::string key;
    if (mode == 0) key = std::string("*!") + g.Text(alphabet, sizeof(alphabet) - 1, 18);
    else if (mode == 1) key = std::string(g.Coin() ? "##" : "#") + g.Text(alphabet, sizeof(alphabet) - 1, 8) + "!" + g.Text(alphabet, sizeof(alphabet) - 1, 18);
    else if (mode == 2) key = g.Text(alphabet, sizeof(alphabet) - 1, 6) + "!" + g.Text(alphabet, sizeof(alphabet) - 1, 18);
    else if (mode == 3) key = g.Text(alphabet, sizeof(alphabet) - 1, 20);
    else key = g.Text(alphabet, sizeof(alphabet) - 1, 6) + "!" + g.Text(alphabet, sizeof(alphabet) - 1, 6);
    // Often end the text with one of the keyword literals (in either case), to reach the table.
    if (g.Below(3) == 0) {
        const char *lit = ctx.image.At<char>(kLiteralRvas[g.Below(12)]);
        std::string suffix(lit, 5);
        if (g.Coin()) for (auto &c : suffix) c = static_cast<char>(g.Coin() ? toupper(c) : c);
        key += suffix;
    }
    return key;
}

} // namespace

DIFF_TEST(StringKeyParser_ParseKey, 0xae6e60)
{
    SetupFakes();
    auto orig = Orig<uint8_t(__cdecl *)(void *, const char *, uint32_t, const char *)>(ctx, 0xae6e60);
    Gen g(11);
    static const uint32_t results[] = {0, 1, 0x100, 0xff};
    for (int i = 0; i < 20000; ++i) {
        std::string key = ParseKeyCase(g, ctx);
        std::string ext = g.Below(6) == 0 ? std::string() : g.Text("abcAB!", 6, 8);
        const char *extArg = g.Below(8) == 0 ? nullptr : ext.c_str();
        uint32_t preset = g.Below(3) == 0 ? (g.rng() | 1) : 0;
        bool cached = g.Below(4) == 0;
        // The service sometimes hands out nothing (the parser then hashes with the CRC); a query that
        // hands out nothing reports failure, since a successful one takes a reference on the object.
        bool hands = g.Below(3) != 0;
        gServiceStore = hands ? &gObject : nullptr;
        gQueryResult = hands ? results[g.Below(4)] : results[g.Below(2) * 2];
        gLookupResult = g.rng();

        uint32_t outA[4], outB[4];
        std::memset(outA, 0xcc, sizeof(outA));
        std::memset(outB, 0xcc, sizeof(outB));

        gLog.clear();
        SetServiceState(ctx, cached ? &gObject : nullptr);
        uint8_t ra = orig(outA, key.c_str(), preset, extArg);
        void *cacheA = *ctx.image.At<void *>(kServiceCache);
        std::vector<Log> logA = gLog;

        gLog.clear();
        SetServiceState(ctx, cached ? &gObject : nullptr);
        bool rb = StringKeyParser::ParseKey(reinterpret_cast<StringKeyParser::Result *>(outB), key.c_str(), preset, extArg);
        void *cacheB = *ctx.image.At<void *>(kServiceCache);
        ++ctx.cases;

        std::string where = "'" + Show(key) + "' ext " + (extArg ? "'" + Show(ext) + "'" : std::string("null")) + " preset " + std::to_string(preset) + " cached " + std::to_string(cached);
        if ((ra & 0xff) != (rb ? 1 : 0)) ctx.Fail("ParseKey %s: returned %u (original), %d (ours)", where.c_str(), ra, rb);
        if (std::memcmp(outA, outB, sizeof(outA)) != 0)
            ctx.Fail("ParseKey %s: out {%08x %08x %08x %08x} (original) vs {%08x %08x %08x %08x} (ours)", where.c_str(), outA[0], outA[1], outA[2], outA[3], outB[0], outB[1], outB[2], outB[3]);
        if (cacheA != cacheB) ctx.Fail("ParseKey %s: cache %p vs %p", where.c_str(), cacheA, cacheB);
        if (logA.size() != gLog.size() || !std::equal(logA.begin(), logA.end(), gLog.begin()))
            ctx.Fail("ParseKey %s: service calls differ (original %zu, ours %zu)", where.c_str(), logA.size(), gLog.size());
    }
    SetServiceState(ctx, nullptr);
    *ctx.image.At<void *>(kServiceGlobal) = nullptr;
}

// The name table (0x10718c8, owned by lane QD: standalone/owned_statics.cpp defines the same 14 entries from the texts and
// checks at compile time the rule tested here on the original's own table): sorted by hash, each hash the CRC24 of the
// lowercased text | 0xff000000. Also: the owned symbol is the image's table in this harness.
DIFF_TEST(StringKeyParser_NameTable, 0xae6e60)
{
    const auto *table = ctx.image.At<StringKeyParser::HashedText>(kEntryTable);
    ++ctx.cases;
    if (static_cast<const void *>(ts2_rva_10718c8) != static_cast<const void *>(table)) ctx.Fail("the owned table is not the image's");
    uint32_t previous = 0;
    for (int i = 0; i < 14; ++i) {
        const char *text = table[i].text;
        ++ctx.cases;
        if (text == nullptr) {
            ctx.Fail("entry %d: no text", i);
            continue;
        }
        if (table[i].hash != (StringKeyParser::Crc24(text, 1) | 0xff000000)) ctx.Fail("entry %d '%s': hash %08x", i, text, table[i].hash);
        if (i > 0 && table[i].hash <= previous) ctx.Fail("entry %d: not sorted", i);
        previous = table[i].hash;
    }
}
