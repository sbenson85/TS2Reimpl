// Differential tests for src/ui/GZWinControls.*: cGZWinListBox and cGZWinTextEdit, their subobjects at +0xf8, and
// their QueryInterface. Each case builds the same random object bytes twice, runs the original on one copy and ours
// on the other, and compares the result, the log of virtual calls (slot, object, arguments), and the object bytes.
//
// Each object is a 0x400-byte block: the primary object at +0, the subobject (its own vtable) at +0xf8. The vtables
// are copies of the original's with the slots the functions call replaced by recording fakes. The fakes take
// __fastcall parameters, so they pop their stack arguments the way a thiscall callee does. Objects are tagged by
// address (primary = 1, subobject = 2) so the logs compare equal across the two copies.
#include "difftest.h"
#include "gz/cGZWin.h"
#include "ui/GZWinControls.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

struct Event {
    int slot;
    int tag;
    uint32_t a, b;
    bool operator==(const Event &o) const { return slot == o.slot && tag == o.tag && a == o.a && b == o.b; }
};

struct TagEntry {
    const void *object;
    int tag;
};

std::vector<Event> gLog;
std::vector<TagEntry> gTags;

int TagOf(const void *self)
{
    for (const TagEntry &t : gTags)
        if (t.object == self) return t.tag;
    return -1;
}

void Record(int slot, const void *self, uint32_t a = 0, uint32_t b = 0)
{
    gLog.push_back({slot, TagOf(self), a, b});
}

constexpr uint32_t kSub = 0xf8; // the subobject's offset in the object
constexpr size_t kBlock = 0x400;

// Results the fakes return for the next run (set identically before the original and before ours).
uint32_t gRet105 = 0;
uint32_t gRet65 = 0;
uint32_t gRet143 = 0;
bool gRet16 = false;
uint32_t gRet16Out = 0;

uint32_t __fastcall FakeAddRef(void *self, void *)
{
    Record(1, self);
    return 2;
}
uint32_t __fastcall FakeSlot105(void *self, void *)
{
    Record(105, self);
    return gRet105;
}
uint32_t __fastcall FakeSub65(void *self, void *, uint32_t index, uint32_t value)
{
    Record(65, self, index, value);
    return gRet65;
}
uint32_t __fastcall FakeSlot143(void *self, void *, uint32_t value)
{
    Record(143, self, value);
    return gRet143;
}
// Sometimes the fake leaves *out alone (gRet16Write false), so the caller's initial value of the local is what the
// slot-143 call receives: the original's starting value must match ours.
bool gRet16Write = true;
bool __fastcall FakeSub16(void *self, void *, uint32_t value, uint32_t *out)
{
    Record(16, self, value);
    if (gRet16Write) *out = gRet16Out;
    return gRet16;
}
void __fastcall FakeSub83(void *self, void *, uint32_t value)
{
    Record(83, self, value);
}

// cGZWinListBox: primary slot 1 (AddRef) and 105; subobject slot 65.
struct ListVt {
    void *primary[190];
    void *sub[72];
    explicit ListVt(const difftest::Context &ctx)
    {
        std::memcpy(primary, ctx.image.At(cGZWinListBox::kPrimaryVTable), sizeof(primary));
        std::memcpy(sub, ctx.image.At(cGZWinListBox::kSubobjectVTable), sizeof(sub));
        primary[1] = reinterpret_cast<void *>(&FakeAddRef);
        primary[105] = reinterpret_cast<void *>(&FakeSlot105);
        sub[65] = reinterpret_cast<void *>(&FakeSub65);
    }
};

// cGZWinTextEdit: primary slots 1, 105 and 143; subobject slots 16 and 83.
struct TextEditVt {
    void *primary[178];
    void *sub[100];
    explicit TextEditVt(const difftest::Context &ctx)
    {
        std::memcpy(primary, ctx.image.At(cGZWinTextEdit::kPrimaryVTable), sizeof(primary));
        std::memcpy(sub, ctx.image.At(cGZWinTextEdit::kSubobjectVTable), sizeof(sub));
        primary[1] = reinterpret_cast<void *>(&FakeAddRef);
        primary[105] = reinterpret_cast<void *>(&FakeSlot105);
        primary[143] = reinterpret_cast<void *>(&FakeSlot143);
        sub[16] = reinterpret_cast<void *>(&FakeSub16);
        sub[83] = reinterpret_cast<void *>(&FakeSub83);
    }
};

