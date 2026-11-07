// Differential tests for cGZPersistBinaryReader: QueryInterface, the scalar readers (bool, Sint8, Uint8,
// Sint16, Sint32, Uint32, Float32, Float64), the bool array, the raw read forward, the Sint16, Sint32 and
// Float32 array readers (with and without a delegate), and the ring lookup GetObjectRef.
//
// The reader's stream and delegate are fakes with their own vtables. The stream serves bytes from a
// script, fills the destination with a filler on a failed read (the original leaves it untouched, and the
// test must not depend on stale stack bytes), and logs each request with its slot. The delegate and the
// ring's element objects log their calls. Each trial runs three sides from identical state: the original
// (on a reader carrying the original vtable), our function called directly, and our function through a
// vtable copy whose slot points at it. The sides must agree on the return value, the output area, the
// reader's bytes, the logs and the script position.
#include "difftest.h"
#include "game/vtable.h"
#include "gz/cGZPersistBinaryReader.h"

#include <array>
#include <cstring>
#include <new>
#include <functional>
#include <random>
#include <type_traits>
#include <vector>

namespace {

using Reader = cGZPersistBinaryReader;
using RingEntry = Reader::RingEntry;

constexpr size_t kSlotCount = 30;
constexpr size_t kOutBytes = 64; // output area; canaries are kCanary bytes on each side
constexpr size_t kCanary = 8;

// ---- scripted stream and logs --------------------------------------------------------------------

struct Script {
    std::vector<uint8_t> data;
    size_t pos = 0;
    bool refuse = false; // when set, every read fails
};
Script gScript;

// Logged events: kind 1 = stream read (slot, size); 2 = delegate call (slot, dst offset, count);
// 3 = element QueryInterface (id).
using Event = std::array<int64_t, 4>;
std::vector<Event> gLog;
uint8_t *gOutBase = nullptr; // delegate destinations are logged relative to this

// Serves n bytes to dst, or fails when refused or when fewer than n bytes remain. A failed read
// leaves dst untouched (the getters then return whatever their result slot held, which the
// original makes deterministic: bytes of `this` or of an argument). Tests of functions whose result
// slot is uninitialised stack set gFillOnFailure so both sides see the same bytes.
bool gFillOnFailure = false;
bool Serve(void *dst, uint32_t n)
{
    if (gScript.refuse || gScript.data.size() - gScript.pos < n) {
        if (gFillOnFailure) std::memset(dst, 0xEE, n);
        return false;
    }
    std::memcpy(dst, gScript.data.data() + gScript.pos, n);
    gScript.pos += n;
    return true;
}

// Stream slots with a fixed size. __fastcall with two ignored register arguments puts (dst) on the
// stack exactly as a __thiscall call does, and the callee pops it.
template <int Slot, uint32_t N> bool __fastcall FakeRead(void * /*self*/, void * /*edx*/, void *dst)
{
    gLog.push_back({1, Slot, N, 0});
    return Serve(dst, N);
}

bool __fastcall FakeRawRead(void * /*self*/, void * /*edx*/, void *dst, uint32_t n)
{
    gLog.push_back({1, 17, n, 0});
    return Serve(dst, n);
}

// Delegate slots: log the call and return (the delegate writes nothing).
template <int Slot> void __fastcall FakeDelegate(void * /*self*/, void * /*edx*/, void *dst, uint32_t count)
{
    gLog.push_back({2, Slot, static_cast<uint8_t *>(dst) - gOutBase, count});
}

// Ring element QueryInterface: logs the id; accepts (id & 3) == 1 and the second id.
bool __fastcall FakeElementQI(void *self, void * /*edx*/, uint32_t id, void **out)
{
    gLog.push_back({3, 0, id, 0});
    if ((id & 3) != 1 && id != 0xfb0c4344u) return false;
    *out = self;
    return true;
}

// Stream slot 3 (Skip(n)) and 15 (GetGZStr(string)): log the argument (the string relative to the output area) and
// answer by its low bit.
bool __fastcall FakeSkip(void * /*self*/, void * /*edx*/, uint32_t n)
{
    gLog.push_back({1, 3, n, 0});
    return (n & 1) != 0;
}
bool __fastcall FakeGetGZStr(void * /*self*/, void * /*edx*/, void *str)
{
    gLog.push_back({1, 15, static_cast<uint8_t *>(str) - gOutBase, 0});
    return (reinterpret_cast<uintptr_t>(str) & 4) != 0;
}

void *gStreamSlots[18];
void *gDelegateSlots[24];
void *gElementSlots[1];

struct FakeObject {
    void *vtable;
};

FakeObject gStream;
FakeObject gDelegate;
FakeObject gElements[8];

void InitFakes()
{
    gStreamSlots[4] = reinterpret_cast<void *>(&FakeRead<4, 1>);
    gStreamSlots[5] = reinterpret_cast<void *>(&FakeRead<5, 1>);
    gStreamSlots[6] = reinterpret_cast<void *>(&FakeRead<6, 2>);
    gStreamSlots[7] = reinterpret_cast<void *>(&FakeRead<7, 2>);
    gStreamSlots[8] = reinterpret_cast<void *>(&FakeRead<8, 4>);
    gStreamSlots[9] = reinterpret_cast<void *>(&FakeRead<9, 4>);
    gStreamSlots[12] = reinterpret_cast<void *>(&FakeRead<12, 4>);
    gStreamSlots[13] = reinterpret_cast<void *>(&FakeRead<13, 8>);
    gStreamSlots[17] = reinterpret_cast<void *>(&FakeRawRead);
    gStreamSlots[3] = reinterpret_cast<void *>(&FakeSkip);
    gStreamSlots[15] = reinterpret_cast<void *>(&FakeGetGZStr);
    gDelegateSlots[8] = reinterpret_cast<void *>(&FakeDelegate<8>);
    gDelegateSlots[10] = reinterpret_cast<void *>(&FakeDelegate<10>);
    gDelegateSlots[12] = reinterpret_cast<void *>(&FakeDelegate<12>);
    gDelegateSlots[14] = reinterpret_cast<void *>(&FakeDelegate<14>);
    gDelegateSlots[20] = reinterpret_cast<void *>(&FakeDelegate<20>);
    gDelegateSlots[22] = reinterpret_cast<void *>(&FakeDelegate<22>);
    gElementSlots[0] = reinterpret_cast<void *>(&FakeElementQI);
    gStream.vtable = gStreamSlots;
    gDelegate.vtable = gDelegateSlots;
    for (auto &e : gElements) e.vtable = gElementSlots;
}

// ---- readers -------------------------------------------------------------------------------------

struct Slots {
    void *slots[kSlotCount];
};

Slots MakeSlots(difftest::Context &ctx)
{
    Slots s{};
    std::memcpy(s.slots, ctx.image.At(Reader::kVTable), sizeof(s.slots));
    return s;
}

template <typename M> void *Addr(M pmf)
{
    static_assert(sizeof(M) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

// The ring the readers point at. Built per trial; the readers only read it.
struct Ring {
    std::vector<RingEntry *> buffer;
    std::vector<RingEntry> entries;
    std::vector<void *> firsts;  // 8 pointers per entry
    std::vector<void *> seconds; // 8 pointers per entry
    uint32_t sentinel[2] = {0, 0};
};

// The state a reader starts from. Every side builds its reader from the same description.
struct Setup {
    uint32_t refs = 0;
    bool delegate = false;
    uint32_t count = 0;
    uint32_t head = 0;
    uint32_t capacity = 1;
    Ring ring;
};

void BuildRing(Setup &s, std::mt19937 &rng)
{
    Ring &ring = s.ring;
    ring.buffer.assign(s.capacity, nullptr);
    ring.entries.assign(s.capacity, RingEntry{});
    ring.firsts.assign(s.capacity * 8, nullptr);
    ring.seconds.assign(s.capacity * 8, nullptr);
    for (uint32_t i = 0; i < s.capacity; ++i) {
        for (uint32_t k = 0; k < 8; ++k) {
            // Mostly elements, sometimes nulls (the not-found path).
            ring.firsts[i * 8 + k] = rng() % 4 == 0 ? nullptr : &gElements[rng() % 8];
            ring.seconds[i * 8 + k] = rng() % 4 == 0 ? nullptr : &gElements[rng() % 8];
        }
        RingEntry &e = ring.entries[i];
        e.mFirst = reinterpret_cast<void **>(&ring.firsts[i * 8]);
        e.mPad4 = 0;
        e.mPad8 = 0;
        e.mSecond = reinterpret_cast<void **>(&ring.seconds[i * 8]);
        ring.buffer[i] = &e;
    }
}

// Lays a reader out as the constructor and creator leave it, with the given vtable.
void InitReader(Reader &r, const void *vtable, const Setup &s)
{
    std::memset(&r, 0, sizeof(r));
    r.mVTable = vtable;
    r.mRefCount = s.refs;
    r.mSentinel = const_cast<uint32_t *>(s.ring.sentinel);
    r.mBuffer = const_cast<RingEntry **>(s.ring.buffer.data());
    r.mCapacity = s.capacity;
    r.mHead = s.head;
    r.mCount = s.count;
    r.mStream = &gStream;
    r.mDelegate = s.delegate ? &gDelegate : nullptr;
}

// ---- outcomes ------------------------------------------------------------------------------------

struct Outcome {
    std::vector<uint8_t> ret; // the return value's bytes (empty for void)
    std::vector<Event> log;
    size_t pos = 0;
    std::vector<uint8_t> object; // the reader's bytes after the vtable field
    std::vector<uint8_t> out;    // the output area with its canaries
};

// Bool results compare by truth value: the original's null-element path returns the raw low byte of an
// index in al, which callers read as true or false.
template <typename R> std::vector<uint8_t> Bytes(const R &v)
{
    if constexpr (std::is_void_v<R>) {
        return {};
    } else if constexpr (std::is_same_v<R, bool>) {
        return {static_cast<uint8_t>(v ? 1 : 0)};
    } else {
        std::vector<uint8_t> b(sizeof(R));
        std::memcpy(b.data(), &v, sizeof(R));
        return b;
    }
}

enum class Mode { Original, Direct, Slot };

// Calls the function under test in one of the three modes.
template <typename R, typename Member, typename... Args>
R Invoke(Mode mode, difftest::Context &ctx, uint32_t rva, int slot, Member member, Reader *obj, Args... args)
{
    switch (mode) {
    case Mode::Original:
        return ctx.Original<R(__thiscall *)(void *, Args...)>(rva)(obj, args...);
    case Mode::Direct:
        return (obj->*member)(args...);
    case Mode::Slot:
        break;
    }
    return VirtualSlot<R(__thiscall *)(void *, Args...)>(obj, slot)(obj, args...);
}

// One side of a case: a fresh reader and output area, the script, and the call. `call` returns the
// result's bytes. Any pointer to the reader in the output area becomes a marker (each side has its own).
struct Case {
    const Setup *setup;
    std::vector<uint8_t> data;
    bool refuse;
    std::function<std::vector<uint8_t>(Mode, Reader *, uint8_t *)> call;
};

// All three sides build their reader in the same storage, because a failed read returns bytes of
// the reader's address.
alignas(16) unsigned char gReaderStorage[sizeof(Reader)];

Outcome RunSide(const void *vtable, const Case &c, Mode mode)
{
    Reader &reader = *new (gReaderStorage) Reader();
    InitReader(reader, vtable, *c.setup);

    // The output starts on a 256-byte boundary, so code whose result depends on the low byte of an
    // address (the destination's, or another argument's) behaves the same on every run.
    alignas(256) static uint8_t buffer[256 + kOutBytes + kCanary];
    constexpr size_t kAreaBytes = kOutBytes + 2 * kCanary;
    uint8_t *area = buffer + 256 - kCanary;
    std::memset(area, 0xA5, kAreaBytes);
    uint8_t *out = area + kCanary;

    gScript.data = c.data;
    gScript.pos = 0;
    gScript.refuse = c.refuse;
    gLog.clear();
    gOutBase = out;

    Outcome o;
    o.ret = c.call(mode, &reader, out);
    o.log = gLog;
    o.pos = gScript.pos;
    o.object.assign(reinterpret_cast<const uint8_t *>(&reader) + 4, reinterpret_cast<const uint8_t *>(&reader) + sizeof(Reader));
    // Normalise pointers to the reader itself, which differ between sides.
    for (size_t i = 0; i + 4 <= kOutBytes; i += 4) {
        uintptr_t v;
        std::memcpy(&v, out + i, sizeof(v));
        if (v == reinterpret_cast<uintptr_t>(&reader)) {
            uintptr_t marker = 1;
            std::memcpy(out + i, &marker, sizeof(marker));
        }
    }
    o.out.assign(area, area + kAreaBytes);
    return o;
}

// Runs a case three ways and compares them. `checkRet` is false where the original's return value is
// not defined (it leaves a stack-dependent byte in al).
void RunCase(difftest::Context &ctx, const char *name, const Slots &plain, const Slots &routed, const Case &c,
             bool checkRet = true)
{
    Outcome a = RunSide(plain.slots, c, Mode::Original);
    Outcome b = RunSide(plain.slots, c, Mode::Direct);
    Outcome s = RunSide(routed.slots, c, Mode::Slot);
    ++ctx.cases;

    if (checkRet && (a.ret != b.ret || a.ret != s.ret)) {
        auto hex = [](const std::vector<uint8_t> &v) {
            std::string h;
            char buf[4];
            for (uint8_t x : v) {
                std::snprintf(buf, sizeof(buf), "%02x", x);
                h += buf;
            }
            return h;
        };
        ctx.Fail("%s: return value differs: original %s, ours %s, via slot %s", name, hex(a.ret).c_str(), hex(b.ret).c_str(), hex(s.ret).c_str());
    }
    if (a.log != b.log || a.log != s.log) ctx.Fail("%s: stream/delegate/element calls differ", name);
    if (a.pos != b.pos || a.pos != s.pos) ctx.Fail("%s: script position differs", name);
    if (a.object != b.object || a.object != s.object) ctx.Fail("%s: reader bytes differ", name);
    if (a.out != b.out || a.out != s.out) {
        const std::vector<uint8_t> &other = a.out != b.out ? b.out : s.out;
        size_t k = 0;
        while (k < a.out.size() && a.out[k] == other[k]) ++k;
        ctx.Fail("%s: output area differs (%s) at byte %zu: original %02x, ours %02x", name, a.out != b.out ? "direct" : "via slot", k,
                 k < a.out.size() ? a.out[k] : 0u, k < other.size() ? other[k] : 0u);
    }
}

// Random script bytes, short and refused reads included.
std::vector<uint8_t> RandomData(std::mt19937 &rng, size_t maxLen)
{
    std::vector<uint8_t> v(rng() % (maxLen + 1));
    for (auto &b : v) b = static_cast<uint8_t>(rng());
    return v;
}

// Random ring state: capacity a power of two, arbitrary head, count up to capacity.
void RandomRing(Setup &s, std::mt19937 &rng)
{
    static const uint32_t caps[] = {1, 2, 4, 8};
    s.capacity = caps[rng() % 4];
    s.count = static_cast<uint32_t>(rng() % (s.capacity + 1));
    s.head = static_cast<uint32_t>(rng());
    BuildRing(s, rng);
}

// Routed copy of the reader's vtable: the slot points at our function.
Slots Route(const Slots &plain, int slot, void *ours)
{
    Slots r = plain;
    r.slots[slot] = ours;
    return r;
}

} // namespace

// ---- tests ---------------------------------------------------------------------------------------

DIFF_TEST(cGZPersistBinaryReader_QueryInterface, 0x9bfcb0)
{
    InitFakes();
    std::mt19937 rng(0x9bfcb0);
    const Slots plain = MakeSlots(ctx);
    const Slots routed = Route(plain, 0, Addr(&Reader::QueryInterface));
    const uint32_t ids[] = {0, 1, 2, 0xfb0c4343u, 0xfb0c4344u, 0xfb0c4345u, 0x80000001u, 0xffffffffu};
    for (int i = 0; i < 3000; ++i) {
        Setup s;
        s.refs = static_cast<uint32_t>(rng());
        s.delegate = rng() % 2;
        s.ring = Ring{};
        RandomRing(s, rng);
        const uint32_t id = i < 8 ? ids[i] : (rng() % 3 == 0 ? 1u : static_cast<uint32_t>(rng()));
        const bool nullOut = i % 17 == 0;
        Case c{&s, {}, false, [&](Mode m, Reader *r, uint8_t *out) {
                   void **o = nullOut ? nullptr : reinterpret_cast<void **>(out);
                   return Bytes(Invoke<bool>(m, ctx, 0x9bfcb0, 0, &Reader::QueryInterface, r, id, o));
               }};
        RunCase(ctx, "QueryInterface", plain, routed, c);
    }
}

DIFF_TEST(cGZPersistBinaryReader_GetBool, 0x9c0510)
{
    InitFakes();
    std::mt19937 rng(0x9c0510);
    const Slots plain = MakeSlots(ctx);
    const Slots routed = Route(plain, 4, Addr(&Reader::GetBool));
    for (int i = 0; i < 3000; ++i) {
        Setup s;
        RandomRing(s, rng);
        Case c{&s, RandomData(rng, 12), rng() % 8 == 0, [&](Mode m, Reader *r, uint8_t *) {
                   return Bytes(Invoke<bool>(m, ctx, 0x9c0510, 4, &Reader::GetBool, r));
               }};
        RunCase(ctx, "GetBool", plain, routed, c);
    }
}

DIFF_TEST(cGZPersistBinaryReader_GetSint8, 0x9c0800)
{
    InitFakes();
    std::mt19937 rng(0x9c0800);
    const Slots plain = MakeSlots(ctx);
    const Slots routed = Route(plain, 5, Addr(&Reader::GetSint8));
    for (int i = 0; i < 3000; ++i) {
        Setup s;
        RandomRing(s, rng);
        Case c{&s, RandomData(rng, 12), rng() % 8 == 0, [&](Mode m, Reader *r, uint8_t *) {
                   return Bytes(Invoke<int8_t>(m, ctx, 0x9c0800, 5, &Reader::GetSint8, r));
               }};
        RunCase(ctx, "GetSint8", plain, routed, c);
    }
}

DIFF_TEST(cGZPersistBinaryReader_GetUint8, 0x9c0900)
{
    InitFakes();
    std::mt19937 rng(0x9c0900);
    const Slots plain = MakeSlots(ctx);
    const Slots routed = Route(plain, 6, Addr(&Reader::GetUint8));
    for (int i = 0; i < 3000; ++i) {
        Setup s;
        RandomRing(s, rng);
        Case c{&s, RandomData(rng, 12), rng() % 8 == 0, [&](Mode m, Reader *r, uint8_t *) {
                   return Bytes(Invoke<uint8_t>(m, ctx, 0x9c0900, 6, &Reader::GetUint8, r));
               }};
        RunCase(ctx, "GetUint8", plain, routed, c);
    }
}

DIFF_TEST(cGZPersistBinaryReader_GetSint16, 0x9c0740)
{
    InitFakes();
    std::mt19937 rng(0x9c0740);
    const Slots plain = MakeSlots(ctx);
    const Slots routed = Route(plain, 7, Addr(&Reader::GetSint16));
    for (int i = 0; i < 3000; ++i) {
        Setup s;
        RandomRing(s, rng);
        Case c{&s, RandomData(rng, 12), rng() % 8 == 0, [&](Mode m, Reader *r, uint8_t *) {
                   return Bytes(Invoke<int16_t>(m, ctx, 0x9c0740, 7, &Reader::GetSint16, r));
               }};
        RunCase(ctx, "GetSint16", plain, routed, c);
    }
}

DIFF_TEST(cGZPersistBinaryReader_GetSint32, 0x9c07a0)
{
    InitFakes();
    std::mt19937 rng(0x9c07a0);
    const Slots plain = MakeSlots(ctx);
    const Slots routed = Route(plain, 9, Addr(&Reader::GetSint32));
    for (int i = 0; i < 3000; ++i) {
        Setup s;
        RandomRing(s, rng);
        Case c{&s, RandomData(rng, 16), rng() % 8 == 0, [&](Mode m, Reader *r, uint8_t *) {
                   return Bytes(Invoke<int32_t>(m, ctx, 0x9c07a0, 9, &Reader::GetSint32, r));
               }};
        RunCase(ctx, "GetSint32", plain, routed, c);
    }
}

DIFF_TEST(cGZPersistBinaryReader_GetUint32, 0x9c08a0)
{
    InitFakes();
    std::mt19937 rng(0x9c08a0);
    const Slots plain = MakeSlots(ctx);
    const Slots routed = Route(plain, 10, Addr(&Reader::GetUint32));
    for (int i = 0; i < 3000; ++i) {
        Setup s;
        RandomRing(s, rng);
        Case c{&s, RandomData(rng, 16), rng() % 8 == 0, [&](Mode m, Reader *r, uint8_t *) {
                   return Bytes(Invoke<uint32_t>(m, ctx, 0x9c08a0, 10, &Reader::GetUint32, r));
               }};
        RunCase(ctx, "GetUint32", plain, routed, c);
    }
}

DIFF_TEST(cGZPersistBinaryReader_GetFloat32, 0x9c0570)
{
    InitFakes();
    std::mt19937 rng(0x9c0570);
    const Slots plain = MakeSlots(ctx);
    const Slots routed = Route(plain, 11, Addr(&Reader::GetFloat32));
    for (int i = 0; i < 3000; ++i) {
        Setup s;
        RandomRing(s, rng);
        Case c{&s, RandomData(rng, 16), rng() % 8 == 0, [&](Mode m, Reader *r, uint8_t *) {
                   return Bytes(Invoke<float>(m, ctx, 0x9c0570, 11, &Reader::GetFloat32, r));
               }};
        RunCase(ctx, "GetFloat32", plain, routed, c);
    }
}

DIFF_TEST(cGZPersistBinaryReader_GetFloat64, 0x9c05d0)
{
    InitFakes();
    std::mt19937 rng(0x9c05d0);
    const Slots plain = MakeSlots(ctx);
    const Slots routed = Route(plain, 12, Addr(&Reader::GetFloat64));
    gFillOnFailure = true; // the original reads into 8 bytes of uninitialised stack
    for (int i = 0; i < 3000; ++i) {
        Setup s;
        RandomRing(s, rng);
        Case c{&s, RandomData(rng, 24), rng() % 8 == 0, [&](Mode m, Reader *r, uint8_t *) {
                   return Bytes(Invoke<double>(m, ctx, 0x9c05d0, 12, &Reader::GetFloat64, r));
               }};
        RunCase(ctx, "GetFloat64", plain, routed, c);
    }
    gFillOnFailure = false;
}

DIFF_TEST(cGZPersistBinaryReader_GetBoolArray, 0x9c0530)
{
    InitFakes();
    std::mt19937 rng(0x9c0530);
    const Slots plain = MakeSlots(ctx);
    const Slots routed = Route(plain, 13, Addr(&Reader::GetBoolArray));
    for (int i = 0; i < 3000; ++i) {
        Setup s;
        RandomRing(s, rng);
        const uint32_t count = static_cast<uint32_t>(rng() % 17);
        Case c{&s, RandomData(rng, 20), rng() % 8 == 0, [&](Mode m, Reader *r, uint8_t *out) {
                   Invoke<void>(m, ctx, 0x9c0530, 13, &Reader::GetBoolArray, r, reinterpret_cast<bool *>(out), count);
                   return std::vector<uint8_t>{};
               }};
        RunCase(ctx, "GetBoolArray", plain, routed, c);
    }
}

DIFF_TEST(cGZPersistBinaryReader_GetVoid, 0x9c0820)
{
    InitFakes();
    std::mt19937 rng(0x9c0820);
    const Slots plain = MakeSlots(ctx);
    const Slots routed = Route(plain, 14, Addr(&Reader::GetVoid));
    for (int i = 0; i < 3000; ++i) {
        Setup s;
        RandomRing(s, rng);
        const uint32_t n = static_cast<uint32_t>(rng() % 25);
        Case c{&s, RandomData(rng, 32), rng() % 8 == 0, [&](Mode m, Reader *r, uint8_t *out) {
                   return Bytes(Invoke<bool>(m, ctx, 0x9c0820, 14, &Reader::GetVoid, r, static_cast<void *>(out), n));
               }};
        RunCase(ctx, "GetVoid", plain, routed, c);
    }
}

DIFF_TEST(cGZPersistBinaryReader_GetSint16Array, 0x9c0760)
{
    InitFakes();
    std::mt19937 rng(0x9c0760);
    const Slots plain = MakeSlots(ctx);
    const Slots routed = Route(plain, 16, Addr(&Reader::GetSint16Array));
    for (int i = 0; i < 3000; ++i) {
        Setup s;
        s.delegate = rng() % 2;
        RandomRing(s, rng);
        const uint32_t count = static_cast<uint32_t>(rng() % 9);
        Case c{&s, RandomData(rng, 24), rng() % 8 == 0, [&](Mode m, Reader *r, uint8_t *out) {
                   Invoke<void>(m, ctx, 0x9c0760, 16, &Reader::GetSint16Array, r, reinterpret_cast<int16_t *>(out), count);
                   return std::vector<uint8_t>{};
               }};
        RunCase(ctx, "GetSint16Array", plain, routed, c);
    }
}

DIFF_TEST(cGZPersistBinaryReader_GetSint32Array, 0x9c07c0)
{
    InitFakes();
    std::mt19937 rng(0x9c07c0);
    const Slots plain = MakeSlots(ctx);
    const Slots routed = Route(plain, 18, Addr(&Reader::GetSint32Array));
    for (int i = 0; i < 3000; ++i) {
        Setup s;
        s.delegate = rng() % 2;
        RandomRing(s, rng);
        const uint32_t count = static_cast<uint32_t>(rng() % 9);
        Case c{&s, RandomData(rng, 40), rng() % 8 == 0, [&](Mode m, Reader *r, uint8_t *out) {
                   Invoke<void>(m, ctx, 0x9c07c0, 18, &Reader::GetSint32Array, r, reinterpret_cast<int32_t *>(out), count);
                   return std::vector<uint8_t>{};
               }};
        RunCase(ctx, "GetSint32Array", plain, routed, c);
    }
}

DIFF_TEST(cGZPersistBinaryReader_GetFloat32Array, 0x9c0590)
{
    InitFakes();
    std::mt19937 rng(0x9c0590);
    const Slots plain = MakeSlots(ctx);
    const Slots routed = Route(plain, 20, Addr(&Reader::GetFloat32Array));
    for (int i = 0; i < 3000; ++i) {
        Setup s;
        s.delegate = rng() % 2;
        RandomRing(s, rng);
        const uint32_t count = static_cast<uint32_t>(rng() % 9);
        Case c{&s, RandomData(rng, 40), rng() % 8 == 0, [&](Mode m, Reader *r, uint8_t *out) {
                   Invoke<void>(m, ctx, 0x9c0590, 20, &Reader::GetFloat32Array, r, reinterpret_cast<float *>(out), count);
                   return std::vector<uint8_t>{};
               }};
        RunCase(ctx, "GetFloat32Array", plain, routed, c);
    }
}

DIFF_TEST(cGZPersistBinaryReader_GetObjectRef, 0x9c06b0)
{
    InitFakes();
    std::mt19937 rng(0x9c06b0);
    const Slots plain = MakeSlots(ctx);
    const Slots routed = Route(plain, 25, Addr(&Reader::GetObjectRef));
    const uint32_t ids[] = {0, 1, 5, 2, 0x1234u, 0xfb0c4344u, 0xfb0c4345u};
    for (int i = 0; i < 4000; ++i) {
        Setup s;
        s.refs = static_cast<uint32_t>(rng());
        RandomRing(s, rng);
        // Full script only: the index is read as a 32-bit value and indexes an 8-entry table, so a
        // failed read (filler 0xEEEEEEEE) would walk off the table in both sides.
        const uint8_t flag1 = static_cast<uint8_t>(i % 5 == 0 ? 0 : (rng() % 3 == 0 ? 0x80 : 1));
        const uint8_t flag2 = static_cast<uint8_t>(rng() % 2 ? 0xff : 0);
        const uint32_t index = static_cast<uint32_t>(rng() % 8);
        std::vector<uint8_t> data = {flag1, flag2, uint8_t(index), 0, 0, 0};
        const uint32_t id = i < 7 ? ids[i] : static_cast<uint32_t>(rng() % 3 == 0 ? 1u : rng());
        const bool checkRet = flag1 != 0; // with no flag the return is the low byte of the out pointer
        Case c{&s, data, false, [&](Mode m, Reader *r, uint8_t *out) {
                   return Bytes(Invoke<bool>(m, ctx, 0x9c06b0, 25, &Reader::GetObjectRef, r, id, reinterpret_cast<void **>(out)));
               }};
        gFillOnFailure = true; // its flags and index live in uninitialised stack
        RunCase(ctx, "GetObjectRef", plain, routed, c, checkRet);
        gFillOnFailure = false;
    }
}

DIFF_TEST(cGZPersistBinaryReader_GetUint16, 0x9c0840)
{
    InitFakes();
    std::mt19937 rng(0x9c0840);
    const Slots plain = MakeSlots(ctx);
    const Slots routed = Route(plain, 8, Addr(&Reader::GetUint16));
    for (int i = 0; i < 3000; ++i) {
        Setup s;
        RandomRing(s, rng);
        // Short and refused reads included: the result is then the low word of the reader's address.
        Case c{&s, RandomData(rng, 6), rng() % 8 == 0, [&](Mode m, Reader *r, uint8_t *) {
                   return Bytes(Invoke<uint16_t>(m, ctx, 0x9c0840, 8, &Reader::GetUint16, r));
               }};
        RunCase(ctx, "GetUint16", plain, routed, c);
    }
}

// The three array readers added with ReadRoot: delegate slot, stream slot and element size each.
template <typename T>
void RunArrayTest(difftest::Context &ctx, uint32_t rva, int slot, void (Reader::*member)(T *, uint32_t), const char *name)
{
    InitFakes();
    std::mt19937 rng(rva);
    const Slots plain = MakeSlots(ctx);
    const Slots routed = Route(plain, slot, Addr(member));
    for (int i = 0; i < 3000; ++i) {
        Setup s;
        s.delegate = rng() % 2;
        RandomRing(s, rng);
        const uint32_t count = static_cast<uint32_t>(rng() % (kOutBytes / sizeof(T) + 1));
        Case c{&s, RandomData(rng, kOutBytes + 8), rng() % 8 == 0, [&](Mode m, Reader *r, uint8_t *out) {
                   Invoke<void>(m, ctx, rva, slot, member, r, reinterpret_cast<T *>(out), count);
                   return std::vector<uint8_t>{};
               }};
        RunCase(ctx, name, plain, routed, c);
    }
}

DIFF_TEST(cGZPersistBinaryReader_GetUint16Array, 0x9c0860)
{
    RunArrayTest<uint16_t>(ctx, 0x9c0860, 17, &Reader::GetUint16Array, "GetUint16Array");
}

DIFF_TEST(cGZPersistBinaryReader_GetUint32Array, 0x9c08c0)
{
    RunArrayTest<uint32_t>(ctx, 0x9c08c0, 19, &Reader::GetUint32Array, "GetUint32Array");
}

DIFF_TEST(cGZPersistBinaryReader_GetFloat64Array, 0x9c05f0)
{
    RunArrayTest<double>(ctx, 0x9c05f0, 21, &Reader::GetFloat64Array, "GetFloat64Array");
}

DIFF_TEST(cGZPersistBinaryReader_GetGZStrAndSkip, 0x9c0830)
{
    // Covers 0x9c0830 (slot 22, stream slot 15) and 0x9c0920 (slot 23, stream slot 3).
    InitFakes();
    std::mt19937 rng(0x9c0830);
    const Slots plain = MakeSlots(ctx);
    const Slots routedStr = Route(plain, 22, Addr(&Reader::GetGZStr));
    const Slots routedSkip = Route(plain, 23, Addr(&Reader::Skip));
    for (int i = 0; i < 400; ++i) {
        Setup s;
        RandomRing(s, rng);
        const uint32_t offset = static_cast<uint32_t>(rng() % 16) * 4;
        Case str{&s, {}, false, [&](Mode m, Reader *r, uint8_t *out) {
                     return Bytes(Invoke<bool>(m, ctx, 0x9c0830, 22, &Reader::GetGZStr, r, static_cast<void *>(out + offset)));
                 }};
        RunCase(ctx, "GetGZStr", plain, routedStr, str);
        const uint32_t n = static_cast<uint32_t>(rng());
        Case skip{&s, {}, false, [&](Mode m, Reader *r, uint8_t *) {
                      return Bytes(Invoke<bool>(m, ctx, 0x9c0920, 23, &Reader::Skip, r, n));
                  }};
        RunCase(ctx, "Skip", plain, routedSkip, skip);
    }
}
