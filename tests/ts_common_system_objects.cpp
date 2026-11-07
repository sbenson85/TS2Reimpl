// Differential tests for the objects cTSCommonSystem makes (lane LF; src/ts/cTSCommonSystemObjects.h,
// src/gz/ExpressionFunctionBase.h), in the standalone shape: each side's objects are built by that side's builder and every
// slot is called through the object's own vtables.
//
// CommonObjects_Functions: the expression functions cIsDayFunction and cIsLightMaterialFunction, built and registered by
// RegisterExpressionFunctions (the original 0xcbb420 / ours) with a registry stand-in that holds them, then a random
// sequence per seed: Evaluate (0xcb9510, 0xcb9590), GetHelp (0xcb9ce0, 0xcb9cf0), GetName (0xcbadf0, 0xcbae00), the face's
// QueryInterface / AddRef / Release / SetContext / GetFunction / slot 6 (the bases' bodies 0xaf7c60, 0x148fb0, 0xaf7d80,
// 0x24c860, 0x142700, 0x12e40), and at the end the last Release (through the deleting destructor 0x247a80, flags logged by a
// recording copy of the object's vtable) or the deleting destructor with flags 0 (0xaf79a0's stores compared).
// Stand-ins: the common system getter 0xb38f60 (null, or a fake whose slot 4 gives null or an effects manager fake whose
// slot 32 answers raw bytes), the registry getter 0xae8980, the game's operator new / free (0x824632 / 0x82540f).
// Not driven: expressions the parser helpers 0xb096d0 / 0xb0a580 reject (they throw a parser error through
// _CxxThrowException, a trapped import in the harness): every text has "(", a quoted name and ")".
//
// CommonObjects_Effects: the effects manager's named objects (cPinToFloorConstraint, cPinToTopsConstraint,
// cWallsAndFloorsCollider, cWallsCollider, cFloorsCollider). The original's builder is Init 0xcba7c0, inline: `new 0xc`,
// the two vtables, a zero count (0xcbaa62..0xcbaba4; CommonSystem_Run compares what Init hands the effects manager); the
// test's original side writes exactly that, ours is `new X`. Random operations per seed: QueryInterface / AddRef / Release on
// both faces (the thunks 0x1e2e00 / 0x1dab20 / 0x1e3970 and 0x356874 / 0x3545d3 / 0x356902 over cRZUnknown's bodies), RemoveRef /
// RefCount, the constraint's Apply (0xcb9640), the colliders' GetStateSize (0x3a3c20), CopyState (0x3a35b0), Collide
// (0xcb97a0, the transform's ToLocal 0x3a34b0 ours and the original's on their sides), slot 6 (0x1474a0) and CollideWorld (0xcb9ab0, 0xcb9c70,
// 0xcb9b60) with the two collision settings' bytes (0x1087518 / 0x1087520) drawn per seed; at the end the last Release (the
// deleting destructor through the +4 face's slot 3, logged) or that destructor with flags 0 (0x3a2af3 / 0x3a2ad0).
// Compared: results (al where the slot returns a bool), every output buffer and its untouched sentinels bit for bit, the
// object's words (vtable words as codes: the game's and ours of the same class and face are one code), the fakes' event
// logs and the stack balance of every slot call.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"

#include "game/game.h"
#include "misc/UtilitiesE.h"
#include "ts/cTSCommonSystem.h"
#include "ts/cTSCommonSystemObjects.h"

#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace cso_test {

using Blob = std::vector<uint32_t>;
using namespace common_system;

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
uint32_t U(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }

Blob gEv; // the fakes' events
void Ev(uint32_t w) { gEv.push_back(w); }

// ---- normalising words ----

struct Range {
    uint32_t base, size, tag;
};
std::vector<Range> gRanges;
void AddRange(const void *p, uint32_t size, uint32_t tag) { gRanges.push_back({P(p), size, tag}); }

// Vtable words: {game RVA, ours} per class and face; a word of either is the same code.
struct VtPair {
    uint32_t game, ours;
};
std::vector<VtPair> gVt;
uint32_t Norm(uint32_t w)
{
    if (!w) return 0;
    for (size_t i = 0; i < gVt.size(); ++i)
        if (w == P(GameAddress<void>(gVt[i].game)) || w == gVt[i].ours) return 0x7a000000 | uint32_t(i);
    for (const Range &r : gRanges)
        if (w - r.base < r.size) return r.tag + (w - r.base);
    return w;
}

// ---- calls ----

