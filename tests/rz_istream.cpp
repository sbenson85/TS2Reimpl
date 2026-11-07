// Differential tests for cRZIStream's getters and QueryInterface. The raw read (vtable slot 17, pure
// in the original class) is replaced by a fake in the vtable copies used by both sides. The fake
// serves bytes from a script and records each request, so the original and ours read the same data
// and their requests (sizes, order, failures) can be compared. Each side also runs once through a
// vtable whose slot points at our function, so the slot path is covered as well as the direct call.
#include "difftest.h"
#include "game/vtable.h"
#include "rz/cRZIStream.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

using Getter = bool (cRZIStream::*)(void *);
using OrigGetter = bool(__thiscall *)(void *, void *);
using QueryMember = bool (cRZIStream::*)(uint32_t, void **);
using OrigQuery = bool(__thiscall *)(void *, uint32_t, void **);

template <typename T> void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

struct Script {
    std::vector<uint8_t> data;
    size_t pos = 0;
    bool refuse = false;             // when set, every read fails
    std::vector<uint32_t> requests;  // requested sizes, in order (failed ones too)
};
Script gScript;

void Reset(const std::vector<uint8_t> &data, bool refuse)
{
    gScript.data = data;
    gScript.pos = 0;
    gScript.refuse = refuse;
    gScript.requests.clear();
}

// Raw read (GetVoid): a thiscall with (dst, n) on the stack, written as __fastcall so the two
// stack arguments come after self and edx, which is the same stack layout. Fails when refused or
// when fewer than n bytes remain; writes nothing on failure.
bool __fastcall FakeRawRead(void * /*self*/, void * /*edx*/, void *dst, uint32_t n)
{
    gScript.requests.push_back(n);
    if (gScript.refuse || gScript.data.size() - gScript.pos < n) return false;
    std::memcpy(dst, gScript.data.data() + gScript.pos, n);
    gScript.pos += n;
    return true;
}

struct Slots {
    void *slots[24];
};

Slots MakeSlots(const difftest::Context &ctx)
{
    Slots s{};
    std::memcpy(s.slots, ctx.image.At(cRZIStream::kVTable), sizeof(s.slots));
    s.slots[cRZIStream::kRawReadSlot] = reinterpret_cast<void *>(&FakeRawRead);
    return s;
}

// Object with canaries before and after it, so writes outside the object show up.
struct Guarded {
    uint8_t front[16];
    cRZIStream obj;
    uint8_t back[16];

    Guarded(const void *vtable, uint32_t refs, uint8_t flag)
    {
        std::memset(this, 0xA5, sizeof(*this));
        std::memset(&obj, 0, sizeof(obj));
        obj.mVTable = vtable;
        obj.mRefCount = refs;
        obj.mSkipByteSwap = flag;
    }
};

// Output buffer with canaries; the getters write up to 8 bytes into value.
struct Out {
    uint8_t front[8];
    uint8_t value[16];
    uint8_t back[8];

    Out() { std::memset(this, 0xA5, sizeof(*this)); }
};

bool SameGuards(const Guarded &x, const Guarded &y)
{
    return std::memcmp(x.front, y.front, sizeof(x.front)) == 0 && std::memcmp(x.back, y.back, sizeof(x.back)) == 0
        && x.obj.mRefCount == y.obj.mRefCount && x.obj.mSkipByteSwap == y.obj.mSkipByteSwap;
}

std::vector<uint8_t> RandomData(std::mt19937 &rng)
{
    static const uint8_t edges[] = {0x00, 0x01, 0x7f, 0x80, 0xff};
    size_t len = rng() % 14; // 0..13 bytes: covers failed, short and complete reads
    std::vector<uint8_t> v(len);
    for (auto &b : v) b = rng() % 3 == 0 ? edges[rng() % 5] : static_cast<uint8_t>(rng());
    return v;
}

// Flag 0 means byte-swap; nonzero values skip it. Weighted towards 0, and includes the top bit.
uint8_t PickFlag(std::mt19937 &rng)
{
    static const uint8_t flags[] = {0, 0, 0, 1, 2, 0x80, 0xff};
    return flags[rng() % 7];
}

