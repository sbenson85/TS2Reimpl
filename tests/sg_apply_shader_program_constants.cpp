// Differential tests of cApplyShaderProgramConstants (lane TM, scenegraph M3a; src/sg/cApplyShaderProgramConstants.h),
// in the world of tests/sg_shader_attr_fx.h, which this file extends:
//   - the device state fake's six constant locks (slots 19-24) note the file, start and count and hand out that file's
//     own register block, noted (and refilled with a pattern) after each Apply and Slot12;
//   - the two source functions (0xaba8f0 float, 0xab7cd0 integer; tests/sg_shader_constant_sources.cpp) are stand-ins
//     on both sides (ours and the image's):
//     each notes which it is, the file and register its destination is, the binding's eight words and whether it was
//     given the context and the item, then writes four words of its own there;
//   - Init's first argument is a caps fake whose slot 21 notes the question and answers a drawn limit for it;
//   - cRZStd's report slot (0x1bfb92) is a stand-in noting the file, line, group, level and the text.
// The bindings are drawn: sources across 0..0x2f (every count rule and the free ones), files 0..5, registers below 40,
// counts mostly small (never a count of 0 at register 0: the image would write a boolean through a null lock).
#include "sg_shader_attr_fx.h"

#include "code_patch.h"
#include "rz/cRZStd.h"
#include "sg/ShaderConstantSources.h"
#include "sg/cApplyShaderProgramConstants.h"

#include <cstdarg>
#include <string>

