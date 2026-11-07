// Differential tests for cGZPersistDBRecord (the 0x1c-byte base record): QueryInterface, Release, SetKey,
// GetAccessFlags, Close, Abort, QueryChild, SetChild, TestHighFlags, SetHighFlags, SetAccessFlags and the
// deleting destructor. Objects are raw buffers carrying a copy of the record's vtable, whose AddRef slot
// (1) and deleting-destructor slot (19) record their calls instead of counting or freeing (the destructor
// is tested directly, on real heap blocks). The child is a fake interface object whose slots record their
// calls; both sides of a trial use the same child objects, so pointers compare directly. Each function
// runs three ways: the original, ours called directly, and ours through the object's vtable slot.
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"
#include "gz/cGZPersistDBRecord.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <vector>

namespace {

using RefFn = uint32_t(__thiscall *)(void *);
using QueryFn = bool(__thiscall *)(void *, uint32_t, void **);
using KeyFn = void(__thiscall *)(void *, const void *);
using ChildSetFn = void(__thiscall *)(void *, void *);
using BoolFn = bool(__thiscall *)(void *);
using MaskFn = bool(__thiscall *)(void *, uint32_t);
using SetHighFn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using U32ArgFn = uint32_t(__thiscall *)(void *, uint32_t);
using DeletingFn = void *(__thiscall *)(void *, uint32_t);

constexpr size_t kSize = cGZPersistDBRecord::kSize;
constexpr uintptr_t kUnchanged = 0xFACE0000u; // initial *ppvObj; never dereferenced

template <typename T> void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

enum Kind : int64_t {
    kRecAddRefEvent = 1,
    kRecDeleteEvent = 2,
    kChildQueryEvent = 3,
    kChildAddRefEvent = 4,
    kChildReleaseEvent = 5,
    kChildCloseEvent = 6,
    kChildAbortEvent = 7,
};

std::vector<int64_t> gLog;
uint8_t *gRec = nullptr; // the object under test; events report offsets from it
bool gChildAnswer = false;

void Push(int64_t kind, int64_t a, int64_t b)
{
    gLog.push_back(kind);
    gLog.push_back(a);
    gLog.push_back(b);
}

bool InRecord(const void *p)
{
    const auto *b = static_cast<const uint8_t *>(p);
    return gRec && b >= gRec && b < gRec + kSize;
}

int64_t Off(const void *p)
{
    return static_cast<const uint8_t *>(p) - gRec;
}

// The two fake children (the objects themselves are compared by identity through their ids).
struct FakeChild {
    void *vtable;
};
FakeChild gChildA, gChildB;
void *gChildVTable[26];

int64_t ChildId(const void *p)
{
    return p == &gChildA ? 1 : p == &gChildB ? 2 : 3;
}

// A value for the logs: the record's offset, a child's id, or a marker for null and for an untouched pointer.
int64_t MapPointer(const void *p)
{
    if (p == reinterpret_cast<const void *>(kUnchanged)) return INT64_MIN;
    if (!p) return INT64_MIN + 1;
    if (p == &gChildA || p == &gChildB) return 1000 + ChildId(p);
    if (InRecord(p)) return Off(p);
    return 2000;
}

// The record's AddRef (slot 1): counts at +4 and records the call, as the real one does.
uint32_t __fastcall RecAddRef(void *self, void * /*edx*/)
{
    Push(kRecAddRefEvent, Off(self), 0);
    uint16_t count;
    std::memcpy(&count, gRec + 4, sizeof(count));
    ++count;
    std::memcpy(gRec + 4, &count, sizeof(count));
    return count;
}

// The record's deleting destructor (slot 19): records the call and frees nothing.
void *__fastcall RecDelete(void *self, void * /*edx*/, uint32_t flags)
{
    Push(kRecDeleteEvent, Off(self), flags);
    return self;
}

bool __fastcall ChildQuery(void *self, void * /*edx*/, uint32_t riid, void **ppvObj)
{
    Push(kChildQueryEvent, ChildId(self), riid);
    if (gChildAnswer) *ppvObj = self;
    return gChildAnswer;
}

uint32_t __fastcall ChildAddRef(void *self, void * /*edx*/)
{
    Push(kChildAddRefEvent, ChildId(self), 0);
    return 2;
}

uint32_t __fastcall ChildRelease(void *self, void * /*edx*/)
{
    Push(kChildReleaseEvent, ChildId(self), 0);
    return 1;
}

// Slots 24 and 25: the record passes itself; the answer is the scene's.
bool __fastcall ChildClose(void *self, void * /*edx*/, void *arg)
{
    Push(kChildCloseEvent, ChildId(self), Off(arg));
    return gChildAnswer;
}

bool __fastcall ChildAbort(void *self, void * /*edx*/, void *arg)
{
    Push(kChildAbortEvent, ChildId(self), Off(arg));
    return !gChildAnswer;
}

void SetupChildren()
{
    for (auto &slot : gChildVTable) slot = nullptr;
    gChildVTable[0] = reinterpret_cast<void *>(&ChildQuery);
    gChildVTable[1] = reinterpret_cast<void *>(&ChildAddRef);
    gChildVTable[2] = reinterpret_cast<void *>(&ChildRelease);
    gChildVTable[cGZPersistDBRecord::kCloseChildSlot] = reinterpret_cast<void *>(&ChildClose);
    gChildVTable[cGZPersistDBRecord::kAbortChildSlot] = reinterpret_cast<void *>(&ChildAbort);
    gChildA.vtable = gChildVTable;
    gChildB.vtable = gChildVTable;
}

// Copies of the record's vtable (21 slots), with the recording slots in place. `routed` copies have one
// slot replaced by our function.
struct Table {
    void *slots[21];
};

Table MakeTable(const difftest::Context &ctx)
{
    SetupChildren();
    Table t{};
    std::memcpy(t.slots, ctx.image.At(cGZPersistDBRecord::kVTable), sizeof(t.slots));
    t.slots[1] = reinterpret_cast<void *>(&RecAddRef);
    t.slots[cGZPersistDBRecord::kDeleteSlot] = reinterpret_cast<void *>(&RecDelete);
    return t;
}

// One trial's inputs, shared by the original and both replacement runs.
struct Scene {
    std::vector<uint8_t> image; // the whole object; the vtable and child fields are filled in by Run
    void *child;                // nullptr or one of the fake children
    bool answer;                // what the fake children's calls return
};

Scene MakeScene(std::mt19937 &rng)
{
    static void *const kChildren[] = {nullptr, &gChildA, &gChildB, &gChildA};
    Scene s;
    s.image.resize(kSize);
    for (auto &b : s.image) b = static_cast<uint8_t>(rng());
    s.child = kChildren[rng() % 4];
    s.answer = rng() % 2 != 0;
    return s;
}

struct Outcome {
    int64_t result = 0;
    int64_t extra = 0; // the out pointer, as MapPointer maps it
    std::vector<int64_t> log;
    std::vector<uint8_t> bytes;
};

// Runs one call on a fresh copy of the object. The vtable pointer differs between the plain and routed
// tables, so it is zeroed before the bytes are kept. The child pointer is compared as it is.
template <typename Call> Outcome Run(const Table &t, const Scene &s, Call call)
{
    std::vector<uint8_t> buf(s.image);
    const void *vtable = t.slots;
    std::memcpy(buf.data(), &vtable, sizeof(vtable));
    std::memcpy(buf.data() + 0x18, &s.child, sizeof(s.child));
    gRec = buf.data();
    gLog.clear();
    gChildAnswer = s.answer;

    Outcome out;
    call(buf.data(), out);
    out.log = gLog;
    std::memset(buf.data(), 0, sizeof(vtable));
    out.bytes = buf;
    gRec = nullptr;
    return out;
}

void Compare(difftest::Context &ctx, const char *name, const Outcome &a, const Outcome &b, const Outcome &c)
{
    ++ctx.cases;
    if (a.result != b.result || a.result != c.result) {
        ctx.Fail("%s: return original %lld, ours %lld, via slot %lld", name, static_cast<long long>(a.result),
                 static_cast<long long>(b.result), static_cast<long long>(c.result));
    }
    if (a.extra != b.extra || a.extra != c.extra) ctx.Fail("%s: output pointer differs", name);
    if (a.log != b.log || a.log != c.log) {
        ctx.Fail("%s: calls made differ (%zu / %zu / %zu values)", name, a.log.size(), b.log.size(), c.log.size());
    }
    if (a.bytes != b.bytes || a.bytes != c.bytes) ctx.Fail("%s: object state differs", name);
}

} // namespace

