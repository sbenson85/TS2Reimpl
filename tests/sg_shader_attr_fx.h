#pragma once
// The world and the behaviour script of the shader attribute differential tests (lanes TG, TJ, TK; scenegraph M3b),
// shared by tests/sg_shader_attributes.cpp and tests/sg_ff_texture_matrix.cpp. Each test file gets its own copy (an
// anonymous namespace).
//
// The world: a render context (random words, a clock of chosen and random floats at +8, a projection of them at
// +0x144, a device state fake at +0x2ac noting ApplyStateCommand's state (its vtable as an RVA, id, size and words),
// SetProjection's 16 words and SetWorld's 13 and which transform it was given; every other slot a trap), a drawn item
// whose +0x20 is null or a table of 64 chosen and random float words and whose +0x28 is a transform of 13 random words,
// and a material manager fake in GetCachedQueriedObject's cache (0x10f8fcc) noting Unregister (slot 27) for registered
// parts the last Release drops. The image's default colour word 0x10f67a0 (cAnimColorScalarAlpha's starting colour)
// is a random word per seed. A test file may set gWorldExtra to draw more of the world after that.
#include "difftest.h"
#include "free_recorder.h"

#include "sg/SGShaderAttributes.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <vector>

using namespace nGZSceneGraph;

namespace {

std::vector<uint32_t> gLog;

void Note(std::initializer_list<uint32_t> words)
{
    gLog.insert(gLog.end(), words.begin(), words.end());
    gLog.push_back(0xeeeeeeee);
}

template <class R, class... A> R Slot(void *o, unsigned slot, A... a)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void ***>(o))[slot])(o, a...);
}

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// ---- the world ----
alignas(16) uint8_t gContext[0x2c0];
uint32_t gTransforms[2][13];
uint32_t gValues[72]; // indices reach 252, and a colour reads three words there
float gEventScale[4];
uintptr_t gImageBase;
alignas(16) uint8_t gItem[0x50];

// The item's colour modulators (cAttributeColorScalar): 32 positions, a quarter of them empty; slot 42 notes what it is
// given (which modulator, the colour so far, the index) and gives four chosen scales.
struct Modulator {
    void **vt;
};
void *gModulatorVTable[44];
Modulator gModulators[32];
Modulator *gModulatorHolders[32];
Modulator **gModulatorElements[32];
float gModulatorResult[4];

uint32_t TransformId(const void *p)
{
    if (p == gTransforms[0]) return 1;
    if (p == gTransforms[1]) return 2;
    return 0xbad0bad0;
}

struct State {
    void **vt;
};
void *gStateVTable[40];
State gState{gStateVTable};
void __fastcall StateSetProjection(State *, void *, const uint32_t *m)
{
    std::vector<uint32_t> w{25, m == reinterpret_cast<const uint32_t *>(gContext + 0x144)};
    w.insert(w.end(), m, m + 16);
    gLog.insert(gLog.end(), w.begin(), w.end());
    gLog.push_back(0xeeeeeeee);
}
void __fastcall StateSetWorld(State *, void *, const uint32_t *t)
{
    std::vector<uint32_t> w{26, TransformId(t)};
    w.insert(w.end(), t, t + 13);
    gLog.insert(gLog.end(), w.begin(), w.end());
    gLog.push_back(0xeeeeeeee);
}
void __fastcall StateApplyCommand(State *, void *, uint32_t command)
{
    const uint8_t *const p = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(command));
    uint32_t vt;
    uint16_t size;
    std::memcpy(&vt, p, 4);
    std::memcpy(&size, p + 6, 2);
    std::vector<uint32_t> w{12, vt - static_cast<uint32_t>(gImageBase), p[4], size};
    for (uint32_t off = 8; off + 4 <= size && off < 0x100; off += 4) {
        uint32_t v;
        std::memcpy(&v, p + off, 4);
        // cFFTextureTransformTypeState's +0xe..+0xf are padding the image leaves unwritten on its stack.
        if (vt - static_cast<uint32_t>(gImageBase) == 0xda0278 && off == 0xc) v &= 0xffff;
        w.push_back(v);
    }
    gLog.insert(gLog.end(), w.begin(), w.end());
    gLog.push_back(0xeeeeeeee);
}
void __fastcall StateTrap(State *, void *) { Note({0xdead}); }
// The shader constant registers slots 19 (vertex) and 22 (pixel) hand out: noted (and refilled) after each Apply.
uint32_t gConstants[16];
uint32_t *__fastcall StateLockVertexFloats(State *, void *, uint32_t start, uint32_t count)
{
    Note({19, start, count});
    return gConstants;
}
uint32_t *__fastcall StateLockPixelFloats(State *, void *, uint32_t start, uint32_t count)
{
    Note({22, start, count});
    return gConstants;
}
void NoteConstants()
{
    gLog.push_back(0x71);
    gLog.insert(gLog.end(), std::begin(gConstants), std::end(gConstants));
    gLog.push_back(0xeeeeeeee);
    for (uint32_t &c : gConstants) c = 0xcdcdcdcd;
}

