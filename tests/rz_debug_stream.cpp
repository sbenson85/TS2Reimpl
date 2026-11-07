// Differential tests for src/rz/cDebugStream.*: the debug stream of the sim code (vtable 0xdcf704): its numeric and
// text setters (slots 4-9, 12, 14, 15: 0x520d70, 0x5211f0, 0x520bd0, 0x521050, 0x520ca0, 0x521120, 0x520720, 0x520ab0,
// 0x520800), the destructor body 0x511760 and the deleting destructor 0x512070, with the constructor sequence the sim
// code (0x5144a0) runs inline.
//
// The original side builds its object the way the sim code does (cRZOFStream constructor 0x1b4dd2, the vtable store,
// two empty strings stored field by field) and runs the image's table; ours is cDebugStream::Construct and our own
// table (ts2_own_vt_dcf704, every slot ours, the base's setters behind them too). Both write a sandbox file of their
// own. After each call the result (al), the text collected at +0x18, the words of both strings and the file's size on
// disk are compared; at the end the file's bytes and the destroyed object's words.
#define _CRT_SECURE_NO_WARNINGS
#include "difftest.h"
#include "game/game.h"
#include "hook/hooks.h"
#include "rz/cDebugStream.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kOFStreamConstruct = 0x1b4dd2;
constexpr uint32_t kDebugDestruct = 0x511760;
constexpr uint32_t kDeletingSlot = 21;

// The original's stdio and locale state: see tests/rz_file_streams.cpp (the harness runs none of the image's
// initialisers, so the two library pieces its file streams need are run here, once per process).
constexpr uint32_t kStreamTableCell = 0x10dfcb4;
constexpr uint32_t kInitializeStdio = 0x838d0d;
constexpr uint32_t kInitLocks = 0x821884;
constexpr uint32_t kInitLocksCount = 0x104f49c;
constexpr uint32_t kClassicLocale = 0x10df6cc;
constexpr size_t kClassicLocaleSize = 8;

void PrepareOriginalStdio(difftest::Context &ctx)
{
    if (*ctx.image.At<int32_t>(kInitLocksCount) == -1) {
        uint32_t dummy = 0;
        ctx.Original<void *(__thiscall *)(void *)>(kInitLocks)(&dummy);
    }
    if (*ctx.image.At<void *>(kStreamTableCell) == nullptr) ctx.Original<int(__cdecl *)()>(kInitializeStdio)();
}

struct ClassicLocaleKept {
    uint8_t *object;
    uint8_t saved[kClassicLocaleSize];
    explicit ClassicLocaleKept(difftest::Context &ctx) : object(ctx.image.At<uint8_t>(kClassicLocale))
    {
        std::memcpy(saved, object, kClassicLocaleSize);
    }
    ~ClassicLocaleKept() { std::memcpy(object, saved, kClassicLocaleSize); }
};

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

struct GameString {
    cRZString s;
    explicit GameString(const char *text) { s.Construct(text); }
    ~GameString() { s.Destruct(); }
    GameString(const GameString &) = delete;
    GameString &operator=(const GameString &) = delete;
    cIGZString *Interface() { return s.AsInterface(); }
};

// Storage for one object with canaries around it.
struct Storage {
    uint8_t bytes[cDebugStream::kSize + 32];
    Storage() { std::memset(bytes, 0x5a, sizeof(bytes)); }
    uint8_t *Object() { return bytes + 16; }
    uint32_t Word(int offset) const
    {
        uint32_t v;
        std::memcpy(&v, bytes + 16 + offset, 4);
        return v;
    }
    void SetWord(int offset, uint32_t v) { std::memcpy(bytes + 16 + offset, &v, 4); }
    const cRZString *Line() const { return reinterpret_cast<const cRZString *>(bytes + 16 + 0x18); }
};

template <typename Fn> Fn SlotOf(const void *object, int slot)
{
    const void *const *vt = *static_cast<const void *const *const *>(object);
    return reinterpret_cast<Fn>(const_cast<void *>(vt[slot]));
}

uint32_t Junk(std::mt19937 &rng, uint32_t bits, uint32_t value)
{
    const uint32_t mask = bits >= 32 ? 0xffffffffu : (1u << bits) - 1;
    return (rng() & ~mask) | (value & mask);
}

