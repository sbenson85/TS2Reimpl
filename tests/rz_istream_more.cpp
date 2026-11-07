// Differential tests for the rest of cRZIStream: constructor, destructor, deleting destructor,
// GetSint64, the packed-integer reader, the two string readers and GetGZSerializable
// (tests/rz_istream.cpp has QueryInterface and the other getters).
//
// The raw read (slot 17), GetError (slot 18) and Skip (slot 3) are fakes over a byte script; slot 5
// stays the original's GetUint8 body, which reads through the fake raw read. Every call is logged
// into one ordered list (gEv), and the original and the replacement must produce the same list, return
// value, output bytes and object bytes. A test object lives at one fixed address and is re-initialised
// between the two runs, because the packed reader's byte buffer starts out holding bytes of `this`.
#include "difftest.h"
#include "rz/cRZIStream.h"

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <random>
#include <vector>

namespace {

enum : uint64_t { kEvRead = 1, kEvError, kEvSkip, kEvFromChar, kEvAppend, kEvSerialize, kEvOwnerRelease };

std::vector<uint64_t> gEv;
std::vector<uint8_t> gData;
size_t gPos = 0;
bool gRefuse = false;   // every read fails
bool gFill = false;     // a failed read still writes its destination (partial data, then 0x5A)
uint32_t gLatch = 0;    // the stream's error state
uint32_t gNoise = 0;    // upper bits of GetError's and Skip's return values
uint32_t gRes = 0;
const void *gStream = nullptr;
const void *gOwnerSelf = nullptr;
const void *gImageVt = nullptr;
const void *gEmptyLiteral = nullptr;

uint64_t Ev(uint64_t kind, uint64_t value) { return (kind << 56) | (value & 0x00ffffffffffffffull); }

uint32_t Fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}

bool __fastcall FakeRead(void *, void *, void *dst, uint32_t n)
{
    gEv.push_back(Ev(kEvRead, n));
    size_t avail = gData.size() - gPos;
    if (gRefuse || avail < n) {
        if (gFill) {
            size_t have = gRefuse ? 0 : avail;
            auto *d = static_cast<uint8_t *>(dst);
            for (uint32_t i = 0; i < n; ++i) d[i] = i < have ? gData[gPos + i] : 0x5A;
        }
        gLatch = 1;
        return false;
    }
    std::memcpy(dst, gData.data() + gPos, n);
    gPos += n;
    return true;
}

uint32_t __fastcall FakeGetError(void *, void *)
{
    gEv.push_back(Ev(kEvError, 0));
    return (gNoise & 0xffffff00u) | gLatch;
}

uint32_t __fastcall FakeSkip(void *, void *, uint32_t n)
{
    gEv.push_back(Ev(kEvSkip, n));
    if (gData.size() - gPos < n) {
        gPos = gData.size();
        gLatch = 1;
    } else {
        gPos += n;
    }
    return gNoise;
}

uint32_t __fastcall FakeOwnerRelease(void *self, void *)
{
    void *vt;
    std::memcpy(&vt, gStream, sizeof(vt));
    gEv.push_back(Ev(kEvOwnerRelease, uint64_t(self == gOwnerSelf) | (uint64_t(vt == gImageVt) << 1)));
    return gRes;
}

uint32_t __fastcall FakeFromChar(void *, void *, const char *p)
{
    gEv.push_back(Ev(kEvFromChar, uint64_t(p == gEmptyLiteral) | (uint64_t(p ? uint8_t(*p) : 0xff) << 1)));
    return gRes;
}

uint32_t __fastcall FakeAppend(void *, void *, const void *p, uint32_t n)
{
    // The bytes after the n appended ones are not part of the call: only the chunk and its length are.
    gEv.push_back(Ev(kEvAppend, (uint64_t(n) << 32) | Fnv(static_cast<const uint8_t *>(p), n)));
    return gRes;
}

uint32_t __fastcall FakeSerialize(void *self, void *, void *stream)
{
    gEv.push_back(Ev(kEvSerialize, uint64_t(self == gOwnerSelf) | (uint64_t(stream == gStream) << 1)));
    return gRes;
}

struct IVtable {
    void *slots[24];
    explicit IVtable(const difftest::Context &ctx)
    {
        std::memcpy(slots, ctx.image.At(cRZIStream::kVTable), sizeof(slots));
        slots[cRZIStream::kSkipSlot] = reinterpret_cast<void *>(&FakeSkip);
        slots[cRZIStream::kRawReadSlot] = reinterpret_cast<void *>(&FakeRead);
        slots[cRZIStream::kGetErrorSlot] = reinterpret_cast<void *>(&FakeGetError);
    }
};

