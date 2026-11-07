// Differential tests for the string property-set serializers (src/gz/cGZPropertySetSerializer.cpp, lane DU):
//   BinaryT  0x555510 Read, 0x556570 Write, 0x552160 DeletingDestructor
//   dispatcher 0x555c90 Read, 0x555b10 ReadWith, 0x5568c0 Write, 0x552190 DeletingDestructor, 0x555400 QueryInterface
//
// Read: synthesised records (tests/gz_property_set_serializer.h: every value type, 0-6 properties, odd magics,
// versions 0-3 and 0xffff, counts above the properties written, unknown types, empty / NUL / long keys and texts,
// StringSets with 0-3 strings and duplicates, keys repeated with other types and other case) read into a fresh
// game-constructed set on each side; whole records, every truncation of some records, and a failing n-th stream
// call. Compared: the result (al), the set (every key, type and value in order), the serializer's version, the
// stream calls with their positions, and the read position. Write: sets made by reading such records (and an
// empty set, and one with an unknown Any type), written by both sides; compared: result, bytes written, every stream
// call (with its value) and the record-face calls, with a failing n-th stream call and with / without the record
// face. Dispatcher: fake delegates record what they are asked; the creation path is checked by the object it creates.
#include "difftest.h"
#include "free_recorder.h"
#include "gz_property_set_serializer.h"
#include "ts_services_fixture.h" // AllocatorScope

#include "gz/cGZPropertySetSerializer.h"

#include <bit>
#include <cstring>
#include <random>
#include <vector>

using namespace psfake;

namespace {

template <class Fn> Fn Game(uint32_t rva) { return reinterpret_cast<Fn>(GameAddress<void>(rva)); }

using BinRead = uint8_t(__thiscall *)(void *, void *, void *);
using BinWrite = uint8_t(__thiscall *)(void *, void *, void *, uint32_t);

// A game-constructed empty string set (0x22b157), destroyed by the game's destructor (0x1777e0).
struct GameSet {
    alignas(8) uint8_t bytes[0x24];
    GameSet() { Game<void *(__thiscall *)(void *)>(0x22b157)(bytes); }
    ~GameSet() { Game<void(__thiscall *)(void *)>(0x1777e0)(bytes); }
    void *Ptr() { return bytes; }
};

using Bin = cGZPropertySetSerializerBinaryString;

// Slot `slot` of the vtable at `face` (the object's own), called with `this` = face. Byte returns are compared raw (al).
template <class R, class... A> R CallSlot(const void *face, int slot, A... args)
{
    using Fn = R(__thiscall *)(const void *, A...);
    return (*static_cast<Fn *const *>(face))[slot](face, args...);
}
uint32_t Word(const void *p) { return *static_cast<const uint32_t *>(p); }
uint32_t GameWord(uint32_t rva) { return uint32_t(uintptr_t(GameAddress<void>(rva))); }

// The binary delegate (standalone shape, lane EF). Original side: the game's shape, built as the dispatcher's inline code
// builds it (no constructor function: the vtable, count and version); our side: our constructor (our vtable). Calls on our
// side go through the object's own vtable. Mode 2 (the adapters test): the game's shape with a copy of the game's vtable whose
// forwarded slots are traps, called through the adapters.
enum Mode { kOriginal, kOurs, kAdapter };
void *gBinTrapVT[6];
Blob gTraps;
uint32_t __fastcall BinTrap1(void *, void *, uint32_t) { gTraps.push_back(0x7a7a0001); return 0; }
uint32_t __fastcall BinTrap2(void *, void *, uint32_t, uint32_t) { gTraps.push_back(0x7a7a0002); return 0; }
uint32_t __fastcall BinTrap3(void *, void *, uint32_t, uint32_t, uint32_t) { gTraps.push_back(0x7a7a0003); return 0; }

struct BinObject {
    alignas(8) uint8_t memory[sizeof(Bin)] = {};
    Bin *s;
    explicit BinObject(Mode mode)
    {
        if (mode == kOurs) {
            s = ::new (memory) Bin;
        } else {
            *reinterpret_cast<const void **>(memory) = GameAddress<const void>(Bin::kOriginalVTable);
            s = reinterpret_cast<Bin *>(memory);
            if (mode == kAdapter) {
                std::memcpy(gBinTrapVT, GameAddress<void>(Bin::kOriginalVTable), sizeof(gBinTrapVT));
                gBinTrapVT[3] = reinterpret_cast<void *>(&BinTrap2);
                gBinTrapVT[4] = reinterpret_cast<void *>(&BinTrap3);
                gBinTrapVT[5] = reinterpret_cast<void *>(&BinTrap1);
                *reinterpret_cast<void **>(memory) = gBinTrapVT;
            }
        }
        s->mRefCount = 7;
        s->mVersion = 0x5555;
    }
};
// Our binary delegate's vtable.
uint32_t OurBinVTable()
{
    Bin b;
    return Word(&b);
}

struct ReadResult {
    uint8_t ok;
    Blob set, log;
    uint16_t version;
    size_t pos;
};

ReadResult RunRead(difftest::Context &ctx, Mode mode, const std::vector<uint8_t> &record, int failAt,
                   const std::vector<uint8_t> *prefix)
{
    GameSet set;
    BinObject ser(mode);
    if (prefix) { // an earlier record read into the set first (duplicate keys across records)
        Begin(true, *prefix, -1, false);
        ctx.Original<BinRead>(0x555510)(ser.s, &gStream, set.Ptr());
    }
    Begin(true, record, failAt, false);
    gTraps.clear();
    ReadResult r;
    auto *target = reinterpret_cast<cGZPropertySetString *>(set.Ptr());
    r.ok = mode == kOriginal ? ctx.Original<BinRead>(0x555510)(ser.s, &gStream, set.Ptr())
         : mode == kOurs     ? CallSlot<uint8_t>(ser.s, 3, static_cast<void *>(&gStream), target)
                             : cGZPropertySetSerializerBinaryStringAdapters::Read(ser.s, nullptr, &gStream, target);
    r.set = DumpSet(set.Ptr());
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
    if (a.ok != b.ok || a.set != b.set || a.log != b.log || a.version != b.version || a.pos != b.pos)
        ctx.Fail("%s seed %u variant %d: ok %u/%u, set %zu/%zu words%s, log %zu/%zu%s, version %u/%u, pos %zu/%zu", what, seed,
                 variant, a.ok, b.ok, a.set.size(), b.set.size(), a.set == b.set ? "" : " (differ)", a.log.size(), b.log.size(),
                 a.log == b.log ? "" : " (differ)", a.version, b.version, a.pos, b.pos);
}

} // namespace