using OFCtor = void *(__thiscall *)(void *, const void *, uint32_t, uint32_t, uint32_t, uint32_t);
using Set32 = uint32_t(__thiscall *)(void *, uint32_t);
using SetF32 = uint32_t(__thiscall *)(void *, float);
using SetText = uint32_t(__thiscall *)(void *, const char *);
using SetString = uint32_t(__thiscall *)(void *, cIGZString *);
using Deleting = void *(__thiscall *)(void *, uint32_t);
using Destruct = void(__thiscall *)(void *);

struct Call {
    int slot = 4;
    uint32_t word = 0;
    float f32 = 0;
    std::string text;
};

const char *const kTexts[] = {"\n", "", "a", "\n\n", "a\n", "\r\n", " \n", "x", "Hello, world", "100%", "%d %s"};

Call RandomCall(std::mt19937 &rng)
{
    Call c;
    const int slots[] = {4, 5, 6, 7, 8, 9, 12, 14, 14, 15, 15};
    c.slot = slots[rng() % std::size(slots)];
    const uint32_t interesting[] = {0, 1, 0xff, 0x7f, 0x80, 0xffff, 0x8000, 0x7fffffffu, 0x80000000u, 0xffffffffu};
    const uint32_t v = rng() % 3 ? rng() : interesting[rng() % std::size(interesting)];
    switch (c.slot) {
    case 4: case 5: c.word = Junk(rng, 8, v); break;
    case 6: case 7: c.word = Junk(rng, 16, v); break;
    default: c.word = v; break;
    }
    const float floats[] = {0.f, -0.f, 1.f, -1.5f, 3.14159265f, 1e30f, -1e-30f, 123456.789f, 0.1f};
    c.f32 = rng() % 2 ? floats[rng() % std::size(floats)] : float(int32_t(rng())) / float(rng() | 1);
    if (rng() % 3 == 0) c.text = kTexts[rng() % std::size(kTexts)];
    else {
        const uint32_t n = rng() % 8 ? rng() % 30 : rng() % 300;
        for (uint32_t i = 0; i < n; ++i) c.text += static_cast<char>(rng() % 5 ? 0x20 + rng() % 95 : 1 + rng() % 255);
        if (rng() % 4 == 0) c.text += '\n';
    }
    return c;
}

uint32_t Run(void *object, const Call &c)
{
    switch (c.slot) {
    case 12: return SlotOf<SetF32>(object, 12)(object, c.f32) & 0xff;
    case 14: return SlotOf<SetText>(object, 14)(object, c.text.c_str()) & 0xff;
    case 15: {
        GameString s(c.text.c_str());
        return SlotOf<SetString>(object, 15)(object, s.Interface()) & 0xff;
    }
    default: return SlotOf<Set32>(object, c.slot)(object, c.word) & 0xff;
    }
}

struct Outcome {
    std::vector<uint32_t> words;       // results and string words, per call
    std::vector<std::string> lines;    // the text at +0x18 after each call
    std::vector<uint8_t> file;
};

// What a string's words say, apart from the heap pointer and the capacity.
void Snapshot(Outcome &o, const Storage &s)
{
    o.words.push_back(s.Word(0x18));  // vtable
    o.words.push_back(s.Word(0x18 + 0x1c));  // refcount
    o.words.push_back(s.Word(0x38));
    o.words.push_back(s.Word(0x38 + 0x1c));
    o.words.push_back(s.Word(0x38 + 0x14));  // size of the second string
    o.lines.push_back(std::string(s.Line()->mString));
}

