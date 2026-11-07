// Differential tests for cGZPersistBinaryWriter: QueryInterface, GetCurrent (slot 29) and the typed
// writes that forward to the current stream. The writer carries a copy of the original vtable whose
// AddRef (slot 1) and slot 29 are recorders (slot 29 counts and then calls the original GetCurrent).
// The deque (blocks, count, offset, size) is random per trial; the back element is the recording
// stream, every other entry is a decoy with the same recording vtable, so a wrong index shows up.
// Every element's slots 4-9, 12-15, 17, 29, 32, 35 and 38 record their calls; any other slot records a
// trap. The object and out-pointer arguments of the object writes are fake objects with recording
// vtables. Array writes are called with a preset eax, so the count-0 return (the entry eax) is compared.
#include "difftest.h"
#include "gz/cGZPersistBinaryWriter.h"

#include <array>
#include <cstring>
#include <cstdio>
#include <random>
#include <utility>
#include <vector>

namespace {

struct CallRec {
    int slot;             // 1000 + n for a trap in slot n
    const void *self;     // the element the call was made on
    uint64_t a, b;        // arguments (bit patterns for floats)
    std::vector<uint8_t> bytes;
    bool operator==(const CallRec &o) const { return slot == o.slot && self == o.self && a == o.a && b == o.b && bytes == o.bytes; }
};

std::vector<CallRec> gLog;
uint32_t gRet = 0;
int gCurrents = 0;  // calls to the writer's slot 29
int gAddRefs = 0;   // calls to the writer's slot 1
void *(__thiscall *gOrigCurrent)(void *) = nullptr;

// Element values for the object writes, set per trial: slot 32 returns gSlot32[0] then gSlot32[1]
// (the writer calls it twice per flagged write); slots 35 and 38 return gRet35 and gRet38.
uint32_t gSlot32[2] = {0, 0};
int gSlot32Calls = 0;
uint32_t gRet35 = 0, gRet38 = 0;

// Stream recorders: the 8-bit slots compare only the low byte (the original's byte array loop
// pushes a stack slot whose upper bytes it never writes, and SetUint8 reads only the low byte).
template <int S> uint32_t __fastcall RecU(void *self, void * /*edx*/, uint32_t v)
{
    uint64_t arg = (S == 4 || S == 5) ? (v & 0xff) : v;
    gLog.push_back({S, self, arg, 0, {}});
    return gRet;
}

template <int S> uint32_t __fastcall RecF32(void *self, void * /*edx*/, float v)
{
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    gLog.push_back({S, self, bits, 0, {}});
    return gRet;
}

template <int S> uint32_t __fastcall RecF64(void *self, void * /*edx*/, double v)
{
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    gLog.push_back({S, self, bits, 0, {}});
    return gRet;
}

template <int S> uint32_t __fastcall RecRaw(void *self, void * /*edx*/, const void *buf, uint32_t size)
{
    const auto *p = static_cast<const uint8_t *>(buf);
    gLog.push_back({S, self, size, 0, std::vector<uint8_t>(p, p + size)});
    return gRet;
}

// Element slot 29: the element answers itself (the stream is the back element).
template <int S> void *__fastcall RecSelf(void *self, void * /*edx*/)
{
    gLog.push_back({S, self, 0, 0, {}});
    return self;
}

uint32_t __fastcall RecSlot32(void *self, void * /*edx*/)
{
    uint32_t value = gSlot32[gSlot32Calls++ & 1];
    gLog.push_back({32, self, value, 0, {}});
    return value;
}

uint32_t __fastcall RecSlot35(void *self, void * /*edx*/, void *arg)
{
    gLog.push_back({35, self, reinterpret_cast<uintptr_t>(arg), 0, {}});
    return gRet35;
}

uint32_t __fastcall RecSlot38(void *self, void * /*edx*/, uint32_t count)
{
    gLog.push_back({38, self, count, 0, {}});
    return gRet38;
}

template <int S> uint32_t __fastcall Trap(void *self, void * /*edx*/)
{
    gLog.push_back({1000 + S, self, 0, 0, {}});
    return 0;
}

void *gElemSlots[40];

template <size_t... I> void FillTraps(void **slots, std::index_sequence<I...>)
{
    ((slots[I] = reinterpret_cast<void *>(&Trap<static_cast<int>(I)>)), ...);
}

void InitElemSlots()
{
    static bool done = false;
    if (done) return;
    done = true;
    FillTraps(gElemSlots, std::make_index_sequence<40>{});
    gElemSlots[4] = reinterpret_cast<void *>(&RecU<4>);
    gElemSlots[5] = reinterpret_cast<void *>(&RecU<5>);
    gElemSlots[6] = reinterpret_cast<void *>(&RecU<6>);
    gElemSlots[7] = reinterpret_cast<void *>(&RecU<7>);
    gElemSlots[8] = reinterpret_cast<void *>(&RecU<8>);
    gElemSlots[9] = reinterpret_cast<void *>(&RecU<9>);
    gElemSlots[12] = reinterpret_cast<void *>(&RecF32<12>);
    gElemSlots[13] = reinterpret_cast<void *>(&RecF64<13>);
    gElemSlots[14] = reinterpret_cast<void *>(&RecU<14>);
    gElemSlots[15] = reinterpret_cast<void *>(&RecU<15>);
    gElemSlots[17] = reinterpret_cast<void *>(&RecRaw<17>);
    gElemSlots[29] = reinterpret_cast<void *>(&RecSelf<29>);
    gElemSlots[32] = reinterpret_cast<void *>(&RecSlot32);
    gElemSlots[35] = reinterpret_cast<void *>(&RecSlot35);
    gElemSlots[38] = reinterpret_cast<void *>(&RecSlot38);
}

uint32_t __fastcall RecordAddRef(void * /*self*/, void * /*edx*/)
{
    ++gAddRefs;
    return 1;
}

void *__fastcall RecordCurrent(void *self, void * /*edx*/)
{
    ++gCurrents;
    return gOrigCurrent(self);
}

struct Elem {
    const void *vtable;
    uint32_t pad[3];
};

// The deque for one trial. pool[0] is the back element (the stream); the rest are decoys.
struct Rig {
    Elem pool[33];
    void *blockArr[8][4];
    void **topArr[8];
    uint32_t count = 1, offset = 0, size = 1;

