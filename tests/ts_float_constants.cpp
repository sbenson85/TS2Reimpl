// cTSFloatConstants and its entry class (src/ts/cTSFloatConstants.h): original vs ours, one DIFF_TEST per body. Lane NU.
//
// Both sides build the table the game's way (the original constructor 0x4df0c0 on one, our adapter on the other), fill it by
// the same random sequence of SetFloat / SetDescription calls (each side's own code; duplicate names are made by assigning
// one entry's name to another), run the body and compare the result, every call the body makes to its doubles (a log), the
// table's state read from the bytes (reference count, dirty byte, every entry's value bits and both strings, the strings'
// vtable words and counts) and the blocks freed. The original side's calls are made at the RVA, ours through the object's own
// vtable slot (as the game's callers reach it) or the adapter where original code still calls the entry directly.
//
// Doubles: the streams (cIGZIStream: GetUint32 / GetFloat32 / GetGZStr / GetError / SetMetadata / GetMetadata and the
// string-reader interface the older formats ask for; cIGZOStream likewise), the persist system service the table loads and
// saves through (its cache cell 0x10faf6c holds a fake), the file reference whose slot 9 says "writable". A string a stream
// fills is filled through the string's own vtable (the image's cRZString code on both sides). The game's new / delete are ours.
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "rz/cRZString.h"
#include "ts/cTSFloatConstants.h"

#include <windows.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

using Self = cTSFloatConstants;
using Pair = cTSFloatConstantPair;
using Adapters = cTSFloatConstantsAdapters;
using Blob = std::vector<uint64_t>;

std::vector<int64_t> gLog;
std::mt19937 gRng; // the doubles' behaviour, reseeded alike for each side
uint32_t Next() { return gRng(); }
template <typename... T> void Log(T... values) { (gLog.push_back(static_cast<int64_t>(values)), ...); }
void ResetLog()
{
    gLog.clear();
    if (gLog.capacity() < 8192) gLog.reserve(8192); // the log must not allocate inside a recorded span
}
// A bool as the game's code returns it: only the low byte counts.
uint32_t Answer(bool ok) { return (Next() & 0xffffff00u) | (ok ? 1u + (Next() & 0x7e) : 0u); }

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t n) { ::operator delete(p, n); }
struct Patches {
    shape::CodePatch gameNew{0x824632, reinterpret_cast<void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<void *>(&GameDelete)};
};

enum : uint32_t {
    evQuery = 1, evU32, evFloat, evStr, evReaderStr, evRelease, evSetMeta, evSetU32, evSetFloat, evSetStr, evLoad, evSave, evFileRef,
    evAdd,
};

// ---- random inputs ----

uint32_t RandFloatBits(std::mt19937 &rng)
{
    static const uint32_t kSpecial[] = {0x7fc00000u, 0x7f800001u, 0xffc00001u, 0x00000001u, 0x80000000u, 0x00000000u,
                                        0x7f800000u, 0xff800000u, 0x3f800000u, 0x469c4000u};
    switch (rng() % 4) {
    case 0: return kSpecial[rng() % 10];
    case 1: return std::bit_cast<uint32_t>(static_cast<float>(int(rng() % 41) - 20));
    default: return rng();
    }
}
float AsFloat(uint32_t bits) { return std::bit_cast<float>(bits); }

std::string RandText(std::mt19937 &rng)
{
    static const char kAlphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-";
    static const char *const kWords[] = {"alpha", "beta", "money for new family", "Yard Score", "x", "", "upkeep penalty per dirty object"};
    if (rng() % 3 == 0) return kWords[rng() % 7];
    std::string text;
    const unsigned length = rng() % 4 == 0 ? 16 + rng() % 24 : rng() % 18; // both sides of the 15-character buffer
    for (unsigned i = 0; i < length; ++i) text += kAlphabet[rng() % (sizeof(kAlphabet) - 1)];
    return text;
}

std::string FlipCase(std::string text, std::mt19937 &rng)
{
    for (char &c : text)
        if (rng() % 2) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c >= 'A' && c <= 'Z' ? c + 32 : c);
    return text;
}

uint32_t HashText(const std::string &text)
{
    uint32_t h = 0x811c9dc5;
    for (char c : text) h = (h ^ static_cast<uint8_t>(c)) * 0x01000193;
    return h ^ static_cast<uint32_t>(text.size());
}

// ---- a table on one side: a heap block of the object's size ----

struct Table {
    uint8_t *raw = nullptr;
    int side = 0;
    bool gone = false; // the call under test freed the object

