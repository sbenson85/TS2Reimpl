// Differential tests for cRZCmdLine and the vector<cRZString> helpers it uses (cRZStringVector).
// Objects on both sides carry the original vtables (cRZCmdLine's and cRZString's), so calls the code
// makes through them run original code on both sides; the slots that call through `this` get a
// vtable copy whose called slots are recording fakes (QueryInterface, Release, IsSwitchPresent#2).
// Heap frees are recorded per block across each side's own call (free_scope.h).
#include "difftest.h"
#include "free_scope.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// ---- original functions (RVAs) ------------------------------------------------------------------

using CtorDefault = cRZCmdLine *(__thiscall *)(void *);
using CtorText = cRZCmdLine *(__thiscall *)(void *, const char *);
using CtorOther = cRZCmdLine *(__thiscall *)(void *, void *);
using Dtor = void(__thiscall *)(void *);
using DeletingDtor = void *(__thiscall *)(void *, uint32_t);
using FnQI = uint8_t(__thiscall *)(void *, uint32_t, void **);
using FnU32 = uint32_t(__thiscall *)(void *);
using FnI32 = int32_t(__thiscall *)(void *);
using FnArgV = const char *(__thiscall *)(void *, int32_t);
using FnGetText = uint8_t(__thiscall *)(void *, void *);
using FnFind = int32_t(__thiscall *)(void *, const void *, uint32_t);
using FnSwitch1 = uint8_t(__thiscall *)(void *, const void *, void *, uint32_t);
using FnSwitch2 = uint8_t(__thiscall *)(void *, const void *);
using FnSwitch3 = uint8_t(__thiscall *)(void *, char, void *, uint32_t);
using FnIndex1 = int32_t(__thiscall *)(void *, const void *, int32_t);
using FnIndex2 = int32_t(__thiscall *)(void *, char, int32_t);
using FnInsert = uint8_t(__thiscall *)(void *, const void *, int32_t);
using FnErase = uint8_t(__thiscall *)(void *, int32_t);
using FnGetArg = cRZString *(__thiscall *)(void *, int32_t);
using FnParse = void(__thiscall *)(void *, const void *, void *);
using FnRebuild = void(__thiscall *)(void *, void *, void *);

constexpr uint32_t kCtorDefault = 0x1ae5e5, kCtorText = 0x1ae5a3, kCtorOther = 0x1ae4e8, kDtor = 0x14201d, kDeleting = 0x142229;
constexpr uint32_t kQI = 0x1af4a0, kRelease = 0x1af4d8, kArgC = 0x1af537, kArgV = 0x1af541, kGetText = 0x1aedba;
constexpr uint32_t kFind = 0x1ae9d3, kFindSub = 0x1aea85, kSwitch1 = 0x1af0ee, kSwitch2 = 0x1af09e, kSwitch3 = 0x1af321;
constexpr uint32_t kIndex1 = 0x1aedd0, kIndex2 = 0x1aef39, kInsert = 0x1af02a, kErase = 0x1ae992, kGetArg = 0x1ae610;
constexpr uint32_t kParse = 0x1ae6fe, kRebuild = 0x1ae632;

// ---- objects ------------------------------------------------------------------------------------

cRZCmdLine *RawCmd()
{
    auto *p = static_cast<cRZCmdLine *>(::operator new(sizeof(cRZCmdLine)));
    std::memset(p, 0xCD, sizeof(cRZCmdLine));
    return p;
}

bool Live(const void *p)
{
    return !difftest::FreedSinceBegin(p); // the call under test ran after difftest::BeginFrees()
}

bool SameString(const cRZString &a, const cRZString &b)
{
    return a.mVTable == b.mVTable && a.mRefCount == b.mRefCount && a.mString == b.mString && a.mString.capacity() == b.mString.capacity();
}

bool SameVec(const cRZStringVector &a, const cRZStringVector &b)
{
    if (a.Count() != b.Count() || (a.mEnd - a.mFirst) != (b.mEnd - b.mFirst) || (a.mFirst == nullptr) != (b.mFirst == nullptr)) return false;
    for (int32_t i = 0; i < a.Count(); ++i) {
        if (!SameString(a.mFirst[i], b.mFirst[i])) return false;
    }
    return true;
}

bool SameCmd(const cRZCmdLine &a, const cRZCmdLine &b, bool vtable = true)
{
    return (!vtable || a.mVTable == b.mVTable) && a.mRefCount == b.mRefCount && SameVec(a.mArgs, b.mArgs) && SameString(a.mText, b.mText)
        && SameString(a.mEmpty, b.mEmpty);
}

std::string Describe(const cRZCmdLine &c)
{
    std::string s = "[" + std::to_string(c.mArgs.Count()) + " args:";
    for (int32_t i = 0; i < c.mArgs.Count() && i < 12; ++i) s += " <" + c.mArgs.mFirst[i].mString + ">";
    return s + " text <" + c.mText.mString + ">]";
}

// A command-line object on each side, built from the same text by the original constructor and ours.
struct Pair {
    cRZCmdLine *a;
    cRZCmdLine *b;
};

Pair MakePair(difftest::Context &ctx, const std::string &text)
{
    Pair p{RawCmd(), RawCmd()};
    ctx.Original<CtorText>(kCtorText)(p.a, text.c_str());
    p.b->Construct(text.c_str());
    return p;
}

void FreePair(difftest::Context &ctx, Pair p)
{
    ctx.Original<Dtor>(kDtor)(p.a);
    ::operator delete(p.a);
    p.b->Destruct();
    ::operator delete(p.b);
}

// ---- random command lines -----------------------------------------------------------------------

const char *const kWords[] = {
    "a", "Bb", "game.exe", "-w", "/W", "-x", "-X", "/foo:bar", "-Foo:Bar", "\\foo:baz", "/Foo:\"two words\"", "-foo:'single'",
    "-foo:\"\"", "-foo:\"x\"", "/name:", "-name:value", "-n:", "/n:v", "\"quoted arg\"", "\"\"", "\"", "a\"b c\"d",
    "-longvaluelongvaluelongvaluelong:more", "-memoryReport", "/ExceptionHandling:off", "x", "-y", "/Y", "-yy", "-y:5",
    "-y:\"5\"", "-y:\"\"", "-w:", "-w:\"v w\"", "'", "-w:'x'", "-\"", "-w\"\"", "-w\"v\"", "-w\"vv\"", "/W\"", "-Q", "-q:1",
};
const char *const kNames[] = {"foo", "Foo", "FOO", "name", "n", "w", "y", "memoryreport", "MemoryReport", "ExceptionHandling",
                              "missing", "", "fo", "foo:bar", "yy", "x", "q", "longvaluelongvaluelongvaluelong", "W"};
const char kChars[] = {'w', 'W', 'x', 'X', 'y', 'Y', 'f', 'n', 'z', '1', '\\', '-', 'q', 'Q', '"', '\0'};

std::string Ws(std::mt19937 &rng)
{
    static const char *const w[] = {" ", "  ", "\t", "\n", " \t", "\t\n "};
    return w[rng() % 6];
}

std::string Junk(std::mt19937 &rng)
{
    static const char set[] = "ab -/\\:\"'\t\nXY";
    std::string s;
    uint32_t n = 1 + rng() % 25;
    for (uint32_t i = 0; i < n; ++i) s += set[rng() % (sizeof(set) - 1)];
    return s;
}

std::string RandomCmdLine(std::mt19937 &rng)
{
    uint32_t mode = rng() % 14;
    if (mode == 0) return "";
    if (mode == 1) return Ws(rng);
    std::string s;
    if (rng() % 3 == 0) s += Ws(rng);
    uint32_t n = rng() % 4 == 0 ? 1 + rng() % 30 : 1 + rng() % 6;
    for (uint32_t i = 0; i < n; ++i) {
        if (i) s += Ws(rng);
        s += rng() % 5 ? kWords[rng() % std::size(kWords)] : Junk(rng);
    }
    if (rng() % 3 == 0) s += Ws(rng);
    return s;
}

cRZString *MakeString(cRZString &s, const std::string &text)
{
    return s.Construct(text.c_str());
}

std::string RandomString(std::mt19937 &rng, uint32_t maxLen)
{
    static const char set[] = "abcdeXYZ -/\\:\"'0123";
    std::string s(rng() % (maxLen + 1), 'a');
    for (char &c : s) c = set[rng() % (sizeof(set) - 1)];
    return s;
}

// ---- recording fakes for the slots the code calls through `this` ---------------------------------

