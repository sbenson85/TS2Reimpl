// Differential tests for cGZPersistDBSerialRecord::GetFieldString (slot 24), GetFieldVariant (slot 26) and
// SetFieldVariant (slot 50). The record is a raw object carrying a copy of the game's primary vtable in which the raw
// read (slot 27), the raw write (51), GetSize (52), SeekAbsolute (55) and SeekRelative (56) are fakes over a byte
// script; the real typed getters and setters (image code) then do the byte swapping and make the same calls on both
// sides. SetFieldVariant reads a fake variant whose As*/array slots answer from a script and log every call. Each trial
// runs the original and ours on identical inputs and compares the return value, every call made (with arguments and
// written bytes), the script position, the output (the destination string, or the variant that was built) and the sizes
// of the heap blocks freed.
#include "difftest.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZPersistDBSerialRecord.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using ReadOneFn = bool(__thiscall *)(void *, void *);
using StringFn = bool(__thiscall *)(void *, void *);
using VariantOutFn = bool(__thiscall *)(void *, void **);
using VariantInFn = bool(__thiscall *)(void *, const void *);
using ReleaseFn = uint32_t(__thiscall *)(void *);

enum Kind : int64_t {
    kRead = 1,
    kWrite = 2,
    kGetSize = 3,
    kSeekAbs = 4,
    kSeekRel = 5,
    kStrFromChar = 6,
    kStrResize = 7,
    kStrAppend = 8,
    kGetString = 9,
    kVariantSlot = 10,
    kSetUint8Array = 11,
};

// ---- the script and the logs (single-threaded) --------------------------------------------------------------------
struct Env {
    std::vector<uint8_t> source;
    size_t pos = 0;
    int reads = 0, failReadsAfter = -1; // raw reads after this many calls fail (-1: never)
    int writes = 0, failWriteAt = 0;    // the Nth raw write fails (0: never)
    std::vector<int64_t> log;
    std::string cannedText;             // what the fake GetFieldString appends
    bool cannedOk = true;
};
Env E;

void Log(int64_t kind, int64_t a = 0, int64_t b = 0)
{
    E.log.push_back(kind);
    E.log.push_back(a);
    E.log.push_back(b);
}

bool __fastcall FakeRead(void *, void *, void *dst, uint32_t n)
{
    Log(kRead, n);
    ++E.reads;
    if ((E.failReadsAfter >= 0 && E.reads > E.failReadsAfter) || E.source.size() - E.pos < n) return false;
    std::memcpy(dst, E.source.data() + E.pos, n);
    E.pos += n;
    return true;
}

bool __fastcall FakeWrite(void *, void *, const void *src, uint32_t n)
{
    Log(kWrite, n);
    const auto *bytes = static_cast<const uint8_t *>(src);
    for (uint32_t i = 0; i < n; ++i) E.log.push_back(bytes[i]);
    ++E.writes;
    return E.failWriteAt == 0 || E.writes != E.failWriteAt;
}

// Slot 28 (SetFieldUint8 counted) as a logging pass-through, so that a writer chosen through the wrong slot (28 for 51)
// shows in the call log: the real one only tail-calls the raw write.
bool __fastcall FakeSetUint8Array(void *self, void *edx, const void *src, uint32_t n)
{
    Log(kSetUint8Array, n);
    return FakeWrite(self, edx, src, n);
}

uint32_t __fastcall FakeGetSize(void *, void *)
{
    Log(kGetSize);
    return static_cast<uint32_t>(E.source.size());
}

bool __fastcall FakeSeekAbsolute(void *, void *, uint32_t pos)
{
    Log(kSeekAbs, pos);
    E.pos = std::min<size_t>(pos, E.source.size());
    return true;
}

// A correct relative seek (cPreloadedRecord's is the buggy one; that is tested apart).
bool __fastcall FakeSeekRelative(void *, void *, int32_t delta)
{
    Log(kSeekRel, delta);
    int64_t p = static_cast<int64_t>(E.pos) + delta;
    E.pos = static_cast<size_t>(std::clamp<int64_t>(p, 0, static_cast<int64_t>(E.source.size())));
    return true;
}