    void Build(difftest::Context &ctx, int s)
    {
        side = s;
        gone = false;
        raw = static_cast<uint8_t *>(::operator new(sizeof(Self)));
        std::memset(raw, 0xcd, sizeof(Self));
        if (side == 0) ctx.Original<void *(__thiscall *)(void *)>(0x4df0c0)(raw);
        else Adapters::Construct(reinterpret_cast<Self *>(raw), nullptr);
    }
    // The destructor body (the stack users' call), then the block.
    void Destroy(difftest::Context &ctx)
    {
        if (gone) return;
        if (side == 0) ctx.Original<void(__thiscall *)(void *)>(0x4df260)(raw);
        else Adapters::Destruct(reinterpret_cast<Self *>(raw), nullptr);
        ::operator delete(raw, sizeof(Self));
        gone = true;
    }
    uint32_t *Words() { return reinterpret_cast<uint32_t *>(raw); }
    uint8_t *Begin() { return reinterpret_cast<uint8_t *>(Words()[3]); }
    uint32_t Count() { return (Words()[4] - Words()[3]) / 0x48; }
    uint8_t *Entry(uint32_t i) { return Begin() + i * 0x48; }
    static cRZString &Name(uint8_t *entry) { return *reinterpret_cast<cRZString *>(entry + 8); }
    static cRZString &Desc(uint8_t *entry) { return *reinterpret_cast<cRZString *>(entry + 0x28); }

    // Slot `slot` of the object's vtable (ours) or the original at `rva`.
    template <class R, class... A> R Call(difftest::Context &ctx, uint32_t rva, int slot, A... args)
    {
        using Fn = R(__thiscall *)(void *, A...);
        if (side == 0) return ctx.Original<Fn>(rva)(raw, args...);
        return (*reinterpret_cast<Fn **>(raw))[slot](raw, args...);
    }
    float GetFloat(difftest::Context &ctx, const char *name, float value, bool add)
    {
        if (side == 1) return Adapters::GetFloat(reinterpret_cast<Self *>(raw), nullptr, name, value, add);
        return Call<float>(ctx, 0x4df320, 7, name, value, add);
    }
    void SetFloat(difftest::Context &ctx, const char *name, float value) { Call<void>(ctx, 0x4df8c0, 8, name, value); }
    void SetDescription(difftest::Context &ctx, const char *name, const char *text) { Call<void>(ctx, 0x4dfa30, 10, name, text); }
};

// Fills the table: random entries (by SetFloat), descriptions, then some names copied (case flipped) over other entries'
// (duplicates), the reference count and the dirty byte.
void Populate(difftest::Context &ctx, Table &t, std::mt19937 &rng, bool duplicates)
{
    const unsigned n = rng() % 8;
    for (unsigned i = 0; i < n; ++i) {
        const std::string name = RandText(rng);
        t.SetFloat(ctx, name.c_str(), AsFloat(RandFloatBits(rng)));
        if (rng() % 2) {
            const std::string text = RandText(rng);
            t.SetDescription(ctx, name.c_str(), text.c_str());
        }
    }
    if (duplicates && t.Count() >= 2) {
        const unsigned copies = 1 + rng() % 3;
        for (unsigned c = 0; c < copies; ++c) {
            const uint32_t from = rng() % t.Count(), to = rng() % t.Count();
            Table::Name(t.Entry(to)).mString = FlipCase(Table::Name(t.Entry(from)).mString, rng);
        }
    }
    t.Words()[1] = rng() % 4 == 0 ? rng() : rng() % 5;
    t.raw[8] = rng() % 3 == 0 ? static_cast<uint8_t>(rng()) : static_cast<uint8_t>(rng() % 2);
}

// A name for a lookup: an entry's (case flipped now and then), or a fresh one.
std::string PickName(Table &t, std::mt19937 &rng)
{
    if (t.Count() != 0 && rng() % 4 != 0) {
        const std::string name = Table::Name(t.Entry(rng() % t.Count())).mString;
        return rng() % 2 ? FlipCase(name, rng) : name;
    }
    return RandText(rng);
}

void PushText(Blob &b, const std::string &text)
{
    b.push_back(0x7e000000u + text.size());
    for (char c : text) b.push_back(static_cast<uint8_t>(c));
}

// The entry's bytes as the code can tell them: value bits, both texts, the strings' vtable words (equal to each other) and
// reference counts.
void CaptureEntry(Blob &b, uint8_t *entry)
{
    uint32_t value;
    std::memcpy(&value, entry + 4, 4);
    b.push_back(value);
    PushText(b, Table::Name(entry).mString);
    PushText(b, Table::Desc(entry).mString);
    b.insert(b.end(), {*reinterpret_cast<uint32_t *>(entry + 8) == *reinterpret_cast<uint32_t *>(entry + 0x28),
                       *reinterpret_cast<uint32_t *>(entry + 8) == uint32_t(uintptr_t(GameAddress<void>(cRZString::kVTable))),
                       *reinterpret_cast<uint32_t *>(entry + 0x1c), *reinterpret_cast<uint32_t *>(entry + 0x3c)});
}

