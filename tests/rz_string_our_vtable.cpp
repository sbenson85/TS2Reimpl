// cRZString's own vtable (lane QB, docs/standalone-shape.md "Owned statics"; the image-free program's): strings carrying
// rzface::cRZString's vtable, whose 34 slots run our members. Every slot is called by number through each object's
// own vtable: the original's object carries the game's vtable 0xd9b470 (original bodies in the harness, including the
// folded ones: 0x1acd4f ToChar/Data, 0x149d00, 0x18e960 AddRef, 0x1ad030 the reference IsEqual), ours carries ours.
// Interface arguments are a second pair of strings built the same way, so calls our slots make through the other
// string's vtable stay on our side. Compared after every call: the return (al for bool slots; `this`-relative for
// returned strings; the text for returned pointers), both strings' text and counts, and each object's vtable word.
#include "difftest.h"
#include "free_recorder.h"
#include "rz/cRZString.h"

#include <cstdarg>
#include <cstring>
#include <random>
#include <string>

namespace {

constexpr uint32_t kVTable = 0xd9b470;

struct Gen {
    std::mt19937 rng;
    explicit Gen(uint32_t seed) : rng(seed) {}
    std::string Str(int maxLen = 30)
    {
        static const char alphabet[] = {'a', 'A', 'b', 'B', 'z', '0'};
        int len = static_cast<int>(rng() % (maxLen + 1));
        if (rng() % 4 == 0) len = 14 + static_cast<int>(rng() % 4);
        std::string s;
        for (int i = 0; i < len; ++i) s += alphabet[rng() % 6];
        return s;
    }
    uint32_t Below(uint32_t n) { return n ? rng() % n : 0; }
    bool Coin() { return rng() % 2 != 0; }
};

cRZString *New(const void *vtable, const std::string &s, uint32_t refs)
{
    auto *r = new cRZString{};
    r->mVTable = vtable;
    r->mString = s;
    r->mRefCount = refs;
    return r;
}

const void *const *Table(const cRZString *s) { return static_cast<const void *const *>(s->mVTable); }

template <typename R, typename... A> R Call(cRZString *s, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall *)(cRZString *, A...)>(Table(s)[slot])(s, args...);
}

// Slot 32 with a va_list made here, through the object's own vtable.
uint32_t CallVa(cRZString *s, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    uint32_t r = Call<uint32_t>(s, 32, fmt, args);
    va_end(args);
    return r;
}

std::string Show(const std::string &s)
{
    std::string out;
    for (char c : s) out += c ? c : '~';
    return out;
}

} // namespace

