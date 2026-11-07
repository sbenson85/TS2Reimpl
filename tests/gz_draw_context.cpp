// Differential tests for cGZDrawContext as a whole class (src/gz/cGZDrawContext.h; lane NM, rendering R5).
//
// The primitive builders (lane NW, R5b step 2b) are reached through Dispatch on both sides, every kind and route: 0x8bc290
// 0x8bc5f0 0x8bc8f0 0x8bccd0 0x8bd430 0x8bd660 0x8bdc50 0x8be3d0 0x8be790 0x8bea10 0x8bec30 0x8beff0 0x8bf130 0x8bf250
// 0x8bf480 (ours in cGZDrawContextBuilders.cpp against the game's).
//
// Each case builds one context per side: the original by the COM director's creator 0x8bf7d0 (the game's vtable), ours by
// cGZDrawContext::Create (our vtable); every slot is called through the object's own vtable word, so the original side
// runs the game's slot functions and ours our members. The fields the constructor leaves alone (origin, clips, flags,
// bounds, texture area, the state's pad byte) are then written alike on both sides. The world is drawfx fakes:
// - the framework (game global 0x10c9378) answers GetSystemService for the draw system (0xeb2ea3cb / 0x4b2ea3b3);
// - the draw system's CreateEngine hands out the engine fake (counted);
// - the engine (cIGZDrawContextEngine, 35 slots) logs every call: rectangles, colours and the primitives' points by
//   content, pointers into the context as offsets (drawfx::AddRange); GetBounds and SetTexture write the case's
//   rectangles; ChooseRoute answers the case's route per kind (0-3), so Dispatch's every branch and the primitive builders
//   behind it (game code on both sides) run;
// - the target buffers answer GetBoundsPtr (slot 12) with the case's rectangle.
// A case is SetTarget, some fields re-seeded (state, sent state, flags, batch depth, clips), then 40 random slot calls with
// random arguments, a SetTarget to another buffer or none, and the last Release (the destructor). After every call the
// log gets the result, the out-parameters and the whole object but its vtable word: every field, and each vector's size,
// capacity and contents (their buffers are each side's own heap blocks). At the end: the fakes' reference counts and the
// side's heap balance (difftest::AllocationCounter).
#include "difftest.h"
#include "drawfx.h"
#include "free_recorder.h"

#include "gz/cGZDrawContext.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <random>
#include <vector>