// The item's colour event source: slot 10 notes the event it is given and fills its four scales.
struct Source {
    void **vt;
};
void *gSourceVTable[16];
Source gSource{gSourceVTable};
Source *gSourceHolder = &gSource;
void __fastcall SourceGetEvent(Source *, void *, uint32_t *event, uint32_t type, int32_t index)
{
    Note({0x70, event[0] - static_cast<uint32_t>(gImageBase), event[1], event[2], event[3], event[4], type,
          static_cast<uint32_t>(index)});
    std::memcpy(event + 1, gEventScale, 16);
}
void __fastcall SourceTrap(Source *, void *) { Note({0xdeaf}); }

const float *__fastcall ModulatorGet(Modulator *self, void *, const float *color, int32_t index)
{
    Note({0x73, uint32_t(self - gModulators), Bits(color[0]), Bits(color[1]), Bits(color[2]), Bits(color[3]),
          static_cast<uint32_t>(index)});
    return gModulatorResult;
}
void __fastcall ModulatorTrap(Modulator *, void *) { Note({0xdeaa}); }

struct Manager {
    void **vt;
};
void *gManagerVTable[40];
Manager gManager{gManagerVTable};
void *gParts[3];
uint32_t PartId(const void *p)
{
    for (int i = 0; i < 3; ++i)
        if (p == gParts[i]) return 100 + i;
    return 0xbad1bad1;
}
void __fastcall ManagerUnregister(Manager *, void *, void *part) { Note({27, PartId(part)}); }
void __fastcall ManagerTrap(Manager *, void *) { Note({0xdeae}); }

float PickFloat(std::mt19937 &rng)
{
    static const float picks[] = {0.0f, 1.0f, -1.0f, 0.5f, -0.25f, 1000.0f, 1e-3f, -3.5e4f, 7.0f};
    if (rng() % 3) return picks[rng() % 9];
    return static_cast<float>(static_cast<int32_t>(rng() % 200001) - 100000) / static_cast<float>(1 + rng() % 997);
}

// A parameter's value index: none (negative) or a byte offset into the item's values.
int32_t PickIndex(std::mt19937 &rng)
{
    switch (rng() % 4) {
    case 0: return -1;
    case 1: return -static_cast<int32_t>(1 + rng() % 0x7fffffff);
    default: return static_cast<int32_t>(4 * (rng() % 64));
    }
}

// More of the world, drawn last (a test file's own parts).
void (*gWorldExtra)(std::mt19937 &) = nullptr;

