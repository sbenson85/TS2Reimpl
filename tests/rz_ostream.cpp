// Differential tests for cRZOStream's setters and QueryInterface. Objects carry a copy of the
// original vtable with two slots replaced by recorders: slot 1 (AddRef: counts calls and bumps the
// count at +4) and slot 17 (the raw write: records each write's bytes and size and returns a value
// chosen by the test). Original and replacement therefore see identical calls and return values.
// The SetGZSerializable test uses a test serializable whose slot 3 records its call.
#include "difftest.h"
#include "rz/cRZOStream.h"

#include <cstring>
#include <iterator>
#include <random>
#include <vector>

namespace {

struct WriteRecord {
    bool toObject; // written through the object under test
    uint32_t size;
    std::vector<uint8_t> bytes;
};

std::vector<WriteRecord> gWrites;
uint32_t gWriteResult = 0;
int gAddRefs = 0;
const void *gObject = nullptr;

int gSerializeCalls = 0;
bool gSerializeSelfOk = false;   // the serializable argument is the test's serializable
bool gSerializeStreamOk = false; // the stream argument is the object under test
uint32_t gSerializeResult = 0;
const void *gSerializable = nullptr;

uint32_t __fastcall RecordWrite(void *self, void * /*edx*/, const void *buf, uint32_t size)
{
    WriteRecord w;
    w.toObject = self == gObject;
    w.size = size;
    const auto *p = static_cast<const uint8_t *>(buf);
    w.bytes.assign(p, p + size);
    gWrites.push_back(std::move(w));
    return gWriteResult;
}

uint32_t __fastcall RecordAddRef(void *self, void * /*edx*/)
{
    ++gAddRefs;
    auto *count = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(self) + 4);
    return ++*count;
}

uint32_t __fastcall RecordSerialize(void *self, void * /*edx*/, void *stream)
{
    ++gSerializeCalls;
    gSerializeSelfOk = self == gSerializable;
    gSerializeStreamOk = stream == gObject;
    return gSerializeResult;
}

// A copy of cRZOStream's original vtable with the AddRef and raw-write slots replaced by recorders.
struct OStreamVTable {
    void *slots[24];
    explicit OStreamVTable(const difftest::Context &ctx)
    {
        std::memcpy(slots, ctx.image.At(cRZOStream::kVTable), sizeof(slots));
        slots[1] = reinterpret_cast<void *>(&RecordAddRef);
        slots[cRZOStream::kRawWriteSlot] = reinterpret_cast<void *>(&RecordWrite);
    }
};

// Storage for one object: canaries around it, the object at +16, so stray writes show up.
struct Slot {
    uint8_t bytes[64];

    // Vtable pointer, reference count, then the 4 bytes at +8 (byte-order flag and the padding
    // after it) taken from flagWord.
    void Init(const void *vtable, uint32_t refs, uint32_t flagWord)
    {
        std::memset(bytes, 0xA5, sizeof(bytes));
        const void *vt = vtable;
        std::memcpy(bytes + 16, &vt, sizeof(vt));
        std::memcpy(bytes + 20, &refs, 4);
        std::memcpy(bytes + 24, &flagWord, 4);
    }
    cRZOStream *Object() { return reinterpret_cast<cRZOStream *>(bytes + 16); }
    const void *ObjectAddress() const { return bytes + 16; }
};

// What one call did, as observed through the recorders.
struct Trace {
    uint32_t ret = 0;
    std::vector<WriteRecord> writes;
    int addRefs = 0;
    int serializeCalls = 0;
    bool serializeSelfOk = false;
    bool serializeStreamOk = false;
};

template <typename Call> Trace Capture(const void *object, uint32_t writeResult, Call call)
{
    gWrites.clear();
    gWriteResult = writeResult;
    gAddRefs = 0;
    gSerializeCalls = 0;
    gSerializeSelfOk = gSerializeStreamOk = false;
    gObject = object;
    Trace t;
    t.ret = call();
    t.writes = gWrites;
    t.addRefs = gAddRefs;
    t.serializeCalls = gSerializeCalls;
    t.serializeSelfOk = gSerializeSelfOk;
    t.serializeStreamOk = gSerializeStreamOk;
    return t;
}