std::vector<int64_t> gLog;
int32_t gIndexResult = 0;
uint8_t gSwitchResult = 0;
const void *gExpectedSwitch = nullptr;

uint32_t __fastcall FakeAddRef(void * /*self*/, void * /*edx*/)
{
    gLog.push_back(1);
    return 0x1234;
}

void *__fastcall FakeDelete(void *self, void * /*edx*/, uint32_t flags)
{
    gLog.push_back(100 + flags);
    return self;
}

int32_t __fastcall FakeIndex(void * /*self*/, void * /*edx*/, const void *sw, int32_t start)
{
    gLog.push_back(11);
    gLog.push_back(sw == gExpectedSwitch);
    gLog.push_back(start);
    return gIndexResult;
}

uint8_t __fastcall FakeSwitch(void * /*self*/, void * /*edx*/, const void *sw, void *param, uint32_t trim)
{
    gLog.push_back(8);
    gLog.push_back(sw == gExpectedSwitch);
    gLog.push_back(trim);
    // The temp string handed to the slot: a fresh cRZString (refcount 0, empty).
    auto *s = static_cast<cRZString *>(param);
    gLog.push_back(s != nullptr && s->mRefCount == 0 && s->mString.empty());
    return gSwitchResult;
}

struct FakeVTable {
    void *slots[17];
    explicit FakeVTable(const difftest::Context &ctx)
    {
        std::memcpy(slots, ctx.image.At(cRZCmdLine::kVTable), sizeof(slots));
        slots[1] = reinterpret_cast<void *>(&FakeAddRef);
        slots[8] = reinterpret_cast<void *>(&FakeSwitch);
        slots[11] = reinterpret_cast<void *>(&FakeIndex);
        slots[15] = reinterpret_cast<void *>(&FakeDelete);
    }
};

// ---- vector fixtures ----------------------------------------------------------------------------

// A vector with `count` elements of random strings (some heap-sized), room for `capacity`, and
// random nonzero refcounts on the elements (copies must reset them to 0). Built from a seed so both
// sides get identical content.
cRZStringVector MakeVec(const difftest::Context &ctx, uint32_t seed, uint32_t count, uint32_t capacity, uint32_t maxLen = 40)
{
    std::mt19937 rng(seed);
    cRZStringVector v{nullptr, nullptr, nullptr};
    if (capacity) {
        v.mFirst = std::allocator<cRZString>().allocate(capacity);
        v.mLast = v.mFirst + count;
        v.mEnd = v.mFirst + capacity;
        for (uint32_t i = 0; i < count; ++i) {
            std::string s = RandomString(rng, maxLen);
            v.mFirst[i].Construct(s.c_str());
            v.mFirst[i].mRefCount = rng() % 9;
        }
    }
    (void)ctx;
    return v;
}

void FreeVec(cRZStringVector &v)
{
    if (!v.mFirst) return;
    for (cRZString *p = v.mFirst; p != v.mLast; ++p) p->Destruct();
    std::allocator<cRZString>().deallocate(v.mFirst, static_cast<size_t>(v.mEnd - v.mFirst));
    v.mFirst = v.mLast = v.mEnd = nullptr;
}

// The heap blocks a vector owns: its buffer and the long strings of its elements.
// Fixed-size lists: measuring must not allocate, or a block the call just freed could be reused
// by the measurement itself and look live.
template <typename T> struct FixedList {
    T items[96];
    uint32_t count = 0;
    void push_back(T v) { if (count < 96) items[count++] = v; }
    size_t size() const { return count; }
    T &operator[](size_t i) { return items[i]; }
    const T &operator[](size_t i) const { return items[i]; }
    const T *begin() const { return items; }
    const T *end() const { return items + count; }
    bool operator!=(const FixedList &o) const
    {
        if (count != o.count) return true;
        for (uint32_t i = 0; i < count; ++i) if (!(items[i] == o.items[i])) return true;
        return false;
    }
};

struct Blocks {
    FixedList<const void *> ptrs;
};

using LiveSet = FixedList<bool>;

Blocks BlocksOf(const cRZStringVector &v)
{
    Blocks b;
    if (!v.mFirst) return b;
    b.ptrs.push_back(v.mFirst);
    for (cRZString *p = v.mFirst; p != v.mLast; ++p) {
        if (p->mString.capacity() > 15) b.ptrs.push_back(p->mString.data());
    }
    return b;
}

LiveSet LiveMask(const Blocks &b)
{
    LiveSet m;
    for (const void *p : b.ptrs) m.push_back(Live(p));
    return m;
}

// Operations that free and then allocate (assigning strings, parsing into an old vector) may reuse a
// freed block, and which block the heap hands out is not the same on both sides. For those the
// check is that no old block stays allocated without being part of the result: the index of the
// first such block, or -1.
int LeakedBlock(const Blocks &before, const cRZStringVector &after)
{
    Blocks now = BlocksOf(after);
    for (uint32_t i = 0; i < before.ptrs.count; ++i) {
        if (!Live(before.ptrs[i])) continue;
        bool kept = false;
        for (const void *q : now.ptrs) kept |= q == before.ptrs[i];
        if (!kept) return static_cast<int>(i);
    }
    return -1;
}

std::string Mask(const LiveSet &m)
{
    std::string s;
    for (bool x : m) s += x ? '1' : '0';
    return s;
}

} // namespace

// ---- construction and destruction ----------------------------------------------------------------

DIFF_TEST(cRZCmdLine_ConstructDefault, 0x1ae5e5)
{
    auto orig = ctx.Original<CtorDefault>(kCtorDefault);
    for (int run = 0; run < 4; ++run) {
        cRZCmdLine *a = RawCmd(), *b = RawCmd();
        if (run == 1) {
            std::memset(a, 0x00, sizeof(cRZCmdLine));
            std::memset(b, 0x00, sizeof(cRZCmdLine));
        }
        cRZCmdLine *ra = orig(a);
        cRZCmdLine *rb = b->Construct();
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("run %d: return value is not this", run);
        // Nothing here owns heap memory, so the objects must be identical byte for byte.
        if (std::memcmp(a, b, sizeof(cRZCmdLine)) != 0) ctx.Fail("run %d: objects differ", run);
        if (!SameCmd(*a, *b)) ctx.Fail("run %d: state differs", run);
        ::operator delete(a);
        ::operator delete(b);
    }
}

DIFF_TEST(cRZCmdLine_ConstructText, 0x1ae5a3)
{
    auto orig = ctx.Original<CtorText>(kCtorText);
    std::mt19937 rng(0xC0DE1);
    for (int run = 0; run < 3000; ++run) {
        std::string text = RandomCmdLine(rng);
        cRZCmdLine *a = RawCmd(), *b = RawCmd();
        cRZCmdLine *ra = orig(a, text.c_str());
        cRZCmdLine *rb = b->Construct(text.c_str());
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("'%s': return value is not this", text.c_str());
        if (!SameCmd(*a, *b)) ctx.Fail("'%s': original %s ours %s", text.c_str(), Describe(*a).c_str(), Describe(*b).c_str());
        FreePair(ctx, {a, b});
    }
    // Long inputs: enough arguments to reallocate the vector several times, and one very long text.
    for (int run = 0; run < 20; ++run) {
        std::string text;
        uint32_t n = 40 + rng() % 200;
        for (uint32_t i = 0; i < n; ++i) text += (i ? " " : "") + std::string(kWords[rng() % std::size(kWords)]) + std::to_string(i);
        Pair p = MakePair(ctx, text);
        ++ctx.cases;
        if (!SameCmd(*p.a, *p.b)) ctx.Fail("long input %d: state differs", run);
        FreePair(ctx, p);
    }
}

DIFF_TEST(cRZCmdLine_ConstructFromOther, 0x1ae4e8)
{
    auto orig = ctx.Original<CtorOther>(kCtorOther);
    std::mt19937 rng(0xC0DE2);
    for (int run = 0; run < 600; ++run) {
        std::string text = RandomCmdLine(rng);
        if (run % 25 == 0) { // many arguments: reallocations
            text.clear();
            for (uint32_t i = 0, n = 20 + rng() % 120; i < n; ++i) text += " arg" + std::to_string(i);
        }
        cRZCmdLine *source = RawCmd();
        ctx.Original<CtorText>(kCtorText)(source, text.c_str());
        cRZCmdLine *a = RawCmd(), *b = RawCmd();
        cRZCmdLine *ra = orig(a, source);
        cRZCmdLine *rb = b->Construct(reinterpret_cast<cIGZCmdLine *>(source));
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("run %d: return value is not this", run);
        if (!SameCmd(*a, *b)) ctx.Fail("'%s': original %s ours %s", text.c_str(), Describe(*a).c_str(), Describe(*b).c_str());
        // The source is only read.
        ctx.Original<Dtor>(kDtor)(source);
        ::operator delete(source);
        FreePair(ctx, {a, b});
    }
}