DIFF_TEST(PropSetSer_BinaryRead, 0x555510)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 1; seed <= 600; ++seed) {
        std::mt19937 rng(seed);
        const std::vector<uint8_t> record = MakeRecord(rng, seed % 3 == 0, seed % 4 == 0);
        CompareRead(ctx, "whole", seed, 0, record, -1);
        // Truncations: every length for the first seeds, a few random ones after.
        if (seed <= 40) {
            for (size_t len = 0; len < record.size(); ++len)
                CompareRead(ctx, "truncated", seed, int(len), std::vector<uint8_t>(record.begin(), record.begin() + len), -1);
        } else {
            for (int k = 0; k < 3 && !record.empty(); ++k) {
                const size_t len = rng() % record.size();
                CompareRead(ctx, "truncated", seed, int(len), std::vector<uint8_t>(record.begin(), record.begin() + len), -1);
            }
        }
        // A failing n-th stream call.
        for (int k = 0; k < 3; ++k) CompareRead(ctx, "failing call", seed, k, record, int(rng() % 24));
        // The record read twice into one set: every key exists already, with the same type (Assign; StringSets get
        // the strings again). A record whose StringSet key holds another type crashes the original (ORIGINAL-BUG in
        // the source), so the keys must come from the same record.
        if (seed % 5 == 0) CompareRead(ctx, "read twice", seed, 0, record, -1, &record);
    }
}

namespace {

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
};

WriteResult RunWrite(difftest::Context &ctx, Mode mode, void *set, int failAt, bool hasRecord, uint32_t flag)
{
    BinObject ser(mode);
    Begin(false, {}, failAt, hasRecord);
    gTraps.clear();
    WriteResult r;
    auto *source = static_cast<cGZPropertySetString *>(set);
    r.ok = mode == kOriginal ? ctx.Original<BinWrite>(0x556570)(ser.s, &gStream, set, flag)
         : mode == kOurs     ? CallSlot<uint8_t>(ser.s, 4, static_cast<void *>(&gStream), source, flag)
                             : cGZPropertySetSerializerBinaryStringAdapters::Write(ser.s, nullptr, &gStream, source, uint8_t(flag));
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
    if (a.ok != b.ok || a.out != b.out || a.log != b.log || a.recordRefs != b.recordRefs)
        ctx.Fail("%s seed %u fail %d record %d: ok %u/%u, out %zu/%zu bytes%s, log %zu/%zu%s, refs %d/%d", what, seed, failAt,
                 hasRecord, a.ok, b.ok, a.out.size(), b.out.size(), a.out == b.out ? "" : " (differ)", a.log.size(), b.log.size(),
                 a.log == b.log ? "" : " (differ)", a.recordRefs, b.recordRefs);
}

} // namespace