// Fills a block with random bytes and installs the two vtable pointers (primary at +0, subobject at +0xf8).
void Randomize(std::mt19937 &rng, uint8_t *mem, void *const *primVt, void *const *subVt)
{
    for (size_t k = 0; k < kBlock; ++k) mem[k] = static_cast<uint8_t>(rng());
    std::memcpy(mem, &primVt, sizeof(primVt));
    std::memcpy(mem + kSub, &subVt, sizeof(subVt));
}

void Set32(uint8_t *mem, uint32_t offset, uint32_t value)
{
    std::memcpy(mem + offset, &value, sizeof(value));
}

// Interesting values for field checks: the edges plus random bits.
uint32_t Edge(std::mt19937 &rng, int i)
{
    static const uint32_t kEdges[] = {0u, 1u, 0xffffffffu, 0x7fffffffu, 0x80000000u, 0x0000ffffu, 0xff000000u, 0x4000u};
    if (i < static_cast<int>(sizeof(kEdges) / sizeof(kEdges[0])) && i % 2 == 0) return kEdges[i];
    return static_cast<uint32_t>(rng());
}

// The result of a QueryInterface's *ppv as an offset from the object (0xffffffff for null).
uint32_t RelPtr(const void *ptr, const uint8_t *object)
{
    if (!ptr) return 0xffffffffu;
    return static_cast<uint32_t>(static_cast<const uint8_t *>(ptr) - object);
}

// The riids the base cGZWin::QueryInterface stores `this` for (src/rz/QueryInterfaces.h). A null ppv with one of
// these writes through null in the base, so the null-ppv cases avoid them.
bool IsBaseId(uint32_t riid)
{
    return riid == 1 || riid == 0x22ba0121 || riid == 0xe98b2f57;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// cGZWinListBox::QueryInterface, 0x9555e8. riid 0x4132242b stores this + 0xf8 (or null) and AddRefs; a null ppv
// fails with no write. Any other riid goes to the base (0x9325d8).

DIFF_TEST(cGZWinListBox_QueryInterface, 0x9555e8)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0x9555e8);
    ListVt vt(ctx);
    std::mt19937 rng(9401);
    static const uint32_t kRiids[] = {0x4132242bu, 0x4132242bu, 1u, 0x22ba0121u, 0xe98b2f57u, 0x12345678u};
    for (int i = 0; i < 500; ++i) {
        uint8_t a[kBlock], b[kBlock];
        Randomize(rng, a, vt.primary, vt.sub);
        std::memcpy(b, a, sizeof(a));
        uint32_t riid = (i % 3 == 0) ? kRiids[rng() % 6] : kRiids[rng() % 2];
        if (i % 7 == 0) riid = static_cast<uint32_t>(rng());
        bool nullPtr = (rng() % 5) == 0 && !IsBaseId(riid);

        void *outA = nullptr, *outB = nullptr;
        gTags = {{a, 1}, {a + kSub, 2}};
        gLog.clear();
        uint8_t ra = orig(a, riid, nullPtr ? nullptr : &outA);
        std::vector<Event> la = gLog;

        gTags = {{b, 1}, {b + kSub, 2}};
        gLog.clear();
        bool rb = reinterpret_cast<cGZWinListBox *>(b)->QueryInterface(riid, nullPtr ? nullptr : &outB);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if ((ra & 0xff) != (rb ? 1 : 0)) ctx.Fail("QueryInterface: al original %u, ours %d", ra, rb ? 1 : 0);
        if (RelPtr(outA, a) != RelPtr(outB, b)) ctx.Fail("QueryInterface: stored pointer differs");
        if (!(la == lb)) ctx.Fail("QueryInterface: calls differ (original %zu, ours %zu)", la.size(), lb.size());
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("QueryInterface: object changed");
    }
}