// One object with canaries around it (the object at +16). Init is repeatable.
struct Rig {
    uint8_t bytes[64];

    void Init(const void *vtable, uint32_t refs, uint32_t flagWord, uint32_t owner = 0xa5a5a5a5u)
    {
        std::memset(bytes, 0xA5, sizeof(bytes));
        std::memcpy(bytes + 16, &vtable, sizeof(vtable));
        std::memcpy(bytes + 20, &refs, 4);
        std::memcpy(bytes + 24, &flagWord, 4);
        std::memcpy(bytes + 28, &owner, 4);
    }
    cRZIStream *Object() { return reinterpret_cast<cRZIStream *>(bytes + 16); }
    void PutOwner(const void *owner) { std::memcpy(bytes + 28, &owner, sizeof(owner)); }
};

// Output buffer with canaries; the code under test writes into the middle.
struct Out {
    uint8_t bytes[64];
    Out() { std::memset(bytes, 0xA5, sizeof(bytes)); }
    uint8_t *Data() { return bytes + 16; }
};

struct Outcome {
    uint64_t ret = 0;
    std::vector<uint64_t> events;
    size_t pos = 0;
    uint32_t latch = 0;
    uint8_t object[64];
    uint8_t out[64];
    uint32_t tail[2];
};

void Script(const std::vector<uint8_t> &data, bool refuse, bool fill, uint32_t latch, uint32_t noise)
{
    gData = data;
    gPos = 0;
    gRefuse = refuse;
    gFill = fill;
    gLatch = latch;
    gNoise = noise;
    gEv.clear();
}

void Snapshot(Outcome &o, uint64_t ret, const Rig &rig, const Out &out)
{
    o.ret = ret;
    o.events = gEv;
    o.pos = gPos;
    o.latch = gLatch;
    std::memcpy(o.object, rig.bytes, sizeof(o.object));
    std::memcpy(o.out, out.bytes, sizeof(o.out));
}

void Check(difftest::Context &ctx, const char *what, const Outcome &want, const Outcome &got)
{
    ++ctx.cases;
    if (want.ret != got.ret) {
        ctx.Fail("%s: return original 0x%llx, ours 0x%llx", what, (unsigned long long)want.ret, (unsigned long long)got.ret);
    }
    if (want.events != got.events) ctx.Fail("%s: call logs differ (original %zu events, ours %zu)", what, want.events.size(), got.events.size());
    if (want.pos != got.pos || want.latch != got.latch) ctx.Fail("%s: stream position or error state differ", what);
    if (std::memcmp(want.object, got.object, sizeof(want.object)) != 0) ctx.Fail("%s: object memory differs", what);
    if (std::memcmp(want.out, got.out, sizeof(want.out)) != 0) ctx.Fail("%s: output bytes differ", what);
}

// A packed integer (7 bits per byte, low group first, 0x80 continues).
void AppendPacked(std::vector<uint8_t> &v, uint32_t value)
{
    do {
        uint8_t b = value & 0x7f;
        value >>= 7;
        v.push_back(value ? b | 0x80 : b);
    } while (value);
}

void AppendRandomBytes(std::vector<uint8_t> &v, std::mt19937 &rng, size_t n)
{
    for (size_t i = 0; i < n; ++i) v.push_back(static_cast<uint8_t>(rng()));
}

uint8_t PickFlag(std::mt19937 &rng)
{
    static const uint8_t flags[] = {0, 0, 0, 1, 2, 0x80, 0xff};
    return flags[rng() % 7];
}

// True when `this` for these tests has no byte with the top bit set in its low three bytes. The
// packed reader starts from bytes of `this`, and a failed read chain through them must stop.
bool AddressOk(const void *p)
{
    uint32_t a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
    return (a >> 24) < 0x80;
}

} // namespace

DIFF_TEST(cRZIStream_Construct, 0x1b4d82)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1b4d82);
    IVtable vt(ctx);
    std::mt19937 rng(200);
    for (int i = 0; i < 4000; ++i) {
        uint32_t order = i < 6 ? (i == 0 ? 0u : i == 1 ? 1u : i == 2 ? 0x100u : i == 3 ? 0xffffff00u : i == 4 ? 0x80u : 0xffffffffu) : rng();
        Rig a, b;
        a.Init(vt.slots, rng(), rng(), rng());
        b = a;
        ++ctx.cases;
        void *ra = orig(a.Object(), order);
        cRZIStream *rb = b.Object()->Construct(order);
        if (ra != a.Object() || rb != b.Object()) ctx.Fail("Construct(0x%08x): did not return this", order);
        if (std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) != 0) ctx.Fail("Construct(0x%08x): object memory differs", order);
    }
}

