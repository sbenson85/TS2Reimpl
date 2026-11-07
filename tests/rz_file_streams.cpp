// Differential tests for src/rz/RZFileStreams.*: cRZTextOStream, cRZTextODSStream, cRZOFStream, cRZIFStream and the
// path helper 0x1b5fa4.
//
// The streams are hook-shaped (the game's vtables): each side gets its own vtable. The original side keeps the image's
// (every slot the original code), ours a copy of it with our members in the slots this file covers, so every virtual
// call a member makes reaches the same side's code. Values go in through function pointers of the original's
// signature (byte arguments as full words with junk above the byte), so ours sees the raw words the game passes.
//
// The file streams use real files in the harness sandbox: each side writes its own file and the bytes are compared;
// both sides read the same file. The objects are compared byte for byte except the vtable word (each side's own) and
// the stream pointer at +0x10 (a heap address).
#define _CRT_SECURE_NO_WARNINGS
#include "difftest.h"
#include "game/game.h"
#include "hook/hooks.h"
#include "impfx.h"
#include "rz/RZFileStreams.h"
#include "rz/cRZString.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kOFStreamConstruct = 0x1b4dd2;
constexpr uint32_t kOFStreamConstructChars = 0x1b4e80;
constexpr uint32_t kIFStreamConstruct = 0x1b4cdc;
constexpr uint32_t kODSConstruct = 0x1b4f99;
constexpr uint32_t kConvertPath = 0x1b5fa4;
constexpr uint32_t kTextDestruct = 0x1b5319;
constexpr uint32_t kODSDestruct = 0x1b530e;
constexpr uint32_t kOFStreamDestruct = 0x1b52d8;
constexpr uint32_t kIFStreamDestruct = 0x1b528c;
constexpr int kSlots = 24;
constexpr int kDeletingDestructorSlot = 21;

// ---- the original's stdio ----
//
// The harness initialises the original's CRT (__scrt_initialize_crt) but runs none of its initialiser tables, so two
// pieces of library state the original's file streams need are missing: the UCRT's stream table (__acrt_initialize_stdio
// 0x838d0d, a .CRT$XIC initialiser; its __piob cell 0x10dfcb4 is null until then) and the STL's locks
// (std::_Init_locks::_Init_locks 0x821884, a C++ initialiser; its count 0x104f49c starts at -1). Both are library code of
// the image, run here once per process before an original stream opens a file.
constexpr uint32_t kStreamTableCell = 0x10dfcb4;
constexpr uint32_t kInitializeStdio = 0x838d0d;
constexpr uint32_t kInitLocks = 0x821884;
constexpr uint32_t kInitLocksCount = 0x104f49c;

void PrepareOriginalStdio(difftest::Context &ctx)
{
    if (*ctx.image.At<int32_t>(kInitLocksCount) == -1) {
        uint32_t dummy = 0;
        ctx.Original<void *(__thiscall *)(void *)>(kInitLocks)(&dummy);
    }
    if (*ctx.image.At<void *>(kStreamTableCell) == nullptr) ctx.Original<int(__cdecl *)()>(kInitializeStdio)();
}

// The first original stream builds the STL's locale (std::locale::_Init), which also points the image's classic locale
// object (0x10df6cc, 8 bytes, null in the file) at the classic _Locimp with a reference. StaticInitHT_OnImageTable
// destroys that object on both of its sides from the state it finds, so a set pointer there makes its sides differ.
// Each test here puts the object's bytes back as it found them (the reference stays taken: the _Locimp is kept).
constexpr uint32_t kClassicLocale = 0x10df6cc;
constexpr size_t kClassicLocaleSize = 8;

struct ClassicLocaleKept {
    uint8_t *object;
    uint8_t saved[kClassicLocaleSize];
    explicit ClassicLocaleKept(difftest::Context &ctx) : object(ctx.image.At<uint8_t>(kClassicLocale))
    {
        std::memcpy(saved, object, kClassicLocaleSize);
    }
    ~ClassicLocaleKept() { std::memcpy(object, saved, kClassicLocaleSize); }
};

// ---- vtables ----

struct VTable {
    void *slots[kSlots];
    VTable(const difftest::Context &ctx, uint32_t rva) { std::memcpy(slots, ctx.image.At(rva), sizeof(slots)); }
};

void OurTextSetters(VTable &vt)
{
    vt.slots[4] = MemberAddress(&cRZTextOStream::SetSint8);
    vt.slots[5] = MemberAddress(&cRZTextOStream::SetUint8);
    vt.slots[6] = MemberAddress(&cRZTextOStream::SetSint16);
    vt.slots[7] = MemberAddress(&cRZTextOStream::SetUint16);
    vt.slots[8] = MemberAddress(&cRZTextOStream::SetSint32);
    vt.slots[9] = MemberAddress(&cRZTextOStream::SetUint32);
    vt.slots[10] = MemberAddress(&cRZTextOStream::SetSint64);
    vt.slots[11] = MemberAddress(&cRZTextOStream::SetUint64);
    vt.slots[12] = MemberAddress(&cRZTextOStream::SetFloat32);
    vt.slots[13] = MemberAddress(&cRZTextOStream::SetFloat64);
    vt.slots[15] = MemberAddress(&cRZTextOStream::SetGZStr);
    vt.slots[17] = MemberAddress(&cRZTextOStream::SetVoid);
    vt.slots[kDeletingDestructorSlot] = MemberAddress(&cRZTextOStream::DeletingDestructor);
}