// Slot 1 (AddRef, 0x469a20, folded with cTSBehaviorNode): a 16-bit increment at +4, wrapping at 0xffff.
// Whole 0x1c-byte buffers are compared, so a write anywhere else would show.
DIFF_TEST(cGZPersistDBRecord_AddRef, 0x469a20)
{
    auto orig = ctx.Original<RefFn>(0x469a20);
    std::mt19937 rng(0x469a);
    const uint16_t edges[] = {0, 1, 0xfffe, 0xffff};
    for (int i = 0; i < 4000; ++i) {
        uint16_t count = i < 4 ? edges[i] : static_cast<uint16_t>(rng());
        uint8_t a[kSize], b[kSize];
        for (auto &x : a) x = static_cast<uint8_t>(rng());
        std::memcpy(a + 4, &count, sizeof(count));
        std::memcpy(b, a, kSize);
        uint32_t ra = orig(a);
        uint32_t rb = reinterpret_cast<cGZPersistDBRecord *>(b)->AddRef();
        ++ctx.cases;
        if (ra != rb || std::memcmp(a, b, kSize) != 0) ctx.Fail("AddRef from %u: %u vs %u", count, ra, rb);
    }
}

DIFF_TEST(cGZPersistDBRecord_QueryInterface, 0x99cf40)
{
    auto orig = ctx.Original<QueryFn>(0x99cf40);
    Table plain = MakeTable(ctx);
    Table routed = MakeTable(ctx);
    routed.slots[0] = Addr(&cGZPersistDBRecord::QueryInterface);

    const uint32_t ids[] = {0, 1, 2, 0xe56b8f03, 0xe56b8f04, 0xc56b8f08, 0x051b11bd, 0x051b11bc, 0xffffffffu, 0x80000001u};
    std::mt19937 rng(0x99c);
    for (int i = 0; i < 4000; ++i) {
        uint32_t riid = i < 10 ? ids[i] : (i % 2 ? ids[rng() % 10] : static_cast<uint32_t>(rng()));
        Scene s = MakeScene(rng);
        auto a = Run(plain, s, [&](uint8_t *self, Outcome &o) {
            void *out = reinterpret_cast<void *>(kUnchanged);
            o.result = orig(self, riid, &out);
            o.extra = MapPointer(out);
        });
        auto b = Run(plain, s, [&](uint8_t *self, Outcome &o) {
            void *out = reinterpret_cast<void *>(kUnchanged);
            o.result = reinterpret_cast<cGZPersistDBRecord *>(self)->QueryInterface(riid, &out);
            o.extra = MapPointer(out);
        });
        auto c = Run(routed, s, [&](uint8_t *self, Outcome &o) {
            void *out = reinterpret_cast<void *>(kUnchanged);
            o.result = VirtualSlot<QueryFn>(self, 0)(self, riid, &out);
            o.extra = MapPointer(out);
        });
        Compare(ctx, "QueryInterface", a, b, c);
    }
}