DIFF_TEST(cRZString_OurVTable, 0x1acd4f)
{
    const void *game = ctx.image.At(kVTable);
    const void *ours = cRZString::OwnVTable();

    // Ours is our own table: none of its 34 slots is an image address.
    for (int i = 0; i < 34; ++i) {
        ++ctx.cases;
        const uintptr_t at = reinterpret_cast<uintptr_t>(static_cast<const void *const *>(ours)[i]);
        if (at - ctx.image.Base() < ctx.image.Size()) ctx.Fail("our vtable slot %d points into the image", i);
    }

    Gen g(41);
    for (int i = 0; i < 30000; ++i) {
        // The other string: the same text, the same text with some letters' case flipped (equal only ignoring case),
        // or another.
        const std::string text = g.Str();
        std::string otherText = g.Coin() ? text : g.Str();
        if (otherText == text && g.Coin())
            for (char &c : otherText)
                if (g.Coin()) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c >= 'A' && c <= 'Z' ? c + 32 : c);
        const uint32_t refs = 2 + g.Below(3);
        cRZString *a = New(game, text, refs), *b = New(ours, text, refs);
        cRZString *oa = New(game, otherText, 1), *ob = New(ours, otherText, 1);
        const int slot = static_cast<int>(g.Below(34));
        const bool cs = g.Coin();
        const uint32_t size = static_cast<uint32_t>(text.size());
        const uint32_t pos = g.Below(size + 1), len = g.Below(12);
        std::string arg = g.Str(10);
        if (g.Below(3) == 0 && size) arg = text.substr(g.Below(size), 1 + g.Below(4));
        const char *p = arg.c_str();

        enum Kind { None, Bool, Value, Self, Text } kind = Value;
        uint32_t ra = 0, rb = 0;
        switch (slot) {
        case 0: {
            const uint32_t riid = g.Below(3) == 0 ? cRZString::kGZIID_cIGZUnknown : g.Coin() ? cRZString::kGZIID_cIGZString : g.rng();
            void *pa = reinterpret_cast<void *>(1), *pb = reinterpret_cast<void *>(1);
            ra = Call<uint32_t>(a, 0, riid, &pa) & 0xff;
            rb = Call<uint32_t>(b, 0, riid, &pb) & 0xff;
            ++ctx.cases;
            if ((pa == a) != (pb == b) || (pa != a && pa != pb)) ctx.Fail("slot 0 (0x%08x): out pointers differ", riid);
            kind = Value;
            break;
        }
        case 1: case 2: ra = Call<uint32_t>(a, slot); rb = Call<uint32_t>(b, slot); break;
        case 3: Call<void>(a, 3, p, len); Call<void>(b, 3, p, len); kind = None; break;
        case 4: Call<void>(a, 4, p); Call<void>(b, 4, p); kind = None; break;
        case 5: case 6: ra = Call<uint32_t>(a, slot); rb = Call<uint32_t>(b, slot); kind = Text; break;
        case 7: ra = Call<uint32_t>(a, 7); rb = Call<uint32_t>(b, 7); break;
        case 8: ra = Call<uint32_t>(a, 8); rb = Call<uint32_t>(b, 8); kind = Bool; break;
        case 9: ra = Call<uint32_t>(a, 9, p, len, cs); rb = Call<uint32_t>(b, 9, p, len, cs); kind = Bool; break;
        case 10: ra = Call<uint32_t>(a, 10, p, cs); rb = Call<uint32_t>(b, 10, p, cs); kind = Bool; break;
        case 11: case 12: case 15:
            ra = Call<uint32_t>(a, slot, static_cast<const void *>(oa), cs);
            rb = Call<uint32_t>(b, slot, static_cast<const void *>(ob), cs);
            kind = slot == 15 ? Value : Bool;
            break;
        case 13: ra = Call<uint32_t>(a, 13, p, len, cs); rb = Call<uint32_t>(b, 13, p, len, cs); break;
        case 14: ra = Call<uint32_t>(a, 14, p, cs); rb = Call<uint32_t>(b, 14, p, cs); break;
        case 16: case 19:
            ra = Call<uint32_t>(a, slot, static_cast<const void *>(oa));
            rb = Call<uint32_t>(b, slot, static_cast<const void *>(ob));
            kind = Self;
            break;
        case 17: Call<void>(a, 17, static_cast<const void *>(oa)); Call<void>(b, 17, static_cast<const void *>(ob)); kind = None; break;
        case 18: Call<void>(a, 18, len); Call<void>(b, 18, len); kind = None; break;
        case 20: ra = Call<uint32_t>(a, 20, p, len); rb = Call<uint32_t>(b, 20, p, len); kind = Self; break;
        case 21: ra = Call<uint32_t>(a, 21, p); rb = Call<uint32_t>(b, 21, p); kind = Self; break;
        case 22: case 24:
            ra = Call<uint32_t>(a, slot, pos, static_cast<const void *>(oa));
            rb = Call<uint32_t>(b, slot, pos, static_cast<const void *>(ob));
            kind = Self;
            break;
        case 23: case 25: ra = Call<uint32_t>(a, slot, pos, p, len); rb = Call<uint32_t>(b, slot, pos, p, len); kind = Self; break;
        case 26: ra = Call<uint32_t>(a, 26, pos, len); rb = Call<uint32_t>(b, 26, pos, len); kind = Self; break;
        case 27: case 29:
            ra = Call<uint32_t>(a, slot, static_cast<const void *>(oa), pos, cs);
            rb = Call<uint32_t>(b, slot, static_cast<const void *>(ob), pos, cs);
            break;
        case 28: case 30: ra = Call<uint32_t>(a, slot, p, pos, cs); rb = Call<uint32_t>(b, slot, p, pos, cs); break;
        case 31: {
            using Cdecl = uint32_t(__cdecl *)(cRZString *, const char *, ...);
            const int n = static_cast<int>(g.rng());
            ra = reinterpret_cast<Cdecl>(Table(a)[31])(a, "%d<%s>%x", n, p, len);
            rb = reinterpret_cast<Cdecl>(Table(b)[31])(b, "%d<%s>%x", n, p, len);
            kind = Self;
            break;
        }
        case 32: ra = CallVa(a, "[%s|%u]", p, len); rb = CallVa(b, "[%s|%u]", p, len); kind = Self; break;
        default: {
            // Slot 33, the deleting destructor (not an array: bit 1 clear). Flag bit 0 frees the object.
            const uint32_t flags = g.Below(2);
            bool freedA, freedB;
            {
                difftest::FreeRecorder rec;
                ra = Call<uint32_t>(a, 33, flags);
                freedA = rec.Freed(a);
            }
            {
                difftest::FreeRecorder rec;
                rb = Call<uint32_t>(b, 33, flags);
                freedB = rec.Freed(b);
            }
            ++ctx.cases;
            if (ra != reinterpret_cast<uint32_t>(a) || rb != reinterpret_cast<uint32_t>(b)) ctx.Fail("slot 33 (%u): returns differ", flags);
            ++ctx.cases;
            if (freedA != freedB || freedA != (flags == 1)) ctx.Fail("slot 33 (%u): object freed: original %d, ours %d", flags, freedA, freedB);
            if (!flags) {
                ++ctx.cases;
                if (a->mVTable != game || b->mVTable != cRZString::VTable()) ctx.Fail("slot 33: vtable words after destruction");
                ::operator delete(a);
                ::operator delete(b);
            }
            delete oa;
            delete ob;
            continue;
        }
        }

        ++ctx.cases;
        bool same = true;
        switch (kind) {
        case None: break;
        case Bool: same = (ra & 0xff) == (rb & 0xff); break;
        case Value: same = ra == rb; break;
        case Self: same = (ra == reinterpret_cast<uint32_t>(a)) && (rb == reinterpret_cast<uint32_t>(b)); break;
        case Text:
            same = std::strcmp(reinterpret_cast<const char *>(ra), reinterpret_cast<const char *>(rb)) == 0
                && rb == reinterpret_cast<uint32_t>(b->mString.c_str());
            break;
        }
        if (!same) ctx.Fail("slot %d on '%s': original %08x, ours %08x", slot, Show(text).c_str(), ra, rb);
        if (a->mString != b->mString || a->mRefCount != b->mRefCount || oa->mString != ob->mString || oa->mRefCount != ob->mRefCount)
            ctx.Fail("slot %d on '%s': state differs ('%s' vs '%s')", slot, Show(text).c_str(), Show(a->mString).c_str(), Show(b->mString).c_str());
        if (a->mVTable != game || b->mVTable != ours) ctx.Fail("slot %d: vtable words changed", slot);
        delete a;
        delete b;
        delete oa;
        delete ob;
    }

    // Release at a count of 1 ends the string through slot 33 of its own vtable.
    for (int i = 0; i < 200; ++i) {
        const std::string text = g.Str();
        cRZString *a = New(game, text, 1), *b = New(ours, text, 1);
        uint32_t ra = Call<uint32_t>(a, 2), rb = Call<uint32_t>(b, 2);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("last Release: original %u, ours %u", ra, rb);
    }
}
