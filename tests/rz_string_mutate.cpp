// Differential tests for cRZString's mutating, searching and refcounting methods. Each case builds
// two identical objects (both with the original vtable), runs the original method on one and ours
// on the other, then compares return values and the whole objects. Positions stay in range: out of
// range ones throw std::out_of_range in both versions, which the harness can't run for the original.
#include "difftest.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <string>

namespace {

constexpr uint32_t kVTable = 0xd9b470;

struct Gen {
    std::mt19937 rng;
    explicit Gen(uint32_t seed) : rng(seed) {}
    std::string Str(int maxLen = 40)
    {
        static const char alphabet[] = {'a', 'A', 'b', 'B', 'z', 'Z', '0', '\0'};
        int len = std::uniform_int_distribution<int>(0, maxLen)(rng);
        if (rng() % 4 == 0) len = std::uniform_int_distribution<int>(14, 17)(rng);
        std::string s;
        for (int i = 0; i < len; ++i) s += alphabet[rng() % (rng() % 4 == 0 ? 8 : 7)];
        return s;
    }
    uint32_t Below(uint32_t n) { return n ? rng() % n : 0; }
    bool Coin() { return rng() % 2 != 0; }
};

cRZString *New(const difftest::Context &ctx, const std::string &s, uint32_t refs = 1)
{
    auto *r = new cRZString{};
    r->mVTable = ctx.image.At(kVTable);
    r->mString = s;
    r->mRefCount = refs;
    return r;
}

struct Pair {
    cRZString *orig, *ours;
    Pair(const difftest::Context &ctx, const std::string &s, uint32_t refs = 1) : orig(New(ctx, s, refs)), ours(New(ctx, s, refs)) {}
    ~Pair() { delete orig; delete ours; }
    bool Same() const { return orig->mString == ours->mString && orig->mRefCount == ours->mRefCount; }
};

std::string Show(const std::string &s)
{
    std::string out;
    for (char c : s) out += c ? c : '~';
    return out;
}

template <typename Fn> Fn Orig(const difftest::Context &ctx, uint32_t rva) { return ctx.Original<Fn>(rva); }

} // namespace

DIFF_TEST(cRZString_FromChar, 0x1acfbe)
{
    auto withLen = Orig<void(__thiscall *)(void *, const char *, uint32_t)>(ctx, 0x1acfbe);
    auto cstr = Orig<void(__thiscall *)(void *, const char *)>(ctx, 0x1acf84);
    Gen g(1);
    for (int i = 0; i < 20000; ++i) {
        std::string start = g.Str(), src = g.Str();
        bool null = g.Below(10) == 0;
        uint32_t len = g.Below(static_cast<uint32_t>(src.size()) + 1);
        Pair a(ctx, start), b(ctx, start);
        withLen(a.orig, null ? nullptr : src.c_str(), len);
        a.ours->FromChar(null ? nullptr : src.c_str(), len);
        cstr(b.orig, null ? nullptr : src.c_str());
        b.ours->FromChar(null ? nullptr : src.c_str());
        ctx.cases += 2;
        if (!a.Same()) ctx.Fail("FromChar(char*,%u) '%s' <- '%s'", len, Show(start).c_str(), Show(src).c_str());
        if (!b.Same()) ctx.Fail("FromChar(char*) '%s' <- '%s'", Show(start).c_str(), Show(src).c_str());
    }
}