int32_t gStackSkew;
uint32_t CallChecked(const void *fn, void *self, const uint32_t *args, uint32_t count)
{
    uint32_t result, before, after;
    __asm {
        mov before, esp
        mov ecx, count
        mov edx, args
    push_next:
        test ecx, ecx
        jz pushed
        dec ecx
        push dword ptr [edx + ecx * 4]
        jmp push_next
    pushed:
        mov ecx, self
        call fn
        mov result, eax
        mov after, esp
        mov esp, before
    }
    if (after != before) gStackSkew = int32_t(after - before);
    return result;
}
template <class... A> uint32_t Slot(void *face, unsigned slot, A... a)
{
    static_assert(((sizeof(A) == 4) && ...), "dword arguments only");
    const uint32_t args[sizeof...(A) + 1] = {(uint32_t)(uintptr_t)a..., 0};
    return CallChecked((*static_cast<void ***>(face))[slot], face, args, uint32_t(sizeof...(A)));
}
void *At(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

// A deleting destructor reached through a copy of the object's vtable: the flags logged, then the side's own slot.
void *gRealDelete;
void *__fastcall RecordingDelete(void *self, void *, uint32_t flags)
{
    Ev(0xde1e7e00);
    Ev(flags);
    using Fn = void *(__thiscall *)(void *, uint32_t);
    return reinterpret_cast<Fn>(gRealDelete)(self, flags);
}
void *gVtCopy[16];
// The face at `face` gets a copy of its vtable (n slots) with slot `slot` recording.
void RecordDeleteAt(void *face, unsigned slot, unsigned n)
{
    void **table = *static_cast<void ***>(face);
    for (unsigned i = 0; i < n; ++i) gVtCopy[i] = table[i];
    gRealDelete = table[slot];
    gVtCopy[slot] = reinterpret_cast<void *>(&RecordingDelete);
    *static_cast<void ***>(face) = gVtCopy;
}

// ---- the game's heap ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
// Not logged: ours frees on our heap (the deleting destructor's flags are logged instead).
void __cdecl GameFree(void *p) { ::operator delete(p); }

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

// A float worth testing: small integers, fractions, signed zeros, the scale's 1, large, NaN now and then. In a "special"
// seed (one in four) every number is one of 0, -0, 1, -1, 2, inf, -inf, NaN: then a NaN made by 0 * inf meets a drawn
// NaN often enough, also where only a collision lets the result out.
bool gSpecial;
float Num()
{
    if (gSpecial) {
        static const float kSpecial[] = {0.0f, -0.0f, 1.0f, -1.0f, 2.0f, std::numeric_limits<float>::infinity(),
                                         -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
        return kSpecial[Draw(8)];
    }
    switch (Draw(12)) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return 1.0f;
    case 3: return float(int(Draw(9)) - 4);
    case 4: return float(int(Draw(2001)) - 1000) / 64.0f;
    case 5: return float(Draw(400)) / 100.0f; // 0..4: around the floor, the ceiling and the walls
    case 6: return Draw(8) ? 3.0f : std::numeric_limits<float>::quiet_NaN();
    // Infinities: 0 * inf and inf - inf make the default NaN (sign set), so a sum of it and a drawn NaN shows which
    // operand the sum keeps.
    case 7: return Draw(3) ? float(int(Draw(5)) - 2) : (Draw(2) ? 1.0f : -1.0f) * std::numeric_limits<float>::infinity();
    default: return (float(gRng()) / 4294967296.0f - 0.5f) * 8.0f;
    }
}

// ======== the expression functions ========

namespace fn {

// The registry (the material parser's cIBaseParser face): 7 holds a function, 5 unregisters (not driven here).
void *gHeld[2];
uint32_t gHeldCount;
uint32_t __fastcall Register(void *, void *, void *face)
{
    Ev(0x7e900007);
    if (face) {
        Ev(Norm(Word(face, -4)));
        Ev(Norm(Word(face, 0)));
        Ev(Word(face, 4));
        Slot(face, 1);
    }
    if (gHeldCount < 2) gHeld[gHeldCount] = face;
    ++gHeldCount;
    return 0;
}
void *gRegistryTable[8];
struct RegistryObject {
    void **vtable;
} gRegistry;
struct Shell {
    uint32_t pad;
    const int32_t *vbptr;
} gShell;
int32_t gVb[3];
void *__cdecl StandRegistry()
{
    Ev(0xae8980);
    return &gShell;
}

// The common system (slot 4: the effects manager) and the effects manager (slot 32: a debug bit).
void *gCommonTable[8];
void *gEffectsTable[40];
struct Fake {
    void **vtable;
} gCommon, gEffects;
bool gHaveCommon, gHaveEffects;
uint32_t gDebugAnswer;
void *__cdecl StandCommon()
{
    Ev(0xb38f60);
    return gHaveCommon ? &gCommon : nullptr;
}
void *__fastcall CommonEffects(void *, void *)
{
    Ev(0xc0040004);
    return gHaveEffects ? &gEffects : nullptr;
}
uint32_t __fastcall EffectsDebugBit(void *, void *, uint32_t bit)
{
    Ev(0xeff00020);
    Ev(bit);
    return gDebugAnswer;
}

// Expression texts the parser helpers accept: "(", a quoted name, ")", with white space; "lighting" in the name, after it,
// or nowhere.
const char *const kTexts[] = {"(\"lighting\")",       "( \"my_lighting_mat\" )", "(\"abc\")",           "(\"\")",
                              "(\"abc\") lighting",   " \t(\"lightin\")g",       "(\"light\")ing",      "\r\n( \"LIGHTING\" ) x",
                              "(\"lightinglighting\")", "(\"xx\")(\"lighting\")",  "(\"lighting\"",
                              "(\"a\" ) , lighting"};
constexpr uint32_t kTextCount = sizeof(kTexts) / sizeof(kTexts[0]);
// kTexts[10] has no ")": the helper would throw; never drawn (see the file comment).
uint32_t TextIndex(uint32_t a)
{
    const uint32_t i = a % kTextCount;
    return i == 10 ? 0 : i;
}

void LogText(const char *text)
{
    if (!text) {
        Ev(0xdead0000);
        return;
    }
    const size_t n = std::strlen(text);
    Ev(uint32_t(n));
    for (size_t i = 0; i < n; ++i) Ev(uint8_t(text[i]));
}

void BuildWorld(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 3);
    gHaveCommon = rng() % 5 != 0;
    gHaveEffects = rng() % 4 != 0;
    static const uint32_t kAnswers[] = {0, 1, 0x80, 0x100, 0xff, 0x12345601};
    gDebugAnswer = kAnswers[rng() % 6];
    gCommonTable[4] = reinterpret_cast<void *>(&CommonEffects);
    gCommon.vtable = gCommonTable;
    gEffectsTable[32] = reinterpret_cast<void *>(&EffectsDebugBit);
    gEffects.vtable = gEffectsTable;
    gRegistryTable[7] = reinterpret_cast<void *>(&Register);
    gRegistry.vtable = gRegistryTable;
    gVb[0] = -4;
    gVb[1] = 0;
    gVb[2] = int32_t(P(&gRegistry) - P(&gShell.vbptr));
    gShell.vbptr = gVb;
    gHeldCount = 0;
    gHeld[0] = gHeld[1] = nullptr;
}

const char *gCursorBase;
uint8_t gParser[16];
uint8_t gState[16];

void Op(Blob &b, void *face, uint32_t a)
{
    void *const object = At(face, uint32_t(-4));
    switch (a % 9) {
    case 0: { // QueryInterface
        static const uint32_t kIids[] = {1, gzexpr::cIExpressionFunction::kIid, 0, 0x12345678, 0xcc1d5da1};
        void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const bool none = (a >> 4) % 6 == 0;
        b.push_back(Slot(face, 0, kIids[(a >> 8) % 5], none ? nullptr : &out) & 0xff);
        b.push_back(Norm(P(out)));
        break;
    }
    case 1: b.push_back(Slot(face, 1)); break;
    case 2: // Release, above the last reference
        if (Word(face, 4) > 1) b.push_back(Slot(face, 2));
        break;
    case 3: // SetContext
        Slot(face, 3, gParser, (a >> 4) % 3 ? static_cast<void *>(&gState[4 + (a >> 6) % 4]) : nullptr);
        break;
    case 4: b.push_back(Norm(Slot(face, 4))); break; // GetFunction
    case 5: LogText(reinterpret_cast<const char *>(uintptr_t(Slot(face, 5)))); break; // GetName
    case 6: b.push_back(Slot(face, 6, a)); break;
    case 7: LogText(reinterpret_cast<const char *>(uintptr_t(Slot(object, 2)))); break; // GetHelp
    default: { // Evaluate
        uint32_t value[6];
        for (uint32_t &w : value) w = 0x5e5e5e5e;
        const char *text = kTexts[TextIndex(a >> 4)];
        const char *cursor = text;
        gCursorBase = text;
        const uint32_t r = Slot(object, 0, value, &cursor, gParser);
        b.push_back(r == P(value));
        for (uint32_t w : value) b.push_back(w);
        b.push_back(uint32_t(cursor - text));
        break;
    }
    }
}

void DumpFunction(Blob &b, void *face)
{
    if (!face) {
        b.push_back(0xdead);
        return;
    }
    const uint8_t *object = static_cast<uint8_t *>(face) - 4;
    b.push_back(Norm(Word(object, 0)));
    b.push_back(Norm(Word(object, 4)));
    b.push_back(Word(object, 8));
    // +0xc: not written before SetContext (RegisterExpressionFunctions' new leaves it; the original's 0xaf7840 doesn't store
    // it either): compared once a SetContext ran.
}

} // namespace fn