// The table as bytes can show: counts, the dirty byte (and the padding after it), each entry.
void Capture(Blob &b, Table &t)
{
    if (t.gone) {
        b.push_back(0xdead0000u);
        return;
    }
    uint32_t *w = t.Words();
    b.insert(b.end(), {0xeeee0000u, w[1], t.raw[8], t.raw[9] == 0xcd, t.raw[10] == 0xcd, t.raw[11] == 0xcd, t.Count(), w[3] != 0});
    for (uint32_t i = 0; i < t.Count(); ++i) CaptureEntry(b, t.Entry(i));
}

void Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    if (a == b) return;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    ctx.Fail("%s seed %u: differ at word %u (sizes %u/%u): %llx vs %llx", what, seed, unsigned(i), unsigned(a.size()), unsigned(b.size()),
             i < a.size() ? a[i] : 0ull, i < b.size() ? b[i] : 0ull);
}

// One seed on both sides: a fresh table, populated alike, `op(table, rng, blob)` once, then the capture and the doubles' log.
template <class Op> void RunSeeds(difftest::Context &ctx, const char *what, unsigned seeds, bool duplicates, Op &&op)
{
    Patches patches;
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed * 7919u + 11);
            Table t;
            t.Build(ctx, side);
            Populate(ctx, t, rng, duplicates);
            gRng.seed(seed + 5);
            ResetLog();
            Blob &b = blobs[side];
            difftest::FreeRecorder frees;
            op(ctx, t, rng, b);
            frees.Stop();
            size_t sizes[256];
            const size_t n = frees.SortedSizes(sizes, 256);
            b.insert(b.end(), {0xf7ee0000u, n, frees.Overflowed()});
            for (size_t i = 0; i < n; ++i) b.push_back(sizes[i]);
            Capture(b, t);
            b.insert(b.end(), gLog.begin(), gLog.end());
            t.Destroy(ctx);
        }
        Same(ctx, blobs[0], blobs[1], what, seed);
    }
}

// ---- the streams ----

struct Meta { // the metadata object the streams hand back: slot 78 points at the version
    void **vt;
    uint32_t version;
};
void *gMetaVT[80];
void *__fastcall MetaVersion(Meta *m, void *) { return &m->version; }

struct Reader {
    void **vt;
};
void *gReaderVT[24];

struct InStream {
    void **vt;
    uint32_t u32Calls;
    uint32_t scriptedVersion; // the second word GetUint32 hands out
    uint32_t maxCount;
    bool readerAnswers;
    uint8_t **base; // where the string positions are counted from
    Meta meta;
    Reader reader;
} gIn;
void *gInVT[24];

struct OutStream {
    void **vt;
    uint8_t **base;
} gOut;
void *gOutVT[24];

void FillThroughVTable(void *str, const std::string &text)
{
    using FromChar = void(__thiscall *)(void *, const char *);
    (*static_cast<FromChar **>(str))[4](str, text.c_str());
}
uint32_t Position(uint8_t **base, const void *p) { return uint32_t(static_cast<const uint8_t *>(p) - *base); }

uint32_t __fastcall InQuery(InStream *s, void *, uint32_t id, void **out)
{
    const bool ok = s->readerAnswers;
    Log(evQuery, id, ok);
    if (ok) *out = &s->reader;
    return Answer(ok);
}
uint32_t __fastcall InU32(InStream *s, void *, uint32_t *out)
{
    const uint32_t n = s->u32Calls++;
    const uint32_t value = n == 1 ? s->scriptedVersion : n == 3 ? Next() % (s->maxCount + 1) : Next();
    Log(evU32, n, value);
    *out = value;
    return Answer(true);
}
uint32_t __fastcall InFloat(InStream *, void *, float *out)
{
    std::mt19937 rng(Next());
    const uint32_t bits = RandFloatBits(rng);
    Log(evFloat, bits);
    std::memcpy(out, &bits, 4);
    return Answer(true);
}
uint32_t __fastcall InStr(InStream *s, void *, void *str)
{
    std::mt19937 rng(Next());
    const std::string text = RandText(rng);
    Log(evStr, Position(s->base, str), text.size(), HashText(text));
    FillThroughVTable(str, text);
    return Answer(true);
}
uint32_t __fastcall InError(InStream *, void *)
{
    const uint32_t r = Next();
    return (r & 0xffffff00u) | (r % 4 == 0 ? 1u + ((r >> 8) & 0x7e) : 0u);
}
void __fastcall InSetMeta(InStream *s, void *, void *metadata)
{
    uint32_t words[5] = {};
    if (metadata) std::memcpy(words, metadata, sizeof(words));
    Log(evSetMeta, metadata != nullptr, words[0] != 0, words[1], words[2], words[3], words[4]);
    if (metadata) {
        s->meta.version = words[1];
        ::operator delete(metadata, 0x14);
    }
}
void *__fastcall InGetMeta(InStream *s, void *) { return &s->meta; }
uint32_t __fastcall ReaderRelease(Reader *, void *)
{
    Log(evRelease);
    return 0;
}
uint32_t __fastcall ReaderStr(Reader *, void *, void *str)
{
    std::mt19937 rng(Next());
    const std::string text = RandText(rng);
    Log(evReaderStr, Position(gIn.base, str), text.size(), HashText(text));
    FillThroughVTable(str, text);
    return Answer(true);
}