VTable OurOFStreamVTable(const difftest::Context &ctx)
{
    VTable vt(ctx, cRZOFStream::kVTable);
    vt.slots[0] = MemberAddress(&cRZOStream::QueryInterface);
    vt.slots[3] = MemberAddress(&cRZOFStream::Flush);
    vt.slots[4] = MemberAddress(&cRZOFStream::SetSint8);
    vt.slots[5] = MemberAddress(&cRZOFStream::SetUint8);
    vt.slots[6] = MemberAddress(&cRZOFStream::SetSint16);
    vt.slots[7] = MemberAddress(&cRZOFStream::SetUint16);
    vt.slots[8] = MemberAddress(&cRZOFStream::SetSint32);
    vt.slots[9] = MemberAddress(&cRZOFStream::SetUint32);
    vt.slots[10] = MemberAddress(&cRZOFStream::SetSint64);
    vt.slots[11] = MemberAddress(&cRZOFStream::SetUint64);
    vt.slots[12] = MemberAddress(&cRZOFStream::SetFloat32);
    vt.slots[13] = MemberAddress(&cRZOFStream::SetFloat64);
    vt.slots[14] = MemberAddress(&cRZOFStream::SetRZCharStr);
    vt.slots[15] = MemberAddress(&cRZOFStream::SetGZStr);
    vt.slots[16] = MemberAddress(&cRZOStream::SetGZSerializable);
    vt.slots[17] = MemberAddress(&cRZOFStream::SetVoid);
    vt.slots[18] = MemberAddress(&cRZOFStream::GetError);
    vt.slots[kDeletingDestructorSlot] = MemberAddress(&cRZOFStream::DeletingDestructor);
    return vt;
}

VTable OurIFStreamVTable(const difftest::Context &ctx)
{
    VTable vt(ctx, cRZIFStream::kVTable);
    vt.slots[0] = MemberAddress(&cRZIStream::QueryInterface);
    vt.slots[3] = MemberAddress(&cRZIFStream::Skip);
    vt.slots[4] = MemberAddress(&cRZIFStream::GetSint8);
    vt.slots[5] = MemberAddress(&cRZIFStream::GetSint8);
    vt.slots[6] = MemberAddress(&cRZIFStream::GetSint16);
    vt.slots[7] = MemberAddress(&cRZIFStream::GetUint16);
    vt.slots[8] = MemberAddress(&cRZIFStream::GetSint32);
    vt.slots[9] = MemberAddress(&cRZIFStream::GetUint32);
    vt.slots[10] = MemberAddress(&cRZIFStream::GetSint64);
    vt.slots[11] = MemberAddress(&cRZIFStream::GetUint64);
    vt.slots[12] = MemberAddress(&cRZIFStream::GetFloat32);
    vt.slots[13] = MemberAddress(&cRZIFStream::GetFloat64);
    vt.slots[14] = MemberAddress(&cRZIFStream::GetRZCharStr);
    vt.slots[15] = MemberAddress(&cRZIFStream::GetGZStr);
    vt.slots[16] = MemberAddress(&cRZIStream::GetGZSerializable);
    vt.slots[17] = MemberAddress(&cRZIFStream::GetVoid);
    vt.slots[18] = MemberAddress(&cRZOFStream::GetError);
    vt.slots[kDeletingDestructorSlot] = MemberAddress(&cRZIFStream::DeletingDestructor);
    return vt;
}

template <typename Fn> Fn SlotOf(const void *object, int slot)
{
    const void *const *vt = *static_cast<const void *const *const *>(object);
    return reinterpret_cast<Fn>(const_cast<void *>(vt[slot]));
}

// Storage for one object (at most 0x18 bytes at +16) with canaries around it.
struct Slot {
    uint8_t bytes[64];
    void Fill(uint8_t b) { std::memset(bytes, b, sizeof(bytes)); }
    void *Object() { return bytes + 16; }
    void SetWord(int offset, uint32_t value) { std::memcpy(bytes + 16 + offset, &value, 4); }
    uint32_t Word(int offset) const
    {
        uint32_t v;
        std::memcpy(&v, bytes + 16 + offset, 4);
        return v;
    }
};

// Object bytes except the vtable word and (for the file streams) the stream pointer.
bool SameObject(const Slot &a, const Slot &b, bool hasStream)
{
    Slot x = a, y = b;
    x.SetWord(0, 0);
    y.SetWord(0, 0);
    if (hasStream) {
        x.SetWord(0x10, x.Word(0x10) != 0);
        y.SetWord(0x10, y.Word(0x10) != 0);
    }
    return std::memcmp(x.bytes, y.bytes, sizeof(x.bytes)) == 0;
}

std::string Narrow(const std::wstring &w)
{
    std::string s;
    for (wchar_t c : w) s += static_cast<char>(c);
    return s;
}

std::vector<uint8_t> ReadFileBytes(const std::string &path)
{
    std::vector<uint8_t> out;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    int c;
    while ((c = std::fgetc(f)) != EOF) out.push_back(static_cast<uint8_t>(c));
    std::fclose(f);
    return out;
}

void WriteFileBytes(const std::string &path, const std::vector<uint8_t> &bytes)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    if (!bytes.empty()) std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
}

// A cRZString with the game's vtable (its slots are the original's string code on both sides).
struct GameString {
    cRZString s;
    explicit GameString(const char *text) { s.Construct(text); }
    ~GameString() { s.Destruct(); }
    GameString(const GameString &) = delete;
    GameString &operator=(const GameString &) = delete;
    cIGZString *Interface() { return s.AsInterface(); }
};

