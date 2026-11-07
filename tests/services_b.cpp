// Differential tests for cGZRegistry slots 3..7 (Add, RemoveEmpty, Lookup, Format, Enumerate).
// Each test builds a registry with the original constructor and one with ours (the constructor hook's adapter, lane KD),
// runs the original on the first and ours through its own vtable slot on the second with the same arguments, and compares
// the whole tree (shape, colours, strings) and everything the operations report to their arguments.
#include "difftest.h"
#include "gz/cGZRegistry.h"

#include <cctype>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kCtor = 0xc6cb27, kAdd = 0xc6ce2f, kRemoveEmpty = 0xc6d06e, kLookup = 0xc6ccb0, kFormat = 0xc6cd72, kEnumerate = 0xc6cbf3;

// The separators are game globals built by static initialisers the harness doesn't run.
void InitSeparators(difftest::Context &ctx)
{
    ctx.image.At<cRZString>(cGZRegistry::kSchemeSeparatorRva)->Construct("://");
    ctx.image.At<cRZString>(cGZRegistry::kPathSeparatorRva)->Construct("/");
}

struct Pair {
    cGZRegistry *a;
    cGZRegistry *b;
};

Pair MakePair(difftest::Context &ctx)
{
    Pair p{static_cast<cGZRegistry *>(::operator new(sizeof(cGZRegistry))), static_cast<cGZRegistry *>(::operator new(sizeof(cGZRegistry)))};
    std::memset(p.a, 0xCD, sizeof(*p.a));
    std::memset(p.b, 0xCD, sizeof(*p.b));
    ctx.Original<cGZRegistry *(__thiscall *)(void *)>(kCtor)(p.a);
    cGZRegistryAdapters::Construct(p.b, nullptr);
    return p;
}

// Each side's deleting destructor through its own +8 vtable (slot 3), flags 1.
void FreePair(Pair &p)
{
    using Deleting = void *(__thiscall *)(void *, uint32_t);
    for (cGZRegistry *r : {p.a, p.b}) {
        void *face = reinterpret_cast<char *>(r) + 8;
        reinterpret_cast<Deleting>((*reinterpret_cast<void ***>(face))[3])(face, 1);
    }
}

// Slot `slot` of our registry's primary vtable (lane KD: our side is called through its own vtable, in the slot where the
// game's vtable holds the original).
template <class Fn> Fn OurSlot(cGZRegistry *r, int slot) { return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(r))[slot]); }

bool SameString(const cRZString &a, const cRZString &b)
{
    return a.mVTable == b.mVTable && a.mString == b.mString && a.mRefCount == b.mRefCount;
}

bool SameNode(const cGZRegistryNode *a, const cGZRegistryNode *b, const cGZRegistryNode *headA, const cGZRegistryNode *headB)
{
    if ((a == headA) != (b == headB)) return false;
    if (a == headA) return true;
    if (a->mColor != b->mColor || a->mIsNil != b->mIsNil) return false;
    if (!SameString(a->mKey, b->mKey) || !SameString(a->mFirst, b->mFirst) || !SameString(a->mSecond, b->mSecond)) return false;
    if ((a->mLeft != headA && a->mLeft->mParent != a) || (a->mRight != headA && a->mRight->mParent != a)) return false;
    if ((b->mLeft != headB && b->mLeft->mParent != b) || (b->mRight != headB && b->mRight->mParent != b)) return false;
    return SameNode(a->mLeft, b->mLeft, headA, headB) && SameNode(a->mRight, b->mRight, headA, headB);
}