uint32_t __fastcall OutU32(OutStream *, void *, uint32_t value)
{
    Log(evSetU32, value);
    return Answer(true);
}
uint32_t __fastcall OutFloat(OutStream *, void *, float value)
{
    Log(evSetFloat, std::bit_cast<uint32_t>(value));
    return Answer(true);
}
uint32_t __fastcall OutStr(OutStream *s, void *, void *str)
{
    const std::string &text = static_cast<cRZString *>(str)->mString;
    Log(evSetStr, Position(s->base, str), text.size(), HashText(text));
    return Answer(true);
}
uint32_t __fastcall OutError(OutStream *, void *) { return InError(nullptr, nullptr); }
void __fastcall OutSetMeta(OutStream *, void *, void *metadata)
{
    uint32_t words[5] = {};
    if (metadata) std::memcpy(words, metadata, sizeof(words));
    Log(evSetMeta, metadata != nullptr, words[0] != 0, words[1], words[2], words[3], words[4]);
    if (metadata) ::operator delete(metadata, 0x14);
}

void InitStreams()
{
    for (auto &s : gMetaVT) s = nullptr;
    gMetaVT[78] = reinterpret_cast<void *>(&MetaVersion);
    for (auto &s : gReaderVT) s = nullptr;
    gReaderVT[2] = reinterpret_cast<void *>(&ReaderRelease);
    gReaderVT[20] = reinterpret_cast<void *>(&ReaderStr);
    for (auto &s : gInVT) s = nullptr;
    gInVT[0] = reinterpret_cast<void *>(&InQuery);
    gInVT[9] = reinterpret_cast<void *>(&InU32);
    gInVT[12] = reinterpret_cast<void *>(&InFloat);
    gInVT[15] = reinterpret_cast<void *>(&InStr);
    gInVT[18] = reinterpret_cast<void *>(&InError);
    gInVT[19] = reinterpret_cast<void *>(&InSetMeta);
    gInVT[20] = reinterpret_cast<void *>(&InGetMeta);
    for (auto &s : gOutVT) s = nullptr;
    gOutVT[9] = reinterpret_cast<void *>(&OutU32);
    gOutVT[12] = reinterpret_cast<void *>(&OutFloat);
    gOutVT[15] = reinterpret_cast<void *>(&OutStr);
    gOutVT[18] = reinterpret_cast<void *>(&OutError);
    gOutVT[19] = reinterpret_cast<void *>(&OutSetMeta);
}

void ResetIn(std::mt19937 &rng, uint8_t **base, uint32_t maxCount)
{
    static const uint32_t kVersions[] = {0, 1, 2, 0x3e, 0x3f, 0x40, 0x55, 0x56, 0xffffffffu};
    gIn.vt = gInVT;
    gIn.u32Calls = 0;
    gIn.scriptedVersion = rng() % 5 == 0 ? rng() : kVersions[rng() % 9];
    gIn.maxCount = maxCount;
    gIn.readerAnswers = rng() % 3 != 0;
    gIn.base = base;
    gIn.meta.vt = gMetaVT;
    gIn.meta.version = gIn.scriptedVersion;
    gIn.reader.vt = gReaderVT;
}
void ResetOut(uint8_t **base)
{
    gOut.vt = gOutVT;
    gOut.base = base;
}

// ---- the persist system and a file reference ----

struct FileRef {
    void **vt;
} gFileRef;
void *gFileRefVT[16];
Table *gTable = nullptr;
difftest::Context *gCtx = nullptr;
struct Persist {
    void **vt;
} gPersist;
void *gPersistVT[80];