uint32_t Junk(std::mt19937 &rng, uint32_t byte) { return (rng() & 0xffffff00u) | (byte & 0xff); }

const int64_t kInts[] = {0, 1, -1, 127, -128, 255, 32767, -32768, 65535, 2147483647, -2147483647 - 1, 4294967295ll,
                         0x7fffffffffffffffll, static_cast<int64_t>(0x8000000000000000ull), 123456789012345ll};
const double kFloats[] = {0.0, -0.0, 1.0, -1.5, 3.14159265358979, 1e30, -1e-30, 1e300, 123456.789, 0.1};

int64_t RandomInt(std::mt19937 &rng)
{
    if (rng() % 3 == 0) return kInts[rng() % std::size(kInts)];
    return static_cast<int64_t>((uint64_t(rng()) << 32) | rng()) >> (rng() % 64);
}

double RandomFloat(std::mt19937 &rng)
{
    if (rng() % 2 == 0) return kFloats[rng() % std::size(kFloats)];
    return (double(int32_t(rng())) / double(rng() | 1)) * std::pow(10.0, int(rng() % 20) - 10);
}

std::string RandomText(std::mt19937 &rng, uint32_t maxLength)
{
    std::string s;
    const uint32_t n = rng() % (maxLength + 1);
    for (uint32_t i = 0; i < n; ++i) s += static_cast<char>(rng() % 3 ? 0x20 + rng() % 95 : 1 + rng() % 255);
    return s;
}

// ---- the setters: one call of one output slot ----

using Set8 = uint32_t(__thiscall *)(void *, uint32_t);
using Set64 = uint32_t(__thiscall *)(void *, uint64_t);
using SetF32 = uint32_t(__thiscall *)(void *, float);
using SetF64 = uint32_t(__thiscall *)(void *, double);
using SetText = uint32_t(__thiscall *)(void *, const char *);
using SetString = uint32_t(__thiscall *)(void *, cIGZString *);
using SetBytes = uint32_t(__thiscall *)(void *, const void *, uint32_t);
using NoArgs = uint32_t(__thiscall *)(void *);

struct SetterCall {
    int slot = 4;
    uint32_t word = 0;
    uint64_t wide = 0;
    float f32 = 0;
    double f64 = 0;
    std::string text;
    std::vector<uint8_t> bytes;
};

SetterCall RandomSetter(std::mt19937 &rng, bool withFlushAndError)
{
    SetterCall c;
    const int slots[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 3, 18};
    c.slot = slots[rng() % (std::size(slots) - (withFlushAndError ? 0 : 2))];
    const int64_t v = RandomInt(rng);
    c.word = rng() % 2 ? static_cast<uint32_t>(v) : rng();
    c.wide = static_cast<uint64_t>(v);
    c.f64 = RandomFloat(rng);
    c.f32 = static_cast<float>(RandomFloat(rng));
    c.text = RandomText(rng, rng() % 8 ? 20 : 300);
    const uint32_t n = rng() % 6 == 0 ? 0 : rng() % 80;
    for (uint32_t i = 0; i < n; ++i) c.bytes.push_back(static_cast<uint8_t>(rng()));
    return c;
}

// Calls the slot on `object` through its vtable; returns the result's low byte (the setters return a bool in al) and
// for slot 3 (Flush, no result) 0.
uint32_t CallSetter(void *object, const SetterCall &c)
{
    switch (c.slot) {
    case 3: SlotOf<NoArgs>(object, 3)(object); return 0;
    case 10: case 11: return SlotOf<Set64>(object, c.slot)(object, c.wide) & 0xff;
    case 12: return SlotOf<SetF32>(object, 12)(object, c.f32) & 0xff;
    case 13: return SlotOf<SetF64>(object, 13)(object, c.f64) & 0xff;
    case 14: return SlotOf<SetText>(object, 14)(object, c.text.c_str()) & 0xff;
    case 15: {
        GameString s(c.text.c_str());
        return SlotOf<SetString>(object, 15)(object, s.Interface()) & 0xff;
    }
    case 17: return SlotOf<SetBytes>(object, 17)(object, c.bytes.data(), uint32_t(c.bytes.size())) & 0xff;
    case 18: return SlotOf<NoArgs>(object, 18)(object) & 0xff;
    default: return SlotOf<Set8>(object, c.slot)(object, c.word) & 0xff;
    }
}

// ---- cRZTextOStream: slot 14 is a recorder ----

std::vector<std::string> gWrites;
std::vector<uint32_t> gWriteResults; // what each slot 14 call answers, in order (then 1)
size_t gWriteIndex = 0;

uint32_t __fastcall RecordWrite(void *, void *, const char *text)
{
    gWrites.push_back(text ? text : "(null)");
    const uint32_t r = gWriteIndex < gWriteResults.size() ? gWriteResults[gWriteIndex] : 0xabcd0001u;
    ++gWriteIndex;
    return r;
}

} // namespace