namespace {

using drawfx::Ret;
constexpr drawfx::Slot S0{0, Ret::Int, true, "", ""};
constexpr drawfx::Slot S1{1, Ret::Int, true, "", ""};
constexpr drawfx::Slot S2{2, Ret::Int, true, "", ""};
constexpr drawfx::Slot S3{3, Ret::Int, true, "", ""};
constexpr drawfx::Slot U{0, Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRef{0, Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kRelease{0, Ret::Int, true, "Release", ""};

inline constexpr drawfx::Slot kEngineSlots[] = {
    kQI, kAddRef, kRelease,
    {1, Ret::Int, true, "SetTarget", "i"},
    {0, Ret::Int, true, "Detach", ""},
    {1, Ret::Int, true, "GetBounds", "i"},
    {1, Ret::Int, true, "SetClip", "r"},
    {3, Ret::Int, true, "SetColors", "uuW4"},
    {1, Ret::Int, true, "vf8", "u"},
    {1, Ret::Int, true, "vf9", "b"},
    {1, Ret::Int, true, "vf10", "b"},
    {1, Ret::Int, true, "vf11", "b"},
    {1, Ret::Int, true, "SetLineWidth", "f"},
    {3, Ret::Int, true, "SetGradient", "F12ff"},
    {6, Ret::Int, true, "SetTexture", "iffffi"},
    {0, Ret::Int, true, "ClearTextureClip", ""},
    {1, Ret::Int, true, "SetTextureClip", "r"},
    {0, Ret::Int, true, "vf17", ""},
    {0, Ret::Int, true, "vf18", ""},
    {0, Ret::Int, true, "vf19", ""},
    {2, Ret::Int, true, "vf20", "uu"},
    {0, Ret::Int, true, "vf21", ""},
    {3, Ret::Int, true, "ChooseRoute", "u@2x2u"},
    {2, Ret::Int, true, "DrawSpans", "@1x2u"},
    S1, S0, S0, S0, S1, S1, S1, S2, S3, S2, S2,
};
inline constexpr drawfx::Iface kEngine{"cIGZDrawContextEngine", 35, kEngineSlots};

inline constexpr drawfx::Slot kBufferSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U,
    {0, Ret::Int, true, "GetBoundsPtr", ""},
};
inline constexpr drawfx::Iface kBuffer{"cIGZBuffer", 13, kBufferSlots};

inline constexpr drawfx::Slot kSystemSlots[] = {
    kQI, kAddRef, kRelease, {1, Ret::Int, true, "vf3", "i"}, {2, Ret::Int, true, "CreateEngine", "ii"},
};
inline constexpr drawfx::Iface kSystem{"cIGZDrawSystem", 5, kSystemSlots};

inline constexpr drawfx::Slot kFrameworkSlots[] = {
    kQI, kAddRef, kRelease, U, U, {3, Ret::Int, true, "GetSystemService", "uui"},
};
inline constexpr drawfx::Iface kFramework{"cIGZFrameWork", 6, kFrameworkSlots};

inline constexpr drawfx::Slot kUnknownSlots[] = {kQI, kAddRef, kRelease};
inline constexpr drawfx::Iface kUnknown{"cIGZUnknown", 3, kUnknownSlots};

constexpr uint32_t kFrameworkCell = 0x10c9378;
constexpr uint32_t kCreator = 0x8bf7d0, kConstructor = 0x8baff0, kGameVTable = 0xe4ed70;
constexpr uint32_t kObjectTag = 0x0b000000;

// The case's answers (the same on both sides).
struct Case {
    cGZRect targetArea[2];
    cGZRect bounds;
    cGZRect textureArea;
    uint32_t textured; // SetTexture's answer (a byte; 0x80 too)
    uint32_t routes;   // ChooseRoute: 2 bits per kind
    uint32_t vf20;
};
Case gCase;

drawfx::Fake gEngine, gSystem, gFramework, gTexture;
drawfx::Fake gTargets[2];

uint32_t EngineGetBounds(drawfx::Fake &, const uint32_t *args)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(args[0])), &gCase.bounds, sizeof(cGZRect));
    return 0;
}
uint32_t EngineSetTexture(drawfx::Fake &, const uint32_t *args)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(args[5])), &gCase.textureArea, sizeof(cGZRect));
    return gCase.textured;
}
uint32_t EngineChooseRoute(drawfx::Fake &, const uint32_t *args) { return (gCase.routes >> (2 * (args[0] & 15))) & 3; }
uint32_t SystemCreateEngine(drawfx::Fake &, const uint32_t *args)
{
    *reinterpret_cast<void **>(uintptr_t(args[1])) = &gEngine;
    ++gEngine.refs;
    return 1;
}
uint32_t FrameworkGetService(drawfx::Fake &, const uint32_t *args)
{
    if (args[0] != cIGZDrawSystem::kServiceId || args[1] != cIGZDrawSystem::kIid) return 0;
    *reinterpret_cast<void **>(uintptr_t(args[2])) = &gSystem;
    ++gSystem.refs;
    return 1;
}

void BuildWorld()
{
    gEngine.Reset<kEngine>(301);
    gEngine.On(5, &EngineGetBounds);
    gEngine.On(14, &EngineSetTexture);
    gEngine.On(22, &EngineChooseRoute);
    gEngine.Returns(20, gCase.vf20);
    gSystem.Reset<kSystem>(302);
    gSystem.On(4, &SystemCreateEngine);
    gFramework.Reset<kFramework>(303);
    gFramework.On(5, &FrameworkGetService);
    gTexture.Reset<kUnknown>(304);
    for (unsigned i = 0; i < 2; ++i) {
        gTargets[i].Reset<kBuffer>(310 + i);
        gTargets[i].Returns(12, &gCase.targetArea[i]);
    }
}