// Slot 24 as a fake (for GetFieldVariant's string case): appends the canned text to the destination through its vtable.
bool __fastcall FakeGetString(void *, void *, void *dst)
{
    Log(kGetString, static_cast<int64_t>(E.cannedText.size()));
    if (!E.cannedText.empty())
        VirtualCall<void(__thiscall *)(void *, const char *, uint32_t), 3>(dst, E.cannedText.data(),
                                                                          static_cast<uint32_t>(E.cannedText.size()));
    return E.cannedOk;
}

// ---- the destination string fake (GetFieldString) ------------------------------------------------------------------
struct FakeString {
    void *vtable;
};
std::string gStrContent;
void *gStrVTable[40];

void __fastcall StrFromChar(void *, void *, const char *p, uint32_t n)
{
    Log(kStrFromChar, n);
    for (uint32_t i = 0; i < n; ++i) E.log.push_back(static_cast<uint8_t>(p[i]));
    gStrContent.assign(p, n);
}
void __fastcall StrResize(void *, void *, uint32_t n)
{
    Log(kStrResize, n);
    gStrContent.resize(n);
}
void __fastcall StrAppend(void *, void *, const char *p, uint32_t n)
{
    Log(kStrAppend, n);
    for (uint32_t i = 0; i < n; ++i) E.log.push_back(static_cast<uint8_t>(p[i]));
    gStrContent.append(p, n);
}

struct Tables {
    void *primary[62];
};

Tables MakeTables(const difftest::Context &ctx)
{
    Tables t{};
    std::memcpy(t.primary, ctx.image.At(cGZPersistDBSerialRecord::kVTable), sizeof(t.primary));
    t.primary[27] = reinterpret_cast<void *>(&FakeRead);
    t.primary[51] = reinterpret_cast<void *>(&FakeWrite);
    t.primary[28] = reinterpret_cast<void *>(&FakeSetUint8Array);
    t.primary[52] = reinterpret_cast<void *>(&FakeGetSize);
    t.primary[55] = reinterpret_cast<void *>(&FakeSeekAbsolute);
    t.primary[56] = reinterpret_cast<void *>(&FakeSeekRelative);
    t.primary[24] = reinterpret_cast<void *>(&FakeGetString);
    return t;
}

// ---- stream building ----------------------------------------------------------------------------------------------
struct Stream {
    std::vector<uint8_t> bytes;
    bool swap; // the record byte-swaps (flag 0): multi-byte values are stored big-endian
    void U8(uint8_t v) { bytes.push_back(v); }
    void Raw(uint64_t v, int width, bool bigEndian)
    {
        for (int i = 0; i < width; ++i) {
            int shift = bigEndian ? (width - 1 - i) * 8 : i * 8;
            bytes.push_back(static_cast<uint8_t>(v >> shift));
        }
    }
    void U16(uint16_t v) { Raw(v, 2, swap); }
    void U32(uint32_t v) { Raw(v, 4, swap); }
    void Random(std::mt19937 &rng, size_t n)
    {
        for (size_t i = 0; i < n; ++i) bytes.push_back(static_cast<uint8_t>(rng()));
    }
};

// Scratch the original and ours both over-read (GetFieldString with encoding 0 reads `count` units from `count` bytes):
// uniform stack contents before every call.
__declspec(noinline) void PoisonStack()
{
    volatile uint8_t scratch[0x3000];
    for (size_t i = 0; i < sizeof(scratch); ++i) scratch[i] = 0xA5;
}

struct Rec {
    alignas(8) uint8_t bytes[0x100];
    explicit Rec(const Tables &t, uint8_t flag)
    {
        std::memset(bytes, 0x5A, sizeof(bytes));
        const void *vt = t.primary;
        std::memcpy(bytes, &vt, sizeof(vt));
        bytes[0x40] = flag;
    }
};

void Prepare(const std::vector<uint8_t> &source, int failReadsAfter, int failWriteAt)
{
    E.source = source;
    E.pos = 0;
    E.reads = E.writes = 0;
    E.failReadsAfter = failReadsAfter;
    E.failWriteAt = failWriteAt;
    E.log.clear();
}