DIFF_TEST(cGZPersistDBRecord_Release, 0x99d050)
{
    auto orig = ctx.Original<RefFn>(0x99d050);
    Table plain = MakeTable(ctx);
    Table routed = MakeTable(ctx);
    routed.slots[2] = Addr(&cGZPersistDBRecord::Release);

    const uint16_t edges[] = {0, 1, 2, 3, 0xfffe, 0xffff};
    std::mt19937 rng(0x9d0);
    for (int i = 0; i < 4000; ++i) {
        uint16_t count = i < 6 ? edges[i] : static_cast<uint16_t>(rng() % 3 == 0 ? rng() % 4 : rng());
        Scene s = MakeScene(rng);
        std::memcpy(s.image.data() + 4, &count, sizeof(count));
        auto a = Run(plain, s, [&](uint8_t *self, Outcome &o) { o.result = orig(self); });
        auto b = Run(plain, s, [&](uint8_t *self, Outcome &o) {
            o.result = reinterpret_cast<cGZPersistDBRecord *>(self)->Release();
        });
        auto c = Run(routed, s, [&](uint8_t *self, Outcome &o) { o.result = VirtualSlot<RefFn>(self, 2)(self); });
        Compare(ctx, "Release", a, b, c);
    }
}

DIFF_TEST(cGZPersistDBRecord_SetKey, 0x1e4dd0)
{
    auto orig = ctx.Original<KeyFn>(0x1e4dd0);
    Table plain = MakeTable(ctx);
    Table routed = MakeTable(ctx);
    routed.slots[4] = Addr(&cGZPersistDBRecord::SetKey);

    std::mt19937 rng(0x4e1);
    for (int i = 0; i < 2000; ++i) {
        uint8_t key[16];
        for (auto &b : key) b = static_cast<uint8_t>(rng());
        Scene s = MakeScene(rng);
        auto a = Run(plain, s, [&](uint8_t *self, Outcome &) { orig(self, key); });
        auto b = Run(plain, s, [&](uint8_t *self, Outcome &) { reinterpret_cast<cGZPersistDBRecord *>(self)->SetKey(key); });
        auto c = Run(routed, s, [&](uint8_t *self, Outcome &) { VirtualSlot<KeyFn>(self, 4)(self, key); });
        Compare(ctx, "SetKey", a, b, c);
    }
}