DIFF_TEST(cRZIStream_Destruct, 0x1b52af)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0x1b52af);
    IVtable vt(ctx);
    gImageVt = ctx.image.At(cRZIStream::kVTable);
    void *ownerVt[3] = {nullptr, nullptr, reinterpret_cast<void *>(&FakeOwnerRelease)};
    struct Owner { void *vtable; } owner{ownerVt};
    gOwnerSelf = &owner;
    std::mt19937 rng(201);
    for (int i = 0; i < 2000; ++i) {
        Rig a, b;
        a.Init(vt.slots, rng(), rng());
        a.PutOwner(i % 3 ? &owner : nullptr);
        b = a;
        gRes = rng();
        gEv.clear();
        gStream = a.bytes + 16;
        orig(a.Object());
        std::vector<uint64_t> want = gEv;
        gEv.clear();
        gStream = b.bytes + 16;
        b.Object()->Destruct();
        ++ctx.cases;
        if (want != gEv) ctx.Fail("Destruct: owner release log differs");
        if (std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) != 0) ctx.Fail("Destruct: object memory differs");
        if (i % 3 && want.size() != 1) ctx.Fail("Destruct: owner not released exactly once by the original");
    }
}

DIFF_TEST(cRZIStream_DeletingDestructor, 0x1b578e)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1b578e);
    IVtable vt(ctx);
    gImageVt = ctx.image.At(cRZIStream::kVTable);
    void *ownerVt[3] = {nullptr, nullptr, reinterpret_cast<void *>(&FakeOwnerRelease)};
    struct Owner { void *vtable; } owner{ownerVt};
    gOwnerSelf = &owner;
    std::mt19937 rng(202);
    const uint32_t flagSet[] = {0, 1, 2, 3, 0xfffffffeu, 0xffffffffu, 0x100, 0x101};
    for (int i = 0; i < 3000; ++i) {
        uint32_t flags = i < 8 ? flagSet[i] : flagSet[rng() % 8];
        auto *ha = static_cast<uint8_t *>(::operator new(sizeof(cRZIStream)));
        auto *hb = static_cast<uint8_t *>(::operator new(sizeof(cRZIStream)));
        Rig seed;
        seed.Init(vt.slots, rng(), rng());
        seed.PutOwner(i % 3 ? &owner : nullptr);
        std::memcpy(ha, seed.bytes + 16, 16);
        std::memcpy(hb, seed.bytes + 16, 16);
        gRes = rng();
        gEv.clear();
        gStream = ha;
        void *ra = orig(ha, flags);
        std::vector<uint64_t> want = gEv;
        gEv.clear();
        gStream = hb;
        cRZIStream *rb = reinterpret_cast<cRZIStream *>(hb)->DeletingDestructor(flags);
        ++ctx.cases;
        if (ra != ha || rb != reinterpret_cast<cRZIStream *>(hb)) ctx.Fail("DeletingDestructor(0x%x): did not return this", flags);
        if (want != gEv) ctx.Fail("DeletingDestructor(0x%x): owner release log differs", flags);
        if (!(flags & 1)) {
            if (std::memcmp(ha, hb, 16) != 0) ctx.Fail("DeletingDestructor(0x%x): object memory differs", flags);
            ::operator delete(ha);
            ::operator delete(hb);
        }
    }
}

DIFF_TEST(cRZIStream_GetSint64, 0x1b5da9)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x1b5da9);
    IVtable vt(ctx);
    std::mt19937 rng(203);
    static Rig rig;
    for (int i = 0; i < 20000; ++i) {
        std::vector<uint8_t> data;
        AppendRandomBytes(data, rng, rng() % 12); // 0..11: failing, exact and surplus
        bool refuse = rng() % 10 == 0;
        uint8_t flag = PickFlag(rng);
        uint32_t refs = rng();
        Outcome want, got;
        Out outA, outB;
        rig.Init(vt.slots, refs, flag);
        Script(data, refuse, false, 0, rng());
        Snapshot(want, orig(rig.Object(), outA.Data()) & 0xff, rig, outA);
        rig.Init(vt.slots, refs, flag);
        Script(data, refuse, false, 0, 0);
        Snapshot(got, rig.Object()->GetSint64(outB.Data()) ? 1 : 0, rig, outB);
        char what[64];
        std::snprintf(what, sizeof(what), "GetSint64 (len %zu, flag 0x%02x, refuse %d)", data.size(), flag, refuse);
        Check(ctx, what, want, got);
    }
}