namespace {

struct DestroyBlocks {
    Blocks blocks;
    const void *object;
};

// Which of the object's heap blocks (vector buffer, long strings, the object itself) are live.
DestroyBlocks Collect(const cRZCmdLine &c)
{
    DestroyBlocks d;
    d.blocks = BlocksOf(c.mArgs);
    if (c.mText.mString.capacity() > 15) d.blocks.ptrs.push_back(c.mText.mString.data());
    if (c.mEmpty.mString.capacity() > 15) d.blocks.ptrs.push_back(c.mEmpty.mString.data());
    d.object = &c;
    return d;
}

} // namespace

DIFF_TEST(cRZCmdLine_Destruct, 0x14201d)
{
    auto orig = ctx.Original<Dtor>(kDtor);
    std::mt19937 rng(0xC0DE3);
    for (int run = 0; run < 300; ++run) {
        std::string text = RandomCmdLine(rng);
        if (run % 7 == 0) text += " averylongargumentaveryl" + std::string(rng() % 60, 'z') + " second";
        Pair p = MakePair(ctx, text);
        DestroyBlocks da = Collect(*p.a), db = Collect(*p.b);
        difftest::BeginFrees();
        orig(p.a);
        LiveSet ma = LiveMask(da.blocks);
        difftest::BeginFrees();
        p.b->Destruct();
        LiveSet mb = LiveMask(db.blocks);
        ++ctx.cases;
        if (ma != mb) ctx.Fail("'%s': freed blocks differ: original %s ours %s", text.c_str(), Mask(ma).c_str(), Mask(mb).c_str());
        for (size_t i = 0; i < ma.size(); ++i) {
            if (ma[i]) ctx.Fail("'%s': original left block %zu allocated", text.c_str(), i);
        }
        // The cRZStrings are left with the string vtable and an empty small string; the vector is zeroed.
        if (p.a->mVTable != p.b->mVTable || p.a->mArgs.mFirst || p.b->mArgs.mFirst || p.a->mArgs.mLast || p.b->mArgs.mLast || p.a->mArgs.mEnd
            || p.b->mArgs.mEnd || p.a->mText.mVTable != p.b->mText.mVTable || p.a->mEmpty.mVTable != p.b->mEmpty.mVTable
            || p.a->mText.mString.size() != p.b->mText.mString.size() || p.a->mText.mString.capacity() != p.b->mText.mString.capacity()) {
            ctx.Fail("'%s': state after destruction differs", text.c_str());
        }
        ::operator delete(p.a);
        ::operator delete(p.b);
    }
}

DIFF_TEST(cRZCmdLine_DeletingDestructor, 0x142229)
{
    auto orig = ctx.Original<DeletingDtor>(kDeleting);
    std::mt19937 rng(0xC0DE4);
    for (int run = 0; run < 200; ++run) {
        uint32_t flags = run % 4; // bit 0 frees the object
        std::string text = RandomCmdLine(rng) + " averylongargumentaveryl" + std::string(rng() % 40, 'q');
        Pair p = MakePair(ctx, text);
        DestroyBlocks da = Collect(*p.a), db = Collect(*p.b);
        difftest::BeginFrees();
        void *ra = orig(p.a, flags);
        bool objectFreedA = !Live(p.a);
        LiveSet ma = LiveMask(da.blocks);
        difftest::BeginFrees();
        void *rb = p.b->DeletingDestructor(flags);
        bool objectFreedB = !Live(p.b);
        LiveSet mb = LiveMask(db.blocks);
        ++ctx.cases;
        if (ra != p.a || rb != p.b) ctx.Fail("flags %u: return value is not this", flags);
        if (objectFreedA != objectFreedB || (flags & 1) != static_cast<uint32_t>(objectFreedA)) ctx.Fail("flags %u: object freed original %d ours %d", flags, objectFreedA, objectFreedB);
        if (ma != mb) ctx.Fail("flags %u: freed blocks differ: original %s ours %s", flags, Mask(ma).c_str(), Mask(mb).c_str());
        if (!(flags & 1)) {
            ::operator delete(p.a);
            ::operator delete(p.b);
        }
    }
}

// ---- slots that call through `this` -----------------------------------------------------------------

DIFF_TEST(cRZCmdLine_QueryInterface, 0x1af4a0)
{
    auto orig = ctx.Original<FnQI>(kQI);
    FakeVTable fake(ctx);
    const uint32_t ids[] = {1, 0xa31e539c, 0, 2, 0x089b7dc8, 0xa31e539d, 0xffffffff, 0xa31e539b};
    for (uint32_t id : ids) {
        Pair p = MakePair(ctx, "x y");
        p.a->mVTable = p.b->mVTable = fake.slots;
        void *outA = reinterpret_cast<void *>(0x5a5a5a5a), *outB = outA;
        gLog.clear();
        uint8_t ra = orig(p.a, id, &outA);
        std::vector<int64_t> logA = gLog;
        gLog.clear();
        uint8_t rb = p.b->QueryInterface(id, &outB);
        ++ctx.cases;
        if (ra != static_cast<uint8_t>(rb) || logA != gLog) ctx.Fail("id 0x%x: result %u/%u, AddRef calls %zu/%zu", id, ra, rb, logA.size(), gLog.size());
        if ((outA == p.a) != (outB == p.b) || (ra && outA != p.a) || (!ra && outA != reinterpret_cast<void *>(0x5a5a5a5a))) ctx.Fail("id 0x%x: out pointer differs", id);
        if (ra != 0 && ra != 1) ctx.Fail("id 0x%x: unexpected original result %u", id, ra);
        FreePair(ctx, {p.a, p.b});
    }
}

DIFF_TEST(cRZCmdLine_Release, 0x1af4d8)
{
    auto orig = ctx.Original<FnU32>(kRelease);
    FakeVTable fake(ctx);
    for (uint32_t refs = 0; refs < 6; ++refs) {
        Pair p = MakePair(ctx, "a b");
        p.a->mVTable = p.b->mVTable = fake.slots;
        p.a->mRefCount = p.b->mRefCount = refs;
        gLog.clear();
        uint32_t ra = orig(p.a);
        std::vector<int64_t> logA = gLog;
        gLog.clear();
        uint32_t rb = p.b->Release();
        ++ctx.cases;
        if (ra != rb || logA != gLog) ctx.Fail("refs %u: result %u/%u, calls %zu/%zu", refs, ra, rb, logA.size(), gLog.size());
        if (p.a->mRefCount != p.b->mRefCount) ctx.Fail("refs %u: counts differ", refs);
        if ((refs <= 1) != (logA.size() == 1 && logA[0] == 101)) ctx.Fail("refs %u: deleting destructor call (flags 1) not as expected", refs);
        FreePair(ctx, {p.a, p.b});
    }
}

DIFF_TEST(cRZCmdLine_IsSwitchPresentAny, 0x1af09e)
{
    auto orig = ctx.Original<FnSwitch2>(kSwitch2);
    FakeVTable fake(ctx);
    std::mt19937 rng(0xC0DE5);
    for (int run = 0; run < 300; ++run) {
        Pair p = MakePair(ctx, "x");
        p.a->mVTable = p.b->mVTable = fake.slots;
        cRZString name{};
        MakeString(name, kNames[rng() % std::size(kNames)]);
        gExpectedSwitch = name.AsInterface();
        const int32_t indexes[] = {-1, 0, 1, 7, -100};
        gIndexResult = indexes[rng() % std::size(indexes)];
        const uint8_t results[] = {0, 1, 2, 0x80};
        gSwitchResult = results[rng() % std::size(results)];
        gLog.clear();
        uint8_t ra = orig(p.a, name.AsInterface());
        std::vector<int64_t> logA = gLog;
        gLog.clear();
        uint8_t rb = p.b->IsSwitchPresent(*name.AsInterface());
        ++ctx.cases;
        if (ra != static_cast<uint8_t>(rb) || logA != gLog) {
            ctx.Fail("index %d switch %u: result %u/%u, calls %zu/%zu", gIndexResult, gSwitchResult, ra, rb, logA.size(), gLog.size());
        }
        FreePair(ctx, {p.a, p.b});
    }
}