bool SameRegistry(const cGZRegistry &a, const cGZRegistry &b)
{
    const cGZRegistryNode *ha = a.mTree.mHead, *hb = b.mTree.mHead;
    if (a.mTree.mSize != b.mTree.mSize) return false;
    // The head's links: root, leftmost and rightmost nodes, in the same positions.
    auto position = [](const cGZRegistryNode *root, const cGZRegistryNode *head, const cGZRegistryNode *target) {
        // Index of `target` in an in-order walk, or -1 for the head itself.
        if (target == head) return -1;
        int index = 0;
        std::vector<const cGZRegistryNode *> stack;
        const cGZRegistryNode *n = root;
        while (n != head || !stack.empty()) {
            while (n != head) {
                stack.push_back(n);
                n = n->mLeft;
            }
            n = stack.back();
            stack.pop_back();
            if (n == target) return index;
            ++index;
            n = n->mRight;
        }
        return -2;
    };
    if (ha->mColor != hb->mColor || ha->mIsNil != hb->mIsNil) return false;
    if (position(ha->mParent, ha, ha->mLeft) != position(hb->mParent, hb, hb->mLeft)) return false;
    if (position(ha->mParent, ha, ha->mRight) != position(hb->mParent, hb, hb->mRight)) return false;
    if ((ha->mParent == ha) != (hb->mParent == hb)) return false;
    return SameNode(ha->mParent, hb->mParent, ha, hb);
}

const char *kSchemes[] = {"http", "https", "ftp", "mailto", "a", "", "averyveryverylongschemename"};
const char *kHosts[] = {"example", "h", "", "a-quite-long-host-name.example.org", "x"};
const char *kPaths[] = {"p", "", "some/more", "longpathsegment12345678", "q"};

template <typename T, size_t N> const T &Pick(std::mt19937 &rng, const T (&list)[N]) { return list[rng() % N]; }

std::string RandomUrl(std::mt19937 &rng)
{
    switch (rng() % 12) {
    case 0: return "plainstringwithoutscheme";   // no "://": the remainder starts at index 2
    case 1: return std::string("http://ho") + '\0' + "st/path"; // the host stops at the NUL
    case 2: return std::string(Pick(rng, kSchemes)) + "://" + Pick(rng, kHosts); // no "/" after the host
    case 3: return std::string(Pick(rng, kSchemes)) + "://" + Pick(rng, kHosts) + "/";
    case 4: return "://" + std::string(Pick(rng, kHosts)) + "/" + Pick(rng, kPaths); // empty scheme
    default: return std::string(Pick(rng, kSchemes)) + "://" + Pick(rng, kHosts) + "/" + Pick(rng, kPaths) + (rng() % 3 == 0 ? "/tail" : "");
    }
}

// An output object: a cIGZString-shaped object whose slots 3 and 4 (FromChar with and without a
// length) record what they are given; its other slots are the game's.
struct Out {
    void *vtable;
    std::string text;
    uint32_t calls = 0;
    uint32_t lastLen = 0;
    bool withLen = false;
};

void *gOutVTable[34];

void __fastcall FakeFromCharLen(Out *self, void *, const char *src, uint32_t len)
{
    self->text.assign(src, len);
    self->lastLen = len;
    self->withLen = true;
    ++self->calls;
}

void __fastcall FakeFromChar(Out *self, void *, const char *src)
{
    self->text.assign(src);
    self->withLen = false;
    ++self->calls;
}

void InitOutVTable(difftest::Context &ctx)
{
    std::memcpy(gOutVTable, ctx.image.At(cRZString::kVTable), sizeof(gOutVTable));
    gOutVTable[3] = reinterpret_cast<void *>(&FakeFromCharLen);
    gOutVTable[4] = reinterpret_cast<void *>(&FakeFromChar);
}

bool SameOut(const Out &a, const Out &b) { return a.text == b.text && a.calls == b.calls && a.lastLen == b.lastLen && a.withLen == b.withLen; }

