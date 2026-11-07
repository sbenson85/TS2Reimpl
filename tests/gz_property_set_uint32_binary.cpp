// Differential tests for the uint32-keyed binary delegate cGZPropertySetSerializerBinaryT<cKeyUint32Traits> (lane EU; the
// template in src/gz/cGZPropertySetSerializerDelegates.h, instantiated in cGZPropertySetSerializerUint32Delegates.size.cpp):
//   0x237dc2 Read, 0x23879c Write, 0x237015 DeletingDestructor, 0x236be6 Construct, 0x236edd Destruct, the helpers 0x237c98
//   ReadKey, 0x23822c ReadText, 0x23876b WriteKey (WriteText 0x238987 through Write), the folded QI 0x237d6f, AddRef 0x148fb0 and
//   Release 0x238690 through our vtable.
// Three routes: the original by RVA on the game's shape (built by 0x236be6), ours through our vtable (our constructor), and the
// adapters on the game's shape with a copy of the game's vtable whose forwarded slots are traps (an adapter that dispatched
// would log a trap). Stream fakes: tests/gz_property_set_serializer.h (failure injection, record face). Sets: the game's uint32
// set (0x1bec5f), dumped through its vtable. Compared: result, the set, the stream calls, the read position, the version, the
// bytes written, the record face's reference count; vtable words as "the side's own".
#include "difftest.h"
#include "free_recorder.h"
#include "gz_property_set_serializer.h"
#include "shape.h"
#include "ts_services_fixture.h" // AllocatorScope

#include "gz/cGZPropertySetSerializer.h"

#include <cstring>
#include <new>
#include <random>
#include <vector>

using namespace psfake;