DIFF_TEST(cGZPersistDBRecord_GetAccessFlags, 0x1df630)
{
    auto orig = ctx.Original<RefFn>(0x1df630);
    Table plain = MakeTable(ctx);
    Table routed = MakeTable(ctx);
    routed.slots[5] = Addr(&cGZPersistDBRecord::GetAccessFlags);

    std::mt19937 rng(0x1df);
    for (int i = 0; i < 2000; ++i) {
        Scene s = MakeScene(rng);
        auto a = Run(plain, s, [&](uint8_t *self, Outcome &o) { o.result = orig(self); });
        auto b = Run(plain, s, [&](uint8_t *self, Outcome &o) {
            o.result = reinterpret_cast<cGZPersistDBRecord *>(self)->GetAccessFlags();
        });
        auto c = Run(routed, s, [&](uint8_t *self, Outcome &o) { o.result = VirtualSlot<RefFn>(self, 5)(self); });
        Compare(ctx, "GetAccessFlags", a, b, c);
    }
}

DIFF_TEST(cGZPersistDBRecord_Close, 0x99ba60)
{
    auto orig = ctx.Original<BoolFn>(0x99ba60);
    Table plain = MakeTable(ctx);
    Table routed = MakeTable(ctx);
    routed.slots[6] = Addr(&cGZPersistDBRecord::Close);

    std::mt19937 rng(0xc10);
    for (int i = 0; i < 4000; ++i) {
        Scene s = MakeScene(rng);
        auto a = Run(plain, s, [&](uint8_t *self, Outcome &o) { o.result = orig(self); });
        auto b = Run(plain, s, [&](uint8_t *self, Outcome &o) {
            o.result = reinterpret_cast<cGZPersistDBRecord *>(self)->Close();
        });
        auto c = Run(routed, s, [&](uint8_t *self, Outcome &o) { o.result = VirtualSlot<BoolFn>(self, 6)(self); });
        Compare(ctx, "Close", a, b, c);
    }
}

DIFF_TEST(cGZPersistDBRecord_Abort, 0x99b9e0)
{
    auto orig = ctx.Original<BoolFn>(0x99b9e0);
    Table plain = MakeTable(ctx);
    Table routed = MakeTable(ctx);
    routed.slots[7] = Addr(&cGZPersistDBRecord::Abort);

    std::mt19937 rng(0xab0);
    for (int i = 0; i < 4000; ++i) {
        Scene s = MakeScene(rng);
        auto a = Run(plain, s, [&](uint8_t *self, Outcome &o) { o.result = orig(self); });
        auto b = Run(plain, s, [&](uint8_t *self, Outcome &o) {
            o.result = reinterpret_cast<cGZPersistDBRecord *>(self)->Abort();
        });
        auto c = Run(routed, s, [&](uint8_t *self, Outcome &o) { o.result = VirtualSlot<BoolFn>(self, 7)(self); });
        Compare(ctx, "Abort", a, b, c);
    }
}