// Every text setter (slots 4-13: SetSint8 0x1b63f6, SetUint8 0x1b65c3, SetSint16 0x1b624d, SetUint16 0x1b648d,
// SetSint32 0x1b62dd, SetUint32 0x1b64f0, SetSint64 0x1b6383, SetUint64 0x1b655f, SetFloat32 0x1b600b, SetFloat64
// 0x1b609a), SetGZStr 0x1b6153 and the hex dump SetVoid 0x1b6648 on a text stream whose slot 14 records the text and
// answers from a script (some writes fail, with junk above al).
DIFF_TEST(RZTextOStream_Setters, 0x1b63f6)
{
    ClassicLocaleKept keepLocale(ctx);
    VTable origVt(ctx, cRZTextOStream::kVTable);
    VTable ourVt(ctx, cRZTextOStream::kVTable);
    OurTextSetters(ourVt);
    origVt.slots[14] = ourVt.slots[14] = reinterpret_cast<void *>(&RecordWrite);
    std::mt19937 rng(7101);
    for (int i = 0; i < 6000; ++i) {
        const SetterCall c = RandomSetter(rng, false);
        std::vector<uint32_t> script;
        for (int k = 0; k < 120; ++k) script.push_back(rng() % 12 == 0 ? rng() & 0xffffff00u : (rng() & 0xffffff00u) | 1 + rng() % 255);
        Slot a, b;
        a.Fill(0x5a);
        a.SetWord(0, 0);
        a.SetWord(4, rng() % 4);
        a.SetWord(8, rng());
        a.SetWord(12, 0);
        b = a;
        a.SetWord(0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(origVt.slots)));
        b.SetWord(0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ourVt.slots)));
        auto run = [&](Slot &s, std::vector<std::string> &writes) {
            gWrites.clear();
            gWriteResults = script;
            gWriteIndex = 0;
            const uint32_t r = CallSetter(s.Object(), c);
            writes = gWrites;
            return r;
        };
        std::vector<std::string> wa, wb;
        const uint32_t ra = run(a, wa);
        const uint32_t rb = run(b, wb);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("case %d slot %d: result original 0x%x, ours 0x%x", i, c.slot, ra, rb);
        if (wa != wb) {
            ctx.Fail("case %d slot %d: writes differ (original %zu, ours %zu)", i, c.slot, wa.size(), wb.size());
            for (size_t k = 0; k < wa.size() && k < wb.size(); ++k) {
                if (wa[k] != wb[k]) {
                    ctx.Fail("  write %zu: original \"%.60s\", ours \"%.60s\"", k, wa[k].c_str(), wb[k].c_str());
                    break;
                }
            }
        }
        if (!SameObject(a, b, false)) ctx.Fail("case %d slot %d: object bytes differ", i, c.slot);
        if (ctx.failures.size() >= 20) return;
    }
}

// cRZTextODSStream: the constructor 0x1b4f99 against ours, then setters through each side's vtable reaching
// SetRZCharStr 0x1b61fe (OutputDebugStringA, faked), then the destructors 0x1b530e (and through it the text stream's
// 0x1b5319) and the deleting destructors 0x1b5829 / 0x1b5848 (flags 0; with 1 on a heap object).
DIFF_TEST(RZTextODSStream_Script, 0x1b61fe)
{
    ClassicLocaleKept keepLocale(ctx);
    impfx::Imports imp(ctx);
    IMPFX_ADD(imp, OutputDebugStringA, "s");
    VTable ourVt(ctx, cRZTextODSStream::kVTable);
    OurTextSetters(ourVt);
    ourVt.slots[14] = MemberAddress(&cRZTextODSStream::SetRZCharStr);
    ourVt.slots[kDeletingDestructorSlot] = MemberAddress(&cRZTextODSStream::DeletingDestructor);
    using Ctor = void *(__thiscall *)(void *);
    using Dtor = void(__thiscall *)(void *);
    using Deleting = void *(__thiscall *)(void *, uint32_t);
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        Slot objects[2];
        auto side = [&](bool ours) {
            return drawfx::Side([&] { imp.BeginSide(); }, [&] {
                std::mt19937 rng(seed);
                Slot &s = objects[ours];
                s.Fill(0xc3);
                void *self = ours ? reinterpret_cast<Ctor>(MemberAddress(&cRZTextODSStream::Construct))(s.Object())
                                  : ctx.Original<Ctor>(kODSConstruct)(s.Object());
                drawfx::Note("this", self == s.Object());
                drawfx::Note("vtable", s.Word(0) == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At(cRZTextODSStream::kVTable))));
                if (ours) s.SetWord(0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ourVt.slots)));
                const int calls = int(rng() % 6);
                for (int k = 0; k < calls; ++k) drawfx::Note("result", CallSetter(s.Object(), RandomSetter(rng, false)));
                const uint32_t end = rng() % 3;
                if (end == 0) {
                    (ours ? reinterpret_cast<Dtor>(MemberAddress(&cRZTextODSStream::Destruct)) : ctx.Original<Dtor>(kODSDestruct))(s.Object());
                } else if (end == 1) {
                    drawfx::Note("deleting", SlotOf<Deleting>(s.Object(), kDeletingDestructorSlot)(s.Object(), rng() & ~1u) == s.Object());
                } else {
                    // A heap copy, deleted with flags & 1.
                    void *heap = ::operator new(0x10);
                    std::memcpy(heap, s.Object(), 0x10);
                    drawfx::Note("deleting heap", SlotOf<Deleting>(heap, kDeletingDestructorSlot)(heap, rng() | 1) == heap);
                }
                drawfx::Note("vtable after", s.Word(0) == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At(cRZOStream::kVTable))));
            });
        };
        auto a = side(false);
        auto b = side(true);
        if (!drawfx::Same(ctx, a.first, b.first, "ods", seed, a.second.empty() ? b.second : a.second)) return;
        if (!SameObject(objects[0], objects[1], false)) {
            ctx.Fail("ods seed %u: object bytes differ", seed);
            return;
        }
    }
}