DIFF_TEST(PropSetSer_BinaryWrite, 0x556570)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 1; seed <= 400; ++seed) {
        std::mt19937 rng(seed);
        const std::vector<uint8_t> record = MakeRecord(rng, false, false);
        GameSet set;
        BinObject reader(kOriginal);
        Begin(true, record, -1, false);
        ctx.Original<BinRead>(0x555510)(reader.s, &gStream, set.Ptr());
        if (seed % 7 == 0) { // an unknown Any type somewhere in the set
            OddAny odd{gOddVT, seed};
            GZServiceString key("odd");
            VirtualSlot<uint8_t(__thiscall *)(void *, void *, void *)>(set.Ptr(), 3)(set.Ptr(), &key, &odd);
        }
        CompareWrite(ctx, "whole", seed, set.Ptr(), -1, seed % 2 == 0);
        for (int k = 0; k < 4; ++k) CompareWrite(ctx, "failing call", seed, set.Ptr(), int(rng() % 30), rng() % 2 == 0);
        if (seed <= 8) // the flag is ignored
            CompareWrite(ctx, "flag", seed, set.Ptr(), -1, true, seed % 2 ? 0u : 0x100u);
    }
    GameSet empty;
    CompareWrite(ctx, "empty set", 0, empty.Ptr(), -1, true);
    CompareWrite(ctx, "empty set", 0, empty.Ptr(), -1, false);
}

// The deleting destructor (slot 5), the folded QueryInterface / AddRef / Release (slots 0-2) on each side's own heap object
// (the original's with a junk vtable word for the destructor, which stores it back; ours through its own vtable). Compared:
// result, out-parameter, the object's words (the vtable word as "the side's own") and whether it was freed.
DIFF_TEST(PropSetSer_BinaryDeletingDestructor, 0x552160)
{
    const uint32_t ourVTable = OurBinVTable();
    const uint32_t ids[] = {1, 0xebe701f3, 0, 0x4bb0d269, 0xebe701f2};
    for (int slot = 0; slot < 6; ++slot) {
        if (slot == 3 || slot == 4) continue; // Read / Write: their own tests
        for (uint32_t count : {0u, 1u, 2u, 0x80000001u})
            for (uint32_t arg = 0; arg < 5; ++arg) {
                Blob runs[2];
                for (int side = 0; side < 2; ++side) {
                    auto *p = static_cast<uint8_t *>(::operator new(0xc));
                    std::memset(p, 0x3c, 0xc);
                    if (side == 1) ::new (p) Bin;
                    if (side == 0) *reinterpret_cast<const void **>(p) = GameAddress<const void>(Bin::kOriginalVTable);
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
                        if (side == 0 && arg == 0) std::memset(p, 0x3c, 4); // the destructor stores the vtable back
                        b.push_back((side == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x552160)(p, arg & 1)
                                               : CallSlot<void *>(p, 5, arg & 1)) == p);
                        break;
                    default: b.push_back(CallSlot<uint32_t>(p, slot)); break;
                    }
                    rec.Stop();
                    const bool freed = rec.Freed(p);
                    b.push_back(freed);
                    if (!freed) {
                        const uint32_t own = side == 0 ? GameWord(Bin::kOriginalVTable) : ourVTable;
                        b.push_back(Word(p) == own && ourVTable != GameWord(Bin::kOriginalVTable) ? 1u : Word(p));
                        b.insert(b.end(), reinterpret_cast<uint32_t *>(p) + 1, reinterpret_cast<uint32_t *>(p) + 3);
                        ::operator delete(p);
                    }
                }
                ++ctx.cases;
                if (runs[0] != runs[1]) ctx.Fail("slot %d count %08x arg %u differs", slot, count, arg);
            }
    }
}