void BuildWorld(std::mt19937 &rng)
{
    for (uint8_t &b : gContext) b = static_cast<uint8_t>(rng());
    const uint32_t time = Bits(rng() % 4 ? PickFloat(rng) : static_cast<float>(rng() % 100000) / 64.0f);
    std::memcpy(gContext + 8, &time, 4);
    for (int i = 0; i < 16; ++i) {
        const uint32_t w = Bits(PickFloat(rng));
        std::memcpy(gContext + 0x144 + 4 * i, &w, 4);
    }
    State *const s = &gState;
    std::memcpy(gContext + 0x2ac, &s, 4);
    for (void *&v : gStateVTable) v = reinterpret_cast<void *>(&StateTrap);
    gStateVTable[12] = reinterpret_cast<void *>(&StateApplyCommand);
    gStateVTable[19] = reinterpret_cast<void *>(&StateLockVertexFloats);
    gStateVTable[22] = reinterpret_cast<void *>(&StateLockPixelFloats);
    for (uint32_t &c : gConstants) c = 0xcdcdcdcd;
    gStateVTable[25] = reinterpret_cast<void *>(&StateSetProjection);
    gStateVTable[26] = reinterpret_cast<void *>(&StateSetWorld);
    for (auto &t : gTransforms)
        for (uint32_t &w : t) w = rng() % 2 ? Bits(PickFloat(rng)) : rng();
    for (uint8_t &b : gItem) b = static_cast<uint8_t>(rng());
    const uint32_t *const t = gTransforms[rng() % 2];
    std::memcpy(gItem + 0x28, &t, 4);
    for (uint32_t &w : gValues) w = Bits(PickFloat(rng));
    const uint32_t *const values = rng() % 4 ? gValues : nullptr;
    std::memcpy(gItem + 0x20, &values, 4);
    for (float &f : gEventScale) f = PickFloat(rng);
    for (void *&v : gSourceVTable) v = reinterpret_cast<void *>(&SourceTrap);
    gSourceVTable[10] = reinterpret_cast<void *>(&SourceGetEvent);
    Source **const holder = &gSourceHolder;
    std::memcpy(gItem + 0x2c, &holder, 4);
    *reinterpret_cast<uint32_t *>(gImageBase + 0x10f67a0) = rng();
    for (void *&v : gManagerVTable) v = reinterpret_cast<void *>(&ManagerTrap);
    gManagerVTable[27] = reinterpret_cast<void *>(&ManagerUnregister);
    for (void *&v : gModulatorVTable) v = reinterpret_cast<void *>(&ModulatorTrap);
    gModulatorVTable[42] = reinterpret_cast<void *>(&ModulatorGet);
    for (int i = 0; i < 32; ++i) {
        gModulators[i].vt = gModulatorVTable;
        gModulatorHolders[i] = &gModulators[i];
        gModulatorElements[i] = rng() % 4 ? &gModulatorHolders[i] : nullptr;
    }
    for (float &f : gModulatorResult) f = PickFloat(rng);
    if (rng() % 2) {
        // A modulator range that is usually non-empty: the first at nibble 3, one past the last at nibble 4.
        const uint32_t first = rng() % 8;
        const uint32_t range = (first + rng() % 8) << 16 | first << 12 | (rng() & 0xfff);
        std::memcpy(gItem + 0x30, &range, 4);
    }
    Modulator ***const elements = gModulatorElements;
    std::memcpy(gItem + 0x3c, &elements, 4);
    if (gWorldExtra) gWorldExtra(rng);
}

// ---- the classes ----
using Factory = void *(__cdecl *)();

struct Leaf {
    const char *name;
    uint32_t vtable, factory, ctor, dtor;
    unsigned d;                          // the deleting destructor's slot
    std::vector<std::pair<unsigned, uint32_t>> bodies; // slot -> the image's body (own and folded)
    Factory ours;
    size_t size;
    void (*set)(void *, std::mt19937 &); // calls one of the class's setters (slots 14..) with drawn values
    uint32_t paramMask;                  // the bytes of +0x10 the class owns
    void (*placement)(void *);           // our constructor on a block
    void (*destroy)(void *);             // our destructor on a block
    uint32_t tailMask = 0xffffffffu;     // the bytes of the last word the class writes (the rest padding)
};
// The fields after the vtable: the count, the registered byte, the hash, then the class's own words (the first under
// the class's mask).
void NoteFields(const void *o, const Leaf &l, uint32_t tag)
{
    const uint8_t *const p = static_cast<const uint8_t *>(o);
    uint32_t w[3];
    std::memcpy(w, p + 4, 12);
    std::vector<uint32_t> words{tag, w[0], w[1] & 0xff, w[2]};
    for (size_t off = 0x10; off + 4 <= l.size; off += 4) {
        uint32_t v;
        std::memcpy(&v, p + off, 4);
        words.push_back(off == 0x10 ? v & l.paramMask : (off + 4 == l.size ? v & l.tailMask : v));
    }
    gLog.insert(gLog.end(), words.begin(), words.end());
    gLog.push_back(0xeeeeeeee);
}