DIFF_TEST(cRZString_CopyAssignResize, 0x1accfe)
{
    auto assign = Orig<void *(__thiscall *)(void *, const void *)>(ctx, 0x1ac9d0);
    auto copy = Orig<void(__thiscall *)(void *, const void *)>(ctx, 0x1accfe);
    auto resize = Orig<void(__thiscall *)(void *, uint32_t)>(ctx, 0x1ad3dc);
    Gen g(2);
    for (int i = 0; i < 20000; ++i) {
        std::string start = g.Str(), src = g.Str();
        cRZString *other = New(ctx, src);
        const cIGZString &o = *other->AsInterface();
        Pair a(ctx, start), b(ctx, start), c(ctx, start);
        void *ret = assign(a.orig, other);
        cRZString *got = a.ours->Assign(o);
        copy(b.orig, other);
        b.ours->Copy(o);
        uint32_t n = g.Below(50);
        resize(c.orig, n);
        c.ours->Resize(n);
        ctx.cases += 3;
        if (!a.Same() || ret != a.orig || got != a.ours) ctx.Fail("operator= '%s' <- '%s'", Show(start).c_str(), Show(src).c_str());
        if (!b.Same()) ctx.Fail("Copy '%s' <- '%s'", Show(start).c_str(), Show(src).c_str());
        if (!c.Same()) ctx.Fail("Resize(%u) '%s'", n, Show(start).c_str());
        if (other->mString != src) ctx.Fail("source string modified");
        delete other;
    }
}

DIFF_TEST(cRZString_AppendInsertReplaceErase, 0x1acabf)
{
    auto appendObj = Orig<void *(__thiscall *)(void *, const void *)>(ctx, 0x1acabf);
    auto appendLen = Orig<void *(__thiscall *)(void *, const char *, uint32_t)>(ctx, 0x1acb13);
    auto appendStr = Orig<void *(__thiscall *)(void *, const char *)>(ctx, 0x1acae6);
    auto insertObj = Orig<void *(__thiscall *)(void *, uint32_t, const void *)>(ctx, 0x1acfde);
    auto insertLen = Orig<void *(__thiscall *)(void *, uint32_t, const char *, uint32_t)>(ctx, 0x1ad009);
    auto replaceObj = Orig<void *(__thiscall *)(void *, uint32_t, const void *)>(ctx, 0x1ad394);
    auto replaceLen = Orig<void *(__thiscall *)(void *, uint32_t, const char *, uint32_t)>(ctx, 0x1ad3bf);
    auto erase = Orig<void *(__thiscall *)(void *, uint32_t, uint32_t)>(ctx, 0x1ace48);
    Gen g(3);
    for (int i = 0; i < 20000; ++i) {
        std::string start = g.Str(), src = g.Str();
        cRZString *other = New(ctx, src);
        const cIGZString &o = *other->AsInterface();
        uint32_t size = static_cast<uint32_t>(start.size());
        uint32_t pos = g.Below(size + 1), len = g.Below(static_cast<uint32_t>(src.size()) + 1);
        uint32_t count = g.Coin() ? g.Below(size + 3) : UINT32_MAX;
        const char *p = src.c_str();

        auto check = [&](const char *what, Pair &x, void *want, cRZString *got) {
            ++ctx.cases;
            if (!x.Same() || want != x.orig || got != x.ours)
                ctx.Fail("%s: '%s' pos %u len %u count %d src '%s': original '%s', ours '%s'", what, Show(start).c_str(), pos, len,
                         (int)count, Show(src).c_str(), Show(x.orig->mString).c_str(), Show(x.ours->mString).c_str());
        };
        { Pair x(ctx, start); check("Append(obj)", x, appendObj(x.orig, other), x.ours->Append(o)); }
        { Pair x(ctx, start); check("Append(char*,len)", x, appendLen(x.orig, p, len), x.ours->Append(p, len)); }
        { Pair x(ctx, start); const char *q = g.Below(10) ? p : nullptr; check("Append(char*)", x, appendStr(x.orig, q), x.ours->Append(q)); }
        { Pair x(ctx, start); check("Insert(obj)", x, insertObj(x.orig, pos, other), x.ours->Insert(pos, o)); }
        { Pair x(ctx, start); check("Insert(char*,len)", x, insertLen(x.orig, pos, p, len), x.ours->Insert(pos, p, len)); }
        { Pair x(ctx, start); check("Replace(obj)", x, replaceObj(x.orig, pos, other), x.ours->Replace(pos, o)); }
        { Pair x(ctx, start); check("Replace(char*,count)", x, replaceLen(x.orig, pos, p, count), x.ours->Replace(pos, p, count)); }
        { Pair x(ctx, start); check("Erase", x, erase(x.orig, pos, count), x.ours->Erase(pos, count)); }
        delete other;
    }
}