// ---- plain slots ----------------------------------------------------------------------------------

DIFF_TEST(cRZCmdLine_ArgAccess, 0x1af541)
{
    auto argC = ctx.Original<FnI32>(kArgC);
    auto argV = ctx.Original<FnArgV>(kArgV);
    auto getArg = ctx.Original<FnGetArg>(kGetArg);
    auto getText = ctx.Original<FnGetText>(kGetText);
    std::mt19937 rng(0xC0DE6);
    for (int run = 0; run < 600; ++run) {
        Pair p = MakePair(ctx, RandomCmdLine(rng));
        ++ctx.cases;
        if (argC(p.a) != p.b->ArgC()) ctx.Fail("ArgC differs");
        int32_t count = p.a->mArgs.Count();
        for (int32_t i = 0; i < count + 3; ++i) {
            const char *sa = argV(p.a, i), *sb = p.b->ArgV(i);
            if (std::strcmp(sa, sb) != 0) ctx.Fail("ArgV(%d) differs: '%s' / '%s'", i, sa, sb);
            // The pointers are the element's own string, or the empty string past the end.
            const char *wantA = i < count ? p.a->mArgs.mFirst[i].mString.c_str() : p.a->mEmpty.mString.c_str();
            const char *wantB = i < count ? p.b->mArgs.mFirst[i].mString.c_str() : p.b->mEmpty.mString.c_str();
            if (sa != wantA || sb != wantB) ctx.Fail("ArgV(%d): not the expected string", i);
            cRZString *ga = getArg(p.a, i), *gb = p.b->GetArgument(i);
            cRZString *wantGA = i < count ? p.a->mArgs.mFirst + i : &p.a->mEmpty;
            cRZString *wantGB = i < count ? p.b->mArgs.mFirst + i : &p.b->mEmpty;
            if (ga != wantGA || gb != wantGB) ctx.Fail("GetArgument(%d) differs", i);
        }
        // GetCommandLineText copies the text through the argument's Copy slot.
        cRZString oa{}, ob{};
        MakeString(oa, "old");
        MakeString(ob, "old");
        uint8_t ta = getText(p.a, oa.AsInterface());
        bool tb = p.b->GetCommandLineText(*ob.AsInterface());
        if (ta != static_cast<uint8_t>(tb) || ta != 1 || !SameString(oa, ob) || oa.mString != p.a->mText.mString) ctx.Fail("GetCommandLineText differs");
        FreePair(ctx, p);
    }
}

DIFF_TEST(cRZCmdLine_Find, 0x1ae9d3)
{
    auto find = ctx.Original<FnFind>(kFind);
    auto findSub = ctx.Original<FnFind>(kFindSub);
    std::mt19937 rng(0xC0DE7);
    for (int run = 0; run < 2500; ++run) {
        std::string text = RandomCmdLine(rng);
        Pair p = MakePair(ctx, text);
        std::string query;
        uint32_t how = rng() % 4;
        if (how == 0 && p.a->mArgs.Count()) query = p.a->mArgs.mFirst[rng() % p.a->mArgs.Count()].mString; // an argument itself
        else if (how == 1 && p.a->mArgs.Count()) { // a case change, a part or an extension of one
            query = p.a->mArgs.mFirst[rng() % p.a->mArgs.Count()].mString;
            for (char &c : query) if (rng() % 2) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : (c >= 'A' && c <= 'Z' ? c + 32 : c));
            if (rng() % 3 == 0 && !query.empty()) query = query.substr(rng() % query.size());
            if (rng() % 4 == 0) query += "z";
        } else if (how == 2) query = kNames[rng() % std::size(kNames)];
        else query = RandomString(rng, 12);
        cRZString q{};
        MakeString(q, query);
        for (uint32_t cs = 0; cs < 2; ++cs) {
            int32_t fa = find(p.a, q.AsInterface(), cs), fb = p.b->Find(*q.AsInterface(), cs != 0);
            int32_t sa = findSub(p.a, q.AsInterface(), cs), sb = p.b->FindSubString(*q.AsInterface(), cs != 0);
            ++ctx.cases;
            if (fa != fb) ctx.Fail("Find('%s', cs %u) in '%s': %d/%d", query.c_str(), cs, text.c_str(), fa, fb);
            if (sa != sb) ctx.Fail("FindSubString('%s', cs %u) in '%s': %d/%d", query.c_str(), cs, text.c_str(), sa, sb);
        }
        if (!SameCmd(*p.a, *p.b)) ctx.Fail("object changed by a search");
        FreePair(ctx, p);
    }
}

namespace {

// A text with switches the lookups are known to meet, built from the pieces the switch tests need.
std::string SwitchText(std::mt19937 &rng)
{
    if (rng() % 4 == 0) return RandomCmdLine(rng);
    std::string s = "game.exe";
    uint32_t n = 1 + rng() % 8;
    for (uint32_t i = 0; i < n; ++i) s += " " + std::string(kWords[rng() % std::size(kWords)]);
    return s;
}

} // namespace

DIFF_TEST(cRZCmdLine_IsSwitchPresentValue, 0x1af0ee)
{
    auto sw1 = ctx.Original<FnSwitch1>(kSwitch1);
    std::mt19937 rng(0xC0DE8);
    for (int run = 0; run < 4000; ++run) {
        std::string text = SwitchText(rng);
        Pair p = MakePair(ctx, text);
        std::string name = kNames[rng() % std::size(kNames)];
        cRZString n{}, pa{}, pb{};
        MakeString(n, name);
        std::string initial = RandomString(rng, 20);
        MakeString(pa, initial);
        MakeString(pb, initial);
        uint32_t trim = rng() % 2;
        // ORIGINAL-BUG avoided here: a value that is a single quote character makes both sides throw
        // (see the next test), so such texts are skipped.
        bool loneQuote = false;
        for (int32_t i = 0; i < p.a->mArgs.Count(); ++i) {
            const std::string &a = p.a->mArgs.mFirst[i].mString;
            if (a.size() == name.size() + 3 && (a.back() == '"' || a.back() == '\'') && a.size() > 1 && a[name.size() + 1] == ':') loneQuote = true;
        }
        if (loneQuote && trim) trim = 0;
        uint8_t ra = sw1(p.a, n.AsInterface(), pa.AsInterface(), trim);
        bool rb = p.b->IsSwitchPresent(*n.AsInterface(), *pb.AsInterface(), trim != 0);
        ++ctx.cases;
        if (ra != static_cast<uint8_t>(rb) || !SameString(pa, pb) || !SameCmd(*p.a, *p.b)) {
            ctx.Fail("'%s' in '%s' trim %u: %u/%u, value <%s>/<%s>", name.c_str(), text.c_str(), trim, ra, rb, pa.mString.c_str(), pb.mString.c_str());
        }
        FreePair(ctx, p);
    }
}

// ORIGINAL-BUG (reproduced): trimming the quotes of a value that is one quote character erases it
// twice and the second erase throws std::out_of_range. Both sides must throw and leave the
// parameter untouched.
DIFF_TEST(cRZCmdLine_IsSwitchPresentLoneQuote, 0x1af0ee)
{
    auto sw1 = ctx.Original<FnSwitch1>(kSwitch1);
    for (const char *text : {"game -x:\"", "game /x:'", "-x:\" -x:ok"}) {
        for (uint32_t trim = 0; trim < 2; ++trim) {
            Pair p = MakePair(ctx, text);
            cRZString n{}, pa{}, pb{};
            MakeString(n, "x");
            MakeString(pa, "old");
            MakeString(pb, "old");
            bool threwA = false, threwB = false;
            uint8_t ra = 0;
            bool rb = false;
            try {
                ra = sw1(p.a, n.AsInterface(), pa.AsInterface(), trim);
            } catch (...) {
                threwA = true;
            }
            try {
                rb = p.b->IsSwitchPresent(*n.AsInterface(), *pb.AsInterface(), trim != 0);
            } catch (...) {
                threwB = true;
            }
            ++ctx.cases;
            if (threwA != threwB || (!threwA && ra != static_cast<uint8_t>(rb)) || !SameString(pa, pb)) {
                ctx.Fail("'%s' trim %u: threw %d/%d, result %u/%u, value <%s>/<%s>", text, trim, threwA, threwB, ra, rb, pa.mString.c_str(), pb.mString.c_str());
            }
            if (trim == 1 && !threwA && std::strcmp(text, "-x:\" -x:ok") != 0) ctx.Fail("'%s': the original did not throw", text);
            // The throwing paths leak temporaries on the original side; the objects themselves are intact.
            FreePair(ctx, p);
        }
    }
}