// ---- calls through the object's own vtable ----
template <class R, class... A> R CallSlot(void *object, unsigned slot, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void ***>(object))[slot])(object, args...);
}

template <class T> T At(void *object, uint32_t offset)
{
    T v;
    std::memcpy(&v, static_cast<uint8_t *>(object) + offset, sizeof(T));
    return v;
}
template <class T> void Put(void *object, uint32_t offset, T v)
{
    std::memcpy(static_cast<uint8_t *>(object) + offset, &v, sizeof(T));
}

// A std::vector's size, capacity and contents at `offset`, all in words.
void NoteVector(void *object, uint32_t offset, uint32_t elementWords, const char *label)
{
    const auto first = At<const uint32_t *>(object, offset);
    const auto last = At<const uint32_t *>(object, offset + 4);
    const auto end = At<const uint32_t *>(object, offset + 8);
    difftest::NotCounted bookkeeping; // the log is the test's (difftest::AllocationCounter)
    drawfx::Call c;
    c.note = label;
    c.words.push_back(uint32_t(last - first) / elementWords);
    c.words.push_back(uint32_t(end - first) / elementWords);
    for (const uint32_t *p = first; p != last; ++p) c.words.push_back(*p);
    drawfx::gLog.push_back(std::move(c));
}

// Every byte of the object but its vtable word and the vectors' pointers.
void NoteObject(void *object)
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "object";
    // By kind (lane RK): the target (+0x14) and the engine (+0x18) are pointers; the rest is data, as it is.
    for (uint32_t off = 4; off < 0x90; off += 4)
        c.words.push_back(off == 0x14 || off == 0x18 ? drawfx::Ptr(At<uint32_t>(object, off)) : At<uint32_t>(object, off));
    c.words.push_back(At<uint32_t>(object, 0xd8));
    drawfx::gLog.push_back(std::move(c));
    NoteVector(object, 0x90, 6, "transforms");
    NoteVector(object, 0x9c, 4, "states");
    NoteVector(object, 0xa8, 1, "points");
    NoteVector(object, 0xb4, 1, "quads");
    NoteVector(object, 0xc0, 1, "quadsOut");
    NoteVector(object, 0xcc, 1, "spans");
}

int32_t Coord(std::mt19937 &rng) { return int32_t(rng() % 300) - 50; }
void RandomRect(std::mt19937 &rng, cGZRect &r)
{
    r = {Coord(rng), Coord(rng), Coord(rng), Coord(rng)};
    if (rng() % 4) {
        if (r.left > r.right) std::swap(r.left, r.right);
        if (r.top > r.bottom) std::swap(r.top, r.bottom);
    }
    if (rng() % 12 == 0) r.right = r.left;
}
float RandomFloat(std::mt19937 &rng)
{
    switch (rng() % 12) {
    case 0: return -0.0f;
    case 1: return std::numeric_limits<float>::quiet_NaN();
    case 2: return 0.5f;
    case 3: return 0.49999997f;
    case 4: return -1.5f;
    // Wide but not absurd: a line width of 1e9 makes the game's line builder 0x8bd430 ask for more memory than there is
    // (its vector throws), on both sides alike.
    case 5: return 300.0f;
    default: return float(int32_t(rng() % 1200) - 200) / 4.0f;
    }
}

// The case's random draws, made once and replayed on each side (the same sequence of values).
struct Values {
    std::vector<uint32_t> words;
    size_t at = 0;
    uint32_t Next() { return words[at++]; }
};

uint32_t gCoefficients[12];

