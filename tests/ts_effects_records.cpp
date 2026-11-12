// Differential tests for one effect's stream records (src/ts/cTSEffectsResource.cpp, lane RX): ReadEffect 0xbf9da0 with
// 0xbf9b00, 0x1a5910 and the components' vector resize (0xbf9620, 0xbf9800, 0xbf99f0: an entry resized over one that holds
// components, smaller, inside the capacity, past it), WriteEffect 0xbf9ee0 with 0xbf9c60, 0x1a5950.
//
// The streams are fakes: reads replay a tape of random numbers, each call logged (slot, what it was asked for); writes are
// logged with the value. Compared: the log, then the whole entry word for word (bytes the original leaves alone masked: +0x21 and
// the three after +0x59 in each component) with vector capacities and the blocks allocated.
#include "ts_effects_collection.h"

#include "difftest.h"
#include "free_recorder.h"
#include "rz/cRZString.h"
#include "ts/cTSEffectsResource.h"

#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

using Log = std::vector<uint32_t>;
using Res = cTSEffectsResource;

Log gLog;
void Note(uint32_t a, uint32_t b)
{
    gLog.push_back(a);
    gLog.push_back(b);
}

struct Stream {
    void *const *vt;
    std::mt19937 *random;
    uint32_t version; // what the metadata hands out
};

template <class T> bool __fastcall GetNumber(Stream *s, void *, T *out)
{
    *out = static_cast<T>((*s->random)());
    Note(0xc1000000u | uint32_t(sizeof(T)), uint32_t(*out));
    return true;
}
// A float the tape can read (the components' are floats, kept as bits).
bool __fastcall GetFloat(Stream *s, void *, uint32_t *out)
{
    *out = (*s->random)() & 0xbfffffffu;
    Note(0xc2000000u, *out);
    return true;
}
// A small count, a type byte, a flags word: all through the same slots as the rest; the count is capped by the tape's scale.
bool __fastcall GetWord(Stream *s, void *, uint32_t *out)
{
    *out = (*s->random)();
    Note(0xc3000000u, *out);
    return true;
}
const char *const kTexts[] = {"", "a", "fifteen letters", "sixteen letters!", "a name past the small buffer of sixteen"};
bool __fastcall GetText(Stream *s, void *, cRZString *out)
{
    const std::string text = kTexts[(*s->random)() % 5];
    Note(0xc4000000u, uint32_t(text.size()));
    using FromChar = void(__thiscall *)(void *, const char *, uint32_t);
    reinterpret_cast<const FromChar *>(out->mVTable)[3](out, text.data(), uint32_t(text.size()));
    return true;
}

uint32_t gVersion;
const uint16_t *__fastcall MetaVersion(void *, void *)
{
    static uint16_t value;
    value = uint16_t(gVersion);
    Note(0xc5000000u, value);
    return &value;
}
void *gMetaTable[75];
struct Meta {
    void **vt = gMetaTable;
} gMeta;
void *__fastcall GetMeta(Stream *, void *)
{
    Note(0xc6000000u, 0);
    return &gMeta;
}

template <class T> bool __fastcall PutNumber(void *, void *, T value)
{
    Note(0xd1000000u | uint32_t(sizeof(T)), uint32_t(value));
    return true;
}
bool __fastcall PutWord32(void *, void *, uint32_t value)
{
    Note(0xd2000000u, value);
    return true;
}
bool __fastcall PutFloatBits(void *, void *, float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    Note(0xd3000000u, bits);
    return true;
}
bool __fastcall PutText(void *, void *, const char *text)
{
    Note(0xd4000000u, uint32_t(std::strlen(text)));
    for (const char *p = text; *p; ++p) gLog.push_back(uint8_t(*p));
    return true;
}
void __fastcall Unexpected(void *, void *)
{
    Note(0xdf000000u, 0);
}

struct Tables {
    void *in[21];
    void *out[15];
    Tables()
    {
        for (void *&p : in) p = reinterpret_cast<void *>(&Unexpected);
        for (void *&p : out) p = reinterpret_cast<void *>(&Unexpected);
        in[5] = reinterpret_cast<void *>(&GetNumber<uint8_t>);
        in[7] = reinterpret_cast<void *>(&GetNumber<uint16_t>);
        in[8] = reinterpret_cast<void *>(&GetNumber<uint32_t>);
        in[9] = reinterpret_cast<void *>(&GetWord);
        in[12] = reinterpret_cast<void *>(&GetFloat);
        in[15] = reinterpret_cast<void *>(&GetText);
        in[20] = reinterpret_cast<void *>(&GetMeta);
        out[5] = reinterpret_cast<void *>(&PutNumber<uint8_t>);
        out[7] = reinterpret_cast<void *>(&PutNumber<uint16_t>);
        out[8] = reinterpret_cast<void *>(&PutNumber<uint32_t>);
        out[9] = reinterpret_cast<void *>(&PutWord32);
        out[12] = reinterpret_cast<void *>(&PutFloatBits);
        out[14] = reinterpret_cast<void *>(&PutText);
        gMetaTable[74] = reinterpret_cast<void *>(&MetaVersion);
    }
};