struct Outcome {
    bool result = false;
    std::vector<int64_t> log;
    size_t pos = 0;
    std::string text;               // GetFieldString: the destination's content
    std::vector<uint8_t> built;     // GetFieldVariant: the variant that was built
    bool outWritten = false;
    std::vector<size_t> freed;
};

void Compare(difftest::Context &ctx, const char *name, uint32_t code, const Outcome &a, const Outcome &b)
{
    ++ctx.cases;
    if (a.result != b.result) ctx.Fail("%s code %x: return original %d, ours %d", name, code, a.result, b.result);
    if (a.log != b.log) {
        size_t i = 0;
        while (i < a.log.size() && i < b.log.size() && a.log[i] == b.log[i]) ++i;
        ctx.Fail("%s code %x: calls differ at entry %zu (%zu / %zu entries; %lld vs %lld)", name, code, i, a.log.size(),
                 b.log.size(), i < a.log.size() ? (long long)a.log[i] : -1LL, i < b.log.size() ? (long long)b.log[i] : -1LL);
    }
    if (a.pos != b.pos) ctx.Fail("%s code %x: script position original %zu, ours %zu", name, code, a.pos, b.pos);
    if (a.text != b.text) ctx.Fail("%s code %x: destination text differs (%zu / %zu bytes)", name, code, a.text.size(), b.text.size());
    if (a.outWritten != b.outWritten) ctx.Fail("%s code %x: *out written original %d, ours %d", name, code, a.outWritten, b.outWritten);
    if (a.built != b.built) ctx.Fail("%s code %x: built variant differs", name, code);
    if (a.freed != b.freed) ctx.Fail("%s code %x: freed blocks differ (%zu / %zu)", name, code, a.freed.size(), b.freed.size());
}

std::vector<size_t> Sizes(const difftest::FreeRecorder &rec)
{
    size_t out[64];
    size_t n = rec.SortedSizes(out, 64);
    return std::vector<size_t>(out, out + n);
}

// ---- GetFieldString -------------------------------------------------------------------------------------------------
Outcome RunString(const Tables &t, uint8_t flag, const std::vector<uint8_t> &source, int failReadsAfter, bool ours,
                  StringFn orig)
{
    Rec rec(t, flag);
    FakeString dst;
    dst.vtable = gStrVTable;
    gStrContent.clear();
    Prepare(source, failReadsAfter, 0);
    Outcome o;
    {
        difftest::FreeRecorder freed;
        PoisonStack(); // last, so nothing between it and the call disturbs the stack the callee will use
        if (ours) o.result = reinterpret_cast<cGZPersistDBSerialRecord *>(rec.bytes)->GetFieldString(reinterpret_cast<cIGZString *>(&dst));
        else o.result = orig(rec.bytes, &dst);
        freed.Stop();
        o.freed = Sizes(freed);
    }
    o.log = E.log;
    o.pos = E.pos;
    o.text = gStrContent;
    return o;
}

void CheckString(difftest::Context &ctx, const Tables &t, StringFn orig, const char *what, uint8_t flag,
                 const std::vector<uint8_t> &source, int failReadsAfter = -1)
{
    Outcome a = RunString(t, flag, source, failReadsAfter, false, orig);
    Outcome b = RunString(t, flag, source, failReadsAfter, true, orig);
    Compare(ctx, what, flag, a, b);
}

void InitStringFake()
{
    for (auto &slot : gStrVTable) slot = nullptr;
    gStrVTable[3] = reinterpret_cast<void *>(&StrFromChar);
    gStrVTable[18] = reinterpret_cast<void *>(&StrResize);
    gStrVTable[20] = reinterpret_cast<void *>(&StrAppend);
    gStrContent.reserve(1 << 20);
    E.log.reserve(1 << 22);
}