DIFF_TEST(cRZIStream_GetPackedUint32, 0x1b588e)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t *, void *)>(0x1b588e);
    IVtable vt(ctx);
    std::mt19937 rng(204);
    static Rig rig;
    if (!AddressOk(rig.bytes)) {
        ctx.Fail("test object address 0x%p has the top bit set; the packed reader's failure path cannot be compared", rig.bytes);
        return;
    }
    const uint32_t edges[] = {0, 1, 0x7f, 0x80, 0x3fff, 0x4000, 0x1fffff, 0x200000, 0xfffffff, 0x10000000, 0xffffffffu};
    for (int i = 0; i < 30000; ++i) {
        std::vector<uint8_t> data;
        uint32_t kind = rng() % 4;
        if (kind == 0) {
            AppendPacked(data, i < int(std::size(edges)) ? edges[i] : rng() >> (rng() % 32));
        } else if (kind == 1) {
            // Arbitrary groups, 1..7 bytes: continuation bytes with random low bits, a last byte without the top bit (it may be 0).
            size_t n = 1 + rng() % 7;
            for (size_t k = 0; k + 1 < n; ++k) data.push_back(static_cast<uint8_t>(rng() | 0x80));
            data.push_back(static_cast<uint8_t>(rng() & 0x7f));
        } else if (kind == 2) {
            // A single byte, any value.
            data.push_back(static_cast<uint8_t>(rng()));
            if (data[0] & 0x80) data.push_back(static_cast<uint8_t>(rng() & 0x7f));
        }
        // kind 3: no data at all. Then cut some streams short, which leaves the buffer bytes stale.
        if (!data.empty() && rng() % 4 == 0) data.resize(rng() % data.size());
        AppendRandomBytes(data, rng, rng() % 3 == 0 ? 2 : 0); // data after the integer: must stay unread
        // A cut stream plus the trailing bytes can make 8 continuation bytes in a row. The original's loop
        // then runs past its 8-byte stack buffer and clears bit 7 of whatever lies above it (its caller's
        // saved registers and return address: the ORIGINAL-BUG in cRZIStream.h), so the outcome depends
        // on the stack contents and crashed the runner. Such input can't be compared: end the run at 7.
        if (data.size() >= 8 && std::all_of(data.begin(), data.begin() + 8, [](uint8_t c) { return (c & 0x80) != 0; }))
            data[7] &= 0x7f;
        bool refuse = rng() % 12 == 0;
        uint32_t sentinel = rng();
        uint32_t refs = rng(), flag = rng();
        Outcome want, got;
        Out outA, outB;
        uint32_t unusedA = 0, unusedB = 0;
        std::memcpy(outA.Data(), &sentinel, 4);
        std::memcpy(outB.Data(), &sentinel, 4);
        rig.Init(vt.slots, refs, flag);
        Script(data, refuse, false, 0, 0);
        Snapshot(want, orig(rig.Object(), reinterpret_cast<uint32_t *>(outA.Data()), &unusedA), rig, outA);
        rig.Init(vt.slots, refs, flag);
        Script(data, refuse, false, 0, 0);
        Snapshot(got, rig.Object()->GetPackedUint32(reinterpret_cast<uint32_t *>(outB.Data()), &unusedB), rig, outB);
        char what[64];
        std::snprintf(what, sizeof(what), "GetPackedUint32 (%zu bytes, refuse %d)", data.size(), refuse);
        Check(ctx, what, want, got);
        if (unusedA != 0 || unusedB != 0) ctx.Fail("%s: the unused argument was written", what);
    }
}