DIFF_TEST(cGZPersistDBRecord_QueryChild, 0x1e03f0)
{
    auto orig = ctx.Original<QueryFn>(0x1e03f0);
    Table plain = MakeTable(ctx);
    Table routed = MakeTable(ctx);
    routed.slots[15] = Addr(&cGZPersistDBRecord::QueryChild);

    std::mt19937 rng(0x1e0);
    for (int i = 0; i < 4000; ++i) {
        uint32_t riid = static_cast<uint32_t>(i % 3 == 0 ? rng() : i % 3 == 1 ? 1 : 0xe56b8f03);
        Scene s = MakeScene(rng);
        auto a = Run(plain, s, [&](uint8_t *self, Outcome &o) {
            void *out = reinterpret_cast<void *>(kUnchanged);
            o.result = orig(self, riid, &out);
            o.extra = MapPointer(out);
        });
        auto b = Run(plain, s, [&](uint8_t *self, Outcome &o) {
            void *out = reinterpret_cast<void *>(kUnchanged);
            o.result = reinterpret_cast<cGZPersistDBRecord *>(self)->QueryChild(riid, &out);
            o.extra = MapPointer(out);
        });
        auto c = Run(routed, s, [&](uint8_t *self, Outcome &o) {
            void *out = reinterpret_cast<void *>(kUnchanged);
            o.result = VirtualSlot<QueryFn>(self, 15)(self, riid, &out);
            o.extra = MapPointer(out);
        });
        Compare(ctx, "QueryChild", a, b, c);
    }
}

DIFF_TEST(cGZPersistDBRecord_SetChild, 0x1e5060)
{
    auto orig = ctx.Original<ChildSetFn>(0x1e5060);
    Table plain = MakeTable(ctx);
    Table routed = MakeTable(ctx);
    routed.slots[16] = Addr(&cGZPersistDBRecord::SetChild);

    std::mt19937 rng(0x5e7);
    static void *const kArgs[] = {nullptr, &gChildA, &gChildB};
    for (int i = 0; i < 4000; ++i) {
        void *arg = kArgs[rng() % 3];
        Scene s = MakeScene(rng);
        auto a = Run(plain, s, [&](uint8_t *self, Outcome &) { orig(self, arg); });
        auto b = Run(plain, s, [&](uint8_t *self, Outcome &) { reinterpret_cast<cGZPersistDBRecord *>(self)->SetChild(arg); });
        auto c = Run(routed, s, [&](uint8_t *self, Outcome &) { VirtualSlot<ChildSetFn>(self, 16)(self, arg); });
        Compare(ctx, "SetChild", a, b, c);
    }
}

DIFF_TEST(cGZPersistDBRecord_TestHighFlags, 0x1e0960)
{
    auto orig = ctx.Original<MaskFn>(0x1e0960);
    Table plain = MakeTable(ctx);
    Table routed = MakeTable(ctx);
    routed.slots[17] = Addr(&cGZPersistDBRecord::TestHighFlags);

    std::mt19937 rng(0x1e09);
    for (int i = 0; i < 4000; ++i) {
        // Masks are passed as 32-bit values whose low byte is the argument; the callee reads one byte.
        uint32_t mask = i < 256 ? static_cast<uint32_t>(i) : static_cast<uint32_t>(rng());
        Scene s = MakeScene(rng);
        auto a = Run(plain, s, [&](uint8_t *self, Outcome &o) { o.result = orig(self, mask); });
        auto b = Run(plain, s, [&](uint8_t *self, Outcome &o) {
            o.result = reinterpret_cast<cGZPersistDBRecord *>(self)->TestHighFlags(static_cast<uint8_t>(mask));
        });
        auto c = Run(routed, s, [&](uint8_t *self, Outcome &o) { o.result = VirtualSlot<MaskFn>(self, 17)(self, mask); });
        Compare(ctx, "TestHighFlags", a, b, c);
    }
}

DIFF_TEST(cGZPersistDBRecord_SetHighFlags, 0x1e5380)
{
    auto orig = ctx.Original<SetHighFn>(0x1e5380);
    Table plain = MakeTable(ctx);
    Table routed = MakeTable(ctx);
    routed.slots[18] = Addr(&cGZPersistDBRecord::SetHighFlags);

    std::mt19937 rng(0x1e53);
    for (int i = 0; i < 6000; ++i) {
        // The value and the set flag are one byte each; the high bits of the 32-bit arguments are noise.
        uint32_t value = static_cast<uint32_t>(rng());
        uint32_t set = i % 2 ? static_cast<uint32_t>(rng() & 0xff00) : static_cast<uint32_t>(rng() & 0x1);
        if (i % 5 == 0) set = 0x100;
        Scene s = MakeScene(rng);
        auto a = Run(plain, s, [&](uint8_t *self, Outcome &o) { o.result = orig(self, value, set); });
        auto b = Run(plain, s, [&](uint8_t *self, Outcome &o) {
            o.result = reinterpret_cast<cGZPersistDBRecord *>(self)->SetHighFlags(static_cast<uint8_t>(value),
                                                                                  static_cast<uint8_t>(set));
        });
        auto c = Run(routed, s, [&](uint8_t *self, Outcome &o) {
            o.result = VirtualSlot<SetHighFn>(self, 18)(self, value, set);
        });
        Compare(ctx, "SetHighFlags", a, b, c);
    }
}

