// Differential test for the effects' world-space objects (lane YA, ran as RZ; src/world/WorldColliders.h), in the standalone
// shape: each side's six objects are built by that side's registrar (the original 0x3a3c30 / ours) against a fake effects
// manager, and every slot is called through the object's own vtables.
//
// WorldColliders_Objects: per seed the six objects are registered (the effects manager's slots 27 / 29 log each name and
// the object's index), then a random sequence of operations on one of them: QueryInterface / AddRef / Release on both faces,
// RemoveRef / RefCount (cRZUnknown's bodies through the thunks), the pins' Apply (0x3a2b00, 0x3a2d20, 0x3a2f80), the
// colliders' GetStateSize (0x3a3c20), CopyState (0x3a35b0), Collide (0x3a30f0, the transform's ToLocal 0x3a34b0 on each
// side), slot 6 (0x1474a0) and CollideWorld (0x3a3410, 0x3a3430, 0x3a3490, so CollideFloors 0x3a35e0 and CollideWalls
// 0x3a39e0); at the end every object's last Release (through the deleting destructor, its flags logged) or that destructor
// with flags 0.
// The world: the grid world of tests/worldgrid.h in the world DB cell (the heights helpers 0x7ddd90 / 0x7ddd60 / 0x7e4680 /
// 0x7ddc50 and ours read it), its slot 52 answering by tile, its slot 100 an outside support fake whose slot 17 fills a point
// and slot 18 changes one; the placement service cell 0x10cc2b0 (read by 0x20de97, the original on both sides) a fake whose
// slot 40 gives the wall object, whose slots 21 (a sphere at a tile) and 19 (a segment between two tiles) answer from a
// per-seed stream and write the three buffers they get. The game's operator new / free (0x824632 / 0x82540f) stand in.
// Compared: results (al where the slot returns a bool), the block of points and buffers bit for bit with its sentinels, the
// objects' words (vtable words as codes), the fakes' logs (pointer arguments as the content they point at, or as codes) and
// the stack balance of every slot call. Runs at the harness's 53-bit x87 precision and again at the game's 24-bit.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "worldgrid.h"

#include "game/game.h"
#include "ts/cTSEffectsManager.h"
#include "world/WorldColliders.h"

#include <cstring>
#include <limits>
#include <new>
#include <random>
#include <vector>

namespace wc_test {

using Blob = std::vector<uint32_t>;
using namespace world_colliders;
using worldfx::Fake;
using worldfx::gLog;
using worldfx::gWorldDb;
using worldgrid::Bits;
using worldgrid::FromBits;
using worldgrid::gWorld;

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }
void *At(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

Blob gEv; // the recording destructor's events

// ---- normalising words ----

struct Range {
    uint32_t base, size, tag;
};
std::vector<Range> gRanges;
void AddRange(const void *p, uint32_t size, uint32_t tag) { gRanges.push_back({P(p), size, tag}); }

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

void *gRealDelete;
void *__fastcall RecordingDelete(void *self, void *, uint32_t flags)
{
    gEv.push_back(0xde1e7e00);
    gEv.push_back(flags);
    using Fn = void *(__thiscall *)(void *, uint32_t);
    return reinterpret_cast<Fn>(gRealDelete)(self, flags);
}
void *gVtCopy[6][16];
void RecordDeleteAt(unsigned which, void *face, unsigned slot, unsigned n)
{
    void **table = *static_cast<void ***>(face);
    for (unsigned i = 0; i < n; ++i) gVtCopy[which][i] = table[i];
    gRealDelete = table[slot];
    gVtCopy[which][slot] = reinterpret_cast<void *>(&RecordingDelete);
    *static_cast<void ***>(face) = gVtCopy[which];
}

// ---- the game's heap ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameFree(void *p) { ::operator delete(p); }

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

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
    case 5: return float(Draw(400)) / 100.0f;
    case 6: return Draw(8) ? 3.0f : std::numeric_limits<float>::quiet_NaN();
    case 7: return Draw(3) ? float(int(Draw(5)) - 2) : (Draw(2) ? 1.0f : -1.0f) * std::numeric_limits<float>::infinity();
    default: return (float(gRng()) / 4294967296.0f - 0.5f) * 8.0f;
    }
}
// A height around the grid world's floors (levels -1..5, walls of up to 3), sometimes well over the top floor.
float Height()
{
    switch (Draw(5)) {
    case 0: return Num();
    case 1: return float(int(Draw(40)) - 8) * 0.5f;
    case 2: return 10.0f + float(Draw(801)) / 40.0f;
    default: return float(int(Draw(1601)) - 300) / 100.0f;
    }
}
// A size or radius: small steps either side of 0, tiny (subnormal) ones whose halving rounds, and huge ones.
float Extent(float step)
{
    switch (Draw(8)) {
    case 0: return Num();
    case 1: return float(int(Draw(9)) - 4) * std::numeric_limits<float>::denorm_min();
    case 2: return (Draw(2) ? 1.0f : -1.0f) * float(1 + Draw(4)) * 1e38f;
    case 3: return -float(Draw(9)) * step;
    default: return float(Draw(9)) * step;
    }
}
// A coordinate on or near the grid.
float Coord()
{
    if (Draw(5) == 0) return Num();
    return -1.0f + float(gWorld.size + 2) * float(Draw(100001)) / 100000.0f;
}