// The text stream's own destructor 0x1b5319 and deleting destructor 0x1b5848 on an object with an owner.
DIFF_TEST(RZTextOStream_Destruct, 0x1b5319)
{
    ClassicLocaleKept keepLocale(ctx);
    static uint32_t releases;
    struct Owner {
        static uint32_t __fastcall Release(void *, void *) { return ++releases; }
    };
    void *ownerVt[3] = {nullptr, nullptr, reinterpret_cast<void *>(&Owner::Release)};
    void *owner = ownerVt;
    using Dtor = void(__thiscall *)(void *);
    using Deleting = void *(__thiscall *)(void *, uint32_t);
    VTable ourVt(ctx, cRZTextOStream::kVTable);
    OurTextSetters(ourVt);
    std::mt19937 rng(7103);
    for (int i = 0; i < 2000; ++i) {
        Slot a;
        a.Fill(0x77);
        a.SetWord(4, rng());
        a.SetWord(8, rng());
        a.SetWord(12, rng() % 2 ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&owner)) : 0);
        Slot b = a;
        const bool deleting = rng() % 2;
        a.SetWord(0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At(cRZTextOStream::kVTable))));
        b.SetWord(0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ourVt.slots)));
        releases = 0;
        uint32_t ra = 0, rb = 0;
        if (deleting) ra = SlotOf<Deleting>(a.Object(), kDeletingDestructorSlot)(a.Object(), rng() & ~1u) == a.Object();
        else ctx.Original<Dtor>(kTextDestruct)(a.Object());
        const uint32_t releasesA = releases;
        releases = 0;
        if (deleting) rb = SlotOf<Deleting>(b.Object(), kDeletingDestructorSlot)(b.Object(), rng() & ~1u) == b.Object();
        else reinterpret_cast<Dtor>(MemberAddress(&cRZTextOStream::Destruct))(b.Object());
        ++ctx.cases;
        if (ra != rb || releasesA != releases) ctx.Fail("case %d: results or owner releases differ", i);
        if (std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) != 0) ctx.Fail("case %d: object bytes differ", i);
        if (ctx.failures.size() >= 20) return;
    }
}

namespace {

// ---- cRZOFStream ----

using OFCtor = void *(__thiscall *)(void *, const void *, uint32_t, uint32_t, uint32_t, uint32_t);

struct OFCase {
    uint32_t binary, order, autoFlush, append;
    bool chars;
    std::vector<uint8_t> prefix;
    std::vector<SetterCall> calls;
    uint32_t endFlags;
};

OFCase RandomOFCase(std::mt19937 &rng)
{
    OFCase c;
    const uint8_t flags[] = {0, 1, 1, 0, 2, 0xff};
    c.binary = Junk(rng, flags[rng() % (rng() % 8 ? 2 : std::size(flags))]);
    c.order = rng();
    c.autoFlush = Junk(rng, flags[rng() % 4]);
    c.append = Junk(rng, flags[rng() % (rng() % 8 ? 2 : std::size(flags))]);
    c.chars = rng() % 3 == 0;
    const uint32_t n = rng() % 3 ? rng() % 20 : 0;
    for (uint32_t i = 0; i < n; ++i) c.prefix.push_back(static_cast<uint8_t>('a' + rng() % 26));
    const int calls = int(rng() % 25);
    for (int k = 0; k < calls; ++k) c.calls.push_back(RandomSetter(rng, true));
    c.endFlags = rng() & ~1u;
    return c;
}

struct OFResult {
    std::vector<uint32_t> results;
    std::vector<uint8_t> file;
    Slot object;
};

OFResult RunOF(difftest::Context &ctx, const OFCase &c, bool ours, const VTable &ourVt, const std::string &path)
{
    OFResult r;
    WriteFileBytes(path, c.prefix);
    Slot &s = r.object;
    s.Fill(0x3c);
    void *self;
    if (c.chars) {
        OFCtor ctor = ours ? reinterpret_cast<OFCtor>(MemberAddress(static_cast<cRZOFStreamFromChars>(&cRZOFStream::Construct)))
                           : ctx.Original<OFCtor>(kOFStreamConstructChars);
        self = ctor(s.Object(), path.c_str(), c.binary, c.order, c.autoFlush, c.append);
    } else {
        GameString name(path.c_str());
        OFCtor ctor = ours ? reinterpret_cast<OFCtor>(MemberAddress(static_cast<cRZOFStreamFromString>(&cRZOFStream::Construct)))
                           : ctx.Original<OFCtor>(kOFStreamConstruct);
        self = ctor(s.Object(), name.Interface(), c.binary, c.order, c.autoFlush, c.append);
    }
    r.results.push_back(self == s.Object());
    r.results.push_back(s.Word(0) == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At(cRZOFStream::kVTable))));
    r.results.push_back(s.Word(0x10) != 0);
    if (ours) s.SetWord(0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ourVt.slots)));
    for (const SetterCall &call : c.calls) {
        r.results.push_back(CallSetter(s.Object(), call));
        r.results.push_back(static_cast<uint32_t>(ReadFileBytes(path).size())); // what reached the disk so far
    }
    using Deleting = void *(__thiscall *)(void *, uint32_t);
    using Dtor = void(__thiscall *)(void *);
    if (c.endFlags & 2) {
        (ours ? reinterpret_cast<Dtor>(MemberAddress(&cRZOFStream::Destruct)) : ctx.Original<Dtor>(kOFStreamDestruct))(s.Object());
    } else {
        r.results.push_back(SlotOf<Deleting>(s.Object(), kDeletingDestructorSlot)(s.Object(), c.endFlags) == s.Object());
    }
    r.results.push_back(s.Word(0) == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At(cRZOStream::kVTable))));
    r.file = ReadFileBytes(path);
    return r;
}

} // namespace