DIFF_TEST(cRZCmdLine_IsSwitchPresentChar, 0x1af321)
{
    auto sw3 = ctx.Original<FnSwitch3>(kSwitch3);
    std::mt19937 rng(0xC0DE9);
    for (int run = 0; run < 4000; ++run) {
        std::string text = SwitchText(rng);
        Pair p = MakePair(ctx, text);
        char c = kChars[rng() % std::size(kChars)];
        cRZString pa{}, pb{};
        std::string initial = RandomString(rng, 20);
        MakeString(pa, initial);
        MakeString(pb, initial);
        uint32_t trim = rng() % 2;
        uint8_t ra = sw3(p.a, c, pa.AsInterface(), trim);
        bool rb = p.b->IsSwitchPresent(c, *pb.AsInterface(), trim != 0);
        ++ctx.cases;
        if (ra != static_cast<uint8_t>(rb) || !SameString(pa, pb) || !SameCmd(*p.a, *p.b)) {
            ctx.Fail("'%c' in '%s' trim %u: %u/%u, value <%s>/<%s>", c, text.c_str(), trim, ra, rb, pa.mString.c_str(), pb.mString.c_str());
        }
        FreePair(ctx, p);
    }
}

DIFF_TEST(cRZCmdLine_GetIndexOfSwitch, 0x1aedd0)
{
    auto idx1 = ctx.Original<FnIndex1>(kIndex1);
    auto idx2 = ctx.Original<FnIndex2>(kIndex2);
    std::mt19937 rng(0xC0DEA);
    for (int run = 0; run < 4000; ++run) {
        std::string text = SwitchText(rng);
        Pair p = MakePair(ctx, text);
        cRZString n{};
        std::string name = kNames[rng() % std::size(kNames)];
        MakeString(n, name);
        char c = kChars[rng() % std::size(kChars)];
        const int32_t starts[] = {-5, -1, 0, 0, 0, 1, 2, 3, 100};
        int32_t start = starts[rng() % std::size(starts)];
        int32_t ra = idx1(p.a, n.AsInterface(), start), rb = p.b->GetIndexOfSwitch(*n.AsInterface(), start);
        int32_t ca = idx2(p.a, c, start), cb = p.b->GetIndexOfSwitch(c, start);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("name '%s' from %d in '%s': %d/%d", name.c_str(), start, text.c_str(), ra, rb);
        if (ca != cb) ctx.Fail("char '%c' from %d in '%s': %d/%d", c, start, text.c_str(), ca, cb);
        if (!SameCmd(*p.a, *p.b)) ctx.Fail("object changed by a lookup");
        FreePair(ctx, p);
    }
}

DIFF_TEST(cRZCmdLine_InsertEraseArgument, 0x1af02a)
{
    auto ins = ctx.Original<FnInsert>(kInsert);
    auto era = ctx.Original<FnErase>(kErase);
    std::mt19937 rng(0xC0DEB);
    for (int run = 0; run < 1500; ++run) {
        std::string text = RandomCmdLine(rng);
        Pair p = MakePair(ctx, text);
        // A series of edits, so the vector reallocates and shifts in all positions.
        for (int step = 0; step < 12; ++step) {
            int32_t count = p.a->mArgs.Count();
            bool insert = count == 0 || rng() % 3 != 0;
            if (insert) {
                const int32_t picks[] = {-3, -1, 0, 0, 1, 2, count / 2, count - 1, count, count + 1, count + 5, 1000};
                int32_t at = picks[rng() % std::size(picks)];
                static const char *const args[] = {"", "x", "a b", "tab\there", "new\nline", "\"q\"", "averylongargumentaveryl-ongargument", "-sw:v", "'"};
                cRZString arg{};
                MakeString(arg, args[rng() % std::size(args)]);
                uint8_t ra = ins(p.a, arg.AsInterface(), at);
                bool rb = p.b->InsertArgument(*arg.AsInterface(), at);
                ++ctx.cases;
                if (ra != static_cast<uint8_t>(rb) || ra != 1) ctx.Fail("insert at %d returned %u/%u", at, ra, rb);
            } else {
                const int32_t picks[] = {0, 0, 1, count - 1, count - 1, count, count + 3, count / 2};
                int32_t at = picks[rng() % std::size(picks)];
                // (Frees are checked in cRZStringVector_Erase; RebuildText allocates after them here.)
                uint8_t ra = era(p.a, at);
                bool rb = p.b->EraseArgument(at);
                ++ctx.cases;
                if (ra != static_cast<uint8_t>(rb) || ra != 1) ctx.Fail("erase at %d returned %u/%u", at, ra, rb);
            }
            if (!SameCmd(*p.a, *p.b)) {
                ctx.Fail("step %d: original %s ours %s", step, Describe(*p.a).c_str(), Describe(*p.b).c_str());
                break;
            }
        }
        FreePair(ctx, p);
    }
}

// ---- the parser and the text builder ---------------------------------------------------------------

DIFF_TEST(cRZCmdLine_Parse, 0x1ae6fe)
{
    auto parse = ctx.Original<FnParse>(kParse);
    std::mt19937 rng(0xC0DEC);
    cRZCmdLine *self = RawCmd(); // `this` is unused by both
    for (int run = 0; run < 4000; ++run) {
        std::string text = RandomCmdLine(rng);
        if (run % 40 == 0) { // many arguments
            text.clear();
            for (uint32_t i = 0, n = 30 + rng() % 100; i < n; ++i) text += " \"quoted arg " + std::to_string(i) + "\" x" + std::to_string(i);
        }
        cRZString src{};
        MakeString(src, text);
        // The destination starts with old content, so the clear is exercised.
        uint32_t oldCount = rng() % 6, oldCap = oldCount + rng() % 4;
        if (rng() % 3 == 0) oldCount = oldCap = 0;
        cRZStringVector va = MakeVec(ctx, run, oldCount, oldCap), vb = MakeVec(ctx, run, oldCount, oldCap);
        Blocks ba = BlocksOf(va), bb = BlocksOf(vb);
        difftest::BeginFrees();
        parse(self, &src, &va);
        int leakA = LeakedBlock(ba, va);
        difftest::BeginFrees();
        self->Parse(src, vb);
        int leakB = LeakedBlock(bb, vb);
        ++ctx.cases;
        if (!SameVec(va, vb)) {
            std::string da = "[", db = "[";
            for (int32_t i = 0; i < va.Count(); ++i) da += "<" + va.mFirst[i].mString + ">";
            for (int32_t i = 0; i < vb.Count(); ++i) db += "<" + vb.mFirst[i].mString + ">";
            ctx.Fail("'%s': original %s] ours %s]", text.c_str(), da.c_str(), db.c_str());
        }
        // The old buffer is kept (cleared, not freed) unless the new arguments outgrow it.
        if (leakA >= 0 || leakB >= 0) ctx.Fail("'%s': an old block is left allocated and unused: original %d ours %d", text.c_str(), leakA, leakB);
        FreeVec(va);
        FreeVec(vb);
    }
    ::operator delete(self);
}

DIFF_TEST(cRZCmdLine_RebuildText, 0x1ae632)
{
    auto rebuild = ctx.Original<FnRebuild>(kRebuild);
    std::mt19937 rng(0xC0DED);
    cRZCmdLine *self = RawCmd();
    for (int run = 0; run < 4000; ++run) {
        uint32_t count = rng() % 9, cap = count + rng() % 3;
        cRZStringVector va = MakeVec(ctx, run, count, cap, 30), vb = MakeVec(ctx, run, count, cap, 30);
        // Arguments with and without whitespace, quotes, and empty ones.
        static const char *const specials[] = {"", " ", "a b", "tab\tin", "nl\nin", "\"", "x", "-a:b c", "long-long-long-long-long-long"};
        for (cRZString *p = va.mFirst, *q = vb.mFirst; p != va.mLast; ++p, ++q) {
            if (rng() % 2) {
                const char *s = specials[rng() % std::size(specials)];
                p->mString.assign(s);
                q->mString.assign(s);
            }
        }
        cRZString ta{}, tb{};
        std::string initial = RandomString(rng, 50);
        MakeString(ta, initial);
        MakeString(tb, initial);
        rebuild(self, &va, &ta);
        self->RebuildText(vb, tb);
        ++ctx.cases;
        if (!SameString(ta, tb) || !SameVec(va, vb)) ctx.Fail("run %d: text <%s> / <%s>", run, ta.mString.c_str(), tb.mString.c_str());
        FreeVec(va);
        FreeVec(vb);
    }
    ::operator delete(self);
}