namespace {

using Bin = cGZPropertySetSerializerBinaryUint32;
using BinAdapters = cGZPropertySetSerializerBinaryUint32Adapters;

using BinRead = uint8_t(__thiscall *)(void *, void *, void *);
using BinWrite = uint8_t(__thiscall *)(void *, void *, void *, uint32_t);
constexpr uint32_t kRead = 0x237dc2, kWrite = 0x23879c;

template <class R, class... A> R CallSlot(const void *face, int slot, A... args)
{
    using Fn = R(__thiscall *)(const void *, A...);
    return (*static_cast<Fn *const *>(face))[slot](face, args...);
}
uint32_t Word(const void *p) { return *static_cast<const uint32_t *>(p); }
uint32_t GameWord(uint32_t rva) { return uint32_t(uintptr_t(GameAddress<void>(rva))); }
uint32_t OurVTable()
{
    Bin b;
    return Word(&b);
}

// A game-constructed empty uint32 set (0x1bec5f, 0x18 bytes), destroyed through its slot 13 (flags 0).
struct GameSet {
    alignas(8) uint8_t bytes[0x18];
    explicit GameSet(difftest::Context &ctx) { ctx.Original<void *(__thiscall *)(void *)>(0x1bec5f)(bytes); }
    ~GameSet() { VirtualSlot<void *(__thiscall *)(void *, uint32_t)>(bytes, 13)(bytes, 0); }
    void *Ptr() { return bytes; }
};

Blob DumpSet32(void *set)
{
    Blob out;
    uint32_t key = 0;
    void *value = nullptr;
    using Step = uint8_t(__thiscall *)(void *, void *, void **);
    for (bool ok = VirtualSlot<Step>(set, 6)(set, &key, &value) != 0; ok; ok = VirtualSlot<Step>(set, 7)(set, &key, &value) != 0) {
        out.push_back(key);
        const uint32_t type = VirtualSlot<uint32_t(__thiscall *)(void *)>(value, 4)(value);
        out.push_back(type);
        switch (type) {
        case gzany::kTypeUint32:
        case gzany::kTypeSint32:
        case gzany::kTypeFloat32: out.push_back(static_cast<InlineAny *>(value)->mValue); break;
        case gzany::kTypeBoolean: out.push_back(static_cast<InlineAny *>(value)->mValue & 0xff); break;
        case gzany::kTypeString: {
            cIGZString *text = static_cast<AnyString *>(value)->mString;
            out.push_back(text ? Hash(text->ToChar()) : 0xdeadu);
            break;
        }
        case gzany::kTypeStringSet:
            out.push_back(uint32_t(static_cast<AnyStringSet *>(value)->mSet.size()));
            for (const GZString &s : static_cast<AnyStringSet *>(value)->mSet) out.push_back(Hash(s.data(), s.size()));
            break;
        default: out.push_back(0xcccc); break;
        }
    }
    return out;
}

// One uint32-keyed record (magic 0x2bea4862). A StringSet never goes under a key that already holds another type (the
// original then inserts into that value: ORIGINAL-BUG in the source).
std::vector<uint8_t> MakeRecord32(std::mt19937 &rng, bool allowUnknown, bool oddHeader)
{
    Bytes r;
    const uint32_t magics[] = {0x2bea4862u, gzpropser::kBinaryMagic, 0u, 0x2bea4863u};
    r.U32(oddHeader && rng() % 3 == 0 ? magics[rng() % 4] : 0x2bea4862u);
    const uint16_t versions[] = {0, 1, 2, 3, 0xffff};
    r.U16(oddHeader && rng() % 3 == 0 ? versions[rng() % 5] : uint16_t(rng() % 3));
    const uint32_t count = rng() % 7;
    r.U32(oddHeader && rng() % 4 == 0 ? count + 1 + rng() % 3 : count);
    const uint32_t types[] = {gzany::kTypeUint32, gzany::kTypeSint32, gzany::kTypeFloat32, gzany::kTypeBoolean,
                              gzany::kTypeString, gzany::kTypeStringSet};
    const uint32_t keys[] = {0, 1, 2, 0xffffffffu, 0x80000000u, 0x10, 0xabc};
    std::vector<std::pair<uint32_t, uint32_t>> seen;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t type = types[rng() % 6];
        if (allowUnknown && rng() % 12 == 0) type = 0x12345678;
        uint32_t key = rng() % 4 ? keys[rng() % std::size(keys)] : uint32_t(rng());
        if (type == gzany::kTypeStringSet)
            for (auto &s : seen)
                if (s.first == key && s.second != gzany::kTypeStringSet) key = 0x5e700000u + i;
        seen.push_back({key, type});
        r.U32(type);
        r.U32(key);
        switch (type) {
        case gzany::kTypeUint32:
        case gzany::kTypeSint32: r.U32(rng() % 4 == 0 ? 0xffffffffu : rng()); break;
        case gzany::kTypeFloat32: r.U32(rng()); break;
        case gzany::kTypeBoolean: r.U8(uint8_t(rng() % 3 == 0 ? rng() : rng() % 2)); break;
        case gzany::kTypeString: r.Text(kTexts[rng() % std::size(kTexts)]); break;
        case gzany::kTypeStringSet: {
            const uint32_t n = rng() % 4;
            r.U32(n);
            for (uint32_t j = 0; j < n; ++j) r.Text(kTexts[rng() % std::size(kTexts)]);
            break;
        }
        default: r.U32(rng()); break;
        }
    }
    return r.b;
}

enum Mode { kOriginal, kOurs, kAdapter };
void *gTrapVT[6];
Blob gTraps;
uint32_t TrapHit(void *, unsigned words) { gTraps.push_back(0x7a7a0000 | words); return 0; }
void *Trap(unsigned words) { return shape::TrapFor<TrapHit>(words); }

// The side's delegate: the original's (0x236be6), ours (our constructor), or the original's with a trap copy of its vtable.
struct BinObject {
    alignas(8) uint8_t memory[sizeof(Bin)] = {};
    Bin *s;
    BinObject(difftest::Context &ctx, Mode mode)
    {
        if (mode == kOurs) {
            s = ::new (memory) Bin;
        } else {
            s = static_cast<Bin *>(ctx.Original<void *(__thiscall *)(void *)>(0x236be6)(memory));
            if (mode == kAdapter) {
                std::memcpy(gTrapVT, GameAddress<void>(Bin::kOriginalVTable), sizeof(gTrapVT));
                gTrapVT[3] = Trap(2);
                gTrapVT[4] = Trap(3);
                gTrapVT[5] = Trap(1);
                *reinterpret_cast<void **>(memory) = gTrapVT;
            }
        }
        s->mRefCount = 7;
        s->mVersion = 0x5555;
    }
};