// The path GetFieldString takes for a stream, mirrored only to keep a corrupt varint from asking for gigabytes: -1 when it
// is not the varint path, else the decoded length.
int64_t PredictVarintLength(const std::vector<uint8_t> &s, uint8_t flag)
{
    if (s.size() < 4) return -1;
    uint32_t header = flag == 0 ? (uint32_t(s[0]) << 24 | uint32_t(s[1]) << 16 | uint32_t(s[2]) << 8 | s[3])
                                : (uint32_t(s[3]) << 24 | uint32_t(s[2]) << 16 | uint32_t(s[1]) << 8 | s[0]);
    uint32_t enc = header >> 24;
    if (enc == 0 || enc == 0x10) return -1;
    if ((header & 0xff) >= 0x20 && (header & 0xff00) >= 0x2000) return -1;
    uint64_t len = 0;
    unsigned shift = 0;
    for (size_t i = 0; i < 5; ++i) {
        uint8_t c = i < s.size() ? s[i] : 0;
        len |= uint64_t(c & 0x7f) << shift;
        shift += 7;
        if (!(c & 0x80)) break;
    }
    return static_cast<int64_t>(len & 0xffffffffu);
}

} // namespace

DIFF_TEST(cGZPersistDBSerialRecord_GetFieldString, 0x99bec0)
{
    auto orig = ctx.Original<StringFn>(0x99bec0);
    Tables t = MakeTables(ctx);
    InitStringFake();
    std::mt19937 rng(0x5714);

    for (uint8_t flag : {uint8_t(0), uint8_t(1)}) {
        const bool swap = flag == 0;
        // Records too short for the header: the fallback reads bytes until a read fails.
        for (size_t n = 0; n < 4; ++n) {
            Stream s{{}, swap};
            s.Random(rng, n);
            CheckString(ctx, t, orig, "short record", flag, s.bytes);
        }
        // Encoding 0 (counted bytes, or UTF-16 when the second byte is 0): the boundaries of the stack buffer and the limit.
        const uint32_t counts[] = {0, 1, 2, 3, 7, 8, 9, 0x100, 0x200, 0x3ff, 0x400, 0x401, 0x402, 200000, 200001, 0xffffff};
        for (uint32_t count : counts) {
            for (int variant = 0; variant < 4; ++variant) {
                Stream s{{}, swap};
                s.U32(count);
                size_t available = count > 0x500 ? (variant == 0 ? 8 : 0x600) : count + (variant == 3 ? 0 : 2);
                if (count > 0x40000) available = 4; // would fail in the read anyway
                s.Random(rng, available);
                // variant 1/2: narrow payload (second byte non-zero), 3: UTF-16 heuristic (second byte 0) where it is safe
                if (s.bytes.size() > 5) {
                    if (variant != 3) s.bytes[5] |= 1;
                    // The over-read of encoding 0 with a zero second byte stays inside the 0x400-byte buffer up to
                    // 0x1f0 units; the last dwords of that buffer are not the same slots in the two frames.
                    if (variant == 3 && count <= 0x1f0) s.bytes[5] = 0;
                }
                CheckString(ctx, t, orig, "counted", flag, s.bytes);
                if (variant == 3 && count <= 0x1f0 && s.bytes.size() > 5) {
                    // UTF-16 payload for it: "ab" style units
                    Stream u{{}, swap};
                    u.U32(count);
                    for (uint32_t i = 0; i < count; ++i) {
                        u.U8(static_cast<uint8_t>(0x20 + rng() % 90));
                        u.U8(0);
                    }
                    CheckString(ctx, t, orig, "counted, UTF-16 by heuristic", flag, u.bytes);
                }
            }
        }
        // Encoding 0x10 (UTF-16 units): small-string boundary, buffer boundary, the heap path.
        const uint32_t unitCounts[] = {0, 1, 7, 8, 9, 0x100, 0x1ff, 0x200, 0x201, 0x300, 1000};
        for (uint32_t count : unitCounts) {
            Stream s{{}, swap};
            s.U32(0x10000000u | count);
            for (uint32_t i = 0; i < count; ++i) s.U16(static_cast<uint16_t>(rng() & 0xfff7 ? rng() : 0x41));
            CheckString(ctx, t, orig, "UTF-16", flag, s.bytes);
            if (count > 2) { // truncated payload: the raw read fails
                s.bytes.resize(s.bytes.size() - 3);
                CheckString(ctx, t, orig, "UTF-16, short", flag, s.bytes);
            }
        }
        // Text: the whole record is read again as bytes.
        for (size_t extra = 0; extra < 40; extra += 3) {
            std::vector<uint8_t> text;
            for (size_t i = 0; i < 4 + extra; ++i) text.push_back(static_cast<uint8_t>(0x30 + rng() % 70));
            CheckString(ctx, t, orig, "text", flag, text);
            CheckString(ctx, t, orig, "text, reads fail after 2", flag, text, 2);
            CheckString(ctx, t, orig, "text, reads fail after 6", flag, text, 6);
        }
        // Varint lengths.
        auto Varint = [&](uint64_t len, int forceBytes) {
            std::vector<uint8_t> v;
            int n = 0;
            do {
                uint8_t byte = len & 0x7f;
                len >>= 7;
                ++n;
                if (len != 0 || n < forceBytes) byte |= 0x80;
                v.push_back(byte);
            } while (len != 0 || n < forceBytes);
            return v;
        };
        const uint64_t lengths[] = {1, 5, 100, 127, 128, 200, 5000, 20000, 70000};
        for (uint64_t len : lengths) {
            for (int extraBytes : {0, 1, 2}) {
                std::vector<uint8_t> v = Varint(len, 1 + extraBytes > 5 ? 5 : 1 + extraBytes);
                for (int tail : {0, 1}) { // 1: the record has all the bytes; 0: it is short
                    Stream s{v, swap};
                    s.Random(rng, tail ? len : len / 2);
                    // keep the header out of the other encodings and the text heuristic
                    if (s.bytes.size() > 3) s.bytes[swap ? 3 : 0] &= 0x1f;
                    if (PredictVarintLength(s.bytes, flag) < 0 || PredictVarintLength(s.bytes, flag) > 100000) continue;
                    CheckString(ctx, t, orig, "varint", flag, s.bytes);
                }
            }
        }
        // A fifth continuation byte, a sixth, and reads failing inside the varint.
        const std::vector<std::vector<uint8_t>> odd = {
            {0x81, 0x80, 0x80, 0x80, 0x00, 'x'}, {0x81, 0x80, 0x80, 0x80, 0x80, 0x01, 'y', 'z'},
            {0x81, 0x81, 0x81, 0x81}, {0x82, 0x81, 0x01, 0x03, 'a', 'b', 'c'}, {0x83, 0x01, 0x01, 0x02, 0x03}};
        for (auto bytes : odd) {
            if (PredictVarintLength(bytes, flag) < 0 || PredictVarintLength(bytes, flag) > 100000) continue;
            CheckString(ctx, t, orig, "varint odd", flag, bytes);
            CheckString(ctx, t, orig, "varint odd, failing read", flag, bytes, 3);
        }
        // Random streams, filtered against huge varint lengths.
        for (int i = 0; i < 3000; ++i) {
            Stream s{{}, swap};
            s.Random(rng, rng() % 40);
            if (!s.bytes.empty() && i % 3 == 0) s.bytes[rng() % s.bytes.size()] = static_cast<uint8_t>(rng() % 6);
            if (s.bytes.size() >= 4 && i % 2 == 0) s.bytes[swap ? 0 : 3] = static_cast<uint8_t>(rng() % 3 ? rng() % 0x30 : 0x10);
            int64_t predicted = PredictVarintLength(s.bytes, flag);
            if (predicted > 100000) continue;
            // a counted record with encoding 0 and a zero second byte over-reads: keep to short counts, where it stays in the buffer
            if (s.bytes.size() >= 4 && predicted < 0) {
                uint32_t header = swap ? (uint32_t(s.bytes[0]) << 24 | uint32_t(s.bytes[1]) << 16 | uint32_t(s.bytes[2]) << 8 | s.bytes[3])
                                       : (uint32_t(s.bytes[3]) << 24 | uint32_t(s.bytes[2]) << 16 | uint32_t(s.bytes[1]) << 8 | s.bytes[0]);
                if ((header >> 24) == 0 && (header & 0xffffff) > 0x1f0) continue;
            }
            CheckString(ctx, t, orig, "random", flag, s.bytes, i % 5 == 0 ? int(rng() % 8) : -1);
        }
    }
}