// cRZOFStream from its two constructors (0x1b4dd2 with a string, 0x1b4e80 with chars; both through 0x1b5fa4) over
// random byte flags (binary, auto-flush, append: raw bytes, junk above), then random setters through the vtable:
// the dispatchers (slots 4-13, 15: 0x1b63d8, 0x1b65b4, 0x1b620d, 0x1b644a, 0x1b62a1, 0x1b64e1, 0x1b632f, 0x1b6542,
// 0x1b5fdb, 0x1b6069, 0x1b6109), SetRZCharStr 0x1b616d, SetVoid 0x1b661f, Flush 0x1b5939, GetError 0x1b5941, and the
// text setters (0x1b63f6, 0x1b65c3, 0x1b624d, 0x1b648d, 0x1b62dd, 0x1b64f0, 0x1b6383, 0x1b655f, 0x1b600b, 0x1b609a,
// 0x1b6153) and cRZOStream's binary ones behind them; then the destructor 0x1b52d8 (and through it the text stream's
// 0x1b5319) directly or through the deleting destructor 0x1b57eb (flags 0). Each side writes its own sandbox file
// (with the same prefix, for append); the file's size on disk after each call (what the auto-flush wrote) and its
// bytes at the end must match.
DIFF_TEST(RZOFStream_Script, 0x1b4dd2)
{
    ClassicLocaleKept keepLocale(ctx);
    PrepareOriginalStdio(ctx);
    const VTable ourVt = OurOFStreamVTable(ctx);
    const std::string pathA = Narrow(difftest::SandboxPath(L"ofstream_orig.txt"));
    const std::string pathB = Narrow(difftest::SandboxPath(L"ofstream_ours.txt"));
    std::mt19937 rng(7104);
    for (int i = 0; i < 1500; ++i) {
        const OFCase c = RandomOFCase(rng);
        ctx.Mark("ofstream original");
        const OFResult a = RunOF(ctx, c, false, ourVt, pathA);
        ctx.Mark("ofstream ours");
        const OFResult b = RunOF(ctx, c, true, ourVt, pathB);
        ++ctx.cases;
        if (a.results != b.results) {
            size_t k = 0;
            while (k < a.results.size() && k < b.results.size() && a.results[k] == b.results[k]) ++k;
            ctx.Fail("case %d (binary 0x%x append 0x%x): result %zu differs (original %zu results, ours %zu)", i, c.binary,
                     c.append, k, a.results.size(), b.results.size());
        }
        if (a.file != b.file) ctx.Fail("case %d (binary 0x%x append 0x%x): files differ (%zu / %zu bytes)", i, c.binary,
                                       c.append, a.file.size(), b.file.size());
        if (!SameObject(a.object, b.object, true)) ctx.Fail("case %d: object bytes differ", i);
        if (ctx.failures.size() >= 20) return;
    }
}

// The path helper 0x1b5fa4 (cdecl): a fresh string converted from the path.
DIFF_TEST(RZFileStreams_ConvertPath, 0x1b5fa4)
{
    using Convert = void *(__cdecl *)(void *, const void *);
    std::mt19937 rng(7105);
    for (int i = 0; i < 3000; ++i) {
        GameString path(RandomText(rng, rng() % 4 ? 30 : 400).c_str());
        alignas(4) uint8_t a[sizeof(cRZString)], b[sizeof(cRZString)];
        std::memset(a, 0xee, sizeof(a));
        std::memset(b, 0xee, sizeof(b));
        void *ra = ctx.Original<Convert>(kConvertPath)(a, path.Interface());
        void *rb = reinterpret_cast<Convert>(&rzfilestreams::ConvertPath)(b, path.Interface());
        auto *sa = reinterpret_cast<cRZString *>(a);
        auto *sb = reinterpret_cast<cRZString *>(b);
        ++ctx.cases;
        if ((ra == a) != (rb == b)) ctx.Fail("case %d: return differs", i);
        if (sa->mString != sb->mString || sa->mVTable != sb->mVTable || sa->mRefCount != sb->mRefCount) {
            ctx.Fail("case %d: strings differ", i);
        }
        sa->Destruct();
        sb->Destruct();
        if (ctx.failures.size() >= 20) return;
    }
}