    void Build(std::mt19937 &rng)
    {
        InitElemSlots();
        for (auto &e : pool) e.vtable = gElemSlots;
        count = 1u << (rng() % 4); // 1, 2, 4 or 8 blocks
        for (uint32_t b = 0; b < 8; ++b) {
            topArr[b] = blockArr[b];
            for (int j = 0; j < 4; ++j) blockArr[b][j] = &pool[1 + (b * 4 + j) % 32];
        }
        offset = rng();
        size = (rng() % 8 == 0) ? 0u : 1u + rng() % (4 * count); // size 0 makes the back offset - 1
        uint32_t back = offset + size - 1;
        blockArr[(back >> 2) & (count - 1)][back & 3] = &pool[0];
    }
    const void *Stream() const { return &pool[0]; }
};

// Fake argument objects for the object writes. Slot 4 writes gOutTarget through its out pointer and
// returns the address of rec (rec[1] is the size); slot 5 records whether it got the writer under
// test (gExpectWriter); slot 6 returns gArgCount. The out object's slot 2 is a recorded Release.
struct FakeArg {
    const void *vtable;
    uint32_t rec[2];
};
struct FakeOut {
    const void *vtable;
    uint32_t pad[3];
};
void *gArgSlots[40];
void *gOutSlots[40];
uint32_t gArgCount = 0, gArgRet5 = 0;
const void *gExpectWriter = nullptr;
const void *gOutTarget = nullptr;

void *__fastcall ArgSlot4(void *self, void * /*edx*/, void **out)
{
    gLog.push_back({4, self, out == nullptr ? 0u : 1u, 0, {}});
    if (out) *out = const_cast<void *>(gOutTarget);
    return &static_cast<FakeArg *>(self)->rec[0];
}

uint32_t __fastcall ArgSlot5(void *self, void * /*edx*/, void *writer)
{
    gLog.push_back({5, self, writer == gExpectWriter ? 1u : 0u, 0, {}});
    return gArgRet5;
}

uint32_t __fastcall ArgSlot6(void *self, void * /*edx*/)
{
    gLog.push_back({6, self, 0, 0, {}});
    return gArgCount;
}

uint32_t __fastcall OutRelease(void *self, void * /*edx*/)
{
    gLog.push_back({2, self, 0, 0, {}});
    return 0;
}

void InitFakeSlots()
{
    static bool done = false;
    if (done) return;
    done = true;
    FillTraps(gArgSlots, std::make_index_sequence<40>{});
    gArgSlots[4] = reinterpret_cast<void *>(&ArgSlot4);
    gArgSlots[5] = reinterpret_cast<void *>(&ArgSlot5);
    gArgSlots[6] = reinterpret_cast<void *>(&ArgSlot6);
    FillTraps(gOutSlots, std::make_index_sequence<40>{});
    gOutSlots[2] = reinterpret_cast<void *>(&OutRelease);
}

// A copy of the writer's original vtable with the AddRef and slot-29 recorders.
struct WriterVTable {
    void *slots[32];
    explicit WriterVTable(const difftest::Context &ctx)
    {
        std::memcpy(slots, ctx.image.At(cGZPersistBinaryWriter::kVTable), sizeof(slots));
        slots[cGZPersistBinaryWriter::kAddRefSlot] = reinterpret_cast<void *>(&RecordAddRef);
        slots[cGZPersistBinaryWriter::kCurrentSlot] = reinterpret_cast<void *>(&RecordCurrent);
        gOrigCurrent = ctx.Original<void *(__thiscall *)(void *)>(0x9bfc80);
    }
};

// Canaries around one writer; the object at +16.
struct Slot {
    uint8_t bytes[64];

