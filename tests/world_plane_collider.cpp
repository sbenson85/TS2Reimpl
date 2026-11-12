// Differential test for the effects' plane collider's own slots (lane YA, ran as RZ; src/world/WorldPlaneCollider.h):
// GetStateSize 0xbdf690, PrepareState 0xbded70, Collide 0xbdea30 and Draw 0xbdf420, each side called through a code
// address (the image's body / ours) with the slot's own arguments, `this` a dummy (no slot reads it), and the stack
// balance checked.
//
// WorldPlaneCollider_Slots: per seed a parallelogram (origin and two edges: small grid values, random ones, parallel or
// zero edges, diagonal walls, specials, one seed in eight NaNs of both signs in half the inputs; one in five a floor whose
// edge leans by a hair, met by straight-down moves at its edge and by moves at the -1e-5 threshold), the transform it is
// placed by and the placement copied for Draw (each rotated or not, random pads and flags), PrepareState into a buffer under sentinels (compared bit for bit, the sentinels too); then moves
// against that state: through the parallelogram's inside or just outside it from in front, along it, from behind, and
// random ones, with random sizes (the context's float at +0x4c) and radii; compared: al, `to` and `normal` (under
// sentinels). Every eighth seed Draw against worldfx fakes: the framework (the static getter 0x1ae18d stood in on both
// sides) whose slot 3 hands out a geometry node and an indexed mesh builder (its virtual base cIGeometryBuilder a
// fourth fake, reached through the vbptr at +4); the log holds each call with pointer arguments as content (the points'
// bits, the group name's characters). Runs at the harness's 53-bit x87 precision and again at the game's 24-bit.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "worldfx.h"

#include "hook/hooks.h"
#include "rz/StaticGetters.h"
#include "world/WorldPlaneCollider.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace wp_test {

using Blob = std::vector<uint32_t>;
using namespace world_plane;
using worldfx::Fake;
using worldfx::gLog;

uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
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
template <class... A> uint32_t Call(const void *fn, A... a)
{
    static_assert(((sizeof(A) == 4) && ...), "dword arguments only");
    static uint32_t dummy[4];
    const uint32_t args[sizeof...(A) + 1] = {(uint32_t)(uintptr_t)a..., 0};
    return CallChecked(fn, dummy, args, uint32_t(sizeof...(A)));
}

// ---- values ----

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
// The default quiet NaN, either sign (built from bits: multiplying a NaN by -1 keeps its sign).
float QuietNaN(bool negative)
{
    const uint32_t bits = negative ? 0xffc00000u : 0x7fc00000u;
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

float Num()
{
    switch (Draw(14)) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return 1.0f;
    case 3: return float(int(Draw(9)) - 4);
    case 4: return float(int(Draw(2001)) - 1000) / 64.0f;
    case 5: return float(Draw(400)) / 100.0f;
    case 6: return Draw(8) ? 0.5f : QuietNaN(Draw(2) != 0);
    case 7: return Draw(3) ? float(int(Draw(5)) - 2) : (Draw(2) ? 1.0f : -1.0f) * std::numeric_limits<float>::infinity();
    case 8: return float(int(Draw(9)) - 4) * std::numeric_limits<float>::denorm_min();
    case 9: return (Draw(2) ? 1.0f : -1.0f) * float(1 + Draw(4)) * 1e38f;
    case 10: return float(int(Draw(9)) - 4) * 1e-4f;
    default: return (float(gRng()) / 4294967296.0f - 0.5f) * 8.0f;
    }
}
// Mostly ordinary: specials one time in eight.
float Mild() { return Draw(8) == 0 ? Num() : (float(gRng()) / 4294967296.0f - 0.5f) * 6.0f; }

void DrawVector(Vector3 &v, bool mild)
{
    if (mild) v = {Mild(), Mild(), Mild()};
    else v = {Num(), Num(), Num()};
}