// ---- the vector<cRZString> helpers -------------------------------------------------------------------

namespace {

using FnPushBack = cRZString *(__thiscall *)(void *, const void *);
using FnEmplace = cRZString *(__thiscall *)(void *, cRZString *, const void *);
using FnInsertVec = cRZString **(__thiscall *)(void *, cRZString **, cRZString *, const void *);
using FnEraseVec = cRZString **(__thiscall *)(void *, cRZString **, cRZString *);
using FnGrowth = uint32_t(__thiscall *)(void *, uint32_t);
using FnChange = void(__thiscall *)(void *, cRZString *, uint32_t, uint32_t);
using FnCopyRange = cRZString *(__cdecl *)(const cRZString *, const cRZString *, cRZString *);
using FnDestroyRange = void(__cdecl *)(cRZString *, cRZString *, const void *);
using FnUninitCopy = cRZString *(__cdecl *)(const cRZString *, const cRZString *, cRZString *, const void *);

ptrdiff_t Index(const cRZStringVector &v, const cRZString *p)
{
    return p ? p - v.mFirst : -1000;
}

} // namespace

DIFF_TEST(cRZCmdLine_Vector_PushBack, 0x1ac271)
{
    auto orig = ctx.Original<FnPushBack>(0x1ac271);
    std::mt19937 rng(0xAB01);
    for (int run = 0; run < 1500; ++run) {
        uint32_t count = rng() % 8, cap = count + 1 + rng() % 3;
        cRZStringVector va = MakeVec(ctx, run, count, cap), vb = MakeVec(ctx, run, count, cap);
        cRZString value{};
        MakeString(value, RandomString(rng, 40));
        value.mRefCount = 1 + rng() % 5;
        cRZString *ra = orig(&va, &value);
        cRZString *rb = vb.PushBack(value);
        ++ctx.cases;
        if (Index(va, ra) != Index(vb, rb) || !SameVec(va, vb) || va.mFirst[count].mRefCount != 0) ctx.Fail("run %d: differs (new element %td/%td)", run, Index(va, ra), Index(vb, rb));
        FreeVec(va);
        FreeVec(vb);
    }
}

DIFF_TEST(cRZCmdLine_Vector_EmplaceReallocate, 0x1ac291)
{
    auto orig = ctx.Original<FnEmplace>(0x1ac291);
    std::mt19937 rng(0xAB02);
    for (int run = 0; run < 3000; ++run) {
        uint32_t count = rng() % 12, cap = count + rng() % 4;
        if (rng() % 5 == 0) cap = count;
        cRZStringVector va = MakeVec(ctx, run, count, cap), vb = MakeVec(ctx, run, count, cap);
        uint32_t at = rng() % (count + 1);
        cRZString value{};
        MakeString(value, RandomString(rng, 40));
        value.mRefCount = 1 + rng() % 5;
        Blocks ba = BlocksOf(va), bb = BlocksOf(vb);
        difftest::BeginFrees();
        cRZString *ra = orig(&va, va.mFirst + at, &value);
        LiveSet ma = LiveMask(ba);
        difftest::BeginFrees();
        cRZString *rb = vb.EmplaceReallocate(vb.mFirst + at, value);
        LiveSet mb = LiveMask(bb);
        ++ctx.cases;
        if (Index(va, ra) != Index(vb, rb) || Index(va, ra) != static_cast<ptrdiff_t>(at) || !SameVec(va, vb)) {
            ctx.Fail("count %u cap %u at %u: new element %td/%td, capacity %td/%td", count, cap, at, Index(va, ra), Index(vb, rb), va.mEnd - va.mFirst, vb.mEnd - vb.mFirst);
        }
        if (ma != mb) ctx.Fail("count %u at %u: freed blocks differ: %s / %s", count, at, Mask(ma).c_str(), Mask(mb).c_str());
        FreeVec(va);
        FreeVec(vb);
    }
}

DIFF_TEST(cRZCmdLine_Vector_Insert, 0x1ae427)
{
    auto orig = ctx.Original<FnInsertVec>(0x1ae427);
    std::mt19937 rng(0xAB03);
    for (int run = 0; run < 4000; ++run) {
        uint32_t count = rng() % 12, cap = count + rng() % 3;
        if (rng() % 3 == 0) cap = count;
        cRZStringVector va = MakeVec(ctx, run, count, cap), vb = MakeVec(ctx, run, count, cap);
        uint32_t at = rng() % (count + 1);
        // The value is sometimes an element of the vector itself (copied before the shift).
        bool alias = count && rng() % 3 == 0;
        uint32_t aliasIndex = alias ? rng() % count : 0;
        cRZString value{};
        MakeString(value, RandomString(rng, 40));
        value.mRefCount = 1 + rng() % 5;
        const void *valA = alias ? &va.mFirst[aliasIndex] : &value;
        const void *valB = alias ? &vb.mFirst[aliasIndex] : &value;
        cRZString *outA = reinterpret_cast<cRZString *>(0x77), *outB = outA;
        Blocks ba = BlocksOf(va), bb = BlocksOf(vb);
        difftest::BeginFrees();
        cRZString **ra = orig(&va, &outA, va.mFirst + at, valA);
        int leakA = LeakedBlock(ba, va);
        difftest::BeginFrees();
        cRZString **rb = vb.Insert(&outB, vb.mFirst + at, *static_cast<const cRZString *>(valB));
        int leakB = LeakedBlock(bb, vb);
        ++ctx.cases;
        if (ra != &outA || rb != &outB || Index(va, outA) != Index(vb, outB) || Index(va, outA) != static_cast<ptrdiff_t>(at) || !SameVec(va, vb)) {
            ctx.Fail("count %u cap %u at %u alias %d: result iterator %td/%td", count, cap, at, alias, Index(va, outA), Index(vb, outB));
        }
        if (leakA >= 0 || leakB >= 0) ctx.Fail("count %u cap %u at %u: an old block is left allocated and unused: original %d ours %d", count, cap, at, leakA, leakB);
        FreeVec(va);
        FreeVec(vb);
    }
}

DIFF_TEST(cRZCmdLine_Vector_Erase, 0x1af56e)
{
    auto orig = ctx.Original<FnEraseVec>(0x1af56e);
    std::mt19937 rng(0xAB04);
    for (int run = 0; run < 3000; ++run) {
        uint32_t count = 1 + rng() % 12, cap = count + rng() % 3;
        cRZStringVector va = MakeVec(ctx, run, count, cap), vb = MakeVec(ctx, run, count, cap);
        uint32_t at = rng() % count;
        cRZString *outA = reinterpret_cast<cRZString *>(0x77), *outB = outA;
        const void *lastBlockA = va.mLast[-1].mString.capacity() > 15 ? va.mLast[-1].mString.data() : nullptr;
        const void *lastBlockB = vb.mLast[-1].mString.capacity() > 15 ? vb.mLast[-1].mString.data() : nullptr;
        difftest::BeginFrees();
        cRZString **ra = orig(&va, &outA, va.mFirst + at);
        bool freedA = lastBlockA && !Live(lastBlockA);
        difftest::BeginFrees();
        cRZString **rb = vb.Erase(&outB, vb.mFirst + at);
        bool freedB = lastBlockB && !Live(lastBlockB);
        ++ctx.cases;
        if (ra != &outA || rb != &outB || Index(va, outA) != Index(vb, outB) || Index(va, outA) != static_cast<ptrdiff_t>(at) || !SameVec(va, vb)) {
            ctx.Fail("count %u at %u: differs (iterator %td/%td)", count, at, Index(va, outA), Index(vb, outB));
        }
        if (freedA != freedB) ctx.Fail("count %u at %u: last element's block freed original %d ours %d", count, at, freedA, freedB);
        FreeVec(va);
        FreeVec(vb);
    }
}