    void Init(void *const *vtable, Rig &rig)
    {
        std::memset(bytes, 0xA5, sizeof(bytes));
        auto *obj = reinterpret_cast<cGZPersistBinaryWriter *>(bytes + 16);
        obj->mVTable = vtable;
        std::memset(obj->mUnknown4, 0x3c, sizeof(obj->mUnknown4));
        obj->mBlocks = rig.topArr;
        obj->mBlockCount = rig.count;
        obj->mOffset = rig.offset;
        obj->mSize = rig.size;
    }
    cGZPersistBinaryWriter *Object() { return reinterpret_cast<cGZPersistBinaryWriter *>(bytes + 16); }
    const void *ObjectAddress() const { return bytes + 16; }
};

struct Trace {
    uint32_t ret = 0;
    std::vector<CallRec> log;
    int currents = 0;
    int addRefs = 0;
};

template <typename Call> Trace Capture(uint32_t writeResult, Call call)
{
    gLog.clear();
    gRet = writeResult;
    gCurrents = 0;
    gAddRefs = 0;
    gSlot32Calls = 0;
    Trace t;
    t.ret = call();
    t.log = gLog;
    t.currents = gCurrents;
    t.addRefs = gAddRefs;
    return t;
}

// Runs the original call and ours on identical writers sharing one rig and one vtable, then compares
// return value (when checkRet), recorded stream calls, slot-29 and AddRef counts, and writer memory.
template <typename OrigCall, typename OursCall>
void Check(difftest::Context &ctx, const char *what, const WriterVTable &vt, Rig &rig, uint32_t wr, bool checkRet,
           OrigCall origCall, OursCall oursCall)
{
    Slot a, b;
    a.Init(vt.slots, rig);
    b.Init(vt.slots, rig);
    Trace want = Capture(wr, [&] { return origCall(a.Object()); });
    Trace got = Capture(wr, [&] { return oursCall(b.Object()); });
    ++ctx.cases;
    if (checkRet && want.ret != got.ret) ctx.Fail("%s: return original 0x%08x, ours 0x%08x", what, want.ret, got.ret);
    if (want.log.size() != got.log.size()) {
        ctx.Fail("%s: original made %zu stream calls, ours %zu", what, want.log.size(), got.log.size());
    } else {
        for (size_t i = 0; i < want.log.size(); ++i) {
            const CallRec &w = want.log[i], &g = got.log[i];
            if (w == g) continue;
            ctx.Fail("%s: call %zu differs: original slot %d a 0x%llx b 0x%llx (%zu bytes), ours slot %d a 0x%llx b 0x%llx (%zu bytes)",
                     what, i, w.slot, static_cast<unsigned long long>(w.a), static_cast<unsigned long long>(w.b), w.bytes.size(),
                     g.slot, static_cast<unsigned long long>(g.a), static_cast<unsigned long long>(g.b), g.bytes.size());
            break;
        }
    }
    if (want.currents != got.currents) ctx.Fail("%s: slot-29 calls original %d, ours %d", what, want.currents, got.currents);
    if (want.addRefs != got.addRefs) ctx.Fail("%s: AddRef calls original %d, ours %d", what, want.addRefs, got.addRefs);
    if (std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) != 0) ctx.Fail("%s: writer memory differs", what);
}