DIFF_TEST(cGZPersistDBRecord_SetAccessFlags, 0x1e4b20)
{
    auto orig = ctx.Original<U32ArgFn>(0x1e4b20);
    Table plain = MakeTable(ctx);
    Table routed = MakeTable(ctx);
    routed.slots[20] = Addr(&cGZPersistDBRecord::SetAccessFlags);

    std::mt19937 rng(0x1e4b);
    for (int i = 0; i < 4000; ++i) {
        uint32_t value = i < 256 ? static_cast<uint32_t>(i) : static_cast<uint32_t>(rng());
        Scene s = MakeScene(rng);
        auto a = Run(plain, s, [&](uint8_t *self, Outcome &o) { o.result = orig(self, value); });
        auto b = Run(plain, s, [&](uint8_t *self, Outcome &o) {
            o.result = reinterpret_cast<cGZPersistDBRecord *>(self)->SetAccessFlags(static_cast<uint8_t>(value));
        });
        auto c = Run(routed, s, [&](uint8_t *self, Outcome &o) { o.result = VirtualSlot<U32ArgFn>(self, 20)(self, value); });
        Compare(ctx, "SetAccessFlags", a, b, c);
    }
}

// The deleting destructor frees the block only when flags has bit 0 set. Each side gets its own heap
// block and its frees are recorded across that side's call (FreeRecorder; a freed block's bytes are not
// read). A kept block's bytes and the events are compared.
DIFF_TEST(cGZPersistDBRecord_DeletingDestructor, 0x99b960)
{
    auto orig = ctx.Original<DeletingFn>(0x99b960);
    Table t = MakeTable(ctx);
    const void *vtable = t.slots;

    std::mt19937 rng(0x99b);
    for (int i = 0; i < 2000; ++i) {
        uint32_t flags = static_cast<uint32_t>(i % 4) | (i % 3 == 0 ? static_cast<uint32_t>(rng()) & 0xfffffffeu : 0u);
        Scene s = MakeScene(rng);
        auto *a = static_cast<uint8_t *>(::operator new(kSize));
        auto *b = static_cast<uint8_t *>(::operator new(kSize));
        for (uint8_t *p : {a, b}) {
            std::memcpy(p, s.image.data(), kSize);
            std::memcpy(p, &vtable, sizeof(vtable));
            std::memcpy(p + 0x18, &s.child, sizeof(s.child));
        }

        gRec = a;
        gLog.clear();
        gChildAnswer = s.answer;
        difftest::FreeRecorder recA;
        void *ra = orig(a, flags);
        recA.Stop();
        bool freedA = recA.Freed(a);
        std::vector<int64_t> logA = gLog;
        std::vector<uint8_t> bytesA;
        if (!freedA) bytesA.assign(a, a + kSize);

        gRec = b;
        gLog.clear();
        gChildAnswer = s.answer;
        difftest::FreeRecorder recB;
        void *rb = reinterpret_cast<cGZPersistDBRecord *>(b)->DeletingDestructor(flags);
        recB.Stop();
        bool freedB = recB.Freed(b);
        std::vector<int64_t> logB = gLog;
        std::vector<uint8_t> bytesB;
        if (!freedB) bytesB.assign(b, b + kSize);
        gRec = nullptr;

        ++ctx.cases;
        if (ra != static_cast<void *>(a) || rb != static_cast<void *>(b)) ctx.Fail("DeletingDestructor: return value differs");
        if (freedA != freedB || freedA != ((flags & 1) != 0)) {
            ctx.Fail("DeletingDestructor (flags %u): freed original %d, ours %d", flags, freedA, freedB);
        }
        if (logA != logB) ctx.Fail("DeletingDestructor (flags %u): calls made differ", flags);
        if (bytesA != bytesB) ctx.Fail("DeletingDestructor (flags %u): object state differs", flags);

        if (!freedA) ::operator delete(a);
        if (!freedB) ::operator delete(b);
    }
}