// ---------------------------------------------------------------------------------------------------------
// cGZWinListBoxAt248::vf248_36, 0x95428e: +0x60 = arg1, +0x61 = arg2 (bytes); al = 1.

DIFF_TEST(cGZWinListBox_vf248_36, 0x95428e)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint8_t, uint8_t)>(0x95428e);
    ListVt vt(ctx);
    std::mt19937 rng(9402);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[kBlock], b[kBlock];
        Randomize(rng, a, vt.primary, vt.sub);
        std::memcpy(b, a, sizeof(a));
        uint8_t x = static_cast<uint8_t>(rng()), y = static_cast<uint8_t>(rng());
        gTags = {{a, 1}, {a + kSub, 2}};
        gLog.clear();
        uint8_t ra = orig(a + kSub, x, y);
        std::vector<Event> la = gLog;

        gTags = {{b, 1}, {b + kSub, 2}};
        gLog.clear();
        bool rb = reinterpret_cast<cGZWinListBoxAt248 *>(b + kSub)->Vf36(x, y);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if ((ra & 0xff) != (rb ? 1 : 0)) ctx.Fail("Vf36: al original %u, ours %d", ra, rb ? 1 : 0);
        if (!(la == lb)) ctx.Fail("Vf36: calls differ");
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("Vf36: object changed");
    }
}

// ---------------------------------------------------------------------------------------------------------
// cGZWinListBoxAt248::vf248_37, 0x9541ca: *arg1 = +0x60, *arg2 = +0x61 (one byte each); al = 1.

DIFF_TEST(cGZWinListBox_vf248_37, 0x9541ca)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint8_t *, uint8_t *)>(0x9541ca);
    ListVt vt(ctx);
    std::mt19937 rng(9403);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[kBlock], b[kBlock];
        Randomize(rng, a, vt.primary, vt.sub);
        std::memcpy(b, a, sizeof(a));
        uint8_t oa[2], ob[2];
        for (int k = 0; k < 2; ++k) oa[k] = ob[k] = static_cast<uint8_t>(rng());

        gTags = {{a, 1}, {a + kSub, 2}};
        gLog.clear();
        uint8_t ra = orig(a + kSub, &oa[0], &oa[1]);
        std::vector<Event> la = gLog;

        gTags = {{b, 1}, {b + kSub, 2}};
        gLog.clear();
        bool rb = reinterpret_cast<cGZWinListBoxAt248 *>(b + kSub)->Vf37(&ob[0], &ob[1]);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if ((ra & 0xff) != (rb ? 1 : 0)) ctx.Fail("Vf37: al original %u, ours %d", ra, rb ? 1 : 0);
        if (oa[0] != ob[0] || oa[1] != ob[1]) ctx.Fail("Vf37: outputs differ");
        if (!(la == lb)) ctx.Fail("Vf37: calls differ");
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("Vf37: object changed");
    }
}

// ---------------------------------------------------------------------------------------------------------
// cGZWinListBoxAt248::vf248_44, 0x954276: returns this->vf248_65(index, 0xdeadbeef) (eax passed through).

DIFF_TEST(cGZWinListBox_vf248_44, 0x954276)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x954276);
    ListVt vt(ctx);
    std::mt19937 rng(9404);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[kBlock], b[kBlock];
        Randomize(rng, a, vt.primary, vt.sub);
        std::memcpy(b, a, sizeof(a));
        uint32_t index = Edge(rng, i);
        gRet65 = static_cast<uint32_t>(rng());

        gTags = {{a, 1}, {a + kSub, 2}};
        gLog.clear();
        uint32_t ra = orig(a + kSub, index);
        std::vector<Event> la = gLog;

        gTags = {{b, 1}, {b + kSub, 2}};
        gLog.clear();
        uint32_t rb = reinterpret_cast<cGZWinListBoxAt248 *>(b + kSub)->Vf44(index);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if (ra != rb) ctx.Fail("Vf44: eax original %x, ours %x", ra, rb);
        if (!(la == lb)) ctx.Fail("Vf44: calls differ (original %zu, ours %zu)", la.size(), lb.size());
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("Vf44: object changed");
    }
}