// ---- SetFieldVariant ------------------------------------------------------------------------------------------------
namespace {

struct VariantScript {
    uint32_t tag = 0;
    uint32_t count = 0;
    uint64_t bits = 0;       // what the scalar As* slots answer
    std::vector<uint8_t> data; // what the array slots point at
};
VariantScript V;

template <int S> uint32_t __fastcall VarU32(void *, void *) { Log(kVariantSlot, S); return static_cast<uint32_t>(V.bits); }
template <int S> uint64_t __fastcall VarU64(void *, void *) { Log(kVariantSlot, S); return V.bits; }
template <int S> float __fastcall VarF32(void *, void *) { Log(kVariantSlot, S); float f; uint32_t b = static_cast<uint32_t>(V.bits); std::memcpy(&f, &b, 4); return f; }
template <int S> double __fastcall VarF64(void *, void *) { Log(kVariantSlot, S); double d; std::memcpy(&d, &V.bits, 8); return d; }
template <int S> const void *__fastcall VarPtr(void *, void *) { Log(kVariantSlot, S); return V.data.data(); }
uint32_t __fastcall VarGetType(void *, void *) { Log(kVariantSlot, 3); return V.tag; }
uint32_t __fastcall VarGetCount(void *, void *) { Log(kVariantSlot, 4); return V.count; }

void *gVarVTable[133];

#define SLOT(Fn, S) gVarVTable[S] = reinterpret_cast<void *>(&Fn<S>)
void InitVariantFake()
{
    for (auto &s : gVarVTable) s = nullptr;
    gVarVTable[3] = reinterpret_cast<void *>(&VarGetType);
    gVarVTable[4] = reinterpret_cast<void *>(&VarGetCount);
    SLOT(VarU32, 67); SLOT(VarU32, 69); SLOT(VarU32, 71); SLOT(VarU32, 73); SLOT(VarU32, 75); SLOT(VarU32, 77); SLOT(VarU32, 79);
    SLOT(VarU64, 81); SLOT(VarU64, 83); SLOT(VarF32, 85); SLOT(VarF64, 87); SLOT(VarU32, 89); SLOT(VarU32, 91); SLOT(VarU32, 93);
    SLOT(VarU32, 95);
    SLOT(VarPtr, 98); SLOT(VarPtr, 100); SLOT(VarPtr, 102); SLOT(VarPtr, 104); SLOT(VarPtr, 106); SLOT(VarPtr, 108);
    SLOT(VarPtr, 110); SLOT(VarPtr, 112); SLOT(VarPtr, 114); SLOT(VarPtr, 116); SLOT(VarPtr, 118); SLOT(VarPtr, 120);
    SLOT(VarPtr, 122); SLOT(VarPtr, 124); SLOT(VarPtr, 126); SLOT(VarPtr, 128);
}
#undef SLOT

Outcome RunSet(const Tables &t, uint8_t flag, bool ours, VariantInFn orig, int failWriteAt)
{
    Rec rec(t, flag);
    struct { void *vtable; } variant{gVarVTable};
    Prepare({}, -1, failWriteAt);
    Outcome o;
    {
        difftest::FreeRecorder freed;
        if (ours) o.result = reinterpret_cast<cGZPersistDBSerialRecord *>(rec.bytes)->SetFieldVariant(reinterpret_cast<const cRZVariant *>(&variant));
        else o.result = orig(rec.bytes, &variant);
        freed.Stop();
        o.freed = Sizes(freed);
    }
    o.log = E.log;
    return o;
}

} // namespace