// Adds the same urls to both registries (original on a, ours on b).
void AddBoth(difftest::Context &ctx, Pair &p, const std::string &url, const char *what)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, const cIGZString *)>(kAdd);
    cGZRegistryString urlA, urlB;
    urlA.Str().mString = url;
    urlB.Str().mString = url;
    bool ra = orig(p.a, urlA.Str().AsInterface());
    bool rb = OurSlot<bool(__thiscall *)(void *, const cIGZString *)>(p.b, 3)(p.b, urlB.Str().AsInterface());
    ++ctx.cases;
    if (ra != rb || ra != true) ctx.Fail("%s: Add(%s) returned %d/%d", what, url.c_str(), ra, rb);
    if (!SameRegistry(*p.a, *p.b)) ctx.Fail("%s: trees differ after Add(%s) (size %u/%u)", what, url.c_str(), p.a->mTree.mSize, p.b->mTree.mSize);
}

} // namespace

DIFF_TEST(cGZRegistry_Add, 0xc6ce2f)
{
    InitSeparators(ctx);
    std::mt19937 rng(0xADD);
    for (int run = 0; run < 80; ++run) {
        Pair p = MakePair(ctx);
        int count = 1 + rng() % 14;
        for (int i = 0; i < count; ++i) {
            std::string url = RandomUrl(rng);
            AddBoth(ctx, p, url, "random");
        }
        FreePair(p);
    }
}

DIFF_TEST(cGZRegistry_RemoveEmpty, 0xc6d06e)
{
    InitSeparators(ctx);
    auto orig = ctx.Original<bool(__thiscall *)(void *, void *)>(kRemoveEmpty);
    std::mt19937 rng(0xDE1);
    for (int run = 0; run < 60; ++run) {
        Pair p = MakePair(ctx);
        int count = rng() % 12;
        for (int i = 0; i < count; ++i) AddBoth(ctx, p, RandomUrl(rng), "setup");
        // Entries with an empty key (some with several, of equal key); the call needs one, or it
        // erases the head node (ORIGINAL-BUG), which the test avoids.
        int empties = 1 + rng() % 3;
        for (int i = 0; i < empties; ++i) AddBoth(ctx, p, "://" + std::string(Pick(rng, kHosts)) + "/" + Pick(rng, kPaths), "empty key");
        for (int i = 0; i < 3; ++i) AddBoth(ctx, p, RandomUrl(rng), "setup");
        for (int times = 0; times < empties; ++times) {
            void *argument = reinterpret_cast<void *>(0x1000u * (rng() % 5)); // never read
            bool ra = orig(p.a, argument);
            bool rb = OurSlot<bool(__thiscall *)(void *, void *)>(p.b, 4)(p.b, argument);
            ++ctx.cases;
            if (ra != rb) ctx.Fail("run %d: RemoveEmpty returned %d/%d", run, ra, rb);
            if (!SameRegistry(*p.a, *p.b)) ctx.Fail("run %d: trees differ after RemoveEmpty #%d (size %u/%u)", run, times, p.a->mTree.mSize, p.b->mTree.mSize);
        }
        FreePair(p);
    }
}

DIFF_TEST(cGZRegistry_Lookup, 0xc6ccb0)
{
    InitSeparators(ctx);
    InitOutVTable(ctx);
    auto orig = ctx.Original<bool(__thiscall *)(void *, const char *, const cIGZString *, void *)>(kLookup);
    std::mt19937 rng(0x100C);
    for (int run = 0; run < 60; ++run) {
        Pair p = MakePair(ctx);
        int count = rng() % 16;
        for (int i = 0; i < count; ++i) AddBoth(ctx, p, RandomUrl(rng), "setup");
        for (int q = 0; q < 14; ++q) {
            const char *scheme = q == 0 ? nullptr : Pick(rng, kSchemes);
            cGZRegistryString host(Pick(rng, kHosts));
            // Upper-case queries: the comparison is case sensitive.
            if (rng() % 4 == 0) for (char &c : host.Str().mString) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            Out outA{gOutVTable}, outB{gOutVTable};
            outA.text = outB.text = "untouched";
            bool ra = orig(p.a, scheme, host.Str().AsInterface(), &outA);
            bool rb = OurSlot<bool(__thiscall *)(void *, const char *, const cIGZString *, void *)>(p.b, 5)(p.b, scheme, host.Str().AsInterface(), &outB);
            ++ctx.cases;
            if (ra != rb || !SameOut(outA, outB)) {
                ctx.Fail("run %d: Lookup(%s, %s) -> %d/%d, out '%s'/'%s' (%u/%u calls)", run, scheme ? scheme : "null", host.Str().mString.c_str(), ra, rb, outA.text.c_str(), outB.text.c_str(), outA.calls, outB.calls);
            }
        }
        FreePair(p);
    }
}