struct ReadResult {
    uint8_t ok;
    Blob set, log;
    uint16_t version;
    size_t pos;
    bool operator==(const ReadResult &) const = default;
};

ReadResult RunRead(difftest::Context &ctx, Mode mode, const std::vector<uint8_t> &record, int failAt,
                   const std::vector<uint8_t> *prefix)
{
    GameSet set(ctx);
    BinObject ser(ctx, mode);
    if (prefix) { // an earlier record read into the set first (keys already there)
        Begin(true, *prefix, -1, false);
        BinObject reader(ctx, kOriginal);
        ctx.Original<BinRead>(kRead)(reader.s, &gStream, set.Ptr());
    }
    Begin(true, record, failAt, false);
    gTraps.clear();
    ReadResult r;
    auto *target = reinterpret_cast<cGZPropertySetUint32 *>(set.Ptr());
    r.ok = mode == kOriginal ? ctx.Original<BinRead>(kRead)(ser.s, &gStream, set.Ptr())
         : mode == kOurs     ? CallSlot<uint8_t>(ser.s, 3, static_cast<void *>(&gStream), target)
                             : BinAdapters::Read(ser.s, nullptr, &gStream, target);
    r.set = DumpSet32(set.Ptr());
    r.log = g.log;
    r.log.insert(r.log.end(), gTraps.begin(), gTraps.end());
    r.version = ser.s->mVersion;
    r.pos = g.pos;
    return r;
}

void CompareRead(difftest::Context &ctx, const char *what, uint32_t seed, int variant, const std::vector<uint8_t> &record,
                 int failAt, const std::vector<uint8_t> *prefix = nullptr, Mode ours = kOurs)
{
    const ReadResult a = RunRead(ctx, kOriginal, record, failAt, prefix);
    const ReadResult b = RunRead(ctx, ours, record, failAt, prefix);
    ++ctx.cases;
    if (!(a == b))
        ctx.Fail("%s seed %u variant %d mode %d: ok %u/%u, set %zu/%zu words%s, log %zu/%zu%s, version %u/%u, pos %zu/%zu", what,
                 seed, variant, int(ours), a.ok, b.ok, a.set.size(), b.set.size(), a.set == b.set ? "" : " (differ)", a.log.size(),
                 b.log.size(), a.log == b.log ? "" : " (differ)", a.version, b.version, a.pos, b.pos);
}

// A test Any of an unknown type (the set wraps a clone in AnyAny; the writer refuses it).
struct OddAny {
    void **vt;
    uint32_t value;
};
void *__fastcall OddDelete(OddAny *self, void *, uint32_t) { return self; }
OddAny *__fastcall OddClone(OddAny *self, void *) { return self; }
uint32_t __fastcall OddType(OddAny *, void *) { return 0x0dd0a11u; }
void *gOddVT[6] = {reinterpret_cast<void *>(&OddDelete), nullptr, nullptr, reinterpret_cast<void *>(&OddClone),
                   reinterpret_cast<void *>(&OddType), nullptr};

struct WriteResult {
    uint8_t ok;
    std::vector<uint8_t> out;
    Blob log;
    int32_t recordRefs;
    bool operator==(const WriteResult &) const = default;
};

WriteResult RunWrite(difftest::Context &ctx, Mode mode, void *set, int failAt, bool hasRecord, uint32_t flag)
{
    BinObject ser(ctx, mode);
    Begin(false, {}, failAt, hasRecord);
    gTraps.clear();
    WriteResult r;
    auto *source = static_cast<cGZPropertySetUint32 *>(set);
    r.ok = mode == kOriginal ? ctx.Original<BinWrite>(kWrite)(ser.s, &gStream, set, flag)
         : mode == kOurs     ? CallSlot<uint8_t>(ser.s, 4, static_cast<void *>(&gStream), source, flag)
                             : BinAdapters::Write(ser.s, nullptr, &gStream, source, uint8_t(flag));
    r.out = g.out;
    r.log = g.log;
    r.log.insert(r.log.end(), gTraps.begin(), gTraps.end());
    r.recordRefs = gRecord.refs;
    return r;
}