DIFF_TEST(cRZCmdLine_Vector_CopyRanges, 0x1ae350)
{
    auto backward = ctx.Original<FnCopyRange>(0x1ae350);
    auto forward = ctx.Original<FnCopyRange>(0x1ae37a);
    std::mt19937 rng(0xAB05);
    for (int run = 0; run < 2000; ++run) {
        uint32_t n = rng() % 10;
        // Source and destination vectors; destination elements start with their own content.
        cRZStringVector sa = MakeVec(ctx, run, n, n + 1), sb = MakeVec(ctx, run, n, n + 1);
        cRZStringVector da = MakeVec(ctx, run + 100000, n + 2, n + 2, 30), db = MakeVec(ctx, run + 100000, n + 2, n + 2, 30);
        uint32_t off = rng() % 3;
        bool back = rng() % 2;
        cRZString *retA, *retB;
        if (back) {
            retA = backward(sa.mFirst, sa.mLast, da.mFirst + off + n);
            retB = cRZStringVector::CopyBackward(sb.mFirst, sb.mLast, db.mFirst + off + n);
        } else {
            retA = forward(sa.mFirst, sa.mLast, da.mFirst + off);
            retB = cRZStringVector::CopyForward(sb.mFirst, sb.mLast, db.mFirst + off);
        }
        ++ctx.cases;
        if (Index(da, retA) != Index(db, retB) || !SameVec(da, db) || !SameVec(sa, sb)) ctx.Fail("n %u off %u %s: differs (return %td/%td)", n, off, back ? "backward" : "forward", Index(da, retA), Index(db, retB));
        FreeVec(sa);
        FreeVec(sb);
        FreeVec(da);
        FreeVec(db);
    }
}

DIFF_TEST(cRZCmdLine_Vector_CalculateGrowth, 0x143994)
{
    auto orig = ctx.Original<FnGrowth>(0x143994);
    std::mt19937 rng(0xAB06);
    const uint32_t caps[] = {0, 1, 2, 3, 4, 5, 7, 8, 100, 0x3ffffff, 0x4000000, 0x5555555, 0x7fffffe, 0x7ffffff, 0x8000000, 0xf0000000};
    const uint32_t sizes[] = {0, 1, 2, 3, 4, 5, 6, 9, 101, 0x7ffffff, 0x7fffffe, 0x8000000, 0xffffffff, 0x4000001};
    for (uint32_t cap : caps) {
        for (uint32_t size : sizes) {
            // Synthetic pointers: the function only subtracts them.
            cRZStringVector va{nullptr, nullptr, nullptr}, vb = va;
            va.mEnd = vb.mEnd = reinterpret_cast<cRZString *>(static_cast<uintptr_t>(static_cast<uint64_t>(cap) * 32));
            uint32_t ra = orig(&va, size), rb = vb.CalculateGrowth(size);
            ++ctx.cases;
            if (ra != rb) ctx.Fail("capacity %u new size %u: %u/%u", cap, size, ra, rb);
        }
    }
    for (int run = 0; run < 3000; ++run) {
        uint32_t cap = rng() % 4 ? rng() % 40 : rng() % 0x8000100, size = rng() % 4 ? rng() % 60 : rng();
        cRZStringVector va{nullptr, nullptr, nullptr}, vb = va;
        va.mEnd = vb.mEnd = reinterpret_cast<cRZString *>(static_cast<uintptr_t>(static_cast<uint64_t>(cap) * 32));
        ++ctx.cases;
        uint32_t ra = orig(&va, size), rb = vb.CalculateGrowth(size);
        if (ra != rb) ctx.Fail("capacity %u new size %u: %u/%u", cap, size, ra, rb);
    }
}

DIFF_TEST(cRZCmdLine_Vector_ChangeArray, 0x1ad8db)
{
    auto orig = ctx.Original<FnChange>(0x1ad8db);
    std::mt19937 rng(0xAB07);
    for (int run = 0; run < 800; ++run) {
        uint32_t count = rng() % 8, cap = count + rng() % 3;
        if (rng() % 4 == 0) count = cap = 0;
        cRZStringVector va = MakeVec(ctx, run, count, cap), vb = MakeVec(ctx, run, count, cap);
        uint32_t newCount = rng() % 5, newCap = newCount + 1 + rng() % 4;
        cRZStringVector na = MakeVec(ctx, run + 5000, newCount, newCap), nb = MakeVec(ctx, run + 5000, newCount, newCap);
        Blocks ba = BlocksOf(va), bb = BlocksOf(vb);
        difftest::BeginFrees();
        orig(&va, na.mFirst, newCount, newCap);
        LiveSet ma = LiveMask(ba);
        difftest::BeginFrees();
        vb.ChangeArray(nb.mFirst, newCount, newCap);
        LiveSet mb = LiveMask(bb);
        ++ctx.cases;
        if (!SameVec(va, vb) || ma != mb) ctx.Fail("run %d: differs (freed %s / %s)", run, Mask(ma).c_str(), Mask(mb).c_str());
        FreeVec(va);
        FreeVec(vb);
    }
}

DIFF_TEST(cRZCmdLine_Vector_Tidy, 0x1441eb)
{
    auto orig = ctx.Original<Dtor>(0x1441eb);
    std::mt19937 rng(0xAB08);
    for (int run = 0; run < 600; ++run) {
        uint32_t count = rng() % 9, cap = count + rng() % 3;
        if (rng() % 5 == 0) count = cap = 0;
        cRZStringVector va = MakeVec(ctx, run, count, cap), vb = MakeVec(ctx, run, count, cap);
        Blocks ba = BlocksOf(va), bb = BlocksOf(vb);
        difftest::BeginFrees();
        orig(&va);
        LiveSet ma = LiveMask(ba);
        difftest::BeginFrees();
        vb.Tidy();
        LiveSet mb = LiveMask(bb);
        ++ctx.cases;
        if (va.mFirst || va.mLast || va.mEnd || vb.mFirst || vb.mLast || vb.mEnd || ma != mb) ctx.Fail("run %d: differs (freed %s / %s)", run, Mask(ma).c_str(), Mask(mb).c_str());
        for (bool m : ma) if (m) ctx.Fail("run %d: original left a block allocated", run);
    }
}

DIFF_TEST(cRZCmdLine_Vector_Clear, 0x1adb0b)
{
    // 0x1adb0b is matched as vector<string>::clear by shape, but in this program it clears a
    // vector<cRZString> (the constructor's parser calls it on the argument list): same code, the
    // element destroyer it calls is the cRZString one.
    auto orig = ctx.Original<Dtor>(0x1adb0b);
    std::mt19937 rng(0xAB09);
    for (int run = 0; run < 600; ++run) {
        uint32_t count = rng() % 9, cap = count + rng() % 3;
        cRZStringVector va = MakeVec(ctx, run, count, cap), vb = MakeVec(ctx, run, count, cap);
        Blocks ba = BlocksOf(va), bb = BlocksOf(vb);
        difftest::BeginFrees();
        orig(&va);
        LiveSet ma = LiveMask(ba);
        difftest::BeginFrees();
        vb.Clear();
        LiveSet mb = LiveMask(bb);
        ++ctx.cases;
        if (va.mLast != va.mFirst || vb.mLast != vb.mFirst || (va.mEnd - va.mFirst) != (vb.mEnd - vb.mFirst) || ma != mb) ctx.Fail("run %d: differs (freed %s / %s)", run, Mask(ma).c_str(), Mask(mb).c_str());
        if (count && ma.size() > 1) {
            bool allFreed = true;
            for (size_t i = 1; i < ma.size(); ++i) allFreed &= !ma[i];
            if (!allFreed) ctx.Fail("run %d: original left an element's string allocated", run);
        }
        FreeVec(va);
        FreeVec(vb);
    }
}

DIFF_TEST(cRZCmdLine_Vector_DestroyRange, 0x141663)
{
    auto orig = ctx.Original<FnDestroyRange>(0x141663);
    std::mt19937 rng(0xAB0A);
    for (int run = 0; run < 600; ++run) {
        uint32_t count = rng() % 9;
        cRZStringVector va = MakeVec(ctx, run, count, count + 1), vb = MakeVec(ctx, run, count, count + 1);
        uint32_t from = count ? rng() % (count + 1) : 0;
        Blocks ba = BlocksOf(va), bb = BlocksOf(vb);
        difftest::BeginFrees();
        orig(va.mFirst + from, va.mLast, nullptr);
        LiveSet ma = LiveMask(ba);
        difftest::BeginFrees();
        cRZStringVector::DestroyRange(vb.mFirst + from, vb.mLast, nullptr);
        LiveSet mb = LiveMask(bb);
        ++ctx.cases;
        if (ma != mb) ctx.Fail("run %d from %u of %u: freed %s / %s", run, from, count, Mask(ma).c_str(), Mask(mb).c_str());
        for (uint32_t i = from; i < count; ++i) {
            if (va.mFirst[i].mVTable != vb.mFirst[i].mVTable || va.mFirst[i].mString.size() != vb.mFirst[i].mString.size()
                || va.mFirst[i].mString.capacity() != vb.mFirst[i].mString.capacity()) {
                ctx.Fail("run %d: destroyed element %u differs", run, i);
            }
        }
        va.mLast = va.mFirst + from; // the destroyed elements must not be destroyed again
        vb.mLast = vb.mFirst + from;
        FreeVec(va);
        FreeVec(vb);
    }
}