DIFF_TEST(cRZIStream_GetRZCharStr, 0x1b5b86)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, char *, uint32_t)>(0x1b5b86);
    IVtable vt(ctx);
    std::mt19937 rng(205);
    static Rig rig;
    for (int i = 0; i < 30000; ++i) {
        std::vector<uint8_t> data;
        uint32_t len = rng() % 4 == 0 ? 0 : rng() % 4 == 0 ? rng() % 40 : rng() % 14;
        AppendPacked(data, len);
        AppendRandomBytes(data, rng, len);
        if (rng() % 6 == 0) data.resize(rng() % (data.size() + 1)); // cut short
        AppendRandomBytes(data, rng, rng() % 3 == 0 ? 3 : 0);      // trailing data
        uint32_t size = rng() % 5 == 0 ? len : rng() % 24;        // sometimes exactly the length
        bool refuse = rng() % 12 == 0;
        uint32_t latch = rng() % 10 == 0;
        uint32_t noise = rng();
        Outcome want, got;
        Out outA, outB;
        rig.Init(vt.slots, 0, 0);
        Script(data, refuse, true, latch, noise);
        Snapshot(want, orig(rig.Object(), reinterpret_cast<char *>(outA.Data()), size) & 0xff, rig, outA);
        rig.Init(vt.slots, 0, 0);
        Script(data, refuse, true, latch, noise);
        Snapshot(got, rig.Object()->GetRZCharStr(reinterpret_cast<char *>(outB.Data()), size), rig, outB);
        char what[96];
        std::snprintf(what, sizeof(what), "GetRZCharStr (length %u, size %u, %zu bytes, refuse %d, latch %u)", len, size, data.size(),
                      refuse, latch);
        Check(ctx, what, want, got);
    }
}

namespace {

struct FakeString {
    void *vtable;
};

} // namespace

DIFF_TEST(cRZIStream_GetGZStr, 0x1b5a4d)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x1b5a4d);
    IVtable vt(ctx);
    gEmptyLiteral = ctx.image.At(0xd9b9d0);
    void *strVt[33] = {};
    strVt[4] = reinterpret_cast<void *>(&FakeFromChar);
    strVt[20] = reinterpret_cast<void *>(&FakeAppend);
    FakeString str{strVt};
    std::mt19937 rng(206);
    static Rig rig;
    const uint32_t bigLens[] = {0xfe, 0xff, 0x100, 0x101, 0x1fd, 0x1fe, 0x1ff, 0x200, 300, 600};
    for (int i = 0; i < 12000; ++i) {
        std::vector<uint8_t> data;
        uint32_t pick = rng() % 6;
        uint32_t len = pick == 0 ? 0 : pick < 4 ? rng() % 14 : bigLens[rng() % std::size(bigLens)];
        AppendPacked(data, len);
        AppendRandomBytes(data, rng, len);
        if (rng() % 6 == 0) data.resize(rng() % (data.size() + 1));
        AppendRandomBytes(data, rng, rng() % 3 == 0 ? 3 : 0);
        bool refuse = rng() % 12 == 0;
        uint32_t latch = rng() % 10 == 0;
        uint32_t noise = rng();
        gRes = rng();
        Outcome want, got;
        Out outA, outB;
        rig.Init(vt.slots, 0, 0);
        Script(data, refuse, true, latch, noise);
        Snapshot(want, orig(rig.Object(), &str) & 0xff, rig, outA);
        rig.Init(vt.slots, 0, 0);
        Script(data, refuse, true, latch, noise);
        Snapshot(got, rig.Object()->GetGZStr(reinterpret_cast<cIGZString *>(&str)), rig, outB);
        char what[96];
        std::snprintf(what, sizeof(what), "GetGZStr (length %u, %zu bytes, refuse %d, latch %u)", len, data.size(), refuse, latch);
        Check(ctx, what, want, got);
    }
}

DIFF_TEST(cRZIStream_GetGZSerializable, 0x1b59ce)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x1b59ce);
    IVtable vt(ctx);
    void *serVt[5] = {nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void *>(&FakeSerialize)};
    struct Serializable { void *vtable; } ser{serVt};
    gOwnerSelf = &ser;
    std::mt19937 rng(207);
    static Rig rig;
    for (int i = 0; i < 2000; ++i) {
        Outcome want, got;
        Out outA, outB;
        gRes = rng();
        uint32_t refs = rng(), flag = rng();
        rig.Init(vt.slots, refs, flag);
        Script({}, false, false, 0, 0);
        gStream = rig.bytes + 16;
        Snapshot(want, orig(rig.Object(), &ser), rig, outA);
        rig.Init(vt.slots, refs, flag);
        Script({}, false, false, 0, 0);
        Snapshot(got, rig.Object()->GetGZSerializable(&ser), rig, outB);
        Check(ctx, "GetGZSerializable", want, got);
    }
}