// Runs a side's script on its own objects; the logs of the two sides are compared.
template <class Run> void CheckSides(difftest::Context &ctx, const char *name, unsigned seeds, Run &&run)
{
    gLog.reserve(1 << 14);
    for (unsigned seed = 0; seed < seeds; ++seed) {
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed * 7919u + 3u);
            BuildWorld(rng);
            gLog.clear();
            char mark[96];
            std::snprintf(mark, sizeof(mark), "%s seed %u %s", name, seed, side == 0 ? "original" : "ours");
            ctx.Mark(mark);
            run(side == 0, rng);
            logs[side] = gLog;
        }
        ++ctx.cases;
        if (logs[0] != logs[1]) {
            size_t i = 0;
            while (i < logs[0].size() && i < logs[1].size() && logs[0][i] == logs[1][i]) ++i;
            ctx.Fail("%s seed %u: logs differ at word %zu (original %zu words, ours %zu): %08x vs %08x", name, seed, i,
                     logs[0].size(), logs[1].size(), i < logs[0].size() ? logs[0][i] : 0,
                     i < logs[1].size() ? logs[1][i] : 0);
            return;
        }
    }
}

// The image's vtable holds the bodies the entries name.
bool CheckVTable(difftest::Context &ctx, const Leaf &l, void *gameObject)
{
    void **const vt = *static_cast<void ***>(gameObject);
    if (vt != ctx.image.At<void *>(l.vtable)) {
        ctx.Fail("%s: the factory's object has vtable %p, not %#x", l.name, static_cast<void *>(vt), l.vtable);
        return false;
    }
    for (const auto &[slot, rva] : l.bodies)
        if (vt[slot] != ctx.image.At<void>(rva)) {
            ctx.Fail("%s: slot %u is not %#x", l.name, slot, rva);
            return false;
        }
    return true;
}