void Expect(difftest::Context &ctx, const char *what, const Slot &a, const Slot &b, const Trace &want, const Trace &got)
{
    ++ctx.cases;
    if (want.ret != got.ret) ctx.Fail("%s: return original 0x%08x, ours 0x%08x", what, want.ret, got.ret);
    if (want.writes.size() != got.writes.size()) {
        ctx.Fail("%s: original made %zu writes, ours %zu", what, want.writes.size(), got.writes.size());
    } else {
        for (size_t i = 0; i < want.writes.size(); ++i) {
            const WriteRecord &w = want.writes[i], &g = got.writes[i];
            if (w.size != g.size) ctx.Fail("%s: write %zu size original %u, ours %u", what, i, w.size, g.size);
            else if (w.bytes != g.bytes) ctx.Fail("%s: write %zu bytes differ (size %u)", what, i, w.size);
            if (w.toObject != g.toObject) ctx.Fail("%s: write %zu target differs", what, i);
        }
    }
    if (want.addRefs != got.addRefs) ctx.Fail("%s: AddRef calls original %d, ours %d", what, want.addRefs, got.addRefs);
    if (want.serializeCalls != got.serializeCalls || want.serializeSelfOk != got.serializeSelfOk
        || want.serializeStreamOk != got.serializeStreamOk) {
        ctx.Fail("%s: serializable call differs (original %d/%d/%d, ours %d/%d/%d)", what, want.serializeCalls,
                 want.serializeSelfOk, want.serializeStreamOk, got.serializeCalls, got.serializeSelfOk,
                 got.serializeStreamOk);
    }
    if (std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) != 0) ctx.Fail("%s: object memory differs", what);
}

// Byte-order flags. Only the low byte of the word at +8 is read; the upper bytes are junk.
constexpr uint32_t kFlags[] = {0x00000000u, 0x00000001u, 0x00000080u, 0x000000ffu, 0x00000100u,
                               0x0000ff00u, 0xffffff00u, 0x01000000u, 0xffffffffu, 0x12345600u};
constexpr uint32_t kWriteResults[] = {0u, 1u, 0x100u, 0xffffffffu, 0x80000001u};

uint32_t PickFlag(std::mt19937 &rng, uint32_t i)
{
    return i < std::size(kFlags) ? kFlags[i] : rng();
}

} // namespace

DIFF_TEST(cRZOStream_QueryInterface, 0x1b5f6c)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, void **)>(0x1b5f6c);
    OStreamVTable vt(ctx);
    std::mt19937 rng(1);
    const uint32_t riids[] = {1u, 0xc51b11cbu, 0u, 2u, 0xc51b11cau, 0xc51b11ccu, 0x80000001u, 0xffffffffu,
                              0x01c51b11u, 0xc51b1100u};
    for (int i = 0; i < 20000; ++i) {
        uint32_t riid = i < int(std::size(riids)) ? riids[i] : rng();
        uint32_t refs = rng() % 5;
        uint32_t flag = PickFlag(rng, i);
        Slot a, b;
        a.Init(vt.slots, refs, flag);
        b.Init(vt.slots, refs, flag);
        // ppv starts as a sentinel; the object's address is replaced by a marker for comparison.
        uint32_t outA[2] = {0xdeadbeefu, 0x5a5a5a5au};
        uint32_t outB[2] = {0xdeadbeefu, 0x5a5a5a5au};
        bool nullOut = (i % 97) == 0 && riid != 1u && riid != 0xc51b11cbu;

        Trace want = Capture(a.ObjectAddress(), 0, [&] {
            return orig(a.Object(), riid, nullOut ? nullptr : reinterpret_cast<void **>(outA));
        });
        Trace got = Capture(b.ObjectAddress(), 0, [&] {
            return static_cast<uint32_t>(b.Object()->QueryInterface(riid, nullOut ? nullptr : reinterpret_cast<void **>(outB)));
        });
        // The caller reads only al for a bool return, so compare the low byte.
        want.ret &= 0xff;
        got.ret &= 0xff;
        Expect(ctx, "QueryInterface", a, b, want, got);
        if (!nullOut) {
            auto mark = [](uint32_t value, const void *obj) {
                return value == reinterpret_cast<uintptr_t>(obj) ? 0x0b1ec7u : value;
            };
            uint32_t ma = mark(outA[0], a.ObjectAddress()), mb = mark(outB[0], b.ObjectAddress());
            ++ctx.cases;
            if (ma != mb || outA[1] != outB[1]) ctx.Fail("QueryInterface(0x%08x): out original 0x%08x/0x%08x, ours 0x%08x/0x%08x", riid, ma, outA[1], mb, outB[1]);
        }
    }
}