Outcome RunSide(difftest::Context &ctx, bool ours, const std::vector<Call> &calls, uint32_t endMode, const std::string &path)
{
    Outcome o;
    Storage s;
    GameString name(path.c_str());
    void *object = s.Object();
    if (ours) {
        reinterpret_cast<cDebugStream *>(object)->Construct(*name.Interface());
    } else {
        ctx.Original<OFCtor>(kOFStreamConstruct)(object, name.Interface(), 0, 0, 0, 0);
        // The sim code's inline stores: the vtable, then both strings field by field (an empty cRZString).
        s.SetWord(0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At(cDebugStream::kVTable))));
        const uint32_t stringVTable = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At(cRZString::kVTable)));
        for (int base : {0x18, 0x38}) {
            for (int off = 4; off < 0x14; off += 4) s.SetWord(base + off, 0);
            s.SetWord(base, stringVTable);
            s.SetWord(base + 0x14, 0);
            s.SetWord(base + 0x18, 0xf);
            s.SetWord(base + 0x1c, 0);
        }
    }
    o.words.push_back(s.Word(0));
    o.words.push_back(s.Word(0x10) != 0);
    Snapshot(o, s);
    if (ours) s.SetWord(0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ts2_own_vt_dcf704)));
    for (const Call &c : calls) {
        o.words.push_back(Run(object, c));
        Snapshot(o, s);
        o.words.push_back(static_cast<uint32_t>(ReadFileBytes(path).size()));
    }
    if (endMode == 0) {
        o.words.push_back(SlotOf<Deleting>(object, kDeletingSlot)(object, 0) == object);
    } else if (ours) {
        reinterpret_cast<cDebugStream *>(object)->Destruct();
    } else {
        ctx.Original<Destruct>(kDebugDestruct)(object);
    }
    // After the destructor: the vtable (back to cRZOStream's), both strings emptied and reset.
    o.words.push_back(s.Word(0));
    for (int base : {0x18, 0x38}) {
        o.words.push_back(s.Word(base));
        o.words.push_back(s.Word(base + 0x14));
        o.words.push_back(s.Word(base + 0x18));
        o.words.push_back(s.bytes[16 + base + 4]);
    }
    // Canaries around the object.
    for (int i = 0; i < 16; ++i) o.words.push_back(s.bytes[i] == 0x5a && s.bytes[16 + cDebugStream::kSize + i] == 0x5a);
    o.file = ReadFileBytes(path);
    return o;
}

} // namespace

DIFF_TEST(RZDebugStream_Script, 0x520d70)
{
    ClassicLocaleKept keepLocale(ctx);
    PrepareOriginalStdio(ctx);
    const std::string pathA = Narrow(difftest::SandboxPath(L"debugstream_orig.txt"));
    const std::string pathB = Narrow(difftest::SandboxPath(L"debugstream_ours.txt"));
    // The newline cell holds the "\n" literal.
    ++ctx.cases;
    if (*ctx.image.At<uint32_t>(0x1014980) != static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At(0xd9e644))))
        ctx.Fail("the cell 0x1014980 does not point at the \"\\n\" literal");
    ++ctx.cases;
    if (static_cast<const void *>(&ts2_rva_1014980) != ctx.image.At(0x1014980)) ctx.Fail("the owned cell is not the image's");
    std::mt19937 rng(0x520d70);
    for (int i = 0; i < 1500; ++i) {
        std::vector<Call> calls;
        const int n = int(rng() % 25);
        for (int k = 0; k < n; ++k) calls.push_back(RandomCall(rng));
        const uint32_t endMode = rng() % 2;
        ctx.Mark("debug stream original");
        const Outcome a = RunSide(ctx, false, calls, endMode, pathA);
        ctx.Mark("debug stream ours");
        const Outcome b = RunSide(ctx, true, calls, endMode, pathB);
        ++ctx.cases;
        if (a.words != b.words) {
            size_t k = 0;
            while (k < a.words.size() && k < b.words.size() && a.words[k] == b.words[k]) ++k;
            ctx.Fail("case %d: word %zu differs (original 0x%x, ours 0x%x; %zu / %zu words)", i, k,
                     k < a.words.size() ? a.words[k] : 0, k < b.words.size() ? b.words[k] : 0, a.words.size(), b.words.size());
        }
        if (a.lines != b.lines) {
            size_t k = 0;
            while (k < a.lines.size() && k < b.lines.size() && a.lines[k] == b.lines[k]) ++k;
            ctx.Fail("case %d: the collected text differs after call %zu", i, k);
        }
        if (a.file != b.file) ctx.Fail("case %d: files differ (%zu / %zu bytes)", i, a.file.size(), b.file.size());
        if (ctx.failures.size() >= 20) return;
    }
}