namespace {

// ---- cRZIFStream ----

std::string Hex(const void *data, size_t n)
{
    std::string out;
    char b[4];
    for (size_t i = 0; i < n && i < 40; ++i) {
        std::snprintf(b, sizeof(b), "%02x", static_cast<const uint8_t *>(data)[i]);
        out += b;
    }
    return out;
}

using IFCtor = void *(__thiscall *)(void *, const void *, uint32_t, uint32_t);
using Get = uint32_t(__thiscall *)(void *, void *);
using GetChars = uint32_t(__thiscall *)(void *, char *, uint32_t);
using GetString = uint32_t(__thiscall *)(void *, cIGZString *);
using Skip = uint32_t(__thiscall *)(void *, uint32_t);

// Text the getters parse: numbers in several forms, words, separators, NULs, now and then any byte.
std::vector<uint8_t> RandomInput(std::mt19937 &rng)
{
    std::string s;
    const int tokens = int(rng() % 40);
    for (int t = 0; t < tokens; ++t) {
        switch (rng() % 9) {
        case 0: s += std::to_string(RandomInt(rng)); break;
        case 1: s += std::to_string(static_cast<uint64_t>(RandomInt(rng))); break;
        case 2: { char b[64]; std::snprintf(b, sizeof(b), "%g", RandomFloat(rng)); s += b; break; }
        case 3: s += rng() % 2 ? "+" : "-"; s += std::to_string(rng() % 100000); break;
        case 4: s += RandomText(rng, 8); break;
        case 5: s += '\0'; break;
        case 6: s += rng() % 2 ? " " : rng() % 2 ? "\n" : "\t"; break;
        case 7: s += static_cast<char>(rng()); break;
        default: s += "123456789012345678901234567890"; break;
        }
        if (rng() % 3) s += ' ';
    }
    std::vector<uint8_t> out(s.begin(), s.end());
    if (rng() % 2) out.insert(out.end(), 8, 0);
    return out;
}

struct IFCall {
    int slot;
    uint32_t count;
    uint32_t size;
};

IFCall RandomGetter(std::mt19937 &rng)
{
    IFCall c;
    const int slots[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 18};
    c.slot = slots[rng() % std::size(slots)];
    c.count = rng() % 8 == 0 ? rng() : rng() % 12;
    c.size = rng() % 40;
    return c;
}

// A text-mode script that calls GetGZStr gets a file that ends in a NUL and has no 0x1a (the CRT's text-mode end of
// file) before it, so that a NUL always comes before the end of what is left (see Safe).
void EndTextInNul(std::vector<uint8_t> &file)
{
    for (uint8_t &b : file) {
        if (b == 0x1a) b = 0x1b;
    }
    file.push_back(0);
}

// The packed length (cRZIStream::GetPackedUint32) at `pos` and the number of bytes it takes; 0 bytes when the file
// ends inside it.
uint32_t PackedLengthAt(const std::vector<uint8_t> &file, size_t pos, size_t &bytes)
{
    uint32_t length = 0;
    bytes = 0;
    for (int shift = 0; pos + bytes < file.size() && shift < 35; shift += 7) {
        const uint8_t b = file[pos + bytes++];
        length |= uint32_t(b & 0x7f) << shift;
        if (!(b & 0x80)) return length;
    }
    bytes = 0;
    return length;
}

// Whether the call is safe on the original. `good` is our stream's rdstate() == 0 and `next` its next character
// (rdbuf()->sgetc(), which leaves the stream's state alone; EOF at the end, and at 0x1a in text mode); the stream's
// tellg() is not used for text mode: after a float extraction it reports a position behind the one the next read
// takes. In text mode GetGZStr 0x1b59db reads characters until a NUL and never leaves the loop when a read fails
// (its character is never set then), so it runs only on a good stream with characters left, which EndTextInNul makes
// end in a NUL. GetSint64 0x1b5d04 tests an uninitialised byte when the first character it reads is neither a sign
// nor a digit (both in RZFileStreams.h), so it runs only when the next character is one, or at the end. In binary
// mode GetGZStr reads a packed length and that many bytes in chunks of 0xff, stopping when a read fails; lengths of
// 0x10000 or more are skipped for time.
bool Safe(const IFCall &c, bool binary, bool good, int next, const std::vector<uint8_t> &file, long long pos)
{
    if (c.slot == 15) {
        if (!binary) return good && next != EOF;
        if (pos < 0) return true;
        size_t bytes;
        return PackedLengthAt(file, size_t(pos), bytes) < 0x10000;
    }
    if (c.slot == 10 && !binary) {
        return next == EOF || next == '+' || next == '-' || (next >= '0' && next <= '9');
    }
    return true;
}

// In binary mode the original's GetGZStr (cRZIStream::GetGZStr 0x1b5a4d) appends whole chunks from an uninitialised
// stack buffer, so after a read that ends early the string's tail past the bytes the file supplied is whatever its
// stack held (ours appends zeros there; ORIGINAL-BUG in cRZIStream.size.cpp). Returns how many leading bytes of the
// string the file determines: the string's whole length unless the file ends inside it.
size_t GZStrBytesFromFile(const std::vector<uint8_t> &file, long long pos, size_t stringSize)
{
    if (pos < 0) return stringSize;
    size_t bytes;
    PackedLengthAt(file, size_t(pos), bytes);
    const size_t start = size_t(pos) + bytes;
    const size_t available = start < file.size() ? file.size() - start : 0;
    return (std::min)(stringSize, available);
}

uint32_t CallGetter(void *object, const IFCall &c, std::vector<uint8_t> &out, GameString &text)
{
    out.assign(64, 0xa7);
    switch (c.slot) {
    case 3: return SlotOf<Skip>(object, 3)(object, c.count) & 0xff;
    case 14: return SlotOf<GetChars>(object, 14)(object, reinterpret_cast<char *>(out.data()), c.size) & 0xff;
    case 15: return SlotOf<GetString>(object, 15)(object, text.Interface()) & 0xff;
    case 17: return SlotOf<GetChars>(object, 17)(object, reinterpret_cast<char *>(out.data()), c.size) & 0xff;
    case 18: return SlotOf<NoArgs>(object, 18)(object) & 0xff;
    default: return SlotOf<Get>(object, c.slot)(object, out.data()) & 0xff;
    }
}

} // namespace