// ---------------------------------------------------------------------------------------------------------
// cGZWinListBoxAt248::vf248_52 (0x955ed0) and vf248_54 (0x955c39): one byte stored at +0x45 / +0x46, the other
// byte cleared, then the primary's slot 105 (eax passed through).

DIFF_TEST(cGZWinListBox_vf248_52, 0x955ed0)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint8_t)>(0x955ed0);
    ListVt vt(ctx);
    std::mt19937 rng(9405);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[kBlock], b[kBlock];
        Randomize(rng, a, vt.primary, vt.sub);
        std::memcpy(b, a, sizeof(a));
        uint8_t value = static_cast<uint8_t>(rng());
        gRet105 = static_cast<uint32_t>(rng());

        gTags = {{a, 1}, {a + kSub, 2}};
        gLog.clear();
        uint32_t ra = orig(a + kSub, value);
        std::vector<Event> la = gLog;

        gTags = {{b, 1}, {b + kSub, 2}};
        gLog.clear();
        uint32_t rb = reinterpret_cast<cGZWinListBoxAt248 *>(b + kSub)->Vf52(value);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if (ra != rb) ctx.Fail("Vf52: eax original %x, ours %x", ra, rb);
        if (!(la == lb)) ctx.Fail("Vf52: calls differ (original %zu, ours %zu)", la.size(), lb.size());
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("Vf52: object changed");
    }
}

DIFF_TEST(cGZWinListBox_vf248_54, 0x955c39)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint8_t)>(0x955c39);
    ListVt vt(ctx);
    std::mt19937 rng(9406);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[kBlock], b[kBlock];
        Randomize(rng, a, vt.primary, vt.sub);
        std::memcpy(b, a, sizeof(a));
        uint8_t value = static_cast<uint8_t>(rng());
        gRet105 = static_cast<uint32_t>(rng());

        gTags = {{a, 1}, {a + kSub, 2}};
        gLog.clear();
        uint32_t ra = orig(a + kSub, value);
        std::vector<Event> la = gLog;

        gTags = {{b, 1}, {b + kSub, 2}};
        gLog.clear();
        uint32_t rb = reinterpret_cast<cGZWinListBoxAt248 *>(b + kSub)->Vf54(value);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if (ra != rb) ctx.Fail("Vf54: eax original %x, ours %x", ra, rb);
        if (!(la == lb)) ctx.Fail("Vf54: calls differ (original %zu, ours %zu)", la.size(), lb.size());
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("Vf54: object changed");
    }
}

// ---------------------------------------------------------------------------------------------------------
// cGZWinTextEdit::QueryInterface, 0x95079a. riid 0x231a1c57 stores this + 0xf8 and AddRefs (no null check); any
// other riid goes to the base. The null-ppv cases use only ids the base answers with false.

DIFF_TEST(cGZWinTextEdit_QueryInterface, 0x95079a)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0x95079a);
    TextEditVt vt(ctx);
    std::mt19937 rng(9411);
    static const uint32_t kRiids[] = {0x231a1c57u, 0x231a1c57u, 1u, 0x22ba0121u, 0xe98b2f57u, 0x12345678u};
    for (int i = 0; i < 500; ++i) {
        uint8_t a[kBlock], b[kBlock];
        Randomize(rng, a, vt.primary, vt.sub);
        std::memcpy(b, a, sizeof(a));
        uint32_t riid = (i % 3 == 0) ? kRiids[rng() % 6] : kRiids[rng() % 2];
        if (i % 7 == 0) riid = static_cast<uint32_t>(rng());
        // The matching id is never tested with null ppv (the original writes through it).
        bool nullPtr = (rng() % 5) == 0 && !IsBaseId(riid) && riid != 0x231a1c57u;

        void *outA = nullptr, *outB = nullptr;
        gTags = {{a, 1}, {a + kSub, 2}};
        gLog.clear();
        uint8_t ra = orig(a, riid, nullPtr ? nullptr : &outA);
        std::vector<Event> la = gLog;

        gTags = {{b, 1}, {b + kSub, 2}};
        gLog.clear();
        bool rb = reinterpret_cast<cGZWinTextEdit *>(b)->QueryInterface(riid, nullPtr ? nullptr : &outB);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if ((ra & 0xff) != (rb ? 1 : 0)) ctx.Fail("TE QueryInterface: al original %u, ours %d", ra, rb ? 1 : 0);
        if (RelPtr(outA, a) != RelPtr(outB, b)) ctx.Fail("TE QueryInterface: stored pointer differs");
        if (!(la == lb)) ctx.Fail("TE QueryInterface: calls differ (original %zu, ours %zu)", la.size(), lb.size());
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("TE QueryInterface: object changed");
    }
}