// One side: the context made by `create`, then the case's calls.
void RunSide(void *(*create)(), uint32_t seed)
{
    drawfx::Cells cells;
    cells.Set(kFrameworkCell, &gFramework);
    // The log's growth is the test's own: an earlier test may leave its buffer at any capacity (drawfx::Side clears
    // it, keeping the capacity), and the side would count the log's reallocation or not by the suite's order.
    drawfx::gLog.reserve(1 << 14);
    std::mt19937 rng(seed);
    difftest::AllocationCounter heap;

    void *const object = create();
    {
        // drawfx's range list keeps its capacity from the side before: its growth is the test's.
        difftest::NotCounted bookkeeping;
        drawfx::AddRange(object, cGZDrawContext::kSize, kObjectTag);
    }
    drawfx::Note("created", object != nullptr);
    if (!object) return;
    drawfx::Note("addref", CallSlot<uint32_t>(object, 1));
    // The fields the constructor leaves (heap garbage, different per side): the case's values.
    for (uint32_t off : {0x24u, 0x28u, 0x2cu, 0x30u, 0x34u, 0x38u, 0x3cu, 0x40u, 0x68u, 0x6cu, 0x70u, 0x74u, 0x78u,
                         0x7cu, 0x80u, 0x84u})
        Put<int32_t>(object, off, Coord(rng));
    Put<float>(object, 0x1c, RandomFloat(rng));
    Put<float>(object, 0x20, RandomFloat(rng));
    const uint8_t pad = uint8_t(rng());
    Put<uint8_t>(object, 0x47, pad);
    Put<uint8_t>(object, 0x57, pad);
    Put<uint32_t>(object, 0x64, rng() & 0x01010101);
    Put<uint32_t>(object, 0x88, rng() & 0x01010101);
    NoteObject(object);

    CallSlot<void>(object, 17, static_cast<void *>(&gTargets[0])); // SetTarget
    NoteObject(object);

    // Re-seed some of what SetTarget settled, so the flush and the texture paths see every combination.
    if (rng() % 2) {
        Put<uint32_t>(object, 0x44, rng() & 0x03030303);
        Put<uint32_t>(object, 0x48, rng() % 3);
        Put<float>(object, 0x4c, RandomFloat(rng));
    }
    // The state's rounded line width (+0x50) is 1 from the constructor and nothing in the class changes it, but the span and
    // quad builders read it: seeded here so their wide-line branches run.
    if (rng() % 2) Put<int32_t>(object, 0x50, int32_t(rng() % 14) - 3);
    if (rng() % 2) {
        Put<uint32_t>(object, 0x54, rng() & 0x03030303);
        Put<uint32_t>(object, 0x58, rng() % 3);
        Put<float>(object, 0x5c, RandomFloat(rng));
    }
    if (rng() % 2) Put<uint32_t>(object, 0x64, rng() & 0x0101);
    if (rng() % 2) Put<uint32_t>(object, 0x88, rng() & 0x0101);
    if (rng() % 3 == 0) Put<int32_t>(object, 0x8c, int32_t(rng() % 3));
    if (rng() % 2)
        for (uint32_t off = 0x34; off < 0x44; off += 4) Put<int32_t>(object, off, Coord(rng));
    NoteObject(object);

    for (int step = 0; step < 40; ++step) {
        const unsigned slot = 3 + rng() % 46;
        drawfx::Note("slot", slot);
        cGZRect r1, r2;
        RandomRect(rng, r1);
        RandomRect(rng, r2);
        cGZPoint pt{Coord(rng), Coord(rng)};
        cGZRect outRect{-7, -7, -7, -7};
        cGZPoint outPoint{-7, -7};
        float points[16];
        for (float &p : points) {
            p = float(int32_t(rng() % 1400) - 200) / 4.0f;
            // Now and then the awkward values: NaN, infinity, huge, negative zero, and ties on the half pixel.
            switch (rng() % 40) {
            case 0: p = std::numeric_limits<float>::quiet_NaN(); break;
            case 1: p = std::numeric_limits<float>::infinity(); break;
            case 2: p = -3.0e4f; break;
            case 3: p = -0.0f; break;
            case 4: p = float(int32_t(rng() % 100)) + 0.5f; break;
            case 5: p = float(int32_t(rng() % 100)) + 0.001f; break;
            // A point (almost) on the one two floats back (a line end on its start) (lines of no length or of 2e-4: 1e-8 is the quad builder's bound), and
            // whole pixels in the clips' range (lines and rows that end exactly on a clip edge).
            case 6: p = &p < &points[2] ? 0.0f : *(&p - 2); break;
            case 7: p = &p < &points[2] ? 0.0f : *(&p - 2) + 2.0e-4f; break;
            case 8: case 9: case 10: p = float(Coord(rng)); break;
            default: break;
            }
        }
        const int32_t count = int32_t(rng() % 9);
        const uint32_t word = rng();
        void *const texture = rng() % 4 ? static_cast<void *>(&gTexture) : nullptr;
        switch (slot) {
        case 5: case 6: case 32: case 36: CallSlot<void>(object, slot, &r1); break;
        case 7: case 8: CallSlot<void>(object, slot, &pt); break;
        case 12: case 13: {
            // The original returns its argument (eax), which callers read (cGZDrawFontRenderer's slot 5).
            const uint32_t ret = CallSlot<uint32_t>(object, slot, &outRect);
            drawfx::Note("rect", {uint32_t(outRect.left), uint32_t(outRect.top), uint32_t(outRect.right),
                                  uint32_t(outRect.bottom), uint32_t(ret == uint32_t(uintptr_t(&outRect)))});
            break;
        }
        case 14: {
            const uint32_t ret = CallSlot<uint32_t>(object, slot, &outPoint);
            drawfx::Note("point", {uint32_t(outPoint.x), uint32_t(outPoint.y),
                                   uint32_t(ret == uint32_t(uintptr_t(&outPoint)))});
            break;
        }
        case 15: drawfx::Note("depth", CallSlot<uint32_t>(object, slot)); break;
        case 16: case 42: drawfx::Note("bool", CallSlot<uint32_t>(object, slot) & 0xff); break;
        case 17: CallSlot<void>(object, slot, static_cast<void *>(&gTargets[rng() % 2])); break;
        case 18: drawfx::Note("engine", drawfx::Norm(CallSlot<uint32_t>(object, slot))); break;
        case 20: CallSlot<void>(object, slot, word & 3, rng()); break;
        case 21: case 22: case 23: case 24: case 25: CallSlot<void>(object, slot, word); break;
        case 26: CallSlot<void>(object, slot, RandomFloat(rng)); break;
        case 28:
            for (uint32_t &c : gCoefficients) c = rng();
            CallSlot<void>(object, slot, rng() % 4 ? static_cast<void *>(gCoefficients) : nullptr, RandomFloat(rng),
                           RandomFloat(rng));
            break;
        case 30:
            CallSlot<void>(object, slot, texture, RandomFloat(rng), RandomFloat(rng), RandomFloat(rng), RandomFloat(rng));
            break;
        case 34: CallSlot<void>(object, slot, pt.x, pt.y); break;
        case 35: case 37: case 38: CallSlot<void>(object, slot, r1.left, r1.top, r1.right, r1.bottom); break;
        case 39: CallSlot<void>(object, slot, texture, &r1, &r2); break;
        case 40: CallSlot<void>(object, slot, texture, &r1, &r2, pt.x, pt.y); break;
        case 45: case 46: {
            // Polygons (kind 8) of fewer than two points are left out: 0x8bf480's route 1 sizes its vector to 6 floats a
            // point minus 12, negative below two points (the game's vector then throws).
            uint32_t kind = word % 10;
            if ((word >> 8) % 4 == 0) kind = 1 + (word >> 12) % 2; // a quarter of the calls are lines or curves (the span builder's width branches are deep)
            // NaN and infinite points are left out of the kinds that can reach the polygon scan converter (quads from
            // rectangles, lines, points, ellipse outlines; triangles, quads, polygons): it walks rows by the points' integer
            // parts (0x80000000 for NaN or infinity), so the game asks for more memory than there is and throws. Kinds
            // 5 (ellipses) takes NaN; 0 (points) and 5 take infinity.
            if (kind == 1 && (word >> 20) % 3 == 0) // short lines: length squared 8e-8, between 1e-8 and 1e-7 (the quad builder's bound)
                for (int i = 0; i < 16; i += 4) {
                    points[i + 2] = points[i] + 2.0e-4f;
                    points[i + 3] = points[i + 1] + 2.0e-4f;
                }
            const bool nanOk =kind == 5, infOk = kind == 0 || kind == 5;
            for (float &p : points) {
                if (p != p && !nanOk) p = 7.25f;
                if (std::fabs(p) == std::numeric_limits<float>::infinity() && !infOk) p = 7.25f;
            }
            CallSlot<void>(object, slot, kind, static_cast<const float *>(points), kind == 8 && count < 2 ? 2 : count);
            break;
        }
        case 47: drawfx::Note("bool", CallSlot<uint32_t>(object, slot, word, rng()) & 0xff); break;
        default: CallSlot<void>(object, slot); break; // 3, 4, 9, 10, 11, 19, 27, 29, 31, 33, 41, 43, 44, 48
        }
        NoteObject(object);
    }

    // QueryInterface, a last target (another, or none), then the last Release.
    for (uint32_t iid : {1u, cIGZDrawContext::kIid, 0x12345678u}) {
        uint32_t out = 0xdeadbeef;
        const uint32_t answer = CallSlot<uint32_t>(object, 0, iid, static_cast<void *>(&out)) & 0xff;
        drawfx::Note("query", {answer, drawfx::Norm(out)});
    }
    // The constructor starts the count at 0: one AddRef and two answered queries leave 3.
    for (int i = 0; i < 2; ++i) drawfx::Note("release", CallSlot<uint32_t>(object, 2));
    CallSlot<void>(object, 17, rng() % 2 ? static_cast<void *>(&gTargets[1]) : nullptr);
    NoteObject(object);
    drawfx::Note("last release", CallSlot<uint32_t>(object, 2));
    heap.Stop();
    drawfx::Note("refs", {uint32_t(gEngine.refs), uint32_t(gSystem.refs), uint32_t(gTargets[0].refs),
                          uint32_t(gTargets[1].refs), uint32_t(gTexture.refs)});
    drawfx::Note("heap", {uint32_t(heap.Blocks()), uint32_t(heap.Bytes())});
}