// The component count is a slot-9 read: EffectRecord_Read counts those down to it and hands out a small number there.
uint32_t gCountWord = 0; // slot-9 reads left to the count (0: passed)
uint32_t gCountValue = 0;
bool __fastcall GetWordCounted(Stream *s, void *, uint32_t *out)
{
    if (gCountWord != 0 && --gCountWord == 0) {
        *out = gCountValue;
        Note(0xc7000000u, *out);
        return true;
    }
    return GetWord(s, nullptr, out);
}

void PushComponent(Log &b, const EffectComponent &c)
{
    b.push_back(uint32_t(c.mName.size()));
    b.push_back(uint32_t(c.mName.capacity()));
    b.insert(b.end(), c.mName.begin(), c.mName.end());
    const auto *w = reinterpret_cast<const uint32_t *>(&c);
    for (uint32_t i = 6; i < 34; ++i) {
        uint32_t mask = 0xffffffffu;
        if (i == 8) mask = 0xffff00ffu;  // +0x21
        if (i == 22) mask = 0x0000ffffu; // +0x5a
        b.push_back(w[i] & mask);
    }
}

void PushEntry(Log &b, const EffectEntry &e)
{
    b.push_back(0x0b000002);
    b.push_back(e.mFlags);
    b.push_back(e.mVariant);
    for (uint32_t p : e.mParams) b.push_back(p);
    b.push_back(uint32_t(e.mComponents.size()));
    b.push_back(uint32_t(e.mComponents.capacity()));
    for (const EffectComponent &c : e.mComponents) PushComponent(b, c);
}

void FillEntry(EffectEntry &e, std::mt19937 &r, uint32_t count)
{
    e.mComponents.reserve(count + r() % 3);
    for (uint32_t i = 0; i < count; ++i) {
        EffectComponent c;
        c.mName = kTexts[r() % 5];
        c.mType = int32_t(r() & 0xff);
        c.mWord1c = r();
        c.mFlags20.mByte = uint8_t(r());
        for (uint32_t &v : c.mWords24) v = r();
        for (uint32_t &v : c.mWords50) v = r();
        c.mByte58 = uint8_t(r());
        c.mByte59 = uint8_t(r());
        for (uint32_t &v : c.mWords5c) v = r();
        c.mHalf7c = uint16_t(r());
        c.mHalf7e = uint16_t(r());
        c.mWord80 = r();
        c.mIndex = int32_t(r());
        e.mComponents.push_back(c);
    }
}

int __cdecl StubAtexit(void (*)())
{
    return 0;
}

// One side only on purpose: the original's function-local static registers with the game's atexit (CRT, library code, a trap
// in the harness); ours has no such registration to stand in for.
struct NoGameAtexit {
    shape::CodePatch patch{0x824cbeu, shape::CodeOf(&StubAtexit)};
};

} // namespace

DIFF_TEST(EffectRecord_Read, 0xbf9da0)
{
    NoGameAtexit noAtexit;
    static const Tables tables;
    static void *in[21];
    std::memcpy(in, tables.in, sizeof(in));
    in[9] = reinterpret_cast<void *>(&GetWordCounted);
    const auto original = ctx.Original<void(__cdecl *)(void *, uint32_t, EffectEntry *)>(0xbf9da0);
    gLog.reserve(1 << 16);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Log blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed * 13 + 7);
            std::mt19937 tape(seed * 29 + 1);
            const uint32_t version = r() % 5;
            gVersion = version;
            const uint32_t before = r() % 5;
            const uint32_t count = r() % 4 == 0 ? before + r() % 12 : r() % 6;
            auto entry = std::make_unique<EffectEntry>();
            FillEntry(*entry, r, before);
            Stream stream{in, &tape, version};
            gLog.clear();
            // Slot 9's reads before the count: flags, the first parameter (version 3 on), two parameters.
            gCountWord = version < 3 ? 4 : 5;
            gCountValue = count;
            Log &b = blobs[side];
            {
                difftest::AllocationCounter counter;
                if (side == 0) original(&stream, version, entry.get());
                else Res::ReadEffect(&stream, version, entry.get());
                counter.Stop();
                b.push_back(uint32_t(counter.Blocks()));
                b.push_back(uint32_t(counter.Bytes()));
            }
            b.insert(b.end(), gLog.begin(), gLog.end());
            gLog.clear();
            PushEntry(b, *entry);
        }
        fxtest::Same(ctx, blobs[0], blobs[1], "effect read", seed);
    }
}

DIFF_TEST(EffectRecord_Write, 0xbf9ee0)
{
    static const Tables tables;
    const auto original = ctx.Original<void(__cdecl *)(void *, const EffectEntry *)>(0xbf9ee0);
    gLog.reserve(1 << 16);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Log blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed * 37 + 9);
            auto entry = std::make_unique<EffectEntry>();
            entry->mFlags = r();
            entry->mVariant = uint16_t(r());
            for (uint32_t &p : entry->mParams) p = r();
            FillEntry(*entry, r, r() % 5);
            Stream stream{tables.out, nullptr, 0};
            gLog.clear();
            if (side == 0) original(&stream, entry.get());
            else Res::WriteEffect(&stream, entry.get());
            blobs[side] = gLog;
            gLog.clear();
        }
        fxtest::Same(ctx, blobs[0], blobs[1], "effect write", seed);
    }
}