// ---- the fakes ----

// The fakes' answers come from their own stream, seeded alike on both sides.
std::mt19937 gFakeRng;
float FakeNum()
{
    switch (gFakeRng() % 8) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return float(int(gFakeRng() % 9) - 4) * 0.25f;
    case 3: return gFakeRng() % 16 ? 1.0f : std::numeric_limits<float>::quiet_NaN();
    case 4: return -1.0f;
    default: return (float(gFakeRng()) / 4294967296.0f - 0.5f) * float(2 + gWorld.size);
    }
}

uint32_t Mix(uint32_t h, uint32_t w) { return (h ^ w) * 0x9e3779b1u + 0x7f4a7c15u; }
// A pointer argument in the log: the three words it points at, folded (the pointers differ between the sides).
uint32_t Content(uint32_t pointer)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(uintptr_t(pointer));
    return Mix(Mix(Mix(0x3c3c3c3c, w[0]), w[1]), w[2]);
}
// The entry the fake just logged (Dispatch logs before the slot's function runs): its first argument word.
uint32_t *LoggedArgs(unsigned argc) { return gLog.data() + gLog.size() - argc; }

void WritePoint(uint32_t pointer, float x, float y, float z)
{
    float *p = reinterpret_cast<float *>(uintptr_t(pointer));
    p[0] = x;
    p[1] = y;
    p[2] = z;
}

// The effects manager: slots 27 (a collider) and 29 (a constraint) take a name and an object.
Fake gManager;
void *gObjects[6];
unsigned gObjectCount;
uint32_t NameHash(const char *s)
{
    uint32_t h = 0x811c9dc5u;
    for (; *s; ++s) h = (h ^ uint8_t(*s)) * 0x01000193u;
    return h;
}
uint32_t SetNamed(Fake *, const uint32_t *a)
{
    uint32_t *logged = LoggedArgs(2);
    logged[0] = NameHash(reinterpret_cast<const char *>(uintptr_t(a[0])));
    logged[1] = a[1] ? 0x0b000000 | gObjectCount : 0;
    if (gObjectCount < 6) gObjects[gObjectCount] = reinterpret_cast<void *>(uintptr_t(a[1]));
    ++gObjectCount;
    return 0;
}

// The outside support: 17 fills a point (not read), 18 changes one.
Fake gOutside;
uint32_t OutsideTop(Fake *, const uint32_t *a)
{
    LoggedArgs(1)[0] = 0x17;
    WritePoint(a[0], FakeNum(), FakeNum(), FakeNum());
    return 0;
}
uint32_t OutsidePool(Fake *, const uint32_t *a)
{
    LoggedArgs(1)[0] = Content(a[0]);
    float *p = reinterpret_cast<float *>(uintptr_t(a[0]));
    const uint32_t how = gFakeRng() % 4;
    if (how == 0) return 0;
    if (how == 1) p[2] = FakeNum();
    else WritePoint(a[0], p[0] + FakeNum(), p[1], FakeNum());
    return 0;
}