DIFF_TEST(cGZPersistDBSerialRecord_SetFieldVariant, 0x99d660)
{
    auto orig = ctx.Original<VariantInFn>(0x99d660);
    Tables t = MakeTables(ctx);
    InitVariantFake();
    E.log.reserve(1 << 22);
    std::mt19937 rng(0x7e57);
    const uint32_t counts[] = {0, 1, 3, 0x7f, 0x100};
    auto Run = [&](uint32_t tag, uint32_t count, uint8_t flag, int failAt) {
        V.tag = tag;
        V.count = count;
        V.bits = (uint64_t(rng()) << 32) | rng();
        V.bits &= ~(uint64_t(0x7ff) << 52); // finite doubles and floats (no signalling NaNs through the x87 stack)
        V.bits &= ~(uint64_t(0xff) << 23);
        V.data.assign(count * 8 + 64, 0);
        for (auto &b : V.data) b = static_cast<uint8_t>(rng());
        Outcome a = RunSet(t, flag, false, orig, failAt);
        Outcome b = RunSet(t, flag, true, orig, failAt);
        Compare(ctx, "SetFieldVariant", tag, a, b);
    };
    for (uint8_t flag : {uint8_t(0), uint8_t(1)}) {
        for (uint32_t tag = 0; tag < 0x14; ++tag) {
            for (uint32_t arrayBit : {0u, 0x8000u}) {
                for (uint32_t count : counts) {
                    if (!arrayBit && count != 0) continue;
                    // 0 = no failure; 1 = the code write fails; 2 = the count / the value; 3 = the data
                    for (int failAt = 0; failAt <= 3; ++failAt) Run(tag | arrayBit, count, flag, failAt);
                }
            }
        }
        // Strings: lengths around the small-string size, with embedded NULs.
        for (uint32_t len : {0u, 1u, 14u, 15u, 16u, 17u, 100u}) {
            for (int failAt = 0; failAt <= 3; ++failAt) Run(0xe | 0x8000, len, flag, failAt);
        }
    }
}