uint32_t __fastcall FileRefSlot9(FileRef *, void *)
{
    const bool ok = Next() % 2 != 0;
    Log(evFileRef, ok);
    return Answer(ok);
}
uint32_t __fastcall PersistRead(Persist *, void *, void *resource, void *segment, uint32_t instance, uint32_t metadata)
{
    Log(evLoad, resource == gTable->raw, uintptr_t(segment), instance, metadata, gTable->Count(), gTable->raw[8]);
    // What a load does: fills the table through the object's own slots.
    const unsigned adds = Next() % 3;
    for (unsigned i = 0; i < adds; ++i) {
        std::mt19937 rng(Next());
        const std::string name = RandText(rng);
        const uint32_t bits = RandFloatBits(rng);
        Log(evAdd, HashText(name), bits);
        gTable->SetFloat(*gCtx, name.c_str(), AsFloat(bits));
    }
    return Next();
}
uint32_t __fastcall PersistWrite(Persist *, void *, void *resource, void *fileRef, uint32_t instance)
{
    Log(evSave, resource == gTable->raw, fileRef == &gFileRef, instance, gTable->Count(), gTable->raw[8]);
    return Next();
}
void InitPersist()
{
    for (auto &s : gPersistVT) s = nullptr;
    gPersistVT[72] = reinterpret_cast<void *>(&PersistWrite);
    gPersistVT[73] = reinterpret_cast<void *>(&PersistRead);
    gPersist.vt = gPersistVT;
    for (auto &s : gFileRefVT) s = nullptr;
    gFileRefVT[9] = reinterpret_cast<void *>(&FileRefSlot9);
    gFileRef.vt = gFileRefVT;
}
// The service cell holds the fake while a test runs.
struct ServiceCell {
    void **cell = GameAddress<void *>(0x10faf6c);
    void *saved = *cell;
    ServiceCell()
    {
        InitPersist();
        *cell = &gPersist;
    }
    ~ServiceCell() { *cell = saved; }
};

} // namespace

// ---- construction, destruction, the reference slots ----

DIFF_TEST(FloatConstants_Construct, 0x4df0c0)
{
    Patches patches;
    for (uint32_t seed = 0; seed < 4; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            alignas(8) uint8_t raw[sizeof(Self)];
            std::memset(raw, int(0x11 * (seed + 1)), sizeof(raw));
            void *r = side == 0 ? ctx.Original<void *(__thiscall *)(void *)>(0x4df0c0)(raw) : Adapters::Construct(reinterpret_cast<Self *>(raw), nullptr);
            uint32_t words[6];
            std::memcpy(words, raw, sizeof(words));
            blobs[side].push_back(r == raw);
            // The vtable word: the game's on the original side, ours (and not the game's) on ours.
            const uint32_t game = uint32_t(uintptr_t(GameAddress<void>(0xdcd8e0)));
            blobs[side].push_back(side == 0 ? words[0] == game : words[0] != game && words[0] != 0);
            blobs[side].insert(blobs[side].end(), {words[1], raw[8], raw[9], raw[10], raw[11], words[3], words[4], words[5]});
            if (side == 0) ctx.Original<void(__thiscall *)(void *)>(0x4df260)(raw);
            else Adapters::Destruct(reinterpret_cast<Self *>(raw), nullptr);
        }
        Same(ctx, blobs[0], blobs[1], "Construct", seed);
    }
}

DIFF_TEST(FloatConstants_Destruct, 0x4df260)
{
    // The body the stack users call: the entries' blocks go back (the sizes compared); the block of the table is the caller's.
    Patches patches;
    for (uint32_t seed = 0; seed < 24; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed + 3);
            Table t;
            t.Build(ctx, side);
            Populate(ctx, t, rng, false);
            const uint32_t entries = t.Count();
            difftest::FreeRecorder frees;
            if (side == 0) ctx.Original<void(__thiscall *)(void *)>(0x4df260)(t.raw);
            else Adapters::Destruct(reinterpret_cast<Self *>(t.raw), nullptr);
            frees.Stop();
            size_t sizes[256];
            const size_t n = frees.SortedSizes(sizes, 256);
            blobs[side].insert(blobs[side].end(), {entries, n, frees.Overflowed()});
            for (size_t i = 0; i < n; ++i) blobs[side].push_back(sizes[i]);
            ::operator delete(t.raw, sizeof(Self));
        }
        Same(ctx, blobs[0], blobs[1], "Destruct", seed);
    }
}

DIFF_TEST(FloatConstants_DeletingDestructor, 0x4df2a0)
{
    // Slot 19: the flags' bit 0 frees the 0x18-byte block; the entries' blocks always go. Returns the object.
    Patches patches;
    for (uint32_t flags = 0; flags < 4; ++flags) {
        for (uint32_t seed = 0; seed < 6; ++seed) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                std::mt19937 rng(seed + 3);
                Table t;
                t.Build(ctx, side);
                Populate(ctx, t, rng, false);
                difftest::FreeRecorder frees;
                void *r = t.Call<void *>(ctx, 0x4df2a0, 19, flags);
                frees.Stop();
                size_t sizes[256];
                const size_t n = frees.SortedSizes(sizes, 256);
                blobs[side].insert(blobs[side].end(), {r == t.raw, n, frees.Overflowed()});
                for (size_t i = 0; i < n; ++i) blobs[side].push_back(sizes[i]);
                if (!(flags & 1)) ::operator delete(t.raw, sizeof(Self));
            }
            Same(ctx, blobs[0], blobs[1], "DeletingDestructor", flags * 100 + seed);
        }
    }
}