// ---------------------------------------------------------------------------------------------------------
// cGZWinTextEditAt248::vf248_14, 0x94f344: slot 16 (value, &local) with local = 0xff000000; when it returns true,
// *out = the primary's slot 143 (local); al = that result.

DIFF_TEST(cGZWinTextEdit_vf248_14, 0x94f344)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t *)>(0x94f344);
    TextEditVt vt(ctx);
    std::mt19937 rng(9412);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[kBlock], b[kBlock];
        Randomize(rng, a, vt.primary, vt.sub);
        std::memcpy(b, a, sizeof(a));
        uint32_t value = Edge(rng, i);
        gRet16 = (rng() % 4) != 0;
        gRet16Write = (rng() % 3) != 0;
        gRet16Out = static_cast<uint32_t>(rng());
        gRet143 = static_cast<uint32_t>(rng());
        uint32_t outA = static_cast<uint32_t>(rng()), outB = outA;

        gTags = {{a, 1}, {a + kSub, 2}};
        gLog.clear();
        uint8_t ra = orig(a + kSub, value, &outA);
        std::vector<Event> la = gLog;

        gTags = {{b, 1}, {b + kSub, 2}};
        gLog.clear();
        bool rb = reinterpret_cast<cGZWinTextEditAt248 *>(b + kSub)->Vf14(value, &outB);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if ((ra & 0xff) != (rb ? 1 : 0)) ctx.Fail("Vf14: al original %u, ours %d", ra, rb ? 1 : 0);
        if (outA != outB) ctx.Fail("Vf14: *out original %x, ours %x", outA, outB);
        if (!(la == lb)) ctx.Fail("Vf14: calls differ (original %zu, ours %zu)", la.size(), lb.size());
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("Vf14: object changed");
    }
}

// ---------------------------------------------------------------------------------------------------------
// cGZWinTextEditAt248::vf248_19, 0x94f12d: *out = the primary's slot 143 (the dword at +0x138); al = 1.

DIFF_TEST(cGZWinTextEdit_vf248_19, 0x94f12d)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t *)>(0x94f12d);
    TextEditVt vt(ctx);
    std::mt19937 rng(9413);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[kBlock], b[kBlock];
        Randomize(rng, a, vt.primary, vt.sub);
        std::memcpy(b, a, sizeof(a));
        gRet143 = static_cast<uint32_t>(rng());
        uint32_t outA = static_cast<uint32_t>(rng()), outB = outA;

        gTags = {{a, 1}, {a + kSub, 2}};
        gLog.clear();
        uint8_t ra = orig(a + kSub, &outA);
        std::vector<Event> la = gLog;

        gTags = {{b, 1}, {b + kSub, 2}};
        gLog.clear();
        bool rb = reinterpret_cast<cGZWinTextEditAt248 *>(b + kSub)->Vf19(&outB);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if ((ra & 0xff) != (rb ? 1 : 0)) ctx.Fail("Vf19: al original %u, ours %d", ra, rb ? 1 : 0);
        if (outA != outB) ctx.Fail("Vf19: *out original %x, ours %x", outA, outB);
        if (!(la == lb)) ctx.Fail("Vf19: calls differ (original %zu, ours %zu)", la.size(), lb.size());
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("Vf19: object changed");
    }
}

// ---------------------------------------------------------------------------------------------------------
// cGZWinTextEditAt248::vf248_40, 0x94f84f: *flag = bit 4 of +0x68 (a byte); *out = the primary's slot 143 (the dword
// at +0x140); eax is that result.