// The placement service (slot 40: the wall object of a kind) and the wall object: 21 (tile, distance, scratch, hit, normal)
// and 19 (tile, tile, scratch, hit, normal) answer from the stream and write the three buffers (the normal not logged: the
// caller's buffer is uninitialised when Collide calls).
Fake gPlacement, gWalls;
uint32_t gWallOdds;
uint32_t WallAnswer(const uint32_t *a, bool segment)
{
    uint32_t *logged = LoggedArgs(5);
    logged[0] = Content(a[0]);
    if (segment) logged[1] = Content(a[1]);
    logged[2] = Content(a[2]);
    logged[3] = Content(a[3]);
    logged[4] = 0x4e;
    const bool hit = gFakeRng() % 100 < gWallOdds;
    const uint32_t write = gFakeRng() % 4;
    if (hit || write == 0) {
        WritePoint(a[2], FakeNum(), FakeNum(), FakeNum());
        WritePoint(a[3], FakeNum(), FakeNum(), FakeNum());
        WritePoint(a[4], FakeNum(), FakeNum(), FakeNum());
    }
    return hit ? 1u : (gFakeRng() % 2 ? 0x100u : 0u); // only al is the bool
}
uint32_t WallSphere(Fake *, const uint32_t *a) { return WallAnswer(a, false); }
uint32_t WallSegment(Fake *, const uint32_t *a) { return WallAnswer(a, true); }

// The world DB's slot 52 (HasFloorOrGround): the tile logged by content (worldgrid's LoggedTile), the bytes.
uint32_t gFloorAnswer;
uint32_t HasFloorOrGround(Fake *, const uint32_t *a)
{
    const int32_t *t = worldgrid::LoggedTile(a);
    const uint32_t mask = a[1] & 0xff, visible = a[2] & 0xff;
    return (uint32_t(t[0] * 3 + t[1] * 5 + t[2]) + mask + visible + gFloorAnswer) % 4 == 0 ? 0u : 1u;
}

// The log with the world DB's tile pointers cleared and slot 52's byte arguments cut to their bytes.
void NormLog()
{
    for (size_t i = 0; i + 3 <= gLog.size();) {
        const uint32_t id = gLog[i], slot = gLog[i + 1], argc = gLog[i + 2];
        const bool tile = id == gWorldDb.id && worldgrid::TileSlot(slot);
        if (tile) gLog[i + 3] = 0;
        if (id == gWorldDb.id && slot == 52) {
            gLog[i + 4] &= 0xff;
            gLog[i + 5] &= 0xff;
        }
        i += 3 + argc + (tile ? 3 : 0);
    }
}

void BuildWorld(uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 11);
    worldgrid::ProgramWorld();
    worldgrid::RandomWorld(r);
    gWorld.rough = r() % 3 == 0;
    gWorldDb.Set(52, 3, 0, &HasFloorOrGround);
    gWorldDb.Set(100, 0, P(&gOutside));
    gFloorAnswer = r() % 4;
    static const uint32_t kOdds[] = {0, 30, 60, 100};
    gWallOdds = kOdds[r() % 4];
    gManager.Reset(200);
    gManager.Set(27, 2, 0, &SetNamed);
    gManager.Set(29, 2, 0, &SetNamed);
    gOutside.Reset(201);
    gOutside.Set(17, 1, 0, &OutsideTop);
    gOutside.Set(18, 1, 0, &OutsidePool);
    gPlacement.Reset(202);
    gPlacement.Set(40, 1, P(&gWalls));
    gWalls.Reset(203);
    gWalls.Set(19, 5, 0, &WallSegment);
    gWalls.Set(21, 5, 0, &WallSphere);
    gFakeRng.seed(seed * 31 + 7);
}

// ---- the objects ----

struct Class {
    uint32_t vt0, vt1;
    bool collider;
};
// The registrar's order: pinToFloor, pinToTops, pinToPools, wallsAndFloors, walls, floors.
const Class kClasses[6] = {{0xdbd310, 0xdbd324, false}, {0xdbd340, 0xdbd354, false}, {0xdbd370, 0xdbd384, false},
                           {0xdbd3bc, 0xdbd3e0, true},  {0xdbd3fc, 0xdbd420, true},  {0xdbd43c, 0xdbd460, true}};
uint32_t SizeOf(unsigned k) { return kClasses[k].collider ? 0x10 : 0xc; }

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
    if (Draw(3) == 0) { // the world's own space: no rotation, scale 1, a small translation
        t.flags = uint8_t(Draw(2) ? 0 : 4);
        t.pad[0] = t.pad[1] = t.pad[2] = 0;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) t.m[i][j] = i == j ? 1.0f : 0.0f;
        t.scale = 1.0f;
        t.translation = {Draw(2) ? 0.0f : Num(), Draw(2) ? 0.0f : Num(), Draw(2) ? 0.0f : Height()};
        return;
    }
    t.flags = uint8_t(Draw(4) == 0 ? gRng() : (Draw(2) ? 2 : 0));
    t.pad[0] = uint8_t(gRng());
    t.pad[1] = uint8_t(gRng());
    t.pad[2] = uint8_t(gRng());
    for (auto &row : t.m)
        for (float &v : row) v = Num();
    t.scale = Draw(3) == 0 ? 1.0f : Num();
    t.translation = {Coord(), Coord(), Height()};
}
void DrawPoint(Vector3 &p) { p = {Coord(), Coord(), Height()}; }
// The end of a move: often straight up or down from `from`, or a short step.
void DrawEnd(const Vector3 &from, Vector3 &to)
{
    switch (Draw(4)) {
    case 0: to = {from.x, from.y, Height()}; break;
    case 1: to = {from.x + Num() * 0.25f, from.y + Num() * 0.25f, from.z + Num()}; break;
    case 2: to = from; break;
    default: DrawPoint(to); break;
    }
}