// cRZIFStream: the constructor 0x1b4cdc (through 0x1b5fa4) over random byte flags, then random getters through the
// vtable on a sandbox file both sides read: Skip 0x1b6704, the text getters (slots 4-15: 0x1b5ddd, 0x1b5c2b, 0x1b5e1d,
// 0x1b5c9a, 0x1b5e4d, 0x1b5d04, 0x1b5e7d, 0x1b5952, 0x1b5990, 0x1b5b46, 0x1b59db) or cRZIStream's binary ones,
// GetVoid 0x1b5f0a and GetError 0x1b5941; then the destructor 0x1b528c directly or through the deleting destructor
// 0x1b576f (flags 0). The constructor's path goes through ConvertPath 0x1b5fa4. Each result's low byte, each output
// buffer and string, and the object must match.
DIFF_TEST(RZIFStream_Script, 0x1b4cdc)
{
    ClassicLocaleKept keepLocale(ctx);
    PrepareOriginalStdio(ctx);
    const VTable ourVt = OurIFStreamVTable(ctx);
    const std::string path = Narrow(difftest::SandboxPath(L"ifstream_input.txt"));
    std::mt19937 rng(7106);
    for (int i = 0; i < 2500; ++i) {
        std::vector<uint8_t> file = RandomInput(rng);
        const uint8_t flags[] = {0, 1, 2, 0xff};
        const uint32_t binary = Junk(rng, flags[rng() % (rng() % 8 ? 2 : std::size(flags))]);
        const uint32_t order = rng();
        const uint32_t endFlags = rng() & ~1u;
        const int calls = int(rng() % 30);
        std::vector<IFCall> script;
        bool readsGZStr = false;
        for (int k = 0; k < calls; ++k) {
            script.push_back(RandomGetter(rng));
            readsGZStr |= script.back().slot == 15;
        }
        if (readsGZStr && (binary & 0xff) == 0) EndTextInNul(file);
        WriteFileBytes(path, file);

        Slot a, b;
        a.Fill(0x4b);
        b.Fill(0x4b);
        GameString name(path.c_str());
        void *ra = ctx.Original<IFCtor>(kIFStreamConstruct)(a.Object(), name.Interface(), binary, order);
        void *rb = reinterpret_cast<IFCtor>(MemberAddress(&cRZIFStream::Construct))(b.Object(), name.Interface(), binary, order);
        ++ctx.cases;
        if ((ra == a.Object()) != (rb == b.Object())) ctx.Fail("case %d: constructor results differ", i);
        if (a.Word(0) != b.Word(0)) ctx.Fail("case %d: constructor vtables differ", i);
        if (!SameObject(a, b, true)) ctx.Fail("case %d: constructed objects differ", i);
        b.SetWord(0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ourVt.slots)));
        auto *ours = static_cast<cRZIFStream *>(b.Object());
        for (size_t k = 0; k < script.size(); ++k) {
            const IFCall &c = script[k];
            const bool good = ours->mStream->rdstate() == 0;
            const int next = good ? ours->mStream->rdbuf()->sgetc() : EOF;
            const long long pos = good ? static_cast<long long>(ours->mStream->tellg()) : -1;
            if (!Safe(c, (binary & 0xff) != 0, good, next, file, pos)) continue;
            GameString ta(""), tb("");
            std::vector<uint8_t> oa, ob;
            ctx.Mark("ifstream original");
            const uint32_t xa = CallGetter(a.Object(), c, oa, ta);
            ctx.Mark("ifstream ours");
            const uint32_t xb = CallGetter(b.Object(), c, ob, tb);
            ++ctx.cases;
            if (xa != xb) ctx.Fail("case %d call %zu slot %d: result original 0x%x, ours 0x%x", i, k, c.slot, xa, xb);
            if (oa != ob) {
                ctx.Fail("case %d call %zu slot %d (binary 0x%x): output bytes differ: %s / %s", i, k, c.slot, binary,
                         Hex(oa.data(), 8).c_str(), Hex(ob.data(), 8).c_str());
            }
            const size_t determined = (binary & 0xff) ? GZStrBytesFromFile(file, pos, ta.s.mString.size()) : ta.s.mString.size();
            if (ta.s.mString.size() != tb.s.mString.size() ||
                ta.s.mString.compare(0, determined, tb.s.mString, 0, determined) != 0) {
                ctx.Fail("case %d call %zu slot %d (binary 0x%x, at %lld): strings differ: %s / %s", i, k, c.slot, binary, pos,
                         Hex(ta.s.mString.data(), ta.s.mString.size()).c_str(), Hex(tb.s.mString.data(), tb.s.mString.size()).c_str());
            }
            if (!SameObject(a, b, true)) ctx.Fail("case %d call %zu slot %d: object bytes differ", i, k, c.slot);
            if (ctx.failures.size() >= 20) return;
        }
        using Deleting = void *(__thiscall *)(void *, uint32_t);
        using Dtor = void(__thiscall *)(void *);
        uint32_t da = 0, db = 0;
        if (endFlags & 2) {
            ctx.Original<Dtor>(kIFStreamDestruct)(a.Object());
            reinterpret_cast<Dtor>(MemberAddress(&cRZIFStream::Destruct))(b.Object());
        } else {
            da = SlotOf<Deleting>(a.Object(), kDeletingDestructorSlot)(a.Object(), endFlags) == a.Object();
            db = SlotOf<Deleting>(b.Object(), kDeletingDestructorSlot)(b.Object(), endFlags) == b.Object();
        }
        ++ctx.cases;
        if (da != db) ctx.Fail("case %d: deleting destructor results differ", i);
        if (!SameObject(a, b, true)) ctx.Fail("case %d: destroyed objects differ", i);
        if (ctx.failures.size() >= 20) return;
    }
}