// ======== the effects objects ========

namespace csfx {

struct Class {
    uint32_t vt0, vt1;
    bool collider;
};
// pinToFloor, pinToTops, wallsAndFloors, walls, floors (Init's order).
const Class kClasses[5] = {{0xeb7bb0, 0xeb7bc4, false},
                           {0xeb7be0, 0xeb7bf4, false},
                           {0xeb7c2c, 0xeb7c50, true},
                           {0xeb7cac, 0xeb7cd0, true},
                           {0xeb7c6c, 0xeb7c90, true}};

void *BuildOriginal(unsigned k)
{
    auto *words = static_cast<uint32_t *>(::operator new(0xc));
    words[0] = P(GameAddress<void>(kClasses[k].vt0));
    words[1] = P(GameAddress<void>(kClasses[k].vt1));
    words[2] = 0;
    return words;
}
void *BuildOurs(unsigned k)
{
    switch (k) {
    case 0: return static_cast<cITSEffectsConstraint *>(new cPinToFloorConstraint);
    case 1: return static_cast<cITSEffectsConstraint *>(new cPinToTopsConstraint);
    case 2: return static_cast<cITSEffectsCollider *>(new cWallsAndFloorsCollider);
    case 3: return static_cast<cITSEffectsCollider *>(new cWallsCollider);
    default: return static_cast<cITSEffectsCollider *>(new cFloorsCollider);
    }
}

// The transform, points and buffers of a call (in one block, the outputs with sentinels after them).
struct Block {
    EffectsTransform transform;
    EffectsTransform copy;
    uint32_t copyTail[2];
    Vector3 from, to, offset, normal;
    uint32_t tail[2];
    uint8_t context[0x60];
};
Block *gBlock;

void DrawTransform(EffectsTransform &t)
{
    t.flags = uint8_t(Draw(4) == 0 ? gRng() : (Draw(2) ? 2 : 0));
    t.pad[0] = uint8_t(gRng());
    t.pad[1] = uint8_t(gRng());
    t.pad[2] = uint8_t(gRng());
    for (auto &row : t.m)
        for (float &v : row) v = Num();
    t.scale = Draw(3) == 0 ? 1.0f : Num();
    t.translation = {Num(), Num(), Num()};
}
void DrawPoint(Vector3 &p) { p = {Num(), Num(), Num()}; }

void DumpBlock(Blob &b)
{
    const auto *words = reinterpret_cast<const uint32_t *>(gBlock);
    for (size_t i = 0; i < offsetof(Block, context) / 4; ++i) b.push_back(words[i]);
}

void Op(Blob &b, void *object, unsigned k, uint32_t a)
{
    void *const second = At(object, 4);
    void *const faces[2] = {object, second};
    switch (a % 9) {
    case 0: { // QueryInterface
        static const uint32_t kIids[] = {1, 2, 0x12345678, 0};
        void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        b.push_back(Slot(faces[(a >> 4) % 2], 0, kIids[(a >> 5) % 4], &out) & 0xff);
        b.push_back(Norm(P(out)));
        break;
    }
    case 1: b.push_back(Slot(faces[(a >> 4) % 2], 1)); break;
    case 2:
        if (Word(object, 8) > 1) b.push_back(Slot(faces[(a >> 4) % 2], 2));
        break;
    case 3: b.push_back(Slot(second, 4)); break; // RemoveRef (to 0 at most: the count never wraps)
    case 4: b.push_back(Slot(second, 5)); break; // RefCount
    default: {
        DrawTransform(gBlock->transform);
        DrawPoint(gBlock->from);
        DrawPoint(gBlock->to);
        DrawPoint(gBlock->offset);
        DrawPoint(gBlock->normal);
        for (uint8_t &c : gBlock->context) c = uint8_t(gRng());
        *reinterpret_cast<float *>(gBlock->context + 0x4c) = Num();
        if (!kClasses[k].collider) { // Apply
            Slot(object, 3, gRng(), &gBlock->transform, &gBlock->to, &gBlock->offset);
            break;
        }
        switch ((a >> 4) % 5) {
        case 0: b.push_back(Slot(object, 3)); break;
        case 1: Slot(object, 4, gRng(), gRng(), &gBlock->transform, &gBlock->copy); break;
        case 2: b.push_back(Slot(object, 6, gRng(), gRng(), gRng()) & 0xff); break;
        case 3: b.push_back(Slot(object, 7, &gBlock->from, &gBlock->to, U(Num()), &gBlock->normal) & 0xff); break;
        default:
            b.push_back(Slot(object, 5, gBlock->context, &gBlock->transform, U(Num()), &gBlock->from, &gBlock->to, &gBlock->normal) &
                        0xff);
            break;
        }
        break;
    }
    }
    DumpBlock(b);
}

void DumpObject(Blob &b, const void *object)
{
    b.push_back(Norm(Word(object, 0)));
    b.push_back(Norm(Word(object, 4)));
    b.push_back(Word(object, 8));
}

} // namespace csfx