void CheckGetter(difftest::Context &ctx, const char *name, uint32_t rva, Getter ours, int slot, std::mt19937 &rng, int trials)
{
    auto orig = ctx.Original<OrigGetter>(rva);
    Slots plain = MakeSlots(ctx);
    Slots routed = MakeSlots(ctx);
    routed.slots[slot] = Addr(ours);

    for (int i = 0; i < trials; ++i) {
        std::vector<uint8_t> data = RandomData(rng);
        bool refuse = rng() % 8 == 0;
        uint8_t flag = PickFlag(rng);
        uint32_t refs = static_cast<uint32_t>(rng());

        Guarded a(plain.slots, refs, flag), b(plain.slots, refs, flag), c(routed.slots, refs, flag);
        Out da, db, dc;

        Reset(data, refuse);
        bool ra = orig(&a.obj, da.value);
        Script sa = gScript;

        Reset(data, refuse);
        bool rb = (b.obj.*ours)(db.value);
        Script sb = gScript;

        Reset(data, refuse);
        bool rc = VirtualSlot<OrigGetter>(&c.obj, slot)(&c.obj, dc.value);
        Script sc = gScript;

        ++ctx.cases;
        if (ra != rb || ra != rc) {
            ctx.Fail("%s (len %zu, flag 0x%02x, refuse %d): return original %d, ours %d, via slot %d %d", name, data.size(), flag,
                     refuse, ra, rb, slot, rc);
        }
        if (sa.requests != sb.requests || sa.requests != sc.requests || sa.pos != sb.pos || sa.pos != sc.pos) {
            ctx.Fail("%s (len %zu, flag 0x%02x, refuse %d): raw read requests or position differ", name, data.size(), flag, refuse);
        }
        if (std::memcmp(da.value, db.value, sizeof(da.value)) != 0 || std::memcmp(da.value, dc.value, sizeof(da.value)) != 0
            || std::memcmp(da.front, db.front, sizeof(da.front)) != 0 || std::memcmp(da.back, db.back, sizeof(da.back)) != 0
            || std::memcmp(da.front, dc.front, sizeof(da.front)) != 0 || std::memcmp(da.back, dc.back, sizeof(da.back)) != 0) {
            ctx.Fail("%s (len %zu, flag 0x%02x, refuse %d): output buffer differs", name, data.size(), flag, refuse);
        }
        if (std::memcmp(&a, &b, sizeof(a)) != 0 || !SameGuards(a, c)) {
            ctx.Fail("%s (len %zu, flag 0x%02x, refuse %d): object or canaries differ", name, data.size(), flag, refuse);
        }
    }
}

void CheckQuery(difftest::Context &ctx, std::mt19937 &rng)
{
    auto orig = ctx.Original<OrigQuery>(0x1b5f48);
    Slots plain = MakeSlots(ctx);
    Slots routed = MakeSlots(ctx);
    routed.slots[0] = Addr(static_cast<QueryMember>(&cRZIStream::QueryInterface));

    const uint32_t ids[] = {0, 1, 2, 0x051b11bb, 0x051b11bc, 0x051b11bd, 0x80000000u, 0xffffffffu};
    char none;
    for (int i = 0; i < 20000; ++i) {
        uint32_t id = i < 8 ? ids[i] : (i % 4 == 0 ? 1u : i % 4 == 1 ? 0x051b11bcu : static_cast<uint32_t>(rng()));
        uint32_t refs = i % 5 == 0 ? 0xffffffffu : static_cast<uint32_t>(rng());
        Guarded a(plain.slots, refs, 0), b(plain.slots, refs, 0), c(routed.slots, refs, 0);
        void *oa = &none, *ob = &none, *oc = &none;

        bool ra = orig(&a.obj, id, &oa);
        bool rb = b.obj.QueryInterface(id, &ob);
        bool rc = VirtualSlot<OrigQuery>(&c.obj, 0)(&c.obj, id, &oc);

        ++ctx.cases;
        if (ra != rb || ra != rc) ctx.Fail("QueryInterface(0x%08x): return original %d, ours %d, via slot %d", id, ra, rb, rc);
        // On success the out pointer is the object itself; on failure it is left alone.
        void *wantA = ra ? static_cast<void *>(&a.obj) : static_cast<void *>(&none);
        void *wantB = rb ? static_cast<void *>(&b.obj) : static_cast<void *>(&none);
        void *wantC = rc ? static_cast<void *>(&c.obj) : static_cast<void *>(&none);
        if (oa != wantA || ob != wantB || oc != wantC) ctx.Fail("QueryInterface(0x%08x): out pointer differs", id);
        if (a.obj.mRefCount != b.obj.mRefCount || !SameGuards(a, b) || !SameGuards(a, c)) {
            ctx.Fail("QueryInterface(0x%08x) refs %u: object state differs (original %u, ours %u)", id, refs, a.obj.mRefCount,
                     b.obj.mRefCount);
        }
    }
}

} // namespace

DIFF_TEST(cRZIStream_Getters, 0x1b5c5b)
{
    std::mt19937 rng(0x5eed);
    const int trials = 20000;
    // Each getter at its slot and at the unsigned alias slot (the same code).
    CheckGetter(ctx, "GetSint8", 0x1b5e0f, static_cast<Getter>(&cRZIStream::GetSint8), 4, rng, trials);
    CheckGetter(ctx, "GetSint8 (alias)", 0x1b5e0f, static_cast<Getter>(&cRZIStream::GetSint8), 5, rng, trials / 4);
    CheckGetter(ctx, "GetSint16", 0x1b5c5b, static_cast<Getter>(&cRZIStream::GetSint16), 6, rng, trials);
    CheckGetter(ctx, "GetSint16 (alias)", 0x1b5c5b, static_cast<Getter>(&cRZIStream::GetSint16), 7, rng, trials / 4);
    CheckGetter(ctx, "GetSint32", 0x1b5cca, static_cast<Getter>(&cRZIStream::GetSint32), 8, rng, trials);
    CheckGetter(ctx, "GetSint32 (alias)", 0x1b5cca, static_cast<Getter>(&cRZIStream::GetSint32), 9, rng, trials / 4);
    CheckGetter(ctx, "GetFloat32", 0x1b5982, static_cast<Getter>(&cRZIStream::GetFloat32), 12, rng, trials);
    CheckGetter(ctx, "GetFloat64", 0x1b59c0, static_cast<Getter>(&cRZIStream::GetFloat64), 13, rng, trials);
}

DIFF_TEST(cRZIStream_QueryInterface, 0x1b5f48)
{
    std::mt19937 rng(77);
    CheckQuery(ctx, rng);
}