DIFF_TEST(cRZOStream_SetSint16, 0x1b621c)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x1b621c);
    OStreamVTable vt(ctx);
    std::mt19937 rng(16);
    const int16_t edges[] = {0, 1, -1, 0x7fff, static_cast<int16_t>(0x8000), 0x0100, 0x00ff, 0x1234, static_cast<int16_t>(0xff00)};
    for (int i = 0; i < 20000; ++i) {
        int16_t value = i < int(std::size(edges)) ? edges[i] : static_cast<int16_t>(rng());
        uint32_t flag = PickFlag(rng, i);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        Slot a, b;
        a.Init(vt.slots, 0, flag);
        b.Init(vt.slots, 0, flag);
        // The original takes a 32-bit stack slot holding the sign-extended value.
        Trace want = Capture(a.ObjectAddress(), wr, [&] { return orig(a.Object(), static_cast<uint32_t>(static_cast<int32_t>(value))); });
        Trace got = Capture(b.ObjectAddress(), wr, [&] { return b.Object()->SetSint16(value); });
        char what[80];
        std::snprintf(what, sizeof(what), "SetSint16(%d) flag 0x%08x", value, flag);
        Expect(ctx, what, a, b, want, got);
    }
}

DIFF_TEST(cRZOStream_SetUint16, 0x1b6459)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x1b6459);
    OStreamVTable vt(ctx);
    std::mt19937 rng(17);
    const uint16_t edges[] = {0, 1, 0x7fff, 0x8000, 0xffff, 0x0100, 0x00ff, 0x1234, 0xff00};
    for (int i = 0; i < 20000; ++i) {
        uint16_t value = i < int(std::size(edges)) ? edges[i] : static_cast<uint16_t>(rng());
        uint32_t flag = PickFlag(rng, i);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        Slot a, b;
        a.Init(vt.slots, 0, flag);
        b.Init(vt.slots, 0, flag);
        Trace want = Capture(a.ObjectAddress(), wr, [&] { return orig(a.Object(), value); });
        Trace got = Capture(b.ObjectAddress(), wr, [&] { return b.Object()->SetUint16(value); });
        char what[80];
        std::snprintf(what, sizeof(what), "SetUint16(%u) flag 0x%08x", value, flag);
        Expect(ctx, what, a, b, want, got);
    }
}

DIFF_TEST(cRZOStream_SetSint32, 0x1b62b0)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x1b62b0);
    OStreamVTable vt(ctx);
    std::mt19937 rng(32);
    const int32_t edges[] = {0, 1, -1, 0x7fffffff, static_cast<int32_t>(0x80000000u), 0x01020304, 0x000000ff,
                             0x0000ff00, static_cast<int32_t>(0xff000000u), 0x00ff00ff};
    for (int i = 0; i < 20000; ++i) {
        int32_t value = i < int(std::size(edges)) ? edges[i] : static_cast<int32_t>(rng());
        uint32_t flag = PickFlag(rng, i);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        Slot a, b;
        a.Init(vt.slots, 0, flag);
        b.Init(vt.slots, 0, flag);
        Trace want = Capture(a.ObjectAddress(), wr, [&] { return orig(a.Object(), static_cast<uint32_t>(value)); });
        Trace got = Capture(b.ObjectAddress(), wr, [&] { return b.Object()->SetSint32(value); });
        char what[80];
        std::snprintf(what, sizeof(what), "SetSint32(0x%08x) flag 0x%08x", static_cast<uint32_t>(value), flag);
        Expect(ctx, what, a, b, want, got);
    }
}