// The binary delegate's adapters on the game's shape with traps in the forwarded slots (an adapter that dispatched through
// the object's vtable would log a trap), against the original on the same inputs.
DIFF_TEST(PropSetSer_BinaryAdapters, 0x555510)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        std::mt19937 rng(seed);
        const std::vector<uint8_t> record = MakeRecord(rng, seed % 3 == 0, seed % 4 == 0);
        CompareRead(ctx, "adapter", seed, 0, record, -1, nullptr, kAdapter);
        CompareRead(ctx, "adapter failing call", seed, 1, record, int(rng() % 20), nullptr, kAdapter);
        GameSet set;
        BinObject reader(kOriginal);
        Begin(true, record, -1, false);
        ctx.Original<BinRead>(0x555510)(reader.s, &gStream, set.Ptr());
        CompareWrite(ctx, "adapter", seed, set.Ptr(), -1, seed % 2 == 0, 1, kAdapter);
        CompareWrite(ctx, "adapter failing call", seed, set.Ptr(), int(rng() % 30), true, 0x100, kAdapter);
    }
    for (uint32_t flags : {0u, 1u}) {
        Blob runs[2];
        for (int side = 0; side < 2; ++side) {
            auto *p = static_cast<uint8_t *>(::operator new(0xc));
            std::memset(p, 0x3c, 0xc);
            *reinterpret_cast<void **>(p) = gBinTrapVT; // the game's copy with traps (filled by the BinObject above)
            gTraps.clear();
            difftest::FreeRecorder rec;
            void *r = side == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x552160)(p, flags)
                                : cGZPropertySetSerializerBinaryStringAdapters::DeletingDestructor(reinterpret_cast<Bin *>(p), nullptr, flags);
            rec.Stop();
            const bool freed = rec.Freed(p);
            runs[side] = {uint32_t(r == p), uint32_t(freed)};
            runs[side].insert(runs[side].end(), gTraps.begin(), gTraps.end());
            if (!freed) {
                // The original stores the game's vtable, the adapter (our destructor) ours: as "a vtable of the side".
                const uint32_t own = side == 0 ? GameWord(Bin::kOriginalVTable) : OurBinVTable();
                runs[side].push_back(Word(p) == own);
                runs[side].insert(runs[side].end(), reinterpret_cast<uint32_t *>(p) + 1, reinterpret_cast<uint32_t *>(p) + 3);
                ::operator delete(p);
            }
        }
        ++ctx.cases;
        if (runs[0] != runs[1]) ctx.Fail("adapter DeletingDestructor flags %u differs", flags);
    }
}

// ---- dispatcher (standalone shape, lane EL) ----------------------------------------------------------------------
// The dispatcher has no object of its own in the game (only the resource's +0x40 base; no constructor function): the original
// side builds the game's shape by hand (vtable 0xdd4270, fields), as the resource's inline constructor code does; our side runs
// our constructor (our vtable) and calls every slot through it. Vtable words are compared as "the side's own".

namespace {

// A fake delegate (cIGZPropertySetSerializer): Read / Write log their arguments and answer gDelegateResult.
struct Delegate {
    void **vt;
    uint32_t id;
    int32_t refs;
};
uint8_t gDelegateResult = 1;
uint8_t __fastcall DlQI(Delegate *self, void *, uint32_t riid, void **) { Log(0x200, self->id, riid); return 0; }
uint32_t __fastcall DlAddRef(Delegate *self, void *) { Log(0x201, self->id); return uint32_t(++self->refs); }
uint32_t __fastcall DlRelease(Delegate *self, void *) { Log(0x202, self->id); return uint32_t(--self->refs); }
uint8_t __fastcall DlRead(Delegate *self, void *, void *stream, void *set)
{
    Log(0x203, self->id, (stream == &gStream) | (uint32_t(uintptr_t(set) & 0xffff) << 8));
    return gDelegateResult;
}
uint8_t __fastcall DlWrite(Delegate *self, void *, void *stream, void *set, uint32_t flag)
{
    Log(0x204, self->id, (stream == &gStream) | (uint32_t(uintptr_t(set) & 0xffff) << 8));
    Log(0x205, flag);
    return gDelegateResult;
}
uint32_t __fastcall QiAddRef(void *, void *) { Log(0x206); return 1; }
void *gQiVT[6] = {nullptr, reinterpret_cast<void *>(&QiAddRef), nullptr};
void *gDelegateVT[6] = {reinterpret_cast<void *>(&DlQI), reinterpret_cast<void *>(&DlAddRef), reinterpret_cast<void *>(&DlRelease),
                        reinterpret_cast<void *>(&DlRead), reinterpret_cast<void *>(&DlWrite), nullptr};

using Disp = cGZPropertySetSerializerString;
using DispAdapters = cGZPropertySetSerializerStringAdapters;

// Our dispatcher's vtable, from one built by our constructor (not destroyed: it holds nothing).
uint32_t OurDispVTable()
{
    alignas(4) uint8_t raw[sizeof(Disp)];
    ::new (raw) Disp;
    return Word(raw);
}

// The side's dispatcher in raw storage, with two fake delegates (or none) and a count of 3. Not destroyed: the tests that
// destroy one do it explicitly.
struct Dispatcher {
    alignas(4) uint8_t raw[sizeof(Disp)];
    Delegate xml{gDelegateVT, 0x71, 5}, bin{gDelegateVT, 0x72, 5};
    explicit Dispatcher(int side, bool delegates = true)
    {
        if (side == 0) {
            std::memset(raw, 0, sizeof(raw));
            *reinterpret_cast<const void **>(raw) = GameAddress<const void>(Disp::kOriginalVTable);
        } else {
            ::new (raw) Disp;
        }
        S()->mXml = delegates ? &xml : nullptr;
        S()->mBinary = delegates ? &bin : nullptr;
        S()->mRefCount = 3;
    }
    Disp *S() { return reinterpret_cast<Disp *>(raw); }
    Blob State() { return {S()->mRefCount, uint32_t(xml.refs), uint32_t(bin.refs)}; }
};

using DispRead = uint8_t(__thiscall *)(void *, void *, void *);
using DispReadWith = uint8_t(__thiscall *)(void *, void *, void *, uint32_t);
using DispWrite = uint8_t(__thiscall *)(void *, void *, void *, uint32_t);

} // namespace