// ---- our vtables ----

void CollectVTables()
{
    gVt.clear();
    const auto add = [](uint32_t game, const void *object, uint32_t offset) { gVt.push_back({game, Word(object, offset)}); };
    {
        auto *f = new cIsDayFunction;
        add(0xeb7cec, f, 0);
        add(0xeb7cfc, f, 4);
        f->~cIsDayFunction(); // leaves the base's vtables
        add(0xe7d7c4, f, 0);
        add(0xe7d7d0, f, 4);
        ::operator delete(f);
        auto *g = new cIsLightMaterialFunction;
        add(0xeb7d44, g, 0);
        add(0xeb7d54, g, 4);
        delete g;
    }
    for (unsigned k = 0; k < 5; ++k) {
        void *o = csfx::BuildOurs(k);
        add(csfx::kClasses[k].vt0, o, 0);
        add(csfx::kClasses[k].vt1, o, 4);
        if (k == 0) { // the destructor leaves cRZUnknown's vtable at +4
            static_cast<cPinToFloorConstraint *>(static_cast<cITSEffectsConstraint *>(o))->~cPinToFloorConstraint();
            add(0xda2fd0, o, 4);
            ::operator delete(o);
        } else {
            Slot(At(o, 4), 2); // count 0: Release deletes
        }
    }
}