DIFF_TEST(cRZOStream_SetFloat32, 0x1b5ffc)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, float)>(0x1b5ffc);
    OStreamVTable vt(ctx);
    std::mt19937 rng(3232);
    const uint32_t edgeBits[] = {0x00000000u, 0x80000000u, 0x3f800000u, 0x7f800000u, 0xff800000u, 0x7fc00000u,
                                 0x7fa00001u, 0xffa00001u, 0x00000001u, 0x007fffffu, 0x7f7fffffu, 0x01020304u};
    for (int i = 0; i < 20000; ++i) {
        uint32_t bits = i < int(std::size(edgeBits)) ? edgeBits[i] : rng();
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        uint32_t flag = PickFlag(rng, i);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        Slot a, b;
        a.Init(vt.slots, 0, flag);
        b.Init(vt.slots, 0, flag);
        Trace want = Capture(a.ObjectAddress(), wr, [&] { return orig(a.Object(), value); });
        Trace got = Capture(b.ObjectAddress(), wr, [&] { return b.Object()->SetFloat32(value); });
        char what[80];
        std::snprintf(what, sizeof(what), "SetFloat32(0x%08x) flag 0x%08x", bits, flag);
        Expect(ctx, what, a, b, want, got);
    }
}

DIFF_TEST(cRZOStream_SetFloat64, 0x1b608b)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, double)>(0x1b608b);
    OStreamVTable vt(ctx);
    std::mt19937 rng(6464);
    const uint64_t edgeBits[] = {0x0000000000000000ull, 0x8000000000000000ull, 0x3ff0000000000000ull,
                                 0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000000000ull,
                                 0x7ff4000000000001ull, 0x0000000000000001ull, 0x000fffffffffffffull,
                                 0x7fefffffffffffffull, 0x0102030405060708ull};
    for (int i = 0; i < 20000; ++i) {
        uint64_t bits = i < int(std::size(edgeBits)) ? edgeBits[i] : (uint64_t(rng()) << 32) | rng();
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        uint32_t flag = PickFlag(rng, i);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        Slot a, b;
        a.Init(vt.slots, 0, flag);
        b.Init(vt.slots, 0, flag);
        Trace want = Capture(a.ObjectAddress(), wr, [&] { return orig(a.Object(), value); });
        Trace got = Capture(b.ObjectAddress(), wr, [&] { return b.Object()->SetFloat64(value); });
        char what[80];
        std::snprintf(what, sizeof(what), "SetFloat64(0x%016llx) flag 0x%08x", static_cast<unsigned long long>(bits), flag);
        Expect(ctx, what, a, b, want, got);
    }
}

DIFF_TEST(cRZOStream_SetGZSerializable, 0x1b60fc)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x1b60fc);
    OStreamVTable vt(ctx);
    // A serializable whose slot 3 is the recorder.
    void *serSlots[4] = {nullptr, nullptr, nullptr, reinterpret_cast<void *>(&RecordSerialize)};
    struct Serializable {
        void *vtable;
        uint32_t pad[3];
    } serA{serSlots, {}}, serB{serSlots, {}};
    std::mt19937 rng(77);
    for (int i = 0; i < 2000; ++i) {
        uint32_t flag = PickFlag(rng, i);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        uint32_t res = kWriteResults[(i / 5) % std::size(kWriteResults)];
        Slot a, b;
        a.Init(vt.slots, 0, flag);
        b.Init(vt.slots, 0, flag);
        gSerializeResult = res;
        gSerializable = &serA;
        Trace want = Capture(a.ObjectAddress(), wr, [&] { return orig(a.Object(), &serA); });
        gSerializable = &serB;
        Trace got = Capture(b.ObjectAddress(), wr, [&] { return b.Object()->SetGZSerializable(&serB); });
        Expect(ctx, "SetGZSerializable", a, b, want, got);
        // The serializable is only called, never written: its memory must match too.
        ++ctx.cases;
        if (std::memcmp(serA.pad, serB.pad, sizeof(serA.pad)) != 0) ctx.Fail("SetGZSerializable: serializable memory differs");
    }
}