DIFF_TEST(PropSetSer_DispatchRead, 0x555c90)
{
    uint8_t setStandIn[0x24] = {};
    const uint32_t magics[] = {0xcbe750e0, 0x2bea4862, 0, 0x12345678, 0xcbe750e1};
    for (int hasRecord = 0; hasRecord < 2; ++hasRecord)
        for (uint32_t magic : magics)
            for (int shape = 0; shape < 4; ++shape)     // 0 whole, 1 short (peek fails), 2 seek fails, 3 first call fails
                for (uint8_t result : {uint8_t(0), uint8_t(1), uint8_t(0x81)}) {
                    Bytes data;
                    data.U32(magic);
                    data.U32(0xabcd);
                    if (shape == 1) data.b.resize(2);
                    Blob logs[2], states[2];
                    uint8_t oks[2];
                    size_t pos[2];
                    for (int side = 0; side < 2; ++side) {
                        Dispatcher d(side);
                        gDelegateResult = result;
                        Begin(true, data.b, shape == 3 ? 0 : -1, hasRecord != 0, shape != 2);
                        oks[side] = side == 0 ? ctx.Original<DispRead>(0x555c90)(d.raw, &gStream, setStandIn)
                                              : CallSlot<uint8_t>(d.raw, 3, static_cast<void *>(&gStream), static_cast<void *>(setStandIn));
                        logs[side] = g.log;
                        states[side] = d.State();
                        states[side].push_back(uint32_t(gRecord.refs));
                        pos[side] = g.pos;
                    }
                    ++ctx.cases;
                    if (oks[0] != oks[1] || logs[0] != logs[1] || states[0] != states[1] || pos[0] != pos[1])
                        ctx.Fail("record %d magic %08x shape %d result %02x: ok %02x/%02x log %zu/%zu", hasRecord, magic, shape,
                                 result, oks[0], oks[1], logs[0].size(), logs[1].size());
                }
}

DIFF_TEST(PropSetSer_DispatchReadWithWrite, 0x5568c0)
{
    uint8_t setStandIn[0x24] = {};
    for (uint32_t binary : {0u, 1u, 2u, 0x100u})
        for (int which = 0; which < 2; ++which) // 0 ReadWith (not a slot: a direct member call), 1 Write (slot 4)
            for (uint8_t result : {uint8_t(0), uint8_t(1)}) {
                Blob logs[2], states[2];
                uint8_t oks[2];
                for (int side = 0; side < 2; ++side) {
                    Dispatcher d(side);
                    gDelegateResult = result;
                    Begin(true, {}, -1, true);
                    auto *set = reinterpret_cast<cGZPropertySetString *>(setStandIn);
                    if (which == 0)
                        oks[side] = side == 0 ? ctx.Original<DispReadWith>(0x555b10)(d.raw, &gStream, setStandIn, binary)
                                              : d.S()->ReadWith(&gStream, set, uint8_t(binary));
                    else
                        oks[side] = side == 0 ? ctx.Original<DispWrite>(0x5568c0)(d.raw, &gStream, setStandIn, binary)
                                              : CallSlot<uint8_t>(d.raw, 4, static_cast<void *>(&gStream), static_cast<void *>(set), binary);
                    logs[side] = g.log;
                    states[side] = d.State();
                }
                ++ctx.cases;
                if (oks[0] != oks[1] || logs[0] != logs[1] || states[0] != states[1])
                    ctx.Fail("%s binary %x result %u: ok %u/%u log %zu/%zu", which ? "Write" : "ReadWith", binary, result, oks[0], oks[1],
                             logs[0].size(), logs[1].size());
            }
}