void DrawTransform(EffectsTransform &t)
{
    const uint32_t kind = Draw(6);
    t.flags = uint8_t(kind == 0 ? gRng() : (Draw(2) ? EffectsTransform::kRotated : 0));
    for (uint8_t &p : t.pad) p = uint8_t(gRng());
    if (kind == 1) { // identity
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) t.m[i][j] = i == j ? 1.0f : 0.0f;
        t.scale = 1.0f;
        t.translation = {0.0f, 0.0f, 0.0f};
        return;
    }
    if (kind == 2) { // a rotation about z
        const float a = float(Draw(64)) * 0.09817477f;
        const float c = std::cos(a), s = std::sin(a);
        const float rows[3][3] = {{c, -s, 0.0f}, {s, c, 0.0f}, {0.0f, 0.0f, 1.0f}};
        std::memcpy(t.m, rows, sizeof(rows));
    } else {
        const bool mild = kind != 3;
        for (auto &row : t.m)
            for (float &v : row) v = mild ? Mild() : Num();
    }
    t.scale = Draw(3) == 0 ? 1.0f : (Draw(4) ? Mild() : Num());
    DrawVector(t.translation, Draw(4) != 0);
}

void DrawParams(PlaneParams &p)
{
    for (uint8_t &u : p.unknown) u = uint8_t(gRng());
    switch (Draw(8)) {
    case 0: // specials anywhere
        DrawVector(p.origin, false);
        DrawVector(p.edgeA, false);
        DrawVector(p.edgeB, false);
        return;
    case 1: // parallel edges
        DrawVector(p.origin, true);
        DrawVector(p.edgeA, true);
        {
            const float k = float(int(Draw(9)) - 4) * 0.5f;
            p.edgeB = {p.edgeA.x * k, p.edgeA.y * k, p.edgeA.z * k};
        }
        return;
    case 2: // a floor-like or wall-like square on the grid
        p.origin = {float(int(Draw(9)) - 4), float(int(Draw(9)) - 4), float(int(Draw(9)) - 4)};
        p.edgeA = {float(int(Draw(5)) - 2), 0.0f, float(int(Draw(3)) - 1)};
        p.edgeB = {0.0f, float(int(Draw(5)) - 2), Draw(2) ? 0.0f : 1.0f};
        if (Draw(3) == 0) { // a diagonal wall: the normal's x and y tie
            const float k = float(1 + Draw(2));
            p.edgeA = {k, Draw(2) ? k : -k, 0.0f};
            p.edgeB = {0.0f, 0.0f, float(1 + Draw(2))};
        }
        return;
    case 3: // tiny edges (the length test at 1e-6)
        DrawVector(p.origin, true);
        p.edgeA = {float(int(Draw(5)) - 2) * 1e-3f, float(int(Draw(5)) - 2) * 1e-3f, float(int(Draw(5)) - 2) * 1e-3f};
        p.edgeB = {float(int(Draw(5)) - 2) * 1e-3f, float(int(Draw(5)) - 2) * 1e-3f, float(int(Draw(5)) - 2) * 1e-3f};
        return;
    default:
        DrawVector(p.origin, true);
        DrawVector(p.edgeA, true);
        DrawVector(p.edgeB, true);
        return;
    }
}

// A move against `s`: through a point of the parallelogram's plane at (u, v) in its corners' frame, from height h to -k
// along the normal, sideways, or random.
void DrawMove(const PlaneState &s, Vector3 &from, Vector3 &to)
{
    const uint32_t kind = Draw(8);
    if (kind == 0) {
        DrawVector(from, false);
        DrawVector(to, false);
        return;
    }
    if (kind == 1) {
        DrawVector(from, true);
        DrawVector(to, true);
        return;
    }
    const float u = float(int(Draw(29)) - 4) / 20.0f, v = float(int(Draw(29)) - 4) / 20.0f;
    const Vector3 &c0 = s.corner[0], &c1 = s.corner[1], &c3 = s.corner[3];
    const Vector3 p{c0.x + (c1.x - c0.x) * u + (c3.x - c0.x) * v, c0.y + (c1.y - c0.y) * u + (c3.y - c0.y) * v,
                    c0.z + (c1.z - c0.z) * u + (c3.z - c0.z) * v};
    const Vector3 &n = s.normal;
    float h = float(Draw(17)) * 0.25f, k = float(Draw(17)) * 0.25f;
    if (kind == 2) std::swap(h, k);             // from behind
    if (kind == 3) h = float(Draw(5)) * 0.0625f; // close to it
    const float sx = kind == 4 ? Mild() * 0.25f : 0.0f, sy = kind == 4 ? Mild() * 0.25f : 0.0f;
    from = {p.x + n.x * h + sx, p.y + n.y * h + sy, p.z + n.z * h};
    to = {p.x - n.x * k, p.y - n.y * k, p.z - n.z * k};
    if (kind == 5) to = {from.x + Mild(), from.y + Mild(), from.z}; // along it (roughly)
}