void CompareWrite(difftest::Context &ctx, const char *what, uint32_t seed, void *set, int failAt, bool hasRecord, uint32_t flag = 1,
                  Mode ours = kOurs)
{
    const WriteResult a = RunWrite(ctx, kOriginal, set, failAt, hasRecord, flag);
    const WriteResult b = RunWrite(ctx, ours, set, failAt, hasRecord, flag);
    ++ctx.cases;
    if (!(a == b))
        ctx.Fail("%s seed %u fail %d record %d mode %d: ok %u/%u, out %zu/%zu bytes%s, log %zu/%zu%s, refs %d/%d", what, seed,
                 failAt, hasRecord, int(ours), a.ok, b.ok, a.out.size(), b.out.size(), a.out == b.out ? "" : " (differ)",
                 a.log.size(), b.log.size(), a.log == b.log ? "" : " (differ)", a.recordRefs, b.recordRefs);
}

} // namespace

// Read (0x237dc2, with ReadKey 0x237c98 and ReadText 0x23822c inside): whole records, truncations, a failing n-th stream
// call, a record read twice into one set; ours through our vtable, every third seed also through the adapter (trap vtable).
DIFF_TEST(PropSetSer32Bin_Read, 0x237dc2)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 1; seed <= 500; ++seed) {
        std::mt19937 rng(seed);
        const std::vector<uint8_t> record = MakeRecord32(rng, seed % 3 == 0, seed % 4 == 0);
        CompareRead(ctx, "whole", seed, 0, record, -1);
        if (seed % 3 == 0) CompareRead(ctx, "whole", seed, 0, record, -1, nullptr, kAdapter);
        if (seed <= 30) {
            for (size_t len = 0; len < record.size(); ++len)
                CompareRead(ctx, "truncated", seed, int(len), std::vector<uint8_t>(record.begin(), record.begin() + len), -1);
        } else {
            for (int k = 0; k < 3 && !record.empty(); ++k) {
                const size_t len = rng() % record.size();
                CompareRead(ctx, "truncated", seed, int(len), std::vector<uint8_t>(record.begin(), record.begin() + len), -1);
            }
        }
        for (int k = 0; k < 3; ++k) CompareRead(ctx, "failing call", seed, k, record, int(rng() % 24), nullptr, k == 2 ? kAdapter : kOurs);
        if (seed % 5 == 0) CompareRead(ctx, "read twice", seed, 0, record, -1, &record);
    }
}

// Write (0x23879c, with WriteKey 0x23876b and WriteText 0x238987 inside): sets made by the original Read of seeded records (and
// one with an unknown Any type, an empty set), with a failing n-th stream call, with / without the record face, flags.
DIFF_TEST(PropSetSer32Bin_Write, 0x23879c)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 1; seed <= 400; ++seed) {
        std::mt19937 rng(seed);
        const std::vector<uint8_t> record = MakeRecord32(rng, false, false);
        GameSet set(ctx);
        {
            BinObject reader(ctx, kOriginal);
            Begin(true, record, -1, false);
            ctx.Original<BinRead>(kRead)(reader.s, &gStream, set.Ptr());
        }
        if (seed % 7 == 0) { // an unknown Any type somewhere in the set
            OddAny odd{gOddVT, seed};
            const uint32_t key = 0x0dd0000u + seed;
            VirtualSlot<uint8_t(__thiscall *)(void *, const void *, void *)>(set.Ptr(), 3)(set.Ptr(), &key, &odd);
        }
        CompareWrite(ctx, "whole", seed, set.Ptr(), -1, seed % 2 == 0);
        if (seed % 3 == 0) CompareWrite(ctx, "adapter", seed, set.Ptr(), -1, seed % 2 == 0, 1, kAdapter);
        for (int k = 0; k < 4; ++k)
            CompareWrite(ctx, "failing call", seed, set.Ptr(), int(rng() % 30), rng() % 2 == 0, 1, k == 3 ? kAdapter : kOurs);
        if (seed <= 8) CompareWrite(ctx, "flag", seed, set.Ptr(), -1, true, seed % 2 ? 0u : 0x100u);
    }
    GameSet empty(ctx);
    CompareWrite(ctx, "empty set", 0, empty.Ptr(), -1, true);
    CompareWrite(ctx, "empty set", 0, empty.Ptr(), -1, false, 1, kAdapter);
}