DIFF_TEST(FloatConstants_Create, 0x4df2d0)
{
    Patches patches;
    for (uint32_t seed = 0; seed < 3; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            void *object = side == 0 ? ctx.Original<void *(__cdecl *)()>(0x4df2d0)() : static_cast<void *>(Self::Create());
            uint32_t words[6];
            std::memcpy(words, object, sizeof(words));
            const uint32_t game = uint32_t(uintptr_t(GameAddress<void>(0xdcd8e0)));
            blobs[side].push_back(object != nullptr);
            blobs[side].push_back(side == 0 ? words[0] == game : words[0] != game && words[0] != 0);
            blobs[side].insert(blobs[side].end(), {words[1], static_cast<uint8_t *>(object)[8], words[3], words[4], words[5]});
            if (side == 0) ctx.Original<void(__thiscall *)(void *)>(0x4df260)(object);
            else Adapters::Destruct(static_cast<Self *>(object), nullptr);
            ::operator delete(object, sizeof(Self));
        }
        Same(ctx, blobs[0], blobs[1], "Create", seed);
    }
}

DIFF_TEST(FloatConstants_QueryInterface, 0x4df4d0)
{
    static const uint32_t kIds[] = {0, 1, 2, 0x7b6faf7e, 0x7b6faf7d, 0x7b6faf7f, 0xe4fda3d4, 0x46434e53, 0xffffffffu, 0x80000001u};
    RunSeeds(ctx, "QueryInterface", 40, false, [&](difftest::Context &c, Table &t, std::mt19937 &rng, Blob &b) {
        void *out = reinterpret_cast<void *>(0x12345678);
        const uint32_t id = kIds[rng() % 10];
        const uint8_t r = t.Call<uint8_t>(c, 0x4df4d0, 0, id, &out);
        b.insert(b.end(), {r, out == t.raw ? 1u : out == reinterpret_cast<void *>(0x12345678) ? 2u : 3u});
    });
}

DIFF_TEST(FloatConstants_AddRef, 0x148fb0)
{
    RunSeeds(ctx, "AddRef", 24, false, [&](difftest::Context &c, Table &t, std::mt19937 &, Blob &b) {
        b.push_back(t.Call<uint32_t>(c, 0x148fb0, 1));
    });
}

DIFF_TEST(FloatConstants_DropReference, 0x4df870)
{
    RunSeeds(ctx, "DropReference", 24, false, [&](difftest::Context &c, Table &t, std::mt19937 &, Blob &b) {
        b.push_back(t.Call<uint32_t>(c, 0x4df870, 18));
    });
}

DIFF_TEST(FloatConstants_Release, 0x44cc60)
{
    // The last reference (count 0 or 1) frees the object through slot 19; the others count down. The entries' blocks too.
    Patches patches;
    for (uint32_t refs = 0; refs < 5; ++refs) {
        for (uint32_t seed = 0; seed < 4; ++seed) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                std::mt19937 rng(seed + 3);
                Table t;
                t.Build(ctx, side);
                Populate(ctx, t, rng, false);
                t.Words()[1] = refs;
                difftest::FreeRecorder frees;
                const uint32_t r = t.Call<uint32_t>(ctx, 0x44cc60, 2);
                frees.Stop();
                size_t sizes[256];
                const size_t n = frees.SortedSizes(sizes, 256);
                blobs[side].insert(blobs[side].end(), {r, n, frees.Overflowed()});
                for (size_t i = 0; i < n; ++i) blobs[side].push_back(sizes[i]);
                const bool freed = r == 0;
                if (!freed) blobs[side].push_back(t.Words()[1]);
                if (freed) t.gone = true;
                t.Destroy(ctx);
            }
            Same(ctx, blobs[0], blobs[1], "Release", refs * 10 + seed);
        }
    }
}

DIFF_TEST(FloatConstants_GetGZCLSID, 0x4df3f0)
{
    RunSeeds(ctx, "GetGZCLSID", 2, false, [&](difftest::Context &c, Table &t, std::mt19937 &, Blob &b) {
        b.push_back(t.Call<uint32_t>(c, 0x4df3f0, 5));
    });
}

DIFF_TEST(FloatConstants_IsDirty, 0x1480a0)
{
    // The body is shared (folded) with other classes: ours is checked against it through the dirty byte, as it is.
    RunSeeds(ctx, "IsDirty", 24, false, [&](difftest::Context &c, Table &t, std::mt19937 &, Blob &b) {
        b.push_back(t.Call<uint8_t>(c, 0x1480a0, 16));
    });
}