// A floor-like parallelogram whose first edge leans by a hair (s, 1e-8 to 1e-6) off the x axis, placed by the identity:
// its normal is exactly +z, so straight-down moves keep their x and y bits, and the edge's dv is under 1e-6.
void DrawSkewed(PlaneParams &p, EffectsTransform &t, float &s)
{
    s = (Draw(2) ? 1.0f : -1.0f) * float(1 + Draw(100)) * 1e-8f;
    p.origin = {float(int(Draw(9)) - 4), float(int(Draw(9)) - 4), float(int(Draw(5)) - 2)};
    p.edgeA = {float(1 << Draw(3)), s, 0.0f};
    p.edgeB = {0.0f, float(1 + Draw(2)), 0.0f};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) t.m[i][j] = i == j ? 1.0f : 0.0f;
    t.flags = uint8_t(Draw(2) ? EffectsTransform::kRotated : 0);
    t.scale = 1.0f;
    t.translation = {0.0f, 0.0f, 0.0f};
}
// Straight down onto the skewed edge, at a fraction of it (its first corner included), a few hairs either side of it; or
// a move whose dot with the normal is the threshold -1e-5 (or one step either side).
void DrawSkewedMove(const PlaneState &st, float s, Vector3 &from, Vector3 &to)
{
    const Vector3 &c0 = st.corner[0], &c1 = st.corner[1];
    if (Draw(4) == 0) {
        float dz = -1e-5f;
        if (const uint32_t k = Draw(3)) dz = std::nextafter(dz, k == 1 ? 0.0f : -1.0f);
        from = {c0.x + 0.5f, c0.y + 0.5f, 0.0f};
        to = {from.x, from.y, dz};
        return;
    }
    const float f = float(Draw(5)) * 0.25f;
    const float x = Draw(4) ? c0.x + (c1.x - c0.x) * f : c0.x;
    const float y = c0.y + s * f + s * float(int(Draw(41)) - 20) / 16.0f;
    from = {x, y, c0.z + float(1 + Draw(4))};
    to = {x, y, c0.z - float(1 + Draw(4))};
}

// ---- Draw's fakes ----

Fake gCom, gNode, gMesh, gBase;
int32_t gVbtable[2];
constexpr uint32_t kNode = 2, kMesh = 3, kBase = 4;

uint32_t StandClassObject(Fake *, const uint32_t *a)
{
    void **out = reinterpret_cast<void **>(uintptr_t(a[2]));
    gLog.back() = out ? reinterpret_cast<uintptr_t>(*out) == 0 : 0xbad; // the pointer differs; what it held does not
    if (a[0] == 0x7ba3838c) *out = &gNode;
    else if (a[0] == 0x9bffc10d) *out = &gMesh;
    else *out = nullptr;
    return 1;
}
uint32_t StandPosition(Fake *, const uint32_t *a)
{
    const float *v = reinterpret_cast<const float *>(uintptr_t(a[0]));
    gLog.back() = 0x9051;
    for (int i = 0; i < 3; ++i) gLog.push_back(Bits(v[i]));
    return 0;
}
uint32_t StandName(Fake *, const uint32_t *a)
{
    const char *name = reinterpret_cast<const char *>(uintptr_t(a[0]));
    gLog.back() = 0x4e41;
    for (const char *c = name; *c; ++c) gLog.push_back(uint8_t(*c));
    return 0;
}
void *StandFramework()
{
    gLog.push_back(0xf4a3e);
    return &gCom;
}

void ProgramFakes()
{
    gCom.Reset(1);
    gCom.Set(3, 3, 0, &StandClassObject); // GetClassObject(classId, iid, out)
    gNode.Reset(kNode);
    gNode.Set(1, 0, 2); // AddRef
    gNode.Set(2, 0, 1); // Release
    gNode.Set(32, 1);   // AddPart(cIGeometryBuilder *)
    gMesh.Reset(kMesh);
    gMesh.Set(0, 1);                    // Reset(bool)
    gMesh.Set(1, 0);                    // Finish
    gMesh.Set(9, 1, 0, &StandPosition); // AddPosition(const float *)
    gMesh.Set(27, 1, 0, &StandName);    // SetGroupName(const char *)
    gMesh.Set(28, 0);                   // MarkUnknown
    gMesh.Set(29, 0);                   // CommitGroup
    gMesh.Set(35, 1);                   // AddPositionIndex(uint32_t)
    gBase.Reset(kBase);
    gBase.Set(1, 0, 2); // AddRef
    gBase.Set(2, 0, 1); // Release
    worldfx::SetVirtualBase(gMesh, gBase, gVbtable);
}