// The object: Construct 0x236be6 (the original, the adapter, our constructor, on filled memory), Destruct 0x236edd (the
// original, the adapter, our destructor through slot 5 with 0), the deleting destructor 0x237015 (the original, through our
// vtable, the adapter with traps) and the folded QI 0x237d6f / AddRef 0x148fb0 / Release 0x238690 through each side's vtable.
DIFF_TEST(PropSetSer32Bin_Object, 0x237015)
{
    const uint32_t ourVTable = OurVTable();
    const uint32_t gameVTable = GameWord(Bin::kOriginalVTable);
    auto vtCode = [&](int side, uint32_t word) { return (word == (side == 0 ? gameVTable : ourVTable)) && ourVTable != gameVTable ? 1u : word; };
    for (uint32_t fill : {0u, 0x3c3c3c3cu, 0xffffffffu})
        for (int route = 1; route <= 2; ++route) {
            Blob runs[2];
            for (int side = 0; side < 2; ++side) {
                alignas(8) uint32_t memory[3] = {fill, fill, fill};
                void *r = side == 0 ? ctx.Original<void *(__thiscall *)(void *)>(0x236be6)(memory)
                        : route == 1 ? static_cast<void *>(::new (memory) Bin)
                                     : static_cast<void *>(BinAdapters::Construct(memory, nullptr));
                runs[side] = {r == memory, vtCode(side, memory[0]), memory[1], memory[2]};
                // Destruct: stores the vtable back (seeded with junk first).
                memory[0] = 0x3c3c;
                if (side == 0) ctx.Original<void(__thiscall *)(void *)>(0x236edd)(memory);
                else if (route == 1) reinterpret_cast<Bin *>(memory)->Bin::~Bin(); // qualified: not through the junk vtable
                else BinAdapters::Destruct(reinterpret_cast<Bin *>(memory), nullptr);
                runs[side].insert(runs[side].end(), {vtCode(side, memory[0]), memory[1], memory[2]});
            }
            ++ctx.cases;
            if (runs[0] != runs[1]) ctx.Fail("Construct / Destruct fill %08x route %d differs", fill, route);
        }
    const uint32_t ids[] = {1, 0xebe701f3, 0, 0xebb10cdd, 0xebe701f2};
    // Route 2 (the deleting destructor's adapter) is gone (lane GT: only the game's vtable reaches 0x237015): route 1 only.
    for (int route = 1; route <= 1; ++route)
        for (int slot : {0, 1, 2, 5})
            for (uint32_t count : {0u, 1u, 2u, 0x80000001u})
                for (uint32_t arg = 0; arg < 5; ++arg) {
                    Blob runs[2];
                    for (int side = 0; side < 2; ++side) {
                        auto *p = static_cast<uint8_t *>(::operator new(0xc));
                        std::memset(p, 0x3c, 0xc);
                        if (side == 1 && route == 1) ::new (p) Bin;
                        else ctx.Original<void *(__thiscall *)(void *)>(0x236be6)(p);
                        reinterpret_cast<Bin *>(p)->mRefCount = count;
                        reinterpret_cast<Bin *>(p)->mVersion = uint16_t(0x3c3c + arg);
                        Blob &b = runs[side];
                        void *out = reinterpret_cast<void *>(0x5151);
                        difftest::FreeRecorder rec;
                        switch (slot) {
                        case 0:
                            b.push_back(CallSlot<uint8_t>(p, 0, ids[arg], &out));
                            b.push_back(out == p ? 1 : out == reinterpret_cast<void *>(0x5151) ? 2 : 3);
                            break;
                        case 5:
                            b.push_back((side == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x237015)(p, arg & 1)
                                                   : CallSlot<void *>(p, 5, arg & 1)) == p);
                            break;
                        default: b.push_back(CallSlot<uint32_t>(p, slot)); break;
                        }
                        rec.Stop();
                        const bool freed = rec.Freed(p);
                        b.push_back(freed);
                        if (!freed) {
                            b.push_back(route == 2 && side == 1 ? (Word(p) == ourVTable ? 1u : Word(p)) : vtCode(side, Word(p)));
                            b.insert(b.end(), reinterpret_cast<uint32_t *>(p) + 1, reinterpret_cast<uint32_t *>(p) + 3);
                            ::operator delete(p);
                        }
                    }
                    ++ctx.cases;
                    if (runs[0] != runs[1]) ctx.Fail("route %d slot %d count %08x arg %u differs", route, slot, count, arg);
                }
}