DIFF_TEST(cGZWinTextEdit_vf248_40, 0x94f84f)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint8_t *, uint32_t *)>(0x94f84f);
    TextEditVt vt(ctx);
    std::mt19937 rng(9414);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[kBlock], b[kBlock];
        Randomize(rng, a, vt.primary, vt.sub);
        std::memcpy(b, a, sizeof(a));
        gRet143 = static_cast<uint32_t>(rng());
        uint8_t flagA = static_cast<uint8_t>(rng()), flagB = flagA;
        uint32_t outA = static_cast<uint32_t>(rng()), outB = outA;

        gTags = {{a, 1}, {a + kSub, 2}};
        gLog.clear();
        uint32_t ra = orig(a + kSub, &flagA, &outA);
        std::vector<Event> la = gLog;

        gTags = {{b, 1}, {b + kSub, 2}};
        gLog.clear();
        uint32_t rb = reinterpret_cast<cGZWinTextEditAt248 *>(b + kSub)->Vf40(&flagB, &outB);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if (ra != rb) ctx.Fail("Vf40: eax original %x, ours %x", ra, rb);
        if (flagA != flagB) ctx.Fail("Vf40: *flag original %u, ours %u", flagA, flagB);
        if (outA != outB) ctx.Fail("Vf40: *out original %x, ours %x", outA, outB);
        if (!(la == lb)) ctx.Fail("Vf40: calls differ (original %zu, ours %zu)", la.size(), lb.size());
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("Vf40: object changed");
    }
}

// ---------------------------------------------------------------------------------------------------------
// cGZWinTextEditAt248::vf248_44, 0x9522e6: if +0xd4 != value, stores it and, when the primary's TestFlag4000
// (TestFlagsE8 & 0x4000, through the primary's slot 82) is true, calls the primary's slot 105.

DIFF_TEST(cGZWinTextEdit_vf248_44, 0x9522e6)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x9522e6);
    TextEditVt vt(ctx);
    std::mt19937 rng(9415);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[kBlock], b[kBlock];
        Randomize(rng, a, vt.primary, vt.sub);
        // Flags of the primary (TestFlagsE8 reads +0xe8) with the 0x4000 bit on half the cases.
        uint32_t flags = static_cast<uint32_t>(rng());
        flags = (i % 2) ? (flags | 0x4000u) : (flags & ~0x4000u);
        Set32(a, 0xe8, flags);
        uint32_t value = Edge(rng, i);
        // Half the cases hold the value already (no change, no calls).
        if (i % 4 == 0) Set32(a, kSub + 0xd4, value);
        std::memcpy(b, a, sizeof(a));
        gRet105 = static_cast<uint32_t>(rng());

        gTags = {{a, 1}, {a + kSub, 2}};
        gLog.clear();
        orig(a + kSub, value);
        std::vector<Event> la = gLog;

        gTags = {{b, 1}, {b + kSub, 2}};
        gLog.clear();
        reinterpret_cast<cGZWinTextEditAt248 *>(b + kSub)->Vf44(value);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if (!(la == lb)) ctx.Fail("TE Vf44: calls differ (original %zu, ours %zu)", la.size(), lb.size());
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("TE Vf44: object changed");
    }
}

// ---------------------------------------------------------------------------------------------------------
// cGZWinTextEditAt248::vf248_79, 0x94f14e: *out = +0x19c; al = 1.

DIFF_TEST(cGZWinTextEdit_vf248_79, 0x94f14e)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t *)>(0x94f14e);
    TextEditVt vt(ctx);
    std::mt19937 rng(9416);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[kBlock], b[kBlock];
        Randomize(rng, a, vt.primary, vt.sub);
        std::memcpy(b, a, sizeof(a));
        uint32_t outA = static_cast<uint32_t>(rng()), outB = outA;

        uint8_t ra = orig(a + kSub, &outA);
        bool rb = reinterpret_cast<cGZWinTextEditAt248 *>(b + kSub)->Vf79(&outB);

        ++ctx.cases;
        if ((ra & 0xff) != (rb ? 1 : 0)) ctx.Fail("Vf79: al original %u, ours %d", ra, rb ? 1 : 0);
        if (outA != outB) ctx.Fail("Vf79: *out original %x, ours %x", outA, outB);
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("Vf79: object changed");
    }
}