void DumpBlock(Blob &b)
{
    const auto *words = reinterpret_cast<const uint32_t *>(gBlock);
    for (size_t i = 0; i < offsetof(Block, context) / 4; ++i) b.push_back(words[i]);
}

void DumpObject(Blob &b, const void *object, unsigned k)
{
    if (!object) {
        b.push_back(0xdead);
        return;
    }
    b.push_back(Norm(Word(object, 0)));
    b.push_back(Norm(Word(object, 4)));
    b.push_back(Word(object, 8));
    if (kClasses[k].collider) b.push_back(Word(object, 0xc));
}

void Op(Blob &b, void *object, unsigned k, uint32_t a)
{
    void *const second = At(object, 4);
    void *const faces[2] = {object, second};
    switch (a % 10) {
    case 0: {
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
    case 3: b.push_back(Slot(second, 4)); break; // RemoveRef
    case 4: b.push_back(Slot(second, 5)); break; // RefCount
    default: {
        DrawTransform(gBlock->transform);
        DrawPoint(gBlock->from);
        DrawEnd(gBlock->from, gBlock->to);
        gBlock->offset = {Draw(2) ? 0.0f : Num(), Draw(2) ? 0.0f : Num(), Draw(2) ? 0.0f : Num()};
        DrawPoint(gBlock->normal);
        for (uint8_t &c : gBlock->context) c = uint8_t(gRng());
        *reinterpret_cast<float *>(gBlock->context + 0x4c) = Extent(0.125f);
        float radius = Extent(0.25f);
        if (Draw(4) == 0) {
            // A rise that starts over the top level's ground (often over the ground of the level above it, the only place
            // the floors' rising branch sees a ceiling under `from`), with a negative distance.
            const float top = float(gWorld.maxLevel) * (gWorld.wall > 0.0f ? gWorld.wall : 0.0f) + 4.0f;
            gBlock->from.z = top + float(Draw(33)) * 0.25f;
            gBlock->to = {gBlock->from.x + float(int(Draw(9)) - 4) * 0.375f, gBlock->from.y + float(int(Draw(9)) - 4) * 0.375f,
                          gBlock->from.z + float(1 + Draw(32)) * 0.25f};
            radius = -float(1 + Draw(16)) * 0.125f;
        }
        if (!kClasses[k].collider) { // Apply
            Slot(object, 3, gRng(), &gBlock->transform, &gBlock->to, &gBlock->offset);
            break;
        }
        switch ((a >> 4) % 6) {
        case 0: b.push_back(Slot(object, 3)); break;
        case 1: Slot(object, 4, gRng(), gRng(), &gBlock->transform, &gBlock->copy); break;
        case 2: b.push_back(Slot(object, 6, gRng(), gRng(), gRng()) & 0xff); break;
        case 3: b.push_back(Slot(object, 7, &gBlock->from, &gBlock->to, Bits(radius), &gBlock->normal) & 0xff); break;
        default:
            b.push_back(Slot(object, 5, gBlock->context, &gBlock->transform, Bits(radius), &gBlock->from, &gBlock->to,
                             &gBlock->normal) &
                        0xff);
            break;
        }
        break;
    }
    }
    DumpBlock(b);
}

void CollectVTables()
{
    gVt.clear();
    const auto add = [](uint32_t game, const void *object, uint32_t offset) { gVt.push_back({game, Word(object, offset)}); };
    gManager.Reset(200);
    gManager.Set(27, 2, 0, &SetNamed);
    gManager.Set(29, 2, 0, &SetNamed);
    gObjectCount = 0;
    RegisterWorldObjects(reinterpret_cast<cITSEffectsManager *>(&gManager));
    gLog.clear();
    for (unsigned k = 0; k < 6; ++k) {
        add(kClasses[k].vt0, gObjects[k], 0);
        add(kClasses[k].vt1, gObjects[k], 4);
        Slot(At(gObjects[k], 4), 2); // count 0: Release deletes
    }
}

struct Scope {
    shape::CodePatch gameNew{0x824632, shape::CodeOf(&GameNew)};
    shape::CodePatch gameFree{0x82540f, shape::CodeOf(&GameFree)};
    worldfx::Scope cells;
    Scope()
    {
        cells.Set(worldfx::kWorldDbCell, &gWorldDb);
        cells.Set(0x10cc2b0, &gPlacement);
        CollectVTables();
        gBlock = new Block;
    }
    ~Scope()
    {
        delete gBlock;
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
    NormLog();
    b.insert(b.end(), gLog.begin(), gLog.end());
    gLog.clear();
}

bool Same(difftest::Context &ctx, const char *test, uint32_t seed, const Blob (&r)[2])
{
    ++ctx.cases;
    if (r[0] == r[1]) return true;
    size_t i = 0;
    while (i < r[0].size() && i < r[1].size() && r[0][i] == r[1][i]) ++i;
    ctx.Fail("%s seed %u: word %u differs (original %08x, ours %08x; sizes %u / %u)", test, seed, unsigned(i),
             i < r[0].size() ? r[0][i] : 0u, i < r[1].size() ? r[1][i] : 0u, unsigned(r[0].size()), unsigned(r[1].size()));
    return false;
}

void Seeds(difftest::Context &ctx, const char *label, uint32_t first, uint32_t count)
{
    for (uint32_t seed = first; seed < first + count; ++seed) {
        Blob r[2];
        for (int side = 0; side < 2; ++side) {
            Blob &b = r[side];
            BuildWorld(seed);
            gRng.seed(seed * 13 + 5);
            gSpecial = Draw(5) == 0;
            std::memset(gBlock, 0x5e, sizeof(Block));
            gObjectCount = 0;
            for (void *&o : gObjects) o = nullptr;
            gLog.clear();
            if (side == 0)
                ctx.Original<void(__cdecl *)(void *)>(0x3a3c30)(&gManager);
            else
                RegisterWorldObjects(reinterpret_cast<cITSEffectsManager *>(&gManager));
            b.push_back(gObjectCount);
            gRanges.clear();
            for (unsigned k = 0; k < 6; ++k)
                if (gObjects[k]) AddRange(gObjects[k], SizeOf(k), 0x7c000000 + 0x100 * k);
            AddRange(gBlock, sizeof(Block), 0x7d000000);
            for (unsigned k = 0; k < 6; ++k) DumpObject(b, gObjects[k], k);
            Finish(b);
            const unsigned k = Draw(6);
            void *object = gObjects[k];
            Slot(object, 1); // the effects manager's reference
            const unsigned n = 1 + Draw(24);
            for (unsigned i = 0; i < n; ++i) {
                Op(b, object, k, gRng());
                DumpObject(b, object, k);
                Finish(b);
            }
            for (unsigned j = 0; j < 6; ++j) {
                void *o = gObjects[j];
                if (j != k) { // the others never referenced: their count 0, one Release deletes
                    RecordDeleteAt(j, At(o, 4), 3, 6);
                    b.push_back(Slot(o, 2));
                } else if (Draw(3) == 0) { // the deleting destructor with flags 0 (through the +4 face's slot 3)
                    b.push_back(Norm(Slot(At(o, 4), 3, 0u)));
                    ::operator delete(o);
                } else {
                    RecordDeleteAt(j, At(o, 4), 3, 6);
                    while (Word(o, 8) > 1) Slot(At(o, 4), 2);
                    b.push_back(Slot(o, 2));
                }
                Finish(b);
            }
        }
        if (!Same(ctx, label, seed, r)) break;
    }
}

} // namespace wc_test

using namespace wc_test;

DIFF_TEST(WorldColliders_Objects, 0x3a30f0)
{
    // Also: 0x3a3c30 (the registrar), 0x3a2b00, 0x3a2d20, 0x3a2f80, 0x3a3410, 0x3a3430, 0x3a3490, 0x3a35e0, 0x3a39e0, and the
    // bases' bodies 0x3a3c20, 0x3a35b0, 0x1474a0 through our vtables (see the file comment).
    Scope scope;
    Seeds(ctx, "53-bit", 1, 12000);
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    Seeds(ctx, "24-bit", 100001, 6000);
}