// ---- GetFieldVariant ------------------------------------------------------------------------------------------------
namespace {

uintptr_t kSentinel = 0xFACE0000;

uint32_t ElementSizeOfTag(uint32_t tag)
{
    switch (tag & 0x7fff) {
    case 0xa: return 4;
    case 0xb: return 8;
    case 4: case 5: case 0xd: return 2;
    case 6: case 7: return 4;
    case 8: case 9: return 8;
    default: return 1;
    }
}

Outcome RunGet(const Tables &t, uint8_t flag, const std::vector<uint8_t> &source, int failReadsAfter, bool ours,
               VariantOutFn orig)
{
    Rec rec(t, flag);
    void *out = reinterpret_cast<void *>(kSentinel);
    Prepare(source, failReadsAfter, 0);
    PoisonStack();
    Outcome o;
    {
        difftest::FreeRecorder freed;
        if (ours) o.result = reinterpret_cast<cGZPersistDBSerialRecord *>(rec.bytes)->GetFieldVariant(reinterpret_cast<cRZVariant **>(&out));
        else o.result = orig(rec.bytes, &out);
        freed.Stop();
        o.freed = Sizes(freed);
    }
    o.log = E.log;
    o.pos = E.pos;
    o.outWritten = out != reinterpret_cast<void *>(kSentinel);
    if (o.outWritten) {
        const auto *v = static_cast<const uint8_t *>(out);
        uint16_t tag, refs;
        uint32_t count;
        std::memcpy(&refs, v + 0xc, 2);
        std::memcpy(&tag, v + 0xe, 2);
        std::memcpy(&count, v + 0x10, 4);
        auto Put = [&](const void *p, size_t n) { o.built.insert(o.built.end(), static_cast<const uint8_t *>(p), static_cast<const uint8_t *>(p) + n); };
        Put(&refs, 2);
        Put(&tag, 2);
        Put(&count, 4);
        if (tag & 0x8000) {
            const uint8_t *buffer;
            std::memcpy(&buffer, v + 4, sizeof(buffer));
            Put(buffer, size_t(count) * ElementSizeOfTag(tag));
        } else {
            Put(v + 4, 8);
        }
        VirtualCall<ReleaseFn, 2>(out); // outside the free recording
    }
    return o;
}

void CheckGet(difftest::Context &ctx, const Tables &t, VariantOutFn orig, uint8_t flag, const std::vector<uint8_t> &source,
              uint32_t code, int failReadsAfter = -1)
{
    Outcome a = RunGet(t, flag, source, failReadsAfter, false, orig);
    Outcome b = RunGet(t, flag, source, failReadsAfter, true, orig);
    Compare(ctx, "GetFieldVariant", code, a, b);
}

} // namespace