void NewCase(uint32_t seed)
{
    std::mt19937 rng(seed * 7919u + 1);
    RandomRect(rng, gCase.targetArea[0]);
    RandomRect(rng, gCase.targetArea[1]);
    RandomRect(rng, gCase.bounds);
    RandomRect(rng, gCase.textureArea);
    const uint32_t answers[] = {0, 1, 0x80, 0x101};
    gCase.textured = answers[rng() % 4];
    gCase.routes = rng();
    // Two route combinations are left out, for faults in the game's builders (called alike on both sides):
    // - points (kind 0) by route 1: 0x8bc290 sizes the quad vector (+0xb4) for 8 floats a point but steps 12 a point, so
    //   from two points on it writes past the block (a heap overrun);
    // - ellipses (kind 5) by route 1 with polygons (kind 8) by route 1: 0x8bea10 builds each ellipse in the quad vector
    //   and dispatches it as a polygon; 0x8bf480 then resizes that same vector (6 floats a point, minus 12) and goes on
    //   reading its input from the old buffer, freed when the resize moved it (each side's heap words, not comparable).
    if ((gCase.routes & 3) == 1) gCase.routes ^= 3;                                   // kind 0: 1 -> 2
    if ((gCase.routes >> 10 & 3) == 1 && (gCase.routes >> 16 & 3) == 1) gCase.routes ^= 3u << 16; // kind 8: 1 -> 2
    gCase.vf20 = rng();
}