DIFF_TEST(cRZString_FindRFind, 0x1ace61)
{
    auto findObj = Orig<int32_t(__thiscall *)(const void *, const void *, uint32_t, bool)>(ctx, 0x1ace61);
    auto findStr = Orig<int32_t(__thiscall *)(const void *, const char *, uint32_t, bool)>(ctx, 0x1aceeb);
    auto rfindObj = Orig<int32_t(__thiscall *)(const void *, const void *, uint32_t, bool)>(ctx, 0x1ad1e4);
    auto rfindStr = Orig<int32_t(__thiscall *)(const void *, const char *, uint32_t, bool)>(ctx, 0x1ad26e);
    Gen g(4);
    for (int i = 0; i < 30000; ++i) {
        std::string hay = g.Str();
        // Needles: often a (case-flipped) piece of the haystack so searches hit.
        std::string needle = g.Str(6);
        if (g.Coin() && !hay.empty()) {
            uint32_t at = g.Below(static_cast<uint32_t>(hay.size()));
            needle = hay.substr(at, 1 + g.Below(5));
            if (g.Coin()) for (auto &c : needle) if (c >= 'A' && c <= 'z' && g.Coin()) c ^= 0x20;
        }
        bool cs = g.Coin();
        uint32_t pos = g.Below(4) == 0 ? UINT32_MAX : g.Below(static_cast<uint32_t>(hay.size()) + 3);
        cRZString *self = New(ctx, hay), *other = New(ctx, needle);
        const cIGZString &o = *other->AsInterface();
        int32_t w, r;
        ctx.cases += 4;
        if ((w = findObj(self, other, pos, cs)) != (r = self->Find(o, pos, cs)))
            ctx.Fail("Find(obj) '%s' in '%s' pos %d cs %d: original %d, ours %d", Show(needle).c_str(), Show(hay).c_str(), (int)pos, cs, w, r);
        if ((w = findStr(self, needle.c_str(), pos, cs)) != (r = self->Find(needle.c_str(), pos, cs)))
            ctx.Fail("Find(char*) '%s' in '%s' pos %d cs %d: original %d, ours %d", Show(needle).c_str(), Show(hay).c_str(), (int)pos, cs, w, r);
        if ((w = rfindObj(self, other, pos, cs)) != (r = self->RFind(o, pos, cs)))
            ctx.Fail("RFind(obj) '%s' in '%s' pos %d cs %d: original %d, ours %d", Show(needle).c_str(), Show(hay).c_str(), (int)pos, cs, w, r);
        if ((w = rfindStr(self, needle.c_str(), pos, cs)) != (r = self->RFind(needle.c_str(), pos, cs)))
            ctx.Fail("RFind(char*) '%s' in '%s' pos %d cs %d: original %d, ours %d", Show(needle).c_str(), Show(hay).c_str(), (int)pos, cs, w, r);
        if (self->mString != hay || other->mString != needle) ctx.Fail("Find modified a string");
        delete self;
        delete other;
    }
}