// The creation path: a dispatcher without delegates makes one on first use (binary: ReadWith and Write; XML: Write
// on an empty set, which writes nothing and returns 0). Compared: the result, the delegate object (all its bytes),
// the stream calls. The XML reader is not driven here (ReadWith(.., false) with no delegate runs the XML Read, lane
// PS4's; its creation code is the same inline block as Write's).
DIFF_TEST(PropSetSer_DispatchCreate, 0x555b10)
{
    svcfx::AllocatorScope allocator;
    for (int mode = 0; mode < 3; ++mode) { // 0 binary ReadWith, 1 binary Write, 2 XML Write
        Bytes record;
        record.U32(gzpropser::kBinaryMagic);
        record.U16(2);
        record.U32(1);
        record.U32(gzany::kTypeUint32);
        record.Text("k");
        record.U32(42);
        Blob logs[2], sets[2];
        std::vector<uint8_t> objects[2];
        uint8_t oks[2];
        for (int side = 0; side < 2; ++side) {
            GameSet set;
            Dispatcher disp(side, false);
            Disp &d = *disp.S();
            d.mRefCount = 0;
            Begin(mode == 0, mode == 0 ? record.b : std::vector<uint8_t>{}, -1, false);
            auto *s = reinterpret_cast<cGZPropertySetString *>(set.Ptr());
            if (mode == 0)
                oks[side] = side == 0 ? ctx.Original<DispReadWith>(0x555b10)(disp.raw, &gStream, set.Ptr(), 1) : d.ReadWith(&gStream, s, 1);
            else
                oks[side] = side == 0 ? ctx.Original<DispWrite>(0x5568c0)(disp.raw, &gStream, set.Ptr(), mode == 1)
                                      : CallSlot<uint8_t>(disp.raw, 4, static_cast<void *>(&gStream), static_cast<void *>(s), uint32_t(mode == 1));
            logs[side] = g.log;
            sets[side] = DumpSet(set.Ptr());
            void *made = mode == 2 ? d.mXml : d.mBinary;
            const size_t size = mode == 2 ? 0x5c : 0xa;
            objects[side].assign(static_cast<uint8_t *>(made), static_cast<uint8_t *>(made) + size);
            // The delegate's vtable words (the game's on the original side, ours on ours: the classes are in the standalone
            // shape) become 1 when they are the side's own, and ours must not be the game's.
            {
                const uint32_t game[2] = {GameWord(mode == 2 ? cGZPropertySetSerializerXmlString::kOriginalVTable : Bin::kOriginalVTable),
                                          GameWord(cGZPropertySetSerializerXmlString::kOriginalHandlerVTable)};
                uint32_t ours[2] = {OurBinVTable(), 0};
                if (mode == 2) {
                    cGZPropertySetSerializerXmlString x;
                    std::memcpy(ours, &x, 8);
                }
                for (int w = 0; w < (mode == 2 ? 2 : 1); ++w) {
                    const uint32_t own = side == 0 ? game[w] : ours[w];
                    const uint32_t code = Word(objects[side].data() + 4 * w) == own && ours[w] != game[w] ? 1u : 0u;
                    std::memcpy(objects[side].data() + 4 * w, &code, 4);
                }
            }
            // Each side's destructor releases the delegate (flags 0: the storage is the fixture's).
            if (side == 0)
                ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x552190)(disp.raw, 0);
            else
                CallSlot<void *>(disp.raw, 5, 0u);
        }
        ++ctx.cases;
        if (oks[0] != oks[1] || logs[0] != logs[1] || sets[0] != sets[1] || objects[0] != objects[1])
            ctx.Fail("mode %d: ok %u/%u, log %zu/%zu, set %zu/%zu, object %s", mode, oks[0], oks[1], logs[0].size(), logs[1].size(),
                     sets[0].size(), sets[1].size(), objects[0] == objects[1] ? "same" : "differs");
    }
}