// ---------------------------------------------------------------------------------------------------------
// cGZWinTextEditAt248::vf248_82, 0x95151c: when (+0x68 & mask) is zero, slot 83 gets (+0x68 | mask).
// cGZWinTextEditAt248::vf248_84, 0x94d3d7: when (+0x68 & mask) is nonzero, slot 83 gets (+0x68 & ~mask).

DIFF_TEST(cGZWinTextEdit_vf248_82, 0x95151c)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x95151c);
    TextEditVt vt(ctx);
    std::mt19937 rng(9417);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[kBlock], b[kBlock];
        Randomize(rng, a, vt.primary, vt.sub);
        Set32(a, kSub + 0x68, Edge(rng, i));
        std::memcpy(b, a, sizeof(a));
        uint32_t mask = (i % 3 == 0) ? (1u << (rng() % 32)) : Edge(rng, i + 1);

        gTags = {{a, 1}, {a + kSub, 2}};
        gLog.clear();
        orig(a + kSub, mask);
        std::vector<Event> la = gLog;

        gTags = {{b, 1}, {b + kSub, 2}};
        gLog.clear();
        reinterpret_cast<cGZWinTextEditAt248 *>(b + kSub)->Vf82(mask);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if (!(la == lb)) ctx.Fail("Vf82: calls differ (original %zu, ours %zu)", la.size(), lb.size());
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("Vf82: object changed");
    }
}

DIFF_TEST(cGZWinTextEdit_vf248_84, 0x94d3d7)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x94d3d7);
    TextEditVt vt(ctx);
    std::mt19937 rng(9418);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[kBlock], b[kBlock];
        Randomize(rng, a, vt.primary, vt.sub);
        Set32(a, kSub + 0x68, Edge(rng, i));
        std::memcpy(b, a, sizeof(a));
        uint32_t mask = (i % 3 == 0) ? (1u << (rng() % 32)) : Edge(rng, i + 1);

        gTags = {{a, 1}, {a + kSub, 2}};
        gLog.clear();
        orig(a + kSub, mask);
        std::vector<Event> la = gLog;

        gTags = {{b, 1}, {b + kSub, 2}};
        gLog.clear();
        reinterpret_cast<cGZWinTextEditAt248 *>(b + kSub)->Vf84(mask);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if (!(la == lb)) ctx.Fail("Vf84: calls differ (original %zu, ours %zu)", la.size(), lb.size());
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("Vf84: object changed");
    }
}

// ---------------------------------------------------------------------------------------------------------
// cGZWinTextEdit::vf118, 0x95149c: al = SetF4Ok(value) (the base, which sets +0xf4); when the byte at +0x160 has
// bit 0x20, the primary's slot 105 runs.

DIFF_TEST(cGZWinTextEdit_vf118, 0x95149c)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t)>(0x95149c);
    TextEditVt vt(ctx);
    std::mt19937 rng(9419);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[kBlock], b[kBlock];
        Randomize(rng, a, vt.primary, vt.sub);
        a[0x160] = static_cast<uint8_t>((i % 2) ? (a[0x160] | 0x20) : (a[0x160] & ~0x20));
        std::memcpy(b, a, sizeof(a));
        uint32_t value = Edge(rng, i);
        gRet105 = static_cast<uint32_t>(rng());

        gTags = {{a, 1}, {a + kSub, 2}};
        gLog.clear();
        uint8_t ra = orig(a, value);
        std::vector<Event> la = gLog;

        gTags = {{b, 1}, {b + kSub, 2}};
        gLog.clear();
        bool rb = reinterpret_cast<cGZWinTextEdit *>(b)->Vf118(value);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if ((ra & 0xff) != (rb ? 1 : 0)) ctx.Fail("Vf118: al original %u, ours %d", ra, rb ? 1 : 0);
        if (!(la == lb)) ctx.Fail("Vf118: calls differ (original %zu, ours %zu)", la.size(), lb.size());
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("Vf118: object changed");
    }
}