// Calls a __thiscall function (this in ecx, two stack words, callee cleans) with eax preset to
// eaxValue, so the count-0 return of an array write can be compared. The parameters are read from
// the stack frame in asm, so the compiler sees them as unused.
#pragma warning(disable : 4100)
__declspec(naked) uint32_t __cdecl CallThiscallEax(void *fn, void *self, const void *buf, uint32_t count, uint32_t eaxValue)
{
    __asm {
        push ebx
        mov ebx, esp
        push dword ptr [ebx + 0x14]
        push dword ptr [ebx + 0x10]
        mov ecx, dword ptr [ebx + 0xc]
        mov eax, dword ptr [ebx + 0x18]
        call dword ptr [ebx + 8]
        pop ebx
        ret
    }
}

// The address of a member function (a plain address for this class: no bases, no virtual functions).
template <typename MP> void *AddressOf(MP member)
{
    static_assert(sizeof(MP) == sizeof(void *), "member function pointer is a plain address");
    void *p;
    std::memcpy(&p, &member, sizeof(p));
    return p;
}

constexpr uint32_t kWriteResults[] = {0u, 1u, 0x100u, 0xffffffffu, 0x80000001u};

const uint32_t kIntEdges[] = {0u, 1u, 0x7fu, 0x80u, 0xffu, 0x100u, 0x7fffu, 0x8000u, 0xffffu, 0x10000u,
                              0x7fffffffu, 0x80000000u, 0xffffffffu, 0xffffff00u, 0x01020304u};

// Entry eax values for the array tests: zero, the writer's vtable address, and random values.
uint32_t PickEax(std::mt19937 &rng, int i)
{
    switch (i % 3) {
    case 0: return 0u;
    case 1: return cGZPersistBinaryWriter::kVTable;
    default: return rng();
    }
}

} // namespace

DIFF_TEST(cGZPersistBinaryWriter_QueryInterface, 0x9bfce0)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, void **)>(0x9bfce0);
    WriterVTable vt(ctx);
    std::mt19937 rng(9);
    const uint32_t riids[] = {1u, 0x1b0aab0eu, 0xbb100367u, 0u, 2u, 0x1b0aab0fu, 0xbb100366u, 0xffffffffu, 0x80000001u};
    for (int i = 0; i < 4000; ++i) {
        uint32_t riid = i < int(std::size(riids)) ? riids[i] : (i % 3 == 0 ? riids[rng() % std::size(riids)] : rng());
        bool nullOut = (i % 53) == 0;
        Rig rig;
        rig.Build(rng);
        Slot a, b;
        a.Init(vt.slots, rig);
        b.Init(vt.slots, rig);
        uint32_t outA[2] = {0xdeadbeefu, 0x5a5a5a5au};
        uint32_t outB[2] = {0xdeadbeefu, 0x5a5a5a5au};
        Trace want = Capture(0, [&] {
            return orig(a.Object(), riid, nullOut ? nullptr : reinterpret_cast<void **>(outA));
        });
        Trace got = Capture(0, [&] {
            return static_cast<uint32_t>(b.Object()->QueryInterface(riid, nullOut ? nullptr : reinterpret_cast<void **>(outB)));
        });
        // The caller reads only al for a bool return.
        want.ret &= 0xff;
        got.ret &= 0xff;
        ++ctx.cases;
        if (want.ret != got.ret) ctx.Fail("QueryInterface(0x%08x): return original %u, ours %u", riid, want.ret, got.ret);
        if (want.addRefs != got.addRefs) ctx.Fail("QueryInterface(0x%08x): AddRef original %d, ours %d", riid, want.addRefs, got.addRefs);
        if (!nullOut) {
            auto mark = [](uint32_t value, const void *obj) {
                return value == reinterpret_cast<uintptr_t>(obj) ? 0x0b1ec7u : value;
            };
            uint32_t ma = mark(outA[0], a.ObjectAddress()), mb = mark(outB[0], b.ObjectAddress());
            if (ma != mb || outA[1] != outB[1]) ctx.Fail("QueryInterface(0x%08x): out original 0x%08x, ours 0x%08x", riid, ma, mb);
        }
        if (std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) != 0) ctx.Fail("QueryInterface(0x%08x): writer memory differs", riid);
    }
}