DIFF_TEST(cRZCmdLine_Vector_UninitializedCopy, 0x1ac987)
{
    auto orig = ctx.Original<FnUninitCopy>(0x1ac987);
    std::mt19937 rng(0xAB0B);
    for (int run = 0; run < 800; ++run) {
        uint32_t n = rng() % 9;
        cRZStringVector sa = MakeVec(ctx, run, n, n + 1), sb = MakeVec(ctx, run, n, n + 1);
        auto *da = static_cast<cRZString *>(::operator new((n + 1) * sizeof(cRZString)));
        auto *db = static_cast<cRZString *>(::operator new((n + 1) * sizeof(cRZString)));
        std::memset(da, 0xCD, (n + 1) * sizeof(cRZString));
        std::memset(db, 0xCD, (n + 1) * sizeof(cRZString));
        cRZString *ra = orig(sa.mFirst, sa.mLast, da, nullptr);
        cRZString *rb = cRZStringVector::UninitializedCopy(sb.mFirst, sb.mLast, db, nullptr);
        ++ctx.cases;
        cRZStringVector ca{da, ra, da + n + 1}, cb{db, rb, db + n + 1};
        if ((ra - da) != static_cast<ptrdiff_t>(n) || (rb - db) != static_cast<ptrdiff_t>(n) || !SameVec(ca, cb) || !SameVec(sa, sb)) ctx.Fail("n %u: differs", n);
        for (cRZString *p = da; p != ra; ++p) p->Destruct();
        for (cRZString *p = db; p != rb; ++p) p->Destruct();
        ::operator delete(da);
        ::operator delete(db);
        FreeVec(sa);
        FreeVec(sb);
    }
}

DIFF_TEST(cRZCmdLine_Vector_CopyCounted, 0x141633)
{
    auto copyN = ctx.Original<cRZString *(__cdecl *)(const cRZString *, uint32_t, cRZString *)>(0x141633);
    auto uninitN = ctx.Original<cRZString *(__cdecl *)(const cRZString *, uint32_t, cRZString *, const void *)>(0x141a91);
    std::mt19937 rng(0xAB0C);
    for (int run = 0; run < 2000; ++run) {
        uint32_t n = rng() % 10;
        cRZStringVector sa = MakeVec(ctx, run, n, n + 1), sb = MakeVec(ctx, run, n, n + 1);
        if (run % 2 == 0) {
            // copy_n over live elements (they keep their refcounts; the strings are assigned).
            cRZStringVector da = MakeVec(ctx, run + 100000, n + 2, n + 2, 30), db = MakeVec(ctx, run + 100000, n + 2, n + 2, 30);
            uint32_t off = rng() % 3;
            cRZString *ra = copyN(sa.mFirst, n, da.mFirst + off);
            cRZString *rb = cRZStringVector::CopyN(sb.mFirst, n, db.mFirst + off);
            ++ctx.cases;
            if (Index(da, ra) != Index(db, rb) || Index(da, ra) != static_cast<ptrdiff_t>(off + n) || !SameVec(da, db) || !SameVec(sa, sb))
                ctx.Fail("copy_n %u off %u: differs (return %td/%td)", n, off, Index(da, ra), Index(db, rb));
            FreeVec(da);
            FreeVec(db);
        } else {
            // uninitialized_copy_n into raw storage (copies start with refcount 0).
            auto *da = static_cast<cRZString *>(::operator new((n + 1) * sizeof(cRZString)));
            auto *db = static_cast<cRZString *>(::operator new((n + 1) * sizeof(cRZString)));
            std::memset(da, 0xCD, (n + 1) * sizeof(cRZString));
            std::memset(db, 0xCD, (n + 1) * sizeof(cRZString));
            cRZString *ra = uninitN(sa.mFirst, n, da, nullptr);
            cRZString *rb = cRZStringVector::UninitializedCopyN(sb.mFirst, n, db, nullptr);
            ++ctx.cases;
            cRZStringVector ca{da, ra, da + n + 1}, cb{db, rb, db + n + 1};
            if ((ra - da) != static_cast<ptrdiff_t>(n) || (rb - db) != static_cast<ptrdiff_t>(n) || !SameVec(ca, cb) || !SameVec(sa, sb))
                ctx.Fail("uninitialized_copy_n %u: differs", n);
            for (cRZString *p = da; p != ra; ++p) p->Destruct();
            for (cRZString *p = db; p != rb; ++p) p->Destruct();
            ::operator delete(da);
            ::operator delete(db);
        }
        FreeVec(sa);
        FreeVec(sb);
    }
}

DIFF_TEST(cRZCmdLine_Vector_AssignCounted, 0x14155c)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, const cRZString *, uint32_t)>(0x14155c);
    std::mt19937 rng(0xAB0D);
    for (int run = 0; run < 4000; ++run) {
        // All three paths: past the capacity (empty vectors too), past the size, within the size.
        uint32_t count = rng() % 10, cap = count + rng() % 4;
        if (rng() % 6 == 0) count = cap = 0;
        const uint32_t path = rng() % 3;
        uint32_t n = path == 0 ? cap + 1 + rng() % 8 : path == 1 ? count + rng() % (cap - count + 1) : rng() % (count + 1);
        cRZStringVector va = MakeVec(ctx, run, count, cap), vb = MakeVec(ctx, run, count, cap);
        cRZStringVector sa = MakeVec(ctx, run + 200000, n, n + 1), sb = MakeVec(ctx, run + 200000, n, n + 1);
        Blocks ba = BlocksOf(va), bb = BlocksOf(vb);
        difftest::BeginFrees();
        orig(&va, sa.mFirst, n);
        int leakA = LeakedBlock(ba, va);
        difftest::BeginFrees();
        vb.AssignCounted(sb.mFirst, n);
        int leakB = LeakedBlock(bb, vb);
        ++ctx.cases;
        if (!SameVec(va, vb) || va.Count() != static_cast<int32_t>(n) || !SameVec(sa, sb))
            ctx.Fail("count %u cap %u assign %u: size %d/%d capacity %td/%td", count, cap, n, va.Count(), vb.Count(), va.mEnd - va.mFirst,
                vb.mEnd - vb.mFirst);
        for (int32_t i = 0; i < va.Count() && i < static_cast<int32_t>(n); ++i)
            if (va.mFirst[i].mString != sa.mFirst[i].mString) {
                ctx.Fail("count %u cap %u assign %u: element %d not the source's", count, cap, n, i);
                break;
            }
        if (leakA >= 0 || leakB >= 0) ctx.Fail("count %u cap %u assign %u: an old block is left allocated and unused: original %d ours %d", count, cap, n, leakA, leakB);
        FreeVec(va);
        FreeVec(vb);
        FreeVec(sa);
        FreeVec(sb);
    }
    // Past max_size (0x7ffffff elements): length_error before anything changes (the source is never read).
    for (int run = 0; run < 20; ++run) {
        uint32_t count = rng() % 5, cap = count + rng() % 3;
        const uint32_t n = 0x8000000u + rng() % 0x1000;
        cRZStringVector va = MakeVec(ctx, run, count, cap), vb = MakeVec(ctx, run, count, cap);
        cRZStringVector before = va;
        bool threwA = false, threwB = false;
        try {
            orig(&va, nullptr, n);
        } catch (...) { // the game's own length_error type
            threwA = true;
        }
        try {
            vb.AssignCounted(nullptr, n);
        } catch (const std::length_error &) {
            threwB = true;
        }
        ++ctx.cases;
        if (!threwA || !threwB || va.mFirst != before.mFirst || va.mLast != before.mLast || va.mEnd != before.mEnd || !SameVec(va, vb))
            ctx.Fail("assign %x: threw %d/%d", n, threwA, threwB);
        FreeVec(va);
        FreeVec(vb);
    }
}