DIFF_TEST(FloatConstants_SetDirty, 0x1489f0)
{
    RunSeeds(ctx, "SetDirty", 24, false, [&](difftest::Context &c, Table &t, std::mt19937 &rng, Blob &) {
        t.Call<void>(c, 0x1489f0, 17, uint32_t(rng() % 3));
    });
}

// ---- the table ----

DIFF_TEST(FloatConstants_GetName, 0x4df400)
{
    RunSeeds(ctx, "GetName", 60, false, [&](difftest::Context &c, Table &t, std::mt19937 &rng, Blob &b) {
        static const int32_t kEdge[] = {-1, 0x7fffffff, INT32_MIN, -2};
        for (int i = 0; i < 6; ++i) {
            const int32_t index = rng() % 4 == 0 ? kEdge[rng() % 4] : int32_t(rng() % (t.Count() + 3)) - 1;
            const char *r = t.Call<const char *>(c, 0x4df400, 6, index);
            b.push_back(r != nullptr);
            if (r) PushText(b, r);
            if (r && t.Count() != 0) {
                // The pointer is the entry's own text, not a copy.
                const uint32_t at = uint32_t(index);
                b.push_back(at < t.Count() && r == Table::Name(t.Entry(at)).mString.c_str());
            }
        }
    });
}

DIFF_TEST(FloatConstants_GetFloat, 0x4df320)
{
    RunSeeds(ctx, "GetFloat", 80, false, [&](difftest::Context &c, Table &t, std::mt19937 &rng, Blob &b) {
        for (int i = 0; i < 4; ++i) {
            const std::string name = PickName(t, rng);
            const float defaultValue = AsFloat(RandFloatBits(rng));
            const bool add = rng() % 2;
            const float r = t.GetFloat(c, name.c_str(), defaultValue, add);
            b.push_back(std::bit_cast<uint32_t>(r));
        }
    });
}

DIFF_TEST(FloatConstants_SetFloat, 0x4df8c0)
{
    RunSeeds(ctx, "SetFloat", 80, false, [&](difftest::Context &c, Table &t, std::mt19937 &rng, Blob &) {
        for (int i = 0; i < 4; ++i) {
            const std::string name = PickName(t, rng);
            // Often the value an entry holds already (no change, dirty left as it was), sometimes NaN over NaN.
            uint32_t bits = RandFloatBits(rng);
            if (t.Count() != 0 && rng() % 3 == 0) std::memcpy(&bits, t.Entry(rng() % t.Count()) + 4, 4);
            t.raw[8] = rng() % 2 ? 0 : static_cast<uint8_t>(rng());
            t.SetFloat(c, name.c_str(), AsFloat(bits));
        }
    });
}

DIFF_TEST(FloatConstants_GetDescription, 0x4df380)
{
    RunSeeds(ctx, "GetDescription", 60, false, [&](difftest::Context &c, Table &t, std::mt19937 &rng, Blob &b) {
        for (int i = 0; i < 4; ++i) {
            const std::string name = PickName(t, rng);
            const char *r = t.Call<const char *>(c, 0x4df380, 9, name.c_str());
            b.push_back(r != nullptr);
            if (r) PushText(b, r);
        }
    });
}

DIFF_TEST(FloatConstants_SetDescription, 0x4dfa30)
{
    RunSeeds(ctx, "SetDescription", 60, true, [&](difftest::Context &c, Table &t, std::mt19937 &rng, Blob &) {
        for (int i = 0; i < 4; ++i) {
            const std::string name = PickName(t, rng);
            const std::string text = RandText(rng);
            t.SetDescription(c, name.c_str(), text.c_str());
        }
    });
}

DIFF_TEST(FloatConstants_Has, 0x4df460)
{
    RunSeeds(ctx, "Has", 60, true, [&](difftest::Context &c, Table &t, std::mt19937 &rng, Blob &b) {
        for (int i = 0; i < 6; ++i) {
            const std::string name = PickName(t, rng);
            b.push_back(t.Call<uint8_t>(c, 0x4df460, 11, name.c_str()));
        }
    });
}

DIFF_TEST(FloatConstants_Remove, 0x4df760)
{
    RunSeeds(ctx, "Remove", 120, true, [&](difftest::Context &c, Table &t, std::mt19937 &rng, Blob &) {
        for (int i = 0; i < 3; ++i) {
            const std::string name = PickName(t, rng);
            t.raw[8] = rng() % 2 ? 0 : 1;
            if (t.side == 1) Adapters::Remove(reinterpret_cast<Self *>(t.raw), nullptr, name.c_str());
            else t.Call<void>(c, 0x4df760, 12, name.c_str());
        }
    });
}