// ---- the run ----

struct Fns {
    const void *size, *prepare, *collide, *draw;
};

void Run(Blob &b, const Fns &f, uint32_t seed)
{
    gRng.seed(seed * 2654435761u + 0xbded70);
    PlaneParams params;
    EffectsTransform transform, placement;
    DrawParams(params);
    DrawTransform(transform);
    DrawTransform(placement);
    float skew = 0.0f;
    const bool skewed = Draw(5) == 0;
    if (skewed) DrawSkewed(params, transform, skew);
    else if (Draw(8) == 0) { // NaNs of either sign in half the inputs (which operand's NaN an operation keeps)
        auto storm = [](float &v) {
            if (Draw(2)) v = QuietNaN(Draw(2) != 0);
        };
        for (Vector3 *v : {&params.origin, &params.edgeA, &params.edgeB, &transform.translation, &placement.translation}) {
            storm(v->x);
            storm(v->y);
            storm(v->z);
        }
        for (EffectsTransform *t : {&transform, &placement}) {
            for (auto &row : t->m)
                for (float &v : row) storm(v);
            storm(t->scale);
        }
    }

    gStackSkew = 0;
    b.push_back(Call(f.size));
    struct {
        uint32_t before[2];
        PlaneState state;
        uint32_t after[2];
    } box;
    std::memset(&box, 0xcd, sizeof(box));
    Call(f.prepare, &params, &transform, &placement, &box.state);
    const auto *words = reinterpret_cast<const uint32_t *>(&box);
    for (size_t i = 0; i < sizeof(box) / 4; ++i) b.push_back(words[i]);

    for (int m = 0; m < 12; ++m) {
        uint8_t context[0x50];
        for (uint8_t &c : context) c = uint8_t(gRng());
        const float size = Draw(3) ? float(Draw(9)) * 0.125f : Num();
        std::memcpy(context + 0x4c, &size, 4);
        const float radius = Draw(4) ? float(Draw(9)) * 0.25f : Num();
        Vector3 from, to, normal{std::numeric_limits<float>::quiet_NaN(), -7.0f, 7.0f};
        if (skewed && Draw(4) != 0) DrawSkewedMove(box.state, skew, from, to);
        else DrawMove(box.state, from, to);
        b.push_back(Call(f.collide, context, &box.state, Bits(radius), &from, &to, &normal) & 0xff);
        for (const float *v : {&from.x, &to.x, &normal.x})
            for (int i = 0; i < 3; ++i) b.push_back(Bits(v[i]));
    }

    if (seed % 8 == 0) {
        ProgramFakes();
        gLog.clear();
        void *out = &box; // overwritten
        b.push_back(Call(f.draw, gRng(), &box.state, &out) & 0xff);
        b.push_back(out == &gNode);
        b.insert(b.end(), gLog.begin(), gLog.end());
    }
    b.push_back(uint32_t(gStackSkew));
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

void Seeds(difftest::Context &ctx, const char *label, const Fns (&sides)[2], uint32_t first, uint32_t count)
{
    for (uint32_t seed = first; seed < first + count; ++seed) {
        Blob r[2];
        for (int side = 0; side < 2; ++side) Run(r[side], sides[side], seed);
        if (!Same(ctx, label, seed, r)) break;
    }
}

} // namespace wp_test

DIFF_TEST(WorldPlaneCollider_Slots, 0xbded70)
{
    using namespace wp_test;
    shape::StandIn framework(0x1ae18d, &GetDirectorFrameworkObject, &StandFramework);
    const auto image = [&](uint32_t rva) { return reinterpret_cast<const void *>(ctx.Original<void(__cdecl *)()>(rva)); };
    const Fns sides[2] = {
        {image(0xbdf690), image(0xbded70), image(0xbdea30), image(0xbdf420)},
        {MemberAddress<&cTSPlaneCollider::GetStateSize>(), MemberAddress<&cTSPlaneCollider::PrepareState>(),
         MemberAddress<&cTSPlaneCollider::Collide>(), MemberAddress<&cTSPlaneCollider::Draw>()},
    };
    Seeds(ctx, "53-bit", sides, 0, 3000);
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    Seeds(ctx, "24-bit", sides, 3000, 3000);
}