// The behaviour script: three parts from the side's factories (two of the class, one of `other`), each slot exercised.
void Behaviour(difftest::Context &ctx, const Leaf &l, const Leaf &other)
{
    gImageBase = ctx.image.Base();
    void **const cache = ctx.image.At<void *>(0x10f8fcc);
    void *const saved = *cache;
    *cache = &gManager;
    CheckSides(ctx, l.name, 120, [&](bool original, std::mt19937 &rng) {
        const Factory make = original ? ctx.Original<Factory>(l.factory) : l.ours;
        const Factory makeOther = original ? ctx.Original<Factory>(other.factory) : other.ours;
        gParts[0] = make();
        gParts[1] = make();
        gParts[2] = makeOther();
        if (original && !CheckVTable(ctx, l, gParts[0])) return;
        for (int i = 0; i < 3; ++i) {
            Note({0x40, uint32_t(i), Slot<uint32_t>(gParts[i], 1)}); // AddRef: the count 1
            for (int n = 0; n < 2; ++n) (i == 2 ? other : l).set(gParts[i], rng);
            NoteFields(gParts[i], i == 2 ? other : l, 0x41);
        }
        void *const a = gParts[0];
        for (int k = 0; k < 24; ++k) {
            void *const b = gParts[rng() % 3];
            switch (rng() % 12) {
            case 0: Note({0x60, Slot<uint32_t>(a, 10)}); break;
            case 1: l.set(a, rng); break;
            case 2:
                Slot<void>(a, 11, static_cast<void *>(gContext), static_cast<void *>(gItem));
                NoteConstants();
                Note({0x61});
                break;
            case 3:
                Slot<void>(a, 12, static_cast<void *>(&gState), static_cast<void *>(gItem));
                NoteConstants();
                Slot<void>(a, 9, static_cast<void *>(&gState), static_cast<void *>(gContext));
                Slot<void>(a, 3);
                Note({0x62});
                break;
            case 4:
                Slot<void>(a, 13, static_cast<void *>(gContext));
                Note({0x63});
                break;
            case 5: Note({0x64, Slot<uint32_t>(a, l.d + 1)}); break;
            case 6:
                if (b != gParts[2]) Note({0x65, PartId(b), uint32_t(Slot<bool>(a, l.d + 2, b))});
                break;
            case 7: {
                const bool r = Slot<bool>(a, 8, rng(), rng(), rng(), rng());
                Note({0x66, uint32_t(r), Slot<uint32_t>(a, 6)});
                break;
            }
            case 8: Note({0x67, PartId(b), uint32_t(Slot<bool>(a, 7, b))}); break;
            case 9: {
                static const uint32_t iids[] = {1, 0x9c544810, 0x2d2df503, 0xad30a7b6, 0xad30a7b7, 0xca0ca1d4,
                                                0xca0ca1d5, 0x12345678, 0xfcb89ab2, 0xfcb89ab3, 0x8b94423f,
                                                0x8b944240, 0x69ff7545, 0x69ff7546, 0xfca91c0c,
                                                0xfca91c0d, 0x8aa6d940, 0x8aa6d941};
                void *out = nullptr;
                const uint32_t iid = iids[rng() % 18];
                const bool r = Slot<bool>(b, 0, iid, &out);
                Note({0x68, PartId(b), iid, uint32_t(r), PartId(out)});
                break;
            }
            case 10:
                Slot<void>(b, 4, static_cast<uint32_t>(rng() % 3 ? 0 : 1));
                Note({0x69, PartId(b), uint32_t(Slot<bool>(b, 5))});
                break;
            default:
                Note({0x6a, PartId(b), Slot<uint32_t>(b, 1)});
                Note({0x6b, Slot<uint32_t>(b, 2)});
                break;
            }
            // Init now and then on the third part too (its own class's hash).
            if (k % 8 == 7) Note({0x6c, uint32_t(Slot<bool>(gParts[2], 8, 0, 0, 0, 0)), Slot<uint32_t>(gParts[2], 6)});
        }
        for (int i = 0; i < 3; ++i) NoteFields(gParts[i], i == 2 ? other : l, 0x42);
        // Twins: two fresh parts given the same setter calls, then one more call on the second, so that EqualParams and
        // HashParams see pairs one parameter apart (or none).
        for (int t = 0; t < 6; ++t) {
            void *const x = make();
            void *const y = make();
            Slot<uint32_t>(x, 1);
            Slot<uint32_t>(y, 1);
            const unsigned calls = 1 + rng() % 3; // at least one: cFFDepthOffset leaves its offset unset
            std::mt19937 rx = rng;
            for (unsigned n = 0; n < calls; ++n) l.set(x, rx);
            for (unsigned n = 0; n < calls; ++n) l.set(y, rng);
            if (t % 3) l.set(y, rng);
            const uint32_t hx = Slot<uint32_t>(x, l.d + 1), hy = Slot<uint32_t>(y, l.d + 1);
            Note({0x44, uint32_t(Slot<bool>(x, l.d + 2, y)), uint32_t(Slot<bool>(y, l.d + 2, x)), hx, hy});
            NoteFields(y, l, 0x45);
            for (void *const p : {x, y}) {
                uint32_t r = 1;
                for (int n = 0; n < 8 && r != 0; ++n) r = Slot<uint32_t>(p, 2);
            }
        }
        // The last references: unregistered if registered, then freed by the deleting destructor.
        for (int i = 0; i < 3; ++i) {
            difftest::FreeRecorder freed;
            uint32_t r = 1;
            for (int n = 0; n < 8 && r != 0; ++n) r = Slot<uint32_t>(gParts[i], 2);
            freed.Stop();
            Note({0x43, uint32_t(i), r, uint32_t(freed.Count()), uint32_t(freed.Freed(gParts[i]))});
        }
    });
    *cache = saved;
}

} // namespace