DIFF_TEST(cGZPersistDBSerialRecord_GetFieldVariant, 0x99c280)
{
    auto orig = ctx.Original<VariantOutFn>(0x99c280);
    Tables t = MakeTables(ctx);
    InitStringFake();
    std::mt19937 rng(0x6e7f);

    struct Case {
        uint32_t code;
        uint32_t width;
    };
    const Case scalars[] = {{0, 0}, {1, 1}, {2, 2}, {3, 4}, {4, 8}, {5, 1}, {6, 2}, {7, 4}, {8, 8}, {9, 4}, {0xa, 8},
                            {0xb, 1}, {0xc, 1}, {0xd, 2}, {0xe, 1}, {0x10, 1}, {0x11, 1}, {0x12, 1}, {0x13, 1}, {0x7fff, 1}};
    const Case arrays[] = {{0x8000, 1}, {0x8001, 1}, {0x8002, 2}, {0x8003, 4}, {0x8004, 8}, {0x8005, 1}, {0x8006, 2},
                           {0x8007, 4}, {0x8008, 8}, {0x8009, 4}, {0x800a, 8}, {0x800b, 1}, {0x8010, 2}, {0x8011, 1},
                           {0x800d, 1}, {0x800e, 1}, {0x800f, 1}, {0x8012, 1}, {0x8013, 1}};
    const uint32_t counts[] = {0, 1, 2, 0x7f, 0x80, 0x81, 0x100, 0x200};

    for (uint8_t flag : {uint8_t(0), uint8_t(1)}) {
        Stream header{{}, flag == 0};
        for (const Case &c : scalars) {
            Stream s{{}, flag == 0};
            s.U32(c.code);
            s.Random(rng, c.width);
            CheckGet(ctx, t, orig, flag, s.bytes, c.code);
            for (size_t cut = 0; cut < s.bytes.size(); ++cut) { // every truncation point
                CheckGet(ctx, t, orig, flag, std::vector<uint8_t>(s.bytes.begin(), s.bytes.begin() + cut), c.code);
            }
        }
        for (const Case &c : arrays) {
            for (uint32_t count : counts) {
                Stream s{{}, flag == 0};
                s.U32(c.code);
                s.U32(count);
                s.Random(rng, size_t(count) * c.width);
                CheckGet(ctx, t, orig, flag, s.bytes, c.code);
                if (count <= 2) {
                    for (size_t cut = 0; cut < s.bytes.size(); ++cut)
                        CheckGet(ctx, t, orig, flag, std::vector<uint8_t>(s.bytes.begin(), s.bytes.begin() + cut), c.code);
                } else {
                    for (int i = 0; i < 4; ++i) {
                        size_t cut = rng() % s.bytes.size();
                        CheckGet(ctx, t, orig, flag, std::vector<uint8_t>(s.bytes.begin(), s.bytes.begin() + cut), c.code);
                    }
                }
            }
        }
        // Reads failing midway through (the Nth raw read and later ones).
        for (int failAfter = 0; failAfter < 3; ++failAfter) {
            for (const Case &c : arrays) {
                Stream s{{}, flag == 0};
                s.U32(c.code);
                s.U32(3);
                s.Random(rng, size_t(3) * c.width);
                CheckGet(ctx, t, orig, flag, s.bytes, c.code, failAfter);
            }
        }
        // Strings (the record's GetFieldString is a fake that appends canned text).
        for (size_t len : {size_t(0), size_t(1), size_t(15), size_t(16), size_t(100)}) {
            for (bool ok : {true, false}) {
                E.cannedText.assign(len, 'q');
                for (size_t i = 0; i < len; i += 3) E.cannedText[i] = (i % 2) ? '\0' : static_cast<char>('a' + i % 20);
                E.cannedOk = ok;
                Stream s{{}, flag == 0};
                s.U32(0x800c);
                CheckGet(ctx, t, orig, flag, s.bytes, 0x800c);
            }
        }
    }
}