// The deleting destructor 0x552190 (slot 5) on each side's heap object (the original's with a junk vtable word, which it stores
// back), the folded QueryInterface 0x555400 (slot 0, AddRef through the object's own slot 1: a vtable copy with a logging AddRef,
// our slot called through our pristine vtable), AddRef 0x15e520 and Release 0x242810 (slots 1 / 2; Release at 0 or below deletes
// through slot 5). Compared: results, out-parameters, the delegates' Release calls, frees, the object's words (vtable word as
// "the side's own").
DIFF_TEST(PropSetSer_DispatchDestructQI, 0x552190)
{
    const uint32_t ourVT = OurDispVTable();
    const uint32_t gameVT = GameWord(Disp::kOriginalVTable);
    if (ourVT == gameVT) ctx.Fail("our dispatcher carries the game's vtable");
    for (uint32_t flags : {0u, 1u})
        for (int present = 0; present < 4; ++present) {
            Blob logs[2];
            bool freed[2];
            std::vector<uint8_t> after[2];
            for (int side = 0; side < 2; ++side) {
                Delegate xml{gDelegateVT, 0x71, 5}, bin{gDelegateVT, 0x72, 5};
                auto *d = static_cast<Disp *>(::operator new(0x10));
                if (side == 0)
                    *reinterpret_cast<uint32_t *>(d) = 0x1234;
                else
                    ::new (d) Disp;
                d->mXml = present & 1 ? &xml : nullptr;
                d->mBinary = present & 2 ? &bin : nullptr;
                d->mRefCount = 9;
                Begin(true, {}, -1, false);
                difftest::FreeRecorder rec;
                void *x = side == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x552190)(d, flags) : CallSlot<void *>(d, 5, flags);
                rec.Stop();
                freed[side] = rec.Freed(d);
                logs[side] = g.log;
                logs[side].push_back(x == d);
                if (!freed[side]) {
                    after[side].assign(reinterpret_cast<uint8_t *>(d), reinterpret_cast<uint8_t *>(d) + 0x10);
                    const uint32_t code = Word(d) == (side == 0 ? gameVT : ourVT) ? 1u : Word(d);
                    std::memcpy(after[side].data(), &code, 4);
                    ::operator delete(d);
                }
            }
            ++ctx.cases;
            if (logs[0] != logs[1] || freed[0] != freed[1] || after[0] != after[1])
                ctx.Fail("flags %u present %d: log %zu/%zu freed %d/%d", flags, present, logs[0].size(), logs[1].size(), freed[0], freed[1]);
        }
    // QueryInterface 0x555400 (folded: not hooked).
    void *ourQiVT[6];
    std::memcpy(ourQiVT, reinterpret_cast<void *>(uintptr_t(ourVT)), sizeof(ourQiVT));
    ourQiVT[1] = reinterpret_cast<void *>(&QiAddRef);
    using QiFn = uint8_t(__thiscall *)(void *, uint32_t, void **);
    for (uint32_t iid : {1u, 0xebe701f3u, 0u, 2u, 0xebe701f2u, 0x4bb0d269u}) {
        Blob logs[2];
        for (int side = 0; side < 2; ++side) {
            Dispatcher d(side);
            *reinterpret_cast<void **>(d.raw) = side == 0 ? static_cast<void *>(gQiVT) : static_cast<void *>(ourQiVT);
            void *out = reinterpret_cast<void *>(0x5151);
            Begin(true, {}, -1, false);
            const uint8_t ok = side == 0 ? ctx.Original<QiFn>(0x555400)(d.raw, iid, &out)
                                         : reinterpret_cast<const QiFn *>(uintptr_t(ourVT))[0](d.raw, iid, &out);
            logs[side] = g.log;
            logs[side].push_back(ok);
            logs[side].push_back(out == d.raw ? 1 : out == reinterpret_cast<void *>(0x5151) ? 2 : 3);
        }
        ++ctx.cases;
        if (logs[0] != logs[1]) ctx.Fail("QI %08x differs", iid);
    }
    // AddRef / Release through each side's own vtable (the game's 0x15e520 / 0x242810, ours), on heap objects without
    // delegates.
    for (int slot = 1; slot <= 2; ++slot)
        for (uint32_t count : {0u, 1u, 2u, 7u, 0x80000001u, 0xffffffffu}) {
            Blob runs[2];
            for (int side = 0; side < 2; ++side) {
                auto *d = static_cast<Disp *>(::operator new(0x10));
                if (side == 0) {
                    std::memset(d, 0, 0x10);
                    *reinterpret_cast<uint32_t *>(d) = gameVT;
                } else {
                    ::new (d) Disp;
                }
                d->mRefCount = count;
                difftest::FreeRecorder rec;
                const uint32_t r = CallSlot<uint32_t>(d, slot);
                rec.Stop();
                const bool freed = rec.Freed(d);
                runs[side] = {r, uint32_t(freed)};
                if (!freed) {
                    runs[side].push_back(d->mRefCount);
                    runs[side].push_back(Word(d) == (side == 0 ? gameVT : ourVT));
                    ::operator delete(d);
                }
            }
            ++ctx.cases;
            if (runs[0] != runs[1]) ctx.Fail("slot %d count %08x differs", slot, count);
        }
}