DIFF_TEST(cRZString_RefCountQueryInterface, 0x1ad1c0)
{
    auto query = Orig<bool(__thiscall *)(void *, uint32_t, void **)>(ctx, 0x1ad1c0);
    auto addRef = Orig<uint32_t(__thiscall *)(void *)>(ctx, 0x18e960);
    auto release = Orig<uint32_t(__thiscall *)(void *)>(ctx, 0x1ad37a);
    Gen g(5);
    for (int i = 0; i < 20000; ++i) {
        uint32_t refs = g.Below(6);
        std::string s = g.Str();
        uint32_t riid = g.Below(3) == 0 ? cRZString::kGZIID_cIGZUnknown : g.Coin() ? cRZString::kGZIID_cIGZString : g.rng();
        {
            Pair x(ctx, s, refs);
            void *pa = reinterpret_cast<void *>(1), *pb = reinterpret_cast<void *>(1);
            bool ra = query(x.orig, riid, &pa), rb = x.ours->QueryInterface(riid, &pb);
            ++ctx.cases;
            if (ra != rb || (pa == x.orig) != (pb == x.ours) || (!ra && (pa != pb)) || !x.Same())
                ctx.Fail("QueryInterface(0x%08x) refs %u: original %d, ours %d", riid, refs, ra, rb);
            uint32_t wa = addRef(x.orig), wb = x.ours->AddRef();
            ++ctx.cases;
            if (wa != wb || !x.Same()) ctx.Fail("AddRef refs %u: original %u, ours %u", refs, wa, wb);
        }
        // Release: at refs <= 1 the object deletes itself through its vtable (slot 33).
        cRZString *a = New(ctx, s, refs), *b = New(ctx, s, refs);
        uint32_t ra = release(a), rb = b->Release();
        ++ctx.cases;
        if (ra != rb) ctx.Fail("Release refs %u: original %u, ours %u", refs, ra, rb);
        if (refs > 1) {
            if (a->mRefCount != b->mRefCount) ctx.Fail("Release refs %u: counts differ", refs);
            delete a;
            delete b;
        }
    }
}

namespace {

// Compares two constructed strings: object bytes when the text is inline (small-string buffer),
// otherwise the visible state (heap pointers differ).
bool SameConstructed(const cRZString &a, const cRZString &b)
{
    if (a.mVTable != b.mVTable || a.mRefCount != b.mRefCount || a.mString != b.mString) return false;
    if (a.mString.capacity() != b.mString.capacity()) return false;
    return a.mString.capacity() > 15 || std::memcmp(&a, &b, sizeof(a)) == 0;
}

} // namespace

DIFF_TEST(cRZString_Constructors, 0x144f5b)
{
    auto ctorDefault = Orig<void *(__thiscall *)(void *)>(ctx, 0x144f5b);
    auto ctorText = Orig<void *(__thiscall *)(void *, const char *)>(ctx, 0x14116d);
    auto ctorOther = Orig<void *(__thiscall *)(void *, const void *)>(ctx, 0x144f2a);
    auto ctorSub = Orig<void *(__thiscall *)(void *, const void *, uint32_t, uint32_t)>(ctx, 0x141eb3);
    auto dtor = Orig<void(__thiscall *)(void *)>(ctx, 0x141193);
    Gen g(6);
    for (int i = 0; i < 20000; ++i) {
        std::string text = g.Str();
        std::string cstr = text.substr(0, text.find('\0'));
        cRZString *source = New(ctx, text);
        alignas(cRZString) unsigned char ra[sizeof(cRZString)], rb[sizeof(cRZString)];
        auto *a = reinterpret_cast<cRZString *>(ra);
        auto *b = reinterpret_cast<cRZString *>(rb);
        int kind = static_cast<int>(g.Below(4));
        std::memset(ra, 0xCC, sizeof(ra));
        std::memset(rb, 0xCC, sizeof(rb));
        void *w = nullptr;
        cRZString *r = nullptr;
        uint32_t pos = g.Below(static_cast<uint32_t>(text.size()) + 1), count = g.Coin() ? UINT32_MAX : g.Below(20);
        switch (kind) {
        case 0: w = ctorDefault(a); r = b->Construct(); break;
        case 1: w = ctorText(a, cstr.c_str()); r = b->Construct(cstr.c_str()); break;
        case 2: w = ctorOther(a, source); r = b->Construct(*source->AsInterface()); break;
        default: w = ctorSub(a, source, pos, count); r = b->Construct(*source, pos, count); break;
        }
        ++ctx.cases;
        if (w != a || r != b || !SameConstructed(*a, *b)) ctx.Fail("constructor %d differs for '%s'", kind, Show(text).c_str());
        dtor(a);
        b->Destruct();
        ++ctx.cases;
        if (a->mVTable != b->mVTable) ctx.Fail("destructor vtable differs");
        delete source;
    }
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}