DIFF_TEST(cGZRegistry_Format, 0xc6cd72)
{
    InitSeparators(ctx);
    InitOutVTable(ctx);
    auto orig = ctx.Original<bool(__thiscall *)(void *, const char *, const cIGZString *, const cIGZString *, void *)>(kFormat);
    std::mt19937 rng(0xF0A);
    Pair p = MakePair(ctx);
    for (int run = 0; run < 120; ++run) {
        const char *scheme = run % 10 == 0 ? nullptr : Pick(rng, kSchemes);
        cGZRegistryString host(Pick(rng, kHosts)), path(Pick(rng, kPaths));
        Out outA{gOutVTable}, outB{gOutVTable};
        outA.text = outB.text = "untouched";
        bool ra = orig(p.a, scheme, host.Str().AsInterface(), path.Str().AsInterface(), &outA);
        bool rb = OurSlot<bool(__thiscall *)(void *, const char *, const cIGZString *, const cIGZString *, void *)>(p.b, 6)(p.b, scheme, host.Str().AsInterface(), path.Str().AsInterface(), &outB);
        ++ctx.cases;
        if (ra != rb || !SameOut(outA, outB)) ctx.Fail("Format(%s, %s, %s) -> %d/%d, out '%s'/'%s'", scheme ? scheme : "null", host.Str().mString.c_str(), path.Str().mString.c_str(), ra, rb, outA.text.c_str(), outB.text.c_str());
        if (scheme && ra && outA.text != std::string(scheme) + "://" + host.Str().mString + "/" + path.Str().mString) ctx.Fail("original's Format text '%s' is not scheme://host/path", outA.text.c_str());
    }
    FreePair(p);
}

namespace {

struct Seen {
    std::vector<std::string> log;
};

void __cdecl RecordingCallback(const cIGZString *first, const cIGZString *second, void *userData)
{
    auto *seen = static_cast<Seen *>(userData);
    seen->log.push_back(std::string(first->Data()) + "|" + second->Data());
}

} // namespace

DIFF_TEST(cGZRegistry_Enumerate, 0xc6cbf3)
{
    InitSeparators(ctx);
    auto orig = ctx.Original<bool(__thiscall *)(void *, const char *, cGZRegistry::EnumerateCallback, void *)>(kEnumerate);
    std::mt19937 rng(0xE11);
    for (int run = 0; run < 60; ++run) {
        Pair p = MakePair(ctx);
        int count = rng() % 18;
        for (int i = 0; i < count; ++i) AddBoth(ctx, p, RandomUrl(rng), "setup");
        for (int q = 0; q < 12; ++q) {
            const char *scheme = Pick(rng, kSchemes);
            // Null callback or user data: returns false without looking.
            cGZRegistry::EnumerateCallback callback = q == 1 ? nullptr : &RecordingCallback;
            Seen seenA, seenB;
            void *userA = q == 2 ? nullptr : &seenA, *userB = q == 2 ? nullptr : &seenB;
            bool ra = orig(p.a, scheme, callback, userA);
            bool rb = OurSlot<bool(__thiscall *)(void *, const char *, cGZRegistry::EnumerateCallback, void *)>(p.b, 7)(p.b, scheme, callback, userB);
            ++ctx.cases;
            if (ra != rb || seenA.log != seenB.log) ctx.Fail("run %d: Enumerate(%s) -> %d/%d, %zu/%zu callbacks", run, scheme, ra, rb, seenA.log.size(), seenB.log.size());
        }
        FreePair(p);
    }
}