struct Scope {
    shape::CodePatch gameNew{0x824632, shape::CodeOf(&GameNew)};
    shape::CodePatch gameFree{0x82540f, shape::CodeOf(&GameFree)};
    shape::StandIn registry{0xae8980, &GetMaterialParserCached, &fn::StandRegistry};
    shape::CodePatch common{0xb38f60, shape::CodeOf(&fn::StandCommon)};
    uint8_t floorByte, wallByte;
    Scope()
    {
        floorByte = *GameAddress<uint8_t>(0x1087518);
        wallByte = *GameAddress<uint8_t>(0x1087520);
        CollectVTables();
        csfx::gBlock = new csfx::Block;
    }
    ~Scope()
    {
        *GameAddress<uint8_t>(0x1087518) = floorByte;
        *GameAddress<uint8_t>(0x1087520) = wallByte;
        delete csfx::gBlock;
        gRanges.clear();
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};

void Finish(Blob &b)
{
    b.push_back(uint32_t(gStackSkew));
    gStackSkew = 0;
    b.insert(b.end(), gEv.begin(), gEv.end());
    gEv.clear();
}

bool Same(difftest::Context &ctx, const char *test, uint32_t seed, const Blob (&r)[2])
{
    if (r[0] == r[1]) return true;
    size_t i = 0;
    while (i < r[0].size() && i < r[1].size() && r[0][i] == r[1][i]) ++i;
    ctx.Fail("%s seed %u: word %u differs (original %08x, ours %08x; sizes %u / %u)", test, seed, unsigned(i),
             i < r[0].size() ? r[0][i] : 0u, i < r[1].size() ? r[1][i] : 0u, unsigned(r[0].size()), unsigned(r[1].size()));
    return false;
}

} // namespace cso_test

using namespace cso_test;