void *OriginalCreate()
{
    return reinterpret_cast<void *(__cdecl *)()>(GameAddress<void>(kCreator))();
}
void *OurCreate() { return cGZDrawContext::Create(); }

} // namespace

DIFF_TEST(GZDrawContext_Slots, 0x8bb610)
{
    // Every slot (0x8bb370 0x8bb330 0x8bb340 0x8bb3a0 0x8bb3e0 0x8bb410 0x8bb460 0x8bb480 0x8bb4b0 0x8bb4d0 0x8bb510
    // 0x8bb540 0x8bb5a0 0x8bb5b0 0x8bb5c0 0x8bb5e0 0x8bb600 0x8bb610 0x8bb7d0 0x8bb7e0 0x8bb840 0x8bb830 0x8bb870
    // 0x8bb880 0x8bb890 0x8bb8a0 0x8bb8b0 0x8bb8d0 0x8bb8f0 0x8bb920 0x8bb970 0x8bb9e0 0x8bba10 0x8bbac0 0x8bbaf0
    // 0x8bbb40 0x8bbbb0 0x8bbbe0 0x8bbc40 0x8bbca0 0x8bbd90 0x8bba60 0x8bba70 0x8bba80 0x8bbaa0 0x8bbe30 0x8bbeb0
    // 0x8bbed0 0x8bbee0), the creator 0x8bf7d0, the destructor 0x8bb120 (the last Release), FlushState 0x8bbef0,
    // Dispatch 0x8bc040 and DrawKind6 0x8befb0 (through the drawing slots).
    for (uint32_t seed = 0; seed < 12000; ++seed) {
        NewCase(seed);
        ctx.Mark("orig");
        auto a = drawfx::Side(&BuildWorld, [&] { RunSide(&OriginalCreate, seed); });
        NewCase(seed);
        ctx.Mark("ours");
        auto b = drawfx::Side(&BuildWorld, [&] { RunSide(&OurCreate, seed); });
        if (!drawfx::Same(ctx, a.first, b.first, "draw context", seed, a.second)) break;
    }
}