DIFF_TEST(cGZPersistBinaryWriter_GetCurrent, 0x9bfc80)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0x9bfc80);
    WriterVTable vt(ctx);
    std::mt19937 rng(29);
    for (int i = 0; i < 4000; ++i) {
        Rig rig;
        rig.Build(rng);
        // Pointers are returned as 32-bit values (the test is a Win32 build).
        uint32_t ours = 0;
        Check(
            ctx, "GetCurrent", vt, rig, 0, true,
            [&](void *o) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(orig(o))); },
            [&](cGZPersistBinaryWriter *o) {
                ours = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(o->GetCurrent()));
                return ours;
            });
        ++ctx.cases;
        if (ours != static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rig.Stream())))
            ctx.Fail("GetCurrent: returned 0x%08x, not the back element", ours);
    }
}

DIFF_TEST(cGZPersistBinaryWriter_SetUint8, 0x9c0930)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x9c0930);
    WriterVTable vt(ctx);
    std::mt19937 rng(4);
    for (int i = 0; i < 20000; ++i) {
        uint32_t v = i < int(std::size(kIntEdges)) ? kIntEdges[i] : rng();
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "SetUint8(0x%08x)", v);
        Check(ctx, what, vt, rig, wr, true, [&](void *o) { return orig(o, v); },
              [&](cGZPersistBinaryWriter *o) { return o->SetUint8(v); });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_SetSint8, 0x9c0c80)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x9c0c80);
    WriterVTable vt(ctx);
    std::mt19937 rng(5);
    for (int i = 0; i < 20000; ++i) {
        uint32_t v = i < int(std::size(kIntEdges)) ? kIntEdges[i] : rng();
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "SetSint8(0x%08x)", v);
        Check(ctx, what, vt, rig, wr, true, [&](void *o) { return orig(o, v); },
              [&](cGZPersistBinaryWriter *o) { return o->SetSint8(v); });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_SetSint16, 0x9c0be0)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x9c0be0);
    WriterVTable vt(ctx);
    std::mt19937 rng(6);
    for (int i = 0; i < 20000; ++i) {
        uint32_t v = i < int(std::size(kIntEdges)) ? kIntEdges[i] : rng();
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "SetSint16(0x%08x)", v);
        Check(ctx, what, vt, rig, wr, true, [&](void *o) { return orig(o, v); },
              [&](cGZPersistBinaryWriter *o) { return o->SetSint16(v); });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_SetUint16, 0x9c0cb0)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x9c0cb0);
    WriterVTable vt(ctx);
    std::mt19937 rng(8);
    for (int i = 0; i < 20000; ++i) {
        uint32_t v = i < int(std::size(kIntEdges)) ? kIntEdges[i] : rng();
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "SetUint16(0x%08x)", v);
        Check(ctx, what, vt, rig, wr, true, [&](void *o) { return orig(o, v); },
              [&](cGZPersistBinaryWriter *o) { return o->SetUint16(v); });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_SetSint32, 0x9c0c30)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x9c0c30);
    WriterVTable vt(ctx);
    std::mt19937 rng(10);
    for (int i = 0; i < 20000; ++i) {
        uint32_t v = i < int(std::size(kIntEdges)) ? kIntEdges[i] : rng();
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "SetSint32(0x%08x)", v);
        Check(ctx, what, vt, rig, wr, true, [&](void *o) { return orig(o, v); },
              [&](cGZPersistBinaryWriter *o) { return o->SetSint32(v); });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_SetUint32, 0x9c0d00)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x9c0d00);
    WriterVTable vt(ctx);
    std::mt19937 rng(11);
    for (int i = 0; i < 20000; ++i) {
        uint32_t v = i < int(std::size(kIntEdges)) ? kIntEdges[i] : rng();
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "SetUint32(0x%08x)", v);
        Check(ctx, what, vt, rig, wr, true, [&](void *o) { return orig(o, v); },
              [&](cGZPersistBinaryWriter *o) { return o->SetUint32(v); });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_SetFloat32, 0x9c0980)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, float)>(0x9c0980);
    WriterVTable vt(ctx);
    std::mt19937 rng(12);
    const uint32_t edgeBits[] = {0x00000000u, 0x80000000u, 0x3f800000u, 0x7f800000u, 0xff800000u, 0x7fc00000u,
                                 0x7fa00001u, 0x00000001u, 0x7f7fffffu, 0x01020304u};
    for (int i = 0; i < 20000; ++i) {
        uint32_t bits = i < int(std::size(edgeBits)) ? edgeBits[i] : rng();
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "SetFloat32(0x%08x)", bits);
        Check(ctx, what, vt, rig, wr, true, [&](void *o) { return orig(o, value); },
              [&](cGZPersistBinaryWriter *o) { return o->SetFloat32(value); });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_SetFloat64, 0x9c09f0)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, double)>(0x9c09f0);
    WriterVTable vt(ctx);
    std::mt19937 rng(13);
    const uint64_t edgeBits[] = {0x0000000000000000ull, 0x8000000000000000ull, 0x3ff0000000000000ull,
                                 0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000000000ull,
                                 0x0000000000000001ull, 0x0102030405060708ull};
    for (int i = 0; i < 20000; ++i) {
        uint64_t bits = i < int(std::size(edgeBits)) ? edgeBits[i] : (uint64_t(rng()) << 32) | rng();
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "SetFloat64(0x%016llx)", static_cast<unsigned long long>(bits));
        Check(ctx, what, vt, rig, wr, true, [&](void *o) { return orig(o, value); },
              [&](cGZPersistBinaryWriter *o) { return o->SetFloat64(value); });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_SetVoid, 0x9c0bd0)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, const void *, uint32_t)>(0x9c0bd0);
    WriterVTable vt(ctx);
    std::mt19937 rng(14);
    std::vector<uint8_t> data(96);
    for (auto &byte : data) byte = static_cast<uint8_t>(rng());
    for (int i = 0; i < 4000; ++i) {
        uint32_t size = i % 17 == 0 ? 0u : rng() % 90;
        uint32_t off = rng() % 8;
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "SetVoid(%u bytes at +%u)", size, off);
        Check(ctx, what, vt, rig, wr, true, [&](void *o) { return orig(o, data.data() + off, size); },
              [&](cGZPersistBinaryWriter *o) { return o->SetVoid(data.data() + off, size); });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_WriteRZCharStr, 0x9c0c90)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, const char *)>(0x9c0c90);
    WriterVTable vt(ctx);
    std::mt19937 rng(23);
    std::vector<char> text(64);
    for (auto &c : text) c = static_cast<char>(rng());
    for (int i = 0; i < 4000; ++i) {
        const char *str = i % 29 == 0 ? nullptr : text.data() + rng() % 32;
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        // The stream's slot 14 gets the pointer word; the element records it as is.
        Check(ctx, "WriteRZCharStr", vt, rig, wr, true, [&](void *o) { return orig(o, str); },
              [&](cGZPersistBinaryWriter *o) { return o->WriteRZCharStr(str); });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_WriteGZStr, 0x9c0ca0)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, const void *)>(0x9c0ca0);
    WriterVTable vt(ctx);
    std::mt19937 rng(24);
    std::vector<uint8_t> blob(64);
    for (auto &b : blob) b = static_cast<uint8_t>(rng());
    for (int i = 0; i < 4000; ++i) {
        const void *str = i % 31 == 0 ? nullptr : blob.data() + rng() % 32;
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        Check(ctx, "WriteGZStr", vt, rig, wr, true, [&](void *o) { return orig(o, str); },
              [&](cGZPersistBinaryWriter *o) { return o->WriteGZStr(str); });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_SetUint8Array, 0x9c0940)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, const void *, uint32_t)>(0x9c0940);
    WriterVTable vt(ctx);
    std::mt19937 rng(15);
    std::vector<uint8_t> data(64);
    for (auto &byte : data) byte = static_cast<uint8_t>(rng());
    for (int i = 0; i < 4000; ++i) {
        uint32_t count = i % 19 == 0 ? 0u : 1u + rng() % 40;
        uint32_t eax = PickEax(rng, i);
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "SetUint8Array(%u, eax 0x%08x)", count, eax);
        Check(ctx, what, vt, rig, wr, true,
              [&](void *o) { return CallThiscallEax(reinterpret_cast<void *>(orig), o, data.data(), count, eax); },
              [&](cGZPersistBinaryWriter *o) {
                  return CallThiscallEax(AddressOf(&cGZPersistBinaryWriter::SetUint8Array), o, data.data(), count, eax);
              });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_SetSint16Array, 0x9c0bf0)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, const void *, uint32_t)>(0x9c0bf0);
    WriterVTable vt(ctx);
    std::mt19937 rng(16);
    std::vector<uint16_t> data(48);
    for (auto &w : data) w = static_cast<uint16_t>(rng());
    for (int i = 0; i < 4000; ++i) {
        uint32_t count = i % 19 == 0 ? 0u : 1u + rng() % 40;
        uint32_t eax = PickEax(rng, i);
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "SetSint16Array(%u, eax 0x%08x)", count, eax);
        Check(ctx, what, vt, rig, wr, true,
              [&](void *o) { return CallThiscallEax(reinterpret_cast<void *>(orig), o, data.data(), count, eax); },
              [&](cGZPersistBinaryWriter *o) {
                  return CallThiscallEax(AddressOf(&cGZPersistBinaryWriter::SetSint16Array), o, data.data(), count, eax);
              });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_SetUint16Array, 0x9c0cc0)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, const void *, uint32_t)>(0x9c0cc0);
    WriterVTable vt(ctx);
    std::mt19937 rng(17);
    std::vector<uint16_t> data(48);
    for (auto &w : data) w = static_cast<uint16_t>(rng());
    for (int i = 0; i < 4000; ++i) {
        uint32_t count = i % 19 == 0 ? 0u : 1u + rng() % 40;
        uint32_t eax = PickEax(rng, i);
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "SetUint16Array(%u, eax 0x%08x)", count, eax);
        Check(ctx, what, vt, rig, wr, true,
              [&](void *o) { return CallThiscallEax(reinterpret_cast<void *>(orig), o, data.data(), count, eax); },
              [&](cGZPersistBinaryWriter *o) {
                  return CallThiscallEax(AddressOf(&cGZPersistBinaryWriter::SetUint16Array), o, data.data(), count, eax);
              });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_SetSint32Array, 0x9c0c40)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, const void *, uint32_t)>(0x9c0c40);
    WriterVTable vt(ctx);
    std::mt19937 rng(18);
    std::vector<uint32_t> data(32);
    for (auto &w : data) w = rng();
    for (int i = 0; i < 4000; ++i) {
        uint32_t count = i % 19 == 0 ? 0u : 1u + rng() % 24;
        uint32_t eax = PickEax(rng, i);
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "SetSint32Array(%u, eax 0x%08x)", count, eax);
        Check(ctx, what, vt, rig, wr, true,
              [&](void *o) { return CallThiscallEax(reinterpret_cast<void *>(orig), o, data.data(), count, eax); },
              [&](cGZPersistBinaryWriter *o) {
                  return CallThiscallEax(AddressOf(&cGZPersistBinaryWriter::SetSint32Array), o, data.data(), count, eax);
              });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_SetUint32Array, 0x9c0d10)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, const void *, uint32_t)>(0x9c0d10);
    WriterVTable vt(ctx);
    std::mt19937 rng(19);
    std::vector<uint32_t> data(32);
    for (auto &w : data) w = rng();
    for (int i = 0; i < 4000; ++i) {
        uint32_t count = i % 19 == 0 ? 0u : 1u + rng() % 24;
        uint32_t eax = PickEax(rng, i);
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "SetUint32Array(%u, eax 0x%08x)", count, eax);
        Check(ctx, what, vt, rig, wr, true,
              [&](void *o) { return CallThiscallEax(reinterpret_cast<void *>(orig), o, data.data(), count, eax); },
              [&](cGZPersistBinaryWriter *o) {
                  return CallThiscallEax(AddressOf(&cGZPersistBinaryWriter::SetUint32Array), o, data.data(), count, eax);
              });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_SetFloat32Array, 0x9c09a0)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, const void *, uint32_t)>(0x9c09a0);
    WriterVTable vt(ctx);
    std::mt19937 rng(20);
    std::vector<float> data(24);
    for (auto &f : data) {
        uint32_t bits = rng();
        std::memcpy(&f, &bits, sizeof(f));
    }
    for (int i = 0; i < 4000; ++i) {
        uint32_t count = i % 19 == 0 ? 0u : 1u + rng() % 20;
        uint32_t eax = PickEax(rng, i);
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "SetFloat32Array(%u, eax 0x%08x)", count, eax);
        Check(ctx, what, vt, rig, wr, true,
              [&](void *o) { return CallThiscallEax(reinterpret_cast<void *>(orig), o, data.data(), count, eax); },
              [&](cGZPersistBinaryWriter *o) {
                  return CallThiscallEax(AddressOf(&cGZPersistBinaryWriter::SetFloat32Array), o, data.data(), count, eax);
              });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_SetFloat64Array, 0x9c0a10)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, const void *, uint32_t)>(0x9c0a10);
    WriterVTable vt(ctx);
    std::mt19937 rng(21);
    std::vector<double> data(12);
    for (auto &d : data) {
        uint64_t bits = (uint64_t(rng()) << 32) | rng();
        std::memcpy(&d, &bits, sizeof(d));
    }
    for (int i = 0; i < 4000; ++i) {
        uint32_t count = i % 19 == 0 ? 0u : 1u + rng() % 10;
        uint32_t eax = PickEax(rng, i);
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "SetFloat64Array(%u, eax 0x%08x)", count, eax);
        Check(ctx, what, vt, rig, wr, true,
              [&](void *o) { return CallThiscallEax(reinterpret_cast<void *>(orig), o, data.data(), count, eax); },
              [&](cGZPersistBinaryWriter *o) {
                  return CallThiscallEax(AddressOf(&cGZPersistBinaryWriter::SetFloat64Array), o, data.data(), count, eax);
              });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_WriteRecord, 0x9c1230)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, const void *)>(0x9c1230);
    WriterVTable vt(ctx);
    std::mt19937 rng(28);
    for (int i = 0; i < 4000; ++i) {
        uint32_t rec[3] = {rng(), rng(), rng()};
        if (i % 3 == 0) rec[0] = kIntEdges[rng() % std::size(kIntEdges)];
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        Check(ctx, "WriteRecord", vt, rig, wr, true, [&](void *o) { return orig(o, rec); },
              [&](cGZPersistBinaryWriter *o) { return o->WriteRecord(rec); });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_WriteSerialized, 0x9c0a60)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x9c0a60);
    WriterVTable vt(ctx);
    InitFakeSlots();
    std::mt19937 rng(25);
    for (int i = 0; i < 4000; ++i) {
        bool nullObj = i % 13 == 0;
        bool nullOut = i % 3 == 0;
        FakeArg arg;
        arg.vtable = gArgSlots;
        arg.rec[0] = rng();
        arg.rec[1] = rng();
        FakeOut out;
        out.vtable = gOutSlots;
        gOutTarget = nullOut ? nullptr : &out;
        gArgRet5 = rng();
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "WriteSerialized(null %d, out null %d)", nullObj ? 1 : 0, nullOut ? 1 : 0);
        Check(
            ctx, what, vt, rig, wr, true,
            [&](void *o) {
                gExpectWriter = o;
                return orig(o, nullObj ? nullptr : &arg);
            },
            [&](cGZPersistBinaryWriter *o) {
                gExpectWriter = o;
                return o->WriteSerialized(nullObj ? nullptr : &arg);
            });
    }
}