DIFF_TEST(CommonObjects_Functions, 0xcb9510)
{
    // Also: 0xcb9590, 0xcb9ce0, 0xcb9cf0, 0xcbadf0, 0xcbae00, 0xcbb420 (the builder), and the bases' bodies through our
    // vtables (see the file comment).
    Scope scope;
    for (uint32_t seed = 1; seed <= 1500; ++seed) {
        Blob r[2];
        for (int side = 0; side < 2; ++side) {
            Blob &b = r[side];
            fn::BuildWorld(seed);
            gRng.seed(seed * 7 + 1);
            gRanges.clear();
            AddRange(fn::gParser, sizeof(fn::gParser), 0x7b000000);
            AddRange(fn::gState, sizeof(fn::gState), 0x7b100000);
            if (side == 0)
                ctx.Original<void(__cdecl *)()>(0xcbb420)();
            else
                common_system::RegisterExpressionFunctions();
            b.push_back(fn::gHeldCount);
            for (int i = 0; i < 2; ++i)
                if (fn::gHeld[i]) AddRange(static_cast<uint8_t *>(fn::gHeld[i]) - 4, 0x10, 0x7c000000 + 0x100 * i);
            for (uint32_t &w : gEv) w = Norm(w);
            for (int i = 0; i < 2; ++i) fn::DumpFunction(b, fn::gHeld[i]);
            bool context[2] = {false, false};
            const unsigned n = 2 + Draw(24);
            for (unsigned i = 0; i < n; ++i) {
                const uint32_t a = gRng();
                const int which = int((a >> 28) % 2);
                void *face = fn::gHeld[which];
                if (!face) continue;
                if (a % 9 == 3) context[which] = true;
                fn::Op(b, face, a);
                fn::DumpFunction(b, face);
                if (context[which]) b.push_back(Norm(Word(face, 8)));
                Finish(b);
            }
            for (int i = 0; i < 2; ++i) {
                void *face = fn::gHeld[i];
                if (!face) continue;
                void *object = At(face, uint32_t(-4));
                if (Draw(3) == 0) { // the deleting destructor with flags 0, then the memory freed by hand
                    b.push_back(Norm(Slot(object, 1, 0u)));
                    fn::DumpFunction(b, face);
                    ::operator delete(object);
                } else { // the last references released
                    RecordDeleteAt(object, 1, 3);
                    while (Word(face, 4) > 1) Slot(face, 2);
                    b.push_back(Slot(face, 2));
                }
                Finish(b);
            }
        }
        if (!Same(ctx, "CommonObjects_Functions", seed, r)) break;
    }
}

DIFF_TEST(CommonObjects_Effects, 0xcb97a0)
{
    // Also: 0xcb9640, 0xcb9ab0, 0xcb9b60, 0xcb9c70, and the bases' bodies through our vtables (see the file comment).
    Scope scope;
    for (uint32_t seed = 1; seed <= 12000; ++seed) {
        const unsigned k = seed % 5;
        Blob r[2];
        for (int side = 0; side < 2; ++side) {
            Blob &b = r[side];
            gRng.seed(seed * 13 + 5);
            gSpecial = Draw(4) == 0;
            std::memset(csfx::gBlock, 0x5e, sizeof(csfx::Block));
            static const uint8_t kBytes[] = {0, 1, 0x80, 0, 0xff};
            *GameAddress<uint8_t>(0x1087518) = kBytes[Draw(5)];
            *GameAddress<uint8_t>(0x1087520) = kBytes[Draw(5)];
            void *object = side == 0 ? csfx::BuildOriginal(k) : csfx::BuildOurs(k);
            gRanges.clear();
            AddRange(object, 0xc, 0x7c000000);
            AddRange(csfx::gBlock, sizeof(csfx::Block), 0x7d000000);
            csfx::DumpObject(b, object);
            Slot(object, 1); // the effects manager's reference
            const unsigned n = 1 + Draw(30);
            for (unsigned i = 0; i < n; ++i) {
                csfx::Op(b, object, k, gRng());
                csfx::DumpObject(b, object);
                Finish(b);
            }
            if (Draw(3) == 0) { // the deleting destructor with flags 0 (through the +4 face's slot 3)
                b.push_back(Norm(Slot(At(object, 4), 3, 0u)));
                csfx::DumpObject(b, object);
                ::operator delete(object);
            } else {
                RecordDeleteAt(At(object, 4), 3, 6);
                while (Word(object, 8) > 1) Slot(At(object, 4), 2);
                b.push_back(Slot(object, 2));
            }
            Finish(b);
        }
        if (!Same(ctx, "CommonObjects_Effects", seed, r)) break;
    }
}