// The dispatcher's adapters (what runs for a resource the game built with its own vtables, or a direct call): both sides hold
// the game's shape with a copy of the game's vtable whose forwarded slots (3 Read, 4 Write, 5 the destructor) are traps of the
// slot's arity, the original side calls the RVA, ours the adapter.
namespace {
Blob gDispTraps;
uint32_t __fastcall DispTrap1(void *, void *, uint32_t) { gDispTraps.push_back(0x7a7a0001); return 0; }
uint32_t __fastcall DispTrap2(void *, void *, uint32_t, uint32_t) { gDispTraps.push_back(0x7a7a0002); return 0; }
uint32_t __fastcall DispTrap3(void *, void *, uint32_t, uint32_t, uint32_t) { gDispTraps.push_back(0x7a7a0003); return 0; }
void *gDispTrapVT[6];
} // namespace

DIFF_TEST(PropSetSer_DispatchAdapters, 0x555b10)
{
    std::memcpy(gDispTrapVT, GameAddress<void>(Disp::kOriginalVTable), sizeof(gDispTrapVT));
    gDispTrapVT[3] = reinterpret_cast<void *>(&DispTrap2);
    gDispTrapVT[4] = reinterpret_cast<void *>(&DispTrap3);
    gDispTrapVT[5] = reinterpret_cast<void *>(&DispTrap1);
    uint8_t setStandIn[0x24] = {};
    auto *set = reinterpret_cast<cGZPropertySetString *>(setStandIn);
    for (int which = 0; which < 4; ++which) // 0 Read, 1 ReadWith, 2 Write, 3 DeletingDestructor
        for (uint32_t arg : {0u, 1u, 0x100u})
            for (uint8_t result : {uint8_t(0), uint8_t(1)}) {
                Blob runs[2];
                for (int side = 0; side < 2; ++side) {
                    Dispatcher d(0); // the game's shape on both sides
                    *reinterpret_cast<void **>(d.raw) = gDispTrapVT;
                    gDispTraps.clear();
                    gDelegateResult = result;
                    Bytes data;
                    data.U32(arg & 1 ? gzpropser::kBinaryMagic : 0x12345678);
                    Begin(true, data.b, -1, true);
                    auto *self = d.S();
                    uint32_t r = 0;
                    switch (which) {
                    // Read, Write and the deleting destructor: adapters dropped (lane GT; only the game's vtable reaches them,
                    // trapped under the string resource constructor's switch): the original on both sides. Ours: the tests above.
                    case 0:
                        r = ctx.Original<DispRead>(0x555c90)(d.raw, &gStream, setStandIn);
                        break;
                    case 1:
                        r = side == 0 ? ctx.Original<DispReadWith>(0x555b10)(d.raw, &gStream, setStandIn, arg)
                                      : DispAdapters::ReadWith(self, nullptr, &gStream, set, uint8_t(arg));
                        break;
                    case 2:
                        r = ctx.Original<DispWrite>(0x5568c0)(d.raw, &gStream, setStandIn, arg);
                        break;
                    default: {
                        // Flags 0 only: the fixture's storage (frees are the destructor tests' matter).
                        void *x = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x552190)(d.raw, 0);
                        r = x == d.raw;
                        const uint32_t own = GameWord(Disp::kOriginalVTable);
                        runs[side].push_back(Word(d.raw) == own);
                        break;
                    }
                    }
                    runs[side].push_back(which == 3 ? r : r & 0xff);
                    runs[side].insert(runs[side].end(), g.log.begin(), g.log.end());
                    const Blob state = d.State();
                    runs[side].insert(runs[side].end(), state.begin(), state.end());
                    runs[side].insert(runs[side].end(), gDispTraps.begin(), gDispTraps.end());
                }
                ++ctx.cases;
                if (runs[0] != runs[1]) ctx.Fail("adapter %d arg %x result %u differs (%zu/%zu words)", which, arg, result, runs[0].size(), runs[1].size());
            }
}