DIFF_TEST(cGZPersistBinaryWriter_WriteFlaggedObject, 0x9c0ad0)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x9c0ad0);
    WriterVTable vt(ctx);
    InitFakeSlots();
    std::mt19937 rng(27);
    for (int i = 0; i < 4000; ++i) {
        bool nullObj = i % 9 == 0;
        FakeArg arg;
        arg.vtable = gArgSlots;
        gArgCount = rng() % 4; // small, so the equal and unequal branches both occur
        gArgRet5 = 0;
        // The first and second slot-32 results are each either the count, count + 1 or random.
        auto pick = [&] {
            switch (rng() % 3) {
            case 0: return gArgCount;
            case 1: return gArgCount + 1;
            default: return static_cast<uint32_t>(rng());
            }
        };
        gSlot32[0] = pick();
        gSlot32[1] = pick();
        gRet35 = rng();
        gRet38 = rng();
        Rig rig;
        rig.Build(rng);
        uint32_t wr = kWriteResults[i % std::size(kWriteResults)];
        char what[80];
        std::snprintf(what, sizeof(what), "WriteFlaggedObject(null %d, count %u, slot32 %u/%u)", nullObj ? 1 : 0, gArgCount,
                      gSlot32[0], gSlot32[1]);
        Check(
            ctx, what, vt, rig, wr, true,
            [&](void *o) { return orig(o, nullObj ? nullptr : &arg); },
            [&](cGZPersistBinaryWriter *o) { return o->WriteFlaggedObject(nullObj ? nullptr : &arg); });
    }
}