DIFF_TEST(FloatConstants_Clear, 0x4df840)
{
    RunSeeds(ctx, "Clear", 30, false, [&](difftest::Context &c, Table &t, std::mt19937 &, Blob &) { t.Call<void>(c, 0x4df840, 13); });
}

// ---- the resource service ----

DIFF_TEST(FloatConstants_Load, 0x4df4a0)
{
    ServiceCell service;
    gCtx = &ctx;
    RunSeeds(ctx, "Load", 40, false, [&](difftest::Context &c, Table &t, std::mt19937 &rng, Blob &) {
        gTable = &t;
        void *segment = reinterpret_cast<void *>(uintptr_t(rng()));
        const uint32_t instance = rng();
        if (t.side == 1) Adapters::Load(reinterpret_cast<Self *>(t.raw), nullptr, segment, instance);
        else t.Call<void>(c, 0x4df4a0, 14, segment, instance);
    });
}

DIFF_TEST(FloatConstants_Save, 0x4df880)
{
    ServiceCell service;
    gCtx = &ctx;
    RunSeeds(ctx, "Save", 60, false, [&](difftest::Context &c, Table &t, std::mt19937 &rng, Blob &) {
        gTable = &t;
        void *fileRef = rng() % 4 == 0 ? nullptr : &gFileRef;
        const uint32_t instance = rng();
        if (t.side == 1) Adapters::Save(reinterpret_cast<Self *>(t.raw), nullptr, fileRef, instance);
        else t.Call<void>(c, 0x4df880, 15, fileRef, instance);
    });
}

// ---- the stream forms ----

DIFF_TEST(FloatConstants_Write, 0x4dfae0)
{
    InitStreams();
    RunSeeds(ctx, "Write", 40, false, [&](difftest::Context &c, Table &t, std::mt19937 &, Blob &b) {
        ResetOut(reinterpret_cast<uint8_t **>(&t.Words()[3]));
        b.push_back(t.Call<uint8_t>(c, 0x4dfae0, 3, &gOut));
    });
}

DIFF_TEST(FloatConstants_Read, 0x4df5e0)
{
    InitStreams();
    RunSeeds(ctx, "Read", 120, false, [&](difftest::Context &c, Table &t, std::mt19937 &rng, Blob &b) {
        // The strings' positions are counted from the vector's block as it is when each is read: the table's own begin word.
        ResetIn(rng, reinterpret_cast<uint8_t **>(&t.Words()[3]), 5);
        b.push_back(t.Call<uint8_t>(c, 0x4df5e0, 4, &gIn));
        b.push_back(gIn.u32Calls);
    });
}

namespace {
// An entry on its own, with random text and value, and the stream positions counted from it.
struct LoneEntry {
    Pair pair;
    uint8_t *base;
    void Randomise(std::mt19937 &rng)
    {
        pair.mValue = AsFloat(RandFloatBits(rng));
        pair.mName.mString = RandText(rng);
        pair.mDescription.mString = RandText(rng);
        base = reinterpret_cast<uint8_t *>(&pair);
    }
};
} // namespace

DIFF_TEST(FloatConstantPair_Read, 0x4df500)
{
    InitStreams();
    Patches patches;
    for (uint32_t seed = 0; seed < 160; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed * 13 + 1);
            LoneEntry entry;
            entry.Randomise(rng);
            ResetIn(rng, &entry.base, 0);
            gRng.seed(seed + 9);
            ResetLog();
            const uint8_t r = side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x4df500)(&entry.pair, &gIn)
                                        : static_cast<uint8_t>(entry.pair.Read(reinterpret_cast<ifaces::cIGZIStream *>(&gIn)));
            Blob &b = blobs[side];
            b.push_back(r);
            Blob state;
            CaptureEntry(state, reinterpret_cast<uint8_t *>(&entry.pair));
            // (an entry's own vtable word is the side's: not part of the bytes compared)
            b.insert(b.end(), state.begin(), state.end());
            b.insert(b.end(), gLog.begin(), gLog.end());
        }
        Same(ctx, blobs[0], blobs[1], "PairRead", seed);
    }
}

DIFF_TEST(FloatConstantPair_Write, 0x4dfa90)
{
    InitStreams();
    Patches patches;
    for (uint32_t seed = 0; seed < 40; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed * 13 + 1);
            LoneEntry entry;
            entry.Randomise(rng);
            ResetOut(&entry.base);
            gRng.seed(seed + 9);
            ResetLog();
            const uint8_t r = side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x4dfa90)(&entry.pair, &gOut)
                                        : static_cast<uint8_t>(entry.pair.Write(reinterpret_cast<ifaces::cIGZOStream *>(&gOut)));
            Blob &b = blobs[side];
            b.push_back(r);
            b.insert(b.end(), gLog.begin(), gLog.end());
        }
        Same(ctx, blobs[0], blobs[1], "PairWrite", seed);
    }
}