// The helpers on their own (the game calls them from Read / Write only; hooked, so direct calls reach the adapters): ReadKey
// 0x237c98, ReadText 0x23822c (into a game-built cRZString holding an old text), WriteKey 0x23876b; the original by RVA, ours as
// a member and through the adapter, with every failing stream call.
DIFF_TEST(PropSetSer32Bin_Helpers, 0x237c98)
{
    svcfx::AllocatorScope allocator;
    const std::vector<std::vector<uint8_t>> inputs = {
        {}, {1, 2, 3}, {0, 0, 0, 0}, {4, 0, 0, 0, 'a', 'b', 'c', 'd'}, {5, 0, 0, 0, 'a', 0, 'c'},
        {20, 0, 0, 0, 'a', ' ', 'l', 'o', 'n', 'g', 'e', 'r', ' ', 't', 'e', 'x', 't', ' ', 'h', 'e', 'r', 'e', '!', '!'}};
    for (size_t i = 0; i < inputs.size(); ++i)
        for (int failAt = -1; failAt < 3; ++failAt)
            for (int helper = 0; helper < 3; ++helper) {
                Blob runs[3];
                for (int route = 0; route < 3; ++route) {
                    alignas(8) uint8_t memory[sizeof(Bin)] = {};
                    Bin *self = reinterpret_cast<Bin *>(memory);
                    Begin(helper != 2, inputs[i], failAt, false);
                    Blob &b = runs[route];
                    if (helper == 0) {
                        uint32_t key = 0x77777777;
                        b.push_back(route == 0   ? ctx.Original<uint8_t(__thiscall *)(void *, void *, void *)>(0x237c98)(self, &gStream, &key)
                                    : route == 1 ? self->ReadKey(&gStream, &key)
                                                 : BinAdapters::ReadKey(self, nullptr, &gStream, &key));
                        b.push_back(key);
                    } else if (helper == 1) {
                        cRZString text;
                        text.Construct("an old text that is longer than the buffer");
                        b.push_back(route == 0   ? ctx.Original<uint8_t(__thiscall *)(void *, void *, void *)>(0x23822c)(self, &gStream, &text)
                                    : route == 1 ? self->ReadText(&gStream, &text)
                                                 : BinAdapters::ReadText(self, nullptr, &gStream, &text));
                        b.push_back(uint32_t(text.mString.size()));
                        b.push_back(Hash(text.mString.data(), text.mString.size()));
                        text.Destruct();
                    } else {
                        const uint32_t key = 0x12345678u + uint32_t(i);
                        b.push_back(route == 0   ? ctx.Original<uint8_t(__thiscall *)(void *, void *, const void *)>(0x23876b)(self, &gStream, &key)
                                    : route == 1 ? self->WriteKey(&gStream, &key)
                                                 : BinAdapters::WriteKey(self, nullptr, &gStream, &key));
                        b.insert(b.end(), g.out.begin(), g.out.end());
                    }
                    b.insert(b.end(), g.log.begin(), g.log.end());
                    b.push_back(uint32_t(g.pos));
                }
                ctx.cases += 2;
                if (runs[0] != runs[1] || runs[0] != runs[2])
                    ctx.Fail("helper %d input %zu fail %d: member %s, adapter %s", helper, i, failAt, runs[0] == runs[1] ? "same" : "differs",
                             runs[0] == runs[2] ? "same" : "differs");
            }
}