namespace {

using Binding = cApplyShaderProgramConstants::Binding;

void NoteText(uint32_t tag, const std::string &text)
{
    gLog.push_back(tag);
    gLog.push_back(static_cast<uint32_t>(text.size()));
    for (size_t i = 0; i < text.size(); i += 4) {
        uint32_t w = 0;
        std::memcpy(&w, text.data() + i, text.size() - i < 4 ? text.size() - i : 4);
        gLog.push_back(w);
    }
    gLog.push_back(0xeeeeeeee);
}

int32_t __cdecl StandReport(cRZStd *self, const char *group, int32_t level, const char *format, ...)
{
    char buf[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    Note({0x77, self->mSecond, static_cast<uint32_t>(level), self->mFlags});
    NoteText(0x78, std::string(self->File()) + "|" + group + "|" + buf);
    return 0;
}

struct Fake {
    void **vt;
};
void __fastcall FakeTrap(Fake *, void *) { Note({0xdeab}); }

// ---- the caps ----
void *gCapsVTable[24];
Fake gCaps{gCapsVTable};
uint32_t gLimits[0x20];
uint32_t __fastcall CapsLimit(Fake *, void *, uint32_t which)
{
    Note({21, which});
    return gLimits[which & 0x1f];
}

// ---- the register files ----
constexpr uint32_t kRegWords = 64 * 4;
uint32_t gRegs[6][kRegWords];

uint32_t *Lock(uint32_t file, uint32_t start, uint32_t count)
{
    Note({0x80 + file, start, count});
    return gRegs[file];
}
uint32_t *__fastcall LockVF(State *, void *, uint32_t s, uint32_t c) { return Lock(0, s, c); }
uint32_t *__fastcall LockVI(State *, void *, uint32_t s, uint32_t c) { return Lock(1, s, c); }
uint32_t *__fastcall LockVB(State *, void *, uint32_t s, uint32_t c) { return Lock(2, s, c); }
uint32_t *__fastcall LockPF(State *, void *, uint32_t s, uint32_t c) { return Lock(3, s, c); }
uint32_t *__fastcall LockPI(State *, void *, uint32_t s, uint32_t c) { return Lock(4, s, c); }
uint32_t *__fastcall LockPB(State *, void *, uint32_t s, uint32_t c) { return Lock(5, s, c); }

void NoteRegs()
{
    for (uint32_t f = 0; f < 6; ++f) {
        gLog.push_back(0x88 + f);
        gLog.insert(gLog.end(), std::begin(gRegs[f]), std::end(gRegs[f]));
        for (uint32_t i = 0; i < kRegWords; ++i) gRegs[f][i] = 0xcd000000u | (f << 16) | i;
    }
    gLog.push_back(0xeeeeeeee);
}

// The destination's file and word, or 0xffff and whether it is null.
void NoteDestination(const void *dst)
{
    const uint8_t *const p = static_cast<const uint8_t *>(dst);
    for (uint32_t f = 0; f < 6; ++f) {
        const uint8_t *const base = reinterpret_cast<const uint8_t *>(gRegs[f]);
        if (p >= base && p < base + sizeof gRegs[f]) {
            Note({f, static_cast<uint32_t>(p - base)});
            return;
        }
    }
    Note({0xffff, uint32_t(dst == nullptr)});
}

void StandFill(uint32_t tag, void *dst, const Binding *b, void *c, void *item)
{
    Note({tag, uint32_t(c == gContext), uint32_t(item == gItem)});
    NoteDestination(dst);
    std::vector<uint32_t> w(8);
    std::memcpy(w.data(), b, sizeof *b);
    gLog.insert(gLog.end(), w.begin(), w.end());
    uint8_t *const p = static_cast<uint8_t *>(dst);
    const uint8_t *const first = reinterpret_cast<const uint8_t *>(gRegs);
    if (p >= first && p + 16 <= first + sizeof gRegs)
        for (uint32_t i = 0; i < 4; ++i) {
            const uint32_t v = tag << 24 | w[1] << 8 | i;
            std::memcpy(p + 4 * i, &v, 4);
        }
}
void __cdecl StandFloat(void *dst, const Binding *b, void *c, void *item) { StandFill(0x90, dst, b, c, item); }
void __cdecl StandInt(void *dst, const Binding *b, void *c, void *item) { StandFill(0x91, dst, b, c, item); }

void ConstantsWorld(std::mt19937 &rng)
{
    for (void *&v : gCapsVTable) v = reinterpret_cast<void *>(&FakeTrap);
    gCapsVTable[21] = reinterpret_cast<void *>(&CapsLimit);
    for (uint32_t &l : gLimits) l = rng() % 4 ? rng() % 48 : rng();
    gStateVTable[19] = reinterpret_cast<void *>(&LockVF);
    gStateVTable[20] = reinterpret_cast<void *>(&LockVI);
    gStateVTable[21] = reinterpret_cast<void *>(&LockVB);
    gStateVTable[22] = reinterpret_cast<void *>(&LockPF);
    gStateVTable[23] = reinterpret_cast<void *>(&LockPI);
    gStateVTable[24] = reinterpret_cast<void *>(&LockPB);
    for (uint32_t f = 0; f < 6; ++f)
        for (uint32_t i = 0; i < kRegWords; ++i) gRegs[f][i] = 0xcd000000u | (f << 16) | i;
    // The skip byte: set now and then.
    gContext[0x298] = rng() % 6 == 0 ? static_cast<uint8_t>(1 + rng() % 255) : 0;
}

// The world for a test: the extra parts and the three stand-ins.
struct World {
    shape::CodePatch report;
    shape::StandIn fillFloat;
    shape::StandIn fillInt;
    explicit World(difftest::Context &ctx)
        : report(0x1bfb92, reinterpret_cast<const void *>(&StandReport)),
          fillFloat(0xaba8f0, &FillFloatConstants, &StandFloat), fillInt(0xab7cd0, &FillIntConstants, &StandInt)
    {
        gImageBase = ctx.image.Base();
        gWorldExtra = &ConstantsWorld;
    }
    ~World() { gWorldExtra = nullptr; }
};

Binding PickBinding(std::mt19937 &rng)
{
    Binding b;
    uint32_t w[8];
    for (uint32_t &x : w) x = rng();
    std::memcpy(&b, w, sizeof b);
    b.mSource = static_cast<uint8_t>(rng() % 4 ? rng() % 0x30 : 0x19); // 0x19 (the attribute source) has the most checks
    b.mFile = static_cast<uint8_t>(rng() % 6);
    b.mChannel = static_cast<uint8_t>(rng() % 4 ? rng() % 5 : rng());
    b.mDimension = static_cast<uint8_t>(rng() % 4 ? rng() % 5 : rng());
    b.mRegister = rng() % 40;
    switch (rng() % 8) {
    case 0: b.mCount = 0x20; break;
    case 1: b.mCount = 8; break;
    case 2: b.mCount = 1; break;
    default: b.mCount = rng() % 7; break;
    }
    if (b.mRegister == 0 && b.mCount == 0) b.mCount = 1;
    // Negative offsets fail; -1 (the boundary) is drawn often.
    b.mOffset = rng() % 3 ? static_cast<int32_t>(rng() % 0x100)
                          : (rng() % 2 ? -1 : -static_cast<int32_t>(1 + rng() % 0x7fffffff));
    b.mValues[0] = rng() % 3 ? 0.0f : PickFloat(rng);
    if (rng() % 16 == 0) std::memcpy(&b.mValues[0], "\x01\x00\xc0\x7f", 4); // a NaN
    else if (rng() % 16 == 0) b.mValues[0] = -0.0f;
    return b;
}

// The part's ranges and bindings (the vector's own pointers differ between the sides).
void NoteParts(const void *o, uint32_t tag)
{
    const uint8_t *const p = static_cast<const uint8_t *>(o);
    uint32_t w[3 + 12];
    std::memcpy(w, p + 4, sizeof w);
    std::vector<uint32_t> words{tag, w[0], w[1] & 0xff, w[2]};
    words.insert(words.end(), w + 3, w + 15);
    const Binding *const *v = reinterpret_cast<const Binding *const *>(p + 0x40);
    const size_t n = static_cast<size_t>(v[1] - v[0]);
    const size_t cap = static_cast<size_t>(v[2] - v[0]);
    words.push_back(static_cast<uint32_t>(n));
    words.push_back(uint32_t(cap >= n));
    for (size_t i = 0; i < n; ++i) {
        uint32_t b[8];
        std::memcpy(b, v[0] + i, sizeof b);
        words.insert(words.end(), b, b + 8);
    }
    gLog.insert(gLog.end(), words.begin(), words.end());
    gLog.push_back(0xeeeeeeee);
}

void Add(void *p, const Binding &b)
{
    Slot<void>(p, 14, static_cast<const void *>(&b));
    Note({0x6a});
}

uint32_t gKey;

// Init with the caps fake, and limit-choice and report words whose low bytes are drawn.
void NoteInit(void *p, std::mt19937 &rng)
{
    gKey = rng();
    const uint32_t which = (rng() & 0xffffff00u) | (rng() % 2 ? 1u + rng() % 255 : 0u);
    const uint32_t report = (rng() & 0xffffff00u) | (rng() % 3 ? 1u + rng() % 255 : 0u);
    const bool r = Slot<bool>(p, 8, static_cast<void *>(&gCaps), static_cast<void *>(&gKey), which, report);
    Note({0x66, uint32_t(r), Slot<uint32_t>(p, 6)});
}

void NoteApply(void *p)
{
    Slot<void>(p, 11, static_cast<void *>(gContext), static_cast<void *>(gItem));
    Note({0x61});
    NoteRegs();
}

void NoteSlot12(void *p)
{
    gKey = 0;
    Slot<void>(p, 12, static_cast<void *>(&gState), static_cast<const void *>(&gKey));
    Note({0x64});
    NoteRegs();
}

const Leaf &ConstantsLeaf()
{
    static const Leaf leaf = {
        "cApplyShaderProgramConstants", 0xe76afc, 0xac3200, 0xabd8c0, 0xabe250, 15,
        {{0, 0xac5ba0}, {2, 0xac6040}, {3, 0x140fe0}, {7, 0xac4e80}, {8, 0xac4180}, {9, 0x1b9a50}, {10, 0xac2b10},
         {11, 0xabf500}, {12, 0xac1930}, {13, 0x300a0}, {14, 0xabeae0}, {15, 0xabe970}, {16, 0xac3560},
         {17, 0xac5380}},
        reinterpret_cast<Factory>(&cApplyShaderProgramConstants::Create), sizeof(cApplyShaderProgramConstants),
        nullptr, 0, [](void *p) { new (p) cApplyShaderProgramConstants; },
        [](void *p) { static_cast<cApplyShaderProgramConstants *>(p)->~cApplyShaderProgramConstants(); }};
    return leaf;
}

} // namespace

// The factory 0xac3200 and every slot on two parts: AddConstant 0xabeae0 (the vector grown past its capacity, each
// file's range), Init 0xac4180, Slot10 0xac2b10, Apply 0xabf500, Slot12 0xac1930, EndState and Reset (shared no-ops),
// IsEquivalent with HashParams 0xac3560 and EqualParams 0xac5380 (the second part given the first's bindings now and
// then, so that equal ones show), and the last Release (the stub 0xac6040) through the deleting destructor 0xabe970
// (the destructor 0xabe250).
DIFF_TEST(sg_apply_shader_program_constants, 0xac3200)
{
    World world(ctx);
    const Leaf &l = ConstantsLeaf();
    CheckSides(ctx, l.name, 1500, [&](bool original, std::mt19937 &rng) {
        const Factory make = original ? ctx.Original<Factory>(l.factory) : l.ours;
        void *const a = make();
        void *const b = make();
        if (original && !CheckVTable(ctx, l, a)) return;
        Slot<uint32_t>(a, 1);
        Slot<uint32_t>(b, 1);
        NoteParts(a, 0x41);
        std::vector<Binding> added;
        const bool same = rng() % 3 == 0;
        for (int k = 0; k < 14; ++k) {
            switch (rng() % 10) {
            case 0:
            case 1:
            case 2: {
                const Binding x = PickBinding(rng);
                Add(a, x);
                added.push_back(x);
                if (!same && rng() % 2) Add(b, PickBinding(rng));
                break;
            }
            case 3: NoteInit(a, rng); break;
            case 4:
            case 5: NoteApply(a); break;
            case 6: NoteSlot12(a); break;
            case 7:
                Note({0x60, Slot<uint32_t>(a, 10), Slot<uint32_t>(b, 10)});
                Slot<void>(a, 13, static_cast<void *>(gContext));
                Slot<void>(a, 3);
                Slot<void>(a, 9, static_cast<void *>(&gState), static_cast<void *>(gContext));
                Note({0x63});
                break;
            default:
                if (same) {
                    for (const Binding &x : added) Add(b, x);
                    added.clear();
                }
                NoteInit(b, rng);
                Note({0x67, uint32_t(Slot<bool>(a, 7, b)), Slot<uint32_t>(a, 16), Slot<uint32_t>(b, 16),
                      uint32_t(Slot<bool>(a, 17, b))});
                break;
            }
        }
        NoteParts(a, 0x42);
        NoteParts(b, 0x42);
        for (void *const p : {a, b}) {
            difftest::FreeRecorder freed;
            const uint32_t r = Slot<uint32_t>(p, 2);
            freed.Stop();
            Note({0x43, r, uint32_t(freed.Count()), uint32_t(freed.Freed(p))});
        }
    });
}

// Apply 0xabf500 and Slot12 0xac1930 on a part with drawn bindings: each lock, each source call and every register.
DIFF_TEST(sg_apply_shader_program_constants_apply, 0xabf500)
{
    World world(ctx);
    const Leaf &l = ConstantsLeaf();
    CheckSides(ctx, "apply", 3000, [&](bool original, std::mt19937 &rng) {
        const Factory make = original ? ctx.Original<Factory>(l.factory) : l.ours;
        void *const p = make();
        Slot<uint32_t>(p, 1);
        const int n = static_cast<int>(rng() % 12);
        for (int i = 0; i < n; ++i) Add(p, PickBinding(rng));
        NoteApply(p);
        if (rng() % 2) NoteSlot12(p);
        Note({0x60, Slot<uint32_t>(p, 10)});
        Slot<uint32_t>(p, 2);
    });
}

// Init 0xac4180 on parts with a few drawn bindings: each limit and each source's count rule, and their lines.
DIFF_TEST(sg_apply_shader_program_constants_init, 0xac4180)
{
    World world(ctx);
    const Leaf &l = ConstantsLeaf();
    CheckSides(ctx, "init", 4000, [&](bool original, std::mt19937 &rng) {
        const Factory make = original ? ctx.Original<Factory>(l.factory) : l.ours;
        void *const p = make();
        Slot<uint32_t>(p, 1);
        const int n = static_cast<int>(rng() % 4);
        for (int i = 0; i < n; ++i) {
            Binding b = PickBinding(rng);
            if (rng() % 2) b.mCount = rng() % 40; // past the register file limits now and then
            if (b.mRegister == 0 && b.mCount == 0) b.mCount = 1;
            Add(p, b);
        }
        // Raise the limits now and then so that the count rules are reached.
        if (rng() % 2)
            for (uint32_t &x : gLimits) x = 200 + rng() % 4;
        NoteInit(p, rng);
        NoteParts(p, 0x41);
        Slot<uint32_t>(p, 2);
    });
}

// The constructor 0xabd8c0 and destructor 0xabe250 alone on a block of pattern words (the vector's pointers null:
// the destructor's free is the library's, given nothing).
DIFF_TEST(sg_apply_shader_program_constants_construct_destruct, 0xabd8c0)
{
    World world(ctx);
    using Ctor = void *(__thiscall *)(void *);
    using Dtor = void(__thiscall *)(void *);
    const Leaf &l = ConstantsLeaf();
    CheckSides(ctx, l.name, 32, [&](bool original, std::mt19937 &rng) {
        alignas(8) uint32_t block[0x4c / 4];
        for (uint32_t &w : block) w = rng();
        if (original) {
            void *const r = ctx.Original<Ctor>(l.ctor)(block);
            Note({0x50, uint32_t(r == block), uint32_t(block[0] == uint32_t(ctx.image.Base() + l.vtable))});
        } else {
            l.placement(block);
            Note({0x50, 1, 1});
        }
        gLog.insert(gLog.end(), block + 1, block + 0x4c / 4);
        for (size_t i = 1; i < 0x10 / 4; ++i) block[i] = rng();
        if (original)
            ctx.Original<Dtor>(l.dtor)(block);
        else
            l.destroy(block);
        gLog.insert(gLog.end(), block + 1, block + 0x10 / 4);
    });
}