DIFF_TEST(GZDrawContext_Construct, 0x8baff0)
{
    // The constructor on a buffer of seeded bytes: every byte it writes and every byte it leaves, but the vtable word
    // (the game's on the original side, ours on ours).
    const auto original = ctx.Original<void *(__thiscall *)(void *)>(kConstructor);
    alignas(8) uint8_t a[cGZDrawContext::kSize], b[cGZDrawContext::kSize];
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 rng(seed);
        for (auto &x : a) x = uint8_t(rng());
        std::memcpy(b, a, sizeof(a));
        void *const ra = original(a);
        void *const rb = cGZDrawContextAdapters::Construct(b, nullptr);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("seed %u: the constructor returned another address", seed);
        if (At<uint32_t>(a, 0) != uint32_t(uintptr_t(GameAddress<void>(kGameVTable))))
            ctx.Fail("seed %u: the original's vtable word is not the game's", seed);
        if (At<uint32_t>(b, 0) == uint32_t(uintptr_t(GameAddress<void>(kGameVTable))))
            ctx.Fail("seed %u: our constructor stored the game's vtable", seed);
        if (std::memcmp(a + 4, b + 4, sizeof(a) - 4) != 0) {
            for (uint32_t off = 4; off < sizeof(a); ++off)
                if (a[off] != b[off]) {
                    ctx.Fail("seed %u: byte +%#x orig %#x ours %#x", seed, off, a[off], b[off]);
                    break;
                }
            break;
        }
    }
}
