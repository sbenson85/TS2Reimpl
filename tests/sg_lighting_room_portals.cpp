// Differential tests for nTSSG::cLightingManager's room portals (lane 261109r, scenegraph LG4g;
// src/sg/cLightingManagerRoomPortals.cpp): the portal light's and record's constructors 0x3c37b0 / 0x2cada0, the node
// pass over one portal 0x2d5430 (AddRoomPortal) and the wall pass 0x2d50f0 (BuildRoomPortals). Hook shape: the original
// runs on the same manager image as ours.
//
// The world: the manager is 0x548 bytes of junk with a fake vtable (slot 36, the lot value: 100 mostly, 100 with junk
// upper bytes, 99, ...), mLitObject (+0x18, slot 11 fills the record's cPortalLight with drawn words), mTerrain (+0x1c,
// slot 72 the point test with junk upper bytes, 88 / 89 the lowest and highest levels), mWorld (+0x20, null now and
// then; slots 44 (a segment test), 55 / 56 (the walls, drawn), 79 (a quadrant's room)), mPortalLightOffset (+0x78) and
// the sky samples (+0x1f4, a std::vector<cSkySample> of 0..4 samples with spare capacity or none, each with drawn
// vectors, light references, lerp sets and 0..3 portal records referring to pool nodes). The portal (vbptr at +4, its
// virtual base at +0xc) fills a node range of up to six pool nodes (slot 44, the filter logged), gives a kind object
// (slot 120, whose slot 98 answers 8 or another word) and logs slot 124. The nodes (AddRef / Release, attributes 7 /
// 10, AddExtension 22, FindExtension 24 (a drawn answer; now and then it writes the extension and says no), AddChild
// 45, GetWorldTransform 54 (a drawn position near the side, now and then exactly at its start, and a drawn rotation),
// layer tag 81, light flag 91 as a byte, DropLightCopy 92, SetLight 93, GetLight 95) and the diffusion extensions
// (ResetFlags 0; a virtual base whose slot 2 logs the release and slot 15 hands out a clone) log their arguments. The
// sides (DrawSide) start at +0 / -0 now and then, with lengths whose squares sit next to the 1e-6 bound and in 1e-6..1e-5.
//
// Stand-ins on both sides: GroundHeightAt 0x7e4680 (a height per level rising 3 a level from -5 to 6, some levels
// raised or lowered, 0 from 7 up, -1e30 from -6 down, NaN now and then), WithLevelOfGround 0x7ddd90, the half-space
// light's constructor 0x3c7de0 (frees the block, returns a pool object whose +0x2c is a fake light that logs its name),
// the framework getter 0x1ae18d (slot 3 hands out pool nodes), cRZString::Sprintf 0x1ad6f7, and the two original-only
// callees 0x7e37b0 (the wall-side object: a pool object or null) and 0x7f2340 (a side's room: 0..9 with junk upper
// bytes; in half the cases the tag side's room is sometimes 0x7fff, 0x8000, 0x8001, 0xa5c3 or 0xffff, while the sample
// side's stays 0..9: a sample room of 0x8000 and up is the ORIGINAL-BUG of src and never drawn; see StandSideRoom). In
// 0x2d50f0's test AddRoomPortal is stood in too (it logs the portal and both sides). The wall-side objects log AddRef /
// Release and slot 9 (writes a drawn portal or null). EdgeNormal, SegmentToQuadrants, the rigid-transform helpers,
// 0x19d250 (the 2D dot) and 0x3a97d0 (the sample key: ours on our side) run as they are. 0x3a97d0 has its own test over
// every low word. The tag room and the sample room are different rooms, so the high tag rooms reach the spill's keyed tag
// (switch off), the plain layer tag (sign-extended) and record.mSample (both switch states), where 0x3a97d0 is not the
// identity (a negative int16 gives 0x8000); no mutant of AddRoomPortal or 0x3a97d0 survives (counts in the status).
//
// Compared: the log, the manager's bytes, the samples (size, capacity, their bytes, lights and lerp sets as ids, the
// records less their padding), the pools' reference counts, the globals the functions read (the refresh switch 0xffde64,
// the up vector 0xffd918, the zero vector, the grey, the portal light colour 0x100e4fc; drawn and restored after the
// test) and the heap blocks each side made; at the x87's 53-bit and 24-bit precision.
#include "code_patch.h"
#include "difftest.h"
#include "sg_lighting_fakes.h"

#include "hook/hooks.h"
#include "compgeom/CompGeom.h"
#include "misc/SmallVirtuals.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/cLightingManager.h"
#include "sg/nTSSGLights.h"
#include "world/WorldHeights.h"
#include "world/WorldTileMath.h"
#include "world/cWorldDB.h"

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <vector>

namespace {

using nTSSG::cLightingManager;
using nTSSG::cPortalLight;
using nTSSG::cPortalRecord;
using nTSSG::cSkySample;
using WallSide = cLightingManager::cWallSide;

// The shared fakes this test uses (tests/sg_lighting_fakes.h; the node, light and lerp names it defines itself stay its
// own): the framework, the half-space light constructor, the light's name, the lerp sets' references, Sprintf and the
// lot value.
using lightfx::FakeLerp;
using lightfx::FillShared;
using lightfx::GetFramework;
using lightfx::gIdOf;
using lightfx::gLerps;
using lightfx::gLerpVT;
using lightfx::gLightVT;
using lightfx::gLotSalt;
using lightfx::gMade;
using lightfx::gMake;
using lightfx::gManagerVT;
using lightfx::gNextMade;
using lightfx::kLerps;
using lightfx::kMade;
using lightfx::LotValues;
using lightfx::Made;
using lightfx::MakeHalfspace;
using lightfx::Plain;
using lightfx::StandSprintf;

// ---- fakes ----

constexpr int kNodes = 24;  // 0-11 under the portal, 12-23 handed out by the framework
constexpr int kObjects = 6; // the wall-side objects
constexpr int kPortals = 4;
constexpr int kExts = 6;
constexpr int kClones = 4;
constexpr int kKinds = 2;

struct FakeExt {
    void **vt;
    const int32_t *vbptr; // +4: the virtual base at +0xc
    uint32_t id;
    void **baseVt;        // +0xc
};
struct FakeNode {
    void **vt;
    uint32_t id, refs;
    float transform[7];
    uint32_t light;       // GetLight answers a dummy or null
    uint32_t find;        // bit 0: FindExtension's answer; bit 1: it writes the extension even when it says no
    FakeExt *ext;
    uint32_t attrs;       // bit 1: GetAttribute writes the colour; bits 0 / 2 pick the scalars' answers
    float one, color[3], value;
};
struct FakeKind {
    void **vt;
    uint32_t id, kind;
};
struct FakePortal {
    void **vt;
    const int32_t *vbptr; // +4: the virtual base at +0xc
    uint32_t id;
    void **baseVt;        // +0xc
    FakeKind *kind;
    FakeNode *items[6];
    uint32_t count;
    uint32_t rangeNulls;  // bit 0: no begin iterator, bit 1: no end iterator
};
struct FakeObject {
    void **vt;
    uint32_t id, refs;
    FakePortal *portal;   // slot 9's answer (or null)
};
struct FakeIterator {
    void **vt;
    uint32_t id;
    FakePortal *portal;
    uint32_t pos;
};

FakeNode gNodes[kNodes];
FakeObject gObjects[kObjects];
FakePortal gPortals[kPortals];
FakeExt gExts[kExts];
uint32_t gClones[kClones];
FakeKind gKinds[kKinds];
FakeIterator gIters[3];
uint32_t gDummyLight;
Plain gWorld, gTerrain, gLit;
void *gNodeVT[100], *gObjectVT[12], *gPortalVT[128], *gBaseVT[4], *gExtVT[4], *gExtBaseVT[20], *gKindVT[100],
    *gIterVT[8], *gWorldVT[84], *gTerrainVT[92], *gLitVT[16];
const int32_t kVbTable[2] = {-4, 8};
uint32_t gNextNode;

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (int i = 0; i < kNodes; ++i)
        if (p == &gNodes[i]) return 0x100 + i;
    for (int i = 0; i < kObjects; ++i)
        if (p == &gObjects[i]) return 0x200 + i;
    for (int i = 0; i < kPortals; ++i)
        if (p == &gPortals[i]) return 0x300 + i;
    for (int i = 0; i < kMade; ++i) {
        if (p == &gMade[i].light) return 0x400 + i;
        if (p == &gMade[i]) return 0x480 + i;
    }
    for (int i = 0; i < kExts; ++i)
        if (p == &gExts[i]) return 0x500 + i;
    for (int i = 0; i < kClones; ++i)
        if (p == &gClones[i]) return 0x580 + i;
    for (int i = 0; i < kLerps; ++i)
        if (p == &gLerps[i]) return 0x600 + i;
    for (int i = 0; i < 3; ++i)
        if (p == &gIters[i]) return 0x700 + i;
    for (int i = 0; i < kKinds; ++i)
        if (p == &gKinds[i]) return 0x800 + i;
    if (p == &gDummyLight) return 0x900;
    return 0xbad00000u | (P(p) & 0xfffff);
}

uint32_t EventKind(const void *event)
{
    const uint32_t vt = *static_cast<const uint32_t *>(event);
    if (vt == P(GameAddress<const void>(0xdb07f4))) return 1;
    if (vt == P(GameAddress<const void>(0xdb5e70))) return 2;
    return 0xbad;
}
constexpr uint32_t kAttrOne = 0xe9d24d51, kAttrColor = 0x29de3049, kAttrValue = 0x49e37821;

// the nodes
uint32_t __fastcall NodeAddRef(FakeNode *self, void *) { return L(1), L(Id(self)), ++self->refs; }
uint32_t __fastcall NodeRelease(FakeNode *self, void *) { return L(2), L(Id(self)), --self->refs; }
void __fastcall NodeSetAttribute(FakeNode *self, void *, uint32_t id, uint32_t a, uint32_t event)
{
    const auto *e = reinterpret_cast<const uint32_t *>(uintptr_t(event));
    L(7), L(Id(self)), L(id), L(a), L(EventKind(e)), L(e[1]);
    if (id == kAttrColor) L(e[2]), L(e[3]);
}
void __fastcall NodeGetAttribute(FakeNode *self, void *, uint32_t event, uint32_t id, uint32_t a)
{
    // The original leaves the scalar events' values unset (the node always writes them): only the colour's incoming
    // values are logged.
    auto *e = reinterpret_cast<uint32_t *>(uintptr_t(event));
    L(10), L(Id(self)), L(id), L(a), L(EventKind(e));
    if (id == kAttrColor) L(e[1]), L(e[2]), L(e[3]);
    switch (id) {
    case kAttrOne: e[1] = (self->attrs & 1) ? U(self->one) : U(self->value) ^ 0x80000000u; break;
    case kAttrColor:
        if (self->attrs & 2) e[1] = U(self->color[0]), e[2] = U(self->color[1]), e[3] = U(self->color[2]);
        break;
    case kAttrValue: e[1] = (self->attrs & 4) ? U(self->value) : U(self->one) ^ 0x80000000u; break;
    default: break;
    }
}
void __fastcall NodeAddExtension(FakeNode *self, void *, void *ext) { L(22), L(Id(self)), L(Id(ext)); }
uint32_t gFindJunk;
uint32_t __fastcall NodeFind(FakeNode *self, void *, uint32_t iid, void **out, void *start)
{
    L(24), L(Id(self)), L(iid), L(Id(*out)), L(Id(start));
    if (self->find & 3) *out = self->ext;
    return (gFindJunk & ~0xffu) | (self->find & 1);
}
void __fastcall NodeAddChild(FakeNode *self, void *, void *child) { L(45), L(Id(self)), L(Id(child)); }
const FakeNode *gPlacedNode; // the node whose transform was read last (StandSideRoom's facing)
uint32_t *__fastcall NodeTransform(FakeNode *self, void *, uint32_t *out)
{
    L(54), L(Id(self)), gPlacedNode = self;
    std::memcpy(out, self->transform, 0x1c);
    return out;
}
void __fastcall NodeSetLayerTag(FakeNode *self, void *, uint32_t v) { L(81), L(Id(self)), L(v); }
void __fastcall NodeSetLightFlag(FakeNode *self, void *, uint32_t v) { L(91), L(Id(self)), L(v & 0xff); }
// Dropping the light copy changes what the node answers next (the original reads the attributes before and after).
void __fastcall NodeDrop(FakeNode *self, void *)
{
    L(92), L(Id(self));
    self->one = self->one * 0.5f + 0.25f, self->value = -self->value;
    for (float &c : self->color) c = c * 0.75f + 0.125f;
    self->attrs ^= 2;
}
void __fastcall NodeSetLight(FakeNode *self, void *, const void *light) { L(93), L(Id(self)), L(Id(light)); }
void *__fastcall NodeGetLight(FakeNode *self, void *)
{
    L(95), L(Id(self));
    return self->light ? &gDummyLight : nullptr;
}

// the extensions
void __fastcall ExtReset(FakeExt *self, void *) { L(0x1000), L(Id(self)); }
void __fastcall ExtBaseRelease(uint8_t *base, void *) { L(0x1002), L(Id(base - 0xc)); }
void *__fastcall ExtBaseClone(uint8_t *base, void *)
{
    const FakeExt *const ext = reinterpret_cast<const FakeExt *>(base - 0xc);
    L(0x100f), L(Id(ext));
    return &gClones[(ext->id + gNextMade) % kClones];
}

// the portal, its kind object and the node iterators
void __fastcall PortalRange(FakePortal *self, void *, void **range, uint32_t a, const uint32_t *filter)
{
    L(0x302c), L(Id(self)), L(a);
    L(filter[0] == P(GameAddress<const void>(nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::kVTable))), L(filter[1]),
        L(filter[2]);
    gIters[0].portal = self, gIters[0].pos = 0;
    range[0] = &gIters[0];
    range[1] = (self->rangeNulls & 1) ? nullptr : &gIters[1];
    range[2] = (self->rangeNulls & 2) ? nullptr : &gIters[2];
}
FakeKind *__fastcall PortalKind(FakePortal *self, void *) { return L(0x3078), L(Id(self)), self->kind; }
void __fastcall Portal124(FakePortal *self, void *) { L(0x307c), L(Id(self)); }
void __fastcall PortalBaseRelease(uint8_t *base, void *) { L(0x3002), L(Id(base - 0xc)); }
uint32_t __fastcall KindValue(FakeKind *self, void *) { return L(0x8062), L(Id(self)), self->kind; }

uint32_t gEqualJunk;
uint32_t __fastcall IterEqual(FakeIterator *self, void *, const FakeIterator *other)
{
    L(0x704), L(Id(self)), L(self->pos), L(Id(other));
    const bool end = !self->portal || self->pos >= self->portal->count;
    return (gEqualJunk & ~0xffu) | (end ? 1u : 0u);
}
FakeNode *__fastcall IterGet(FakeIterator *self, void *)
{
    L(0x705), L(Id(self)), L(self->pos);
    return self->portal->items[self->pos];
}
void __fastcall IterNext(FakeIterator *self, void *) { L(0x706), L(Id(self)), L(self->pos++); }
void __fastcall IterRelease(FakeIterator *self, void *) { L(0x702), L(Id(self)), L(self->pos); }

// the wall-side objects
uint32_t __fastcall ObjAddRef(FakeObject *self, void *) { return L(0x201), L(Id(self)), ++self->refs; }
uint32_t __fastcall ObjRelease(FakeObject *self, void *) { return L(0x202), L(Id(self)), --self->refs; }
void __fastcall ObjPortal(FakeObject *self, void *, uint32_t kind, uint32_t iid, void **out)
{
    L(0x209), L(Id(self)), L(kind), L(iid), L(Id(*out));
    *out = self->portal;
}

// the framework hands out nodes 12-23, whatever the class
void *Make(uint32_t)
{
    FakeNode *const made = &gNodes[12 + gNextNode++ % 12];
    ++made->refs;
    return made;
}

// the manager's lot values
const uint32_t kLotValues[] = {100, 100, 100, 100, 100, 100, 0x10064, 0xffff0064u, 99, 0xff9c, 0, 0x6400};

// mWorld
uint32_t gWorldSalt, gWorldCalls, gWallCount;
float gWalls[8][6];
uint32_t __fastcall WorldSegment(Plain *, void *, const float *segment)
{
    L(0x4044), LF(segment, 6);
    const uint32_t h = Mix2(gWorldSalt, gWorldCalls++);
    return (h & ~0xffu) | (h % 8 ? 1u : 0u);
}
uint32_t __fastcall WorldWallCount(Plain *, void *) { return L(0x4055), gWallCount; }
uint32_t __fastcall WorldWall(Plain *, void *, float *out, uint32_t i)
{
    L(0x4056), L(i);
    std::memcpy(out, gWalls[i % 8], 24);
    return P(out);
}
uint32_t __fastcall WorldRoom(Plain *, void *, const worldmath::TileQuadrant *q, uint32_t a, uint32_t b)
{
    L(0x4079), L(uint32_t(q->x)), L(uint32_t(q->y)), L(uint32_t(q->level)), L(q->quadrant), L(a), L(b);
    const uint32_t h = Mix2(gWorldSalt ^ 0x79u, gWorldCalls++);
    return (h & 0xffff0000u) | (h % 6 ? (h >> 4) % 4 : (h >> 4) % 0x10000);
}

// mTerrain
uint32_t gTerrainSalt, gTerrainCalls;
int32_t gLowest, gHighest;
uint32_t __fastcall TerrainHas(Plain *, void *, float x, float y)
{
    L(0x5048), L(U(x)), L(U(y));
    const uint32_t h = Mix2(gTerrainSalt, gTerrainCalls++);
    return (h & ~0xffu) | (h % 8 ? 1u : 0u);
}
uint32_t __fastcall TerrainLowest(Plain *, void *) { return L(0x5058), uint32_t(gLowest); }
uint32_t __fastcall TerrainHighest(Plain *, void *) { return L(0x5059), uint32_t(gHighest); }

// mLitObject
uint32_t gLitSalt;
void __fastcall LitLight(Plain *, void *, void *portal, void *node, uint32_t *light)
{
    L(0x600b), L(Id(portal)), L(Id(node));
    for (uint32_t i = 0; i < sizeof(cPortalLight) / 4; ++i) light[i] = Mix2(gLitSalt, i);
    ++gLitSalt;
}

// GroundHeightAt: 3 a level from -5 to 6 plus a part of the point, 0 from 7 up, -1e30 from -6 down, NaN now and then;
// in a third of the cases a level's ground is raised or lowered (by up to 9 levels' worth), so the climbs go both ways
// and the descent runs several levels deep; in a seventh all the way down to -5.
uint32_t gGroundSalt;
double __cdecl StandGround(const nTSWorld::WorldPoint *p)
{
    L(0x7e46), L(U(p->x)), L(U(p->y)), L(uint32_t(p->level));
    const uint32_t h = Mix2(gGroundSalt, U(p->x) * 31u + U(p->y) * 7u + uint32_t(p->level));
    if (h % 61 == 0) return double(Fl(0x7fc00000u | (h & 0x803fffffu)));
    if (p->level >= 7) return h % 5 ? 0.0 : -0.5;
    if (p->level <= -6) return -1e30;
    double ground = double(p->level) * 3.0 + double(h % 1000) / 400.0 + double(h % 7) * 0x1p-30;
    if (gGroundSalt % 3 == 0) {
        const uint32_t k = Mix2(gGroundSalt ^ 0x51u, uint32_t(p->level));
        if (k % 3 == 0) ground += double(int32_t((k >> 8) % 19) - 9) * 3.0;
    }
    // In a seventh of the cases levels -4..6 are all above every node: the descent runs down to -5.
    if (gGroundSalt % 7 == 1 && p->level >= -4) ground += 40.0;
    return ground;
}
int32_t *__cdecl StandLevel(int32_t *out, const float *in)
{
    L(0x7ddd), LF(in, 3);
    out[0] = int32_t(Mix2(gGroundSalt, 1)), out[1] = int32_t(Mix2(gGroundSalt, 2)), out[2] = int32_t(Mix2(gGroundSalt, 3));
    return out;
}

// 0x7e37b0 and 0x7f2340 (original-only)
uint32_t gSideSalt, gSideCalls;
void *__cdecl StandSideObject(const float *side)
{
    L(0x7e37), LF(side, 6);
    const uint32_t h = Mix2(gSideSalt, gSideCalls++);
    return h % 4 == 0 ? nullptr : &gObjects[(h >> 3) % kObjects];
}
// The rooms: 0..9 with junk upper bytes; on the tag side of a node (the side whose room only tags, never indexes the
// samples) also 0x7fff, 0x8000, 0x8001, 0xa5c3 or 0xffff in half the cases (gHighTags). 0x2d5430 asks for the forward
// side's room, then the backward side's (gRoomCalls counts them: even is forward), after reading the node's transform
// (NodeTransform keeps the node in gPlacedNode); which side tags follows the sign of facing = normal . down, computed
// here as AddRoomPortal does it (ours EdgeNormal and RotateVector, on the node's transform and the up vector): a
// positive facing indexes by the backward room and tags with the forward one, a negative the other way round. A high
// room is drawn only when |facing| > 1e-3 (the sign is then certain) and on the tag side, so the sample-side room stays
// 0..9: a high sample room is the ORIGINAL-BUG of src (a negative index: a resize to a huge count or a write before
// the buffer, on both sides), never drawn.
const WallSide *gAddSide;
uint32_t gRoomCalls, gHighTags;
float Facing()
{
    if (!gAddSide || !gPlacedNode) return 0.0f;
    float normal[2];
    cLightingManager::EdgeNormal(normal, gAddSide->v);
    alignas(16) float transform[8] = {};
    std::memcpy(transform, gPlacedNode->transform, 0x1c);
    const float *const up = GameAddress<const float>(0xffd918); // the up vector (kUp)
    float down[3] = {-up[0], -up[1], -up[2]};
    reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(transform)->RotateVector(down);
    return normal[1] * down[1] + normal[0] * down[0];
}
uint32_t __cdecl StandSideRoom(const float *side, int32_t a)
{
    L(0x7f23), LF(side, 6), L(uint32_t(a));
    const uint32_t h = Mix2(gSideSalt ^ 0x7f23u, gSideCalls++);
    uint32_t room = h % 10 < 7 ? (h >> 4) % 5 : 5 + (h >> 4) % 5;
    const bool forward = gRoomCalls++ % 2 == 0;
    if (gHighTags && h % 3 != 0) {
        static const uint32_t kHigh[] = {0x8000, 0x8001, 0xffff, 0xa5c3, 0x7fff, 0x8000, 0xffff};
        const float facing = Facing();
        if (forward ? facing > 1e-3f : facing < -1e-3f) room = kHigh[(h >> 8) % std::size(kHigh)];
    }
    return (h & 0xffff0000u) | room;
}

// AddRoomPortal, in 0x2d50f0's test
void __fastcall StandAdd(void *self, void *, void *portal, WallSide side, WallSide other)
{
    L(0x5430), L(P(self)), L(Id(portal)), LF(side.v, 6), LF(other.v, 6);
}

void FillTables()
{
    FillShared();
    gIdOf = &Id, gMake = &Make;
    LotValues(kLotValues);
    gNodeVT[1] = reinterpret_cast<void *>(&NodeAddRef);
    gNodeVT[2] = reinterpret_cast<void *>(&NodeRelease);
    gNodeVT[7] = reinterpret_cast<void *>(&NodeSetAttribute);
    gNodeVT[10] = reinterpret_cast<void *>(&NodeGetAttribute);
    gNodeVT[22] = reinterpret_cast<void *>(&NodeAddExtension);
    gNodeVT[24] = reinterpret_cast<void *>(&NodeFind);
    gNodeVT[45] = reinterpret_cast<void *>(&NodeAddChild);
    gNodeVT[54] = reinterpret_cast<void *>(&NodeTransform);
    gNodeVT[81] = reinterpret_cast<void *>(&NodeSetLayerTag);
    gNodeVT[91] = reinterpret_cast<void *>(&NodeSetLightFlag);
    gNodeVT[92] = reinterpret_cast<void *>(&NodeDrop);
    gNodeVT[93] = reinterpret_cast<void *>(&NodeSetLight);
    gNodeVT[95] = reinterpret_cast<void *>(&NodeGetLight);
    gExtVT[0] = reinterpret_cast<void *>(&ExtReset);
    gExtBaseVT[2] = reinterpret_cast<void *>(&ExtBaseRelease);
    gExtBaseVT[15] = reinterpret_cast<void *>(&ExtBaseClone);
    gPortalVT[44] = reinterpret_cast<void *>(&PortalRange);
    gPortalVT[120] = reinterpret_cast<void *>(&PortalKind);
    gPortalVT[124] = reinterpret_cast<void *>(&Portal124);
    gBaseVT[2] = reinterpret_cast<void *>(&PortalBaseRelease);
    gKindVT[98] = reinterpret_cast<void *>(&KindValue);
    gIterVT[2] = reinterpret_cast<void *>(&IterRelease);
    gIterVT[4] = reinterpret_cast<void *>(&IterEqual);
    gIterVT[5] = reinterpret_cast<void *>(&IterGet);
    gIterVT[6] = reinterpret_cast<void *>(&IterNext);
    gObjectVT[1] = reinterpret_cast<void *>(&ObjAddRef);
    gObjectVT[2] = reinterpret_cast<void *>(&ObjRelease);
    gObjectVT[9] = reinterpret_cast<void *>(&ObjPortal);
    gWorldVT[44] = reinterpret_cast<void *>(&WorldSegment);
    gWorldVT[55] = reinterpret_cast<void *>(&WorldWallCount);
    gWorldVT[56] = reinterpret_cast<void *>(&WorldWall);
    gWorldVT[79] = reinterpret_cast<void *>(&WorldRoom);
    gTerrainVT[72] = reinterpret_cast<void *>(&TerrainHas);
    gTerrainVT[88] = reinterpret_cast<void *>(&TerrainLowest);
    gTerrainVT[89] = reinterpret_cast<void *>(&TerrainHighest);
    gLitVT[11] = reinterpret_cast<void *>(&LitLight);
    gWorld.vt = gWorldVT, gTerrain.vt = gTerrainVT, gLit.vt = gLitVT;
    for (int i = 0; i < 3; ++i) gIters[i] = {gIterVT, 0x700u + i, nullptr, 0};
}

// The game's globals the functions read, drawn per case and restored after the test.
constexpr uint32_t kRefreshSwitch = 0xffde64, kUp = 0xffd918, kZero = 0x10d9424, kGrey = 0xffd994, kLightColor = 0x100e4fc;

struct Globals : SavedGlobals {
    Globals()
    {
        Save(kRefreshSwitch, 1);
        Save(kUp, 12);
        Save(kZero, 12);
        Save(kGrey, 12);
        Save(kLightColor, 12);
    }
};

void DrawGlobals(std::mt19937 &gen)
{
    *GameAddress<uint8_t>(kRefreshSwitch) = gen() % 2 ? 0 : uint8_t(1 + gen() % 255);
    float *const up = GameAddress<float>(kUp);
    up[0] = 0.0f, up[1] = 0.0f, up[2] = 1.0f;
    if (gen() % 6 == 0) for (int k = 0; k < 3; ++k) up[k] = Wide(gen, 1.0f);
    float *const zero = GameAddress<float>(kZero);
    for (int k = 0; k < 3; ++k) zero[k] = gen() % 6 ? 0.0f : Wide(gen, 1.0f);
    float *const grey = GameAddress<float>(kGrey);
    for (int k = 0; k < 3; ++k) grey[k] = gen() % 4 ? 1.0f : Wide(gen, 1.0f);
    float *const color = GameAddress<float>(kLightColor);
    for (int k = 0; k < 3; ++k) color[k] = gen() % 4 ? 1.0f : Wide(gen, 1.0f);
}

// ---- the drawn world ----

alignas(16) uint8_t gManager[0x548];
std::vector<cSkySample> &Samples() { return *reinterpret_cast<std::vector<cSkySample> *>(gManager + 0x1f4); }
cLightingManager *Manager() { return reinterpret_cast<cLightingManager *>(gManager); }

// A wall side: a start on a half grid, a drawn direction and length (zero or tiny now and then), the levels 0..3 (junk
// now and then).
WallSide DrawSide(std::mt19937 &gen)
{
    static const float kDirs[][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {0.6f, -0.8f}, {-0.28f, 0.96f}};
    static const float kLengths[] = {1.0f, 2.0f, 0.5f, 3.0f, 1.0f, 0.0f, 1e-4f, 4.0f};
    // A side starting at +0 / -0 (now and then): its along vector is the drawn one to the last bit, so its squared length
    // (float, widened, against 1e-6) can sit next to the bound: 0.001f squares to just over 1e-6 and the float below it
    // to just under (adjacent lengths, the nearest squares on each side), more in 1e-6..1e-5. A -0.0 start with an
    // along of +0 or more is what a move by r = 0 (t exactly 0) would turn into +0.
    static const float kTiny[] = {std::nextafter(0.001f, 0.0f), 0.001f, std::nextafter(0.001f, 1.0f),
                                  std::nextafter(std::nextafter(0.001f, 0.0f), 0.0f), 0.00099f, 0.0012f, 0.002f,
                                  0.003f, 0.00316f, 1.0f, 2.0f};
    const bool nearZero = gen() % 6 == 0;
    const float *dir = kDirs[gen() % std::size(kDirs)];
    const float len = nearZero ? kTiny[gen() % std::size(kTiny)] : kLengths[gen() % std::size(kLengths)];
    WallSide s;
    s.v[0] = float(gen() % 40) * 0.5f, s.v[1] = float(gen() % 40) * 0.5f;
    if (nearZero) s.v[0] = gen() % 2 ? -0.0f : 0.0f, s.v[1] = gen() % 2 ? -0.0f : 0.0f;
    s.v[3] = s.v[0] + dir[0] * len, s.v[4] = s.v[1] + dir[1] * len;
    const int32_t level = gen() % 8 ? int32_t(gen() % 4) : int32_t(gen());
    std::memcpy(&s.v[2], &level, 4);
    const int32_t level1 = gen() % 6 ? level : int32_t(gen() % 4);
    std::memcpy(&s.v[5], &level1, 4);
    if (gen() % 16 == 0) s.v[gen() % 2] = NaN(gen);
    return s;
}
WallSide Reversed(const WallSide &s) { return {{s.v[3], s.v[4], s.v[5], s.v[0], s.v[1], s.v[2]}}; }

// A node's transform near the side: along it from -1.6 to 2.6 lengths, a little off it, 0..18 up (NaN and below 0 now
// and then); a drawn rotation (identity, quarter turns about x / y, a random axis, junk).
void DrawTransform(std::mt19937 &gen, float *t, const WallSide &side)
{
    const float along = float(int(gen() % 17) - 6) * 0.25f + float(gen() % 100) * 0.001f;
    const float off = (float(gen() % 61) - 30.0f) * 0.01f;
    const float dx = side.v[3] - side.v[0], dy = side.v[4] - side.v[1];
    t[0] = side.v[0] + along * dx - off * dy;
    t[1] = side.v[1] + along * dy + off * dx;
    // Now and then exactly at the side's start (its offset +0 / +0: the dot and t exactly 0).
    if (gen() % 6 == 0) t[0] = side.v[0], t[1] = side.v[1];
    t[2] = gen() % 12 ? float(gen() % 180) * 0.1f : (gen() % 2 ? -1.0f : 25.0f);
    if (gen() % 32 == 0) t[gen() % 3] = NaN(gen);
    switch (gen() % 8) {
    case 0: t[3] = 0, t[4] = 0, t[5] = 0, t[6] = 1; break;
    case 1: t[3] = 0.70710677f, t[4] = 0, t[5] = 0, t[6] = 0.70710677f; break;
    case 2: t[3] = 0, t[4] = -0.70710677f, t[5] = 0, t[6] = 0.70710677f; break;
    case 3: for (int k = 3; k < 7; ++k) t[k] = Wide(gen, 1.0f); break;
    default: {
        float ax = float(int(gen() % 201) - 100), ay = float(int(gen() % 201) - 100), az = float(int(gen() % 201) - 100);
        const float n = std::sqrt(ax * ax + ay * ay + az * az);
        if (n == 0.0f) ax = 1.0f, ay = 0.0f, az = 0.0f;
        else ax /= n, ay /= n, az /= n;
        const float half = float(gen() % 628) * 0.005f;
        const float s = std::sin(half);
        t[3] = ax * s, t[4] = ay * s, t[5] = az * s, t[6] = std::cos(half);
        break;
    }
    }
}

struct World {
    WallSide side, other;
    FakePortal *portal;
};

World Draw(uint32_t seed, int mode)
{
    std::mt19937 gen(seed * 2654435761u + 0x2d5430u + uint32_t(mode) * 0x1000000u);
    World w{};
    w.side = DrawSide(gen);
    w.other = gen() % 6 ? Reversed(w.side) : DrawSide(gen);
    gFindJunk = gen(), gEqualJunk = gen();
    // the pools
    for (int i = 0; i < kExts; ++i) gExts[i] = {gExtVT, kVbTable, 0x500u + i, gExtBaseVT};
    for (int i = 0; i < kKinds; ++i) gKinds[i] = {gKindVT, 0x800u + i, gen() % 2 ? 8u : (gen() % 2 ? 0x108u : 7u)};
    for (int i = 0; i < kNodes; ++i) {
        FakeNode &n = gNodes[i];
        n.vt = gNodeVT, n.id = 0x100u + i, n.refs = gen() % 4;
        DrawTransform(gen, n.transform, w.side);
        n.light = gen() % 3;
        n.find = gen() % 8 ? gen() % 2 : 2;
        n.ext = &gExts[gen() % kExts];
        n.attrs = gen() % 4 ? 7 : gen() % 8;
        n.one = Wide(gen, 2.0f), n.value = Wide(gen, 2.0f);
        for (float &c : n.color) c = Wide(gen, 2.0f);
    }
    for (int i = 0; i < kPortals; ++i) {
        FakePortal &p = gPortals[i];
        p.vt = gPortalVT, p.vbptr = kVbTable, p.id = 0x300u + i, p.baseVt = gBaseVT;
        p.kind = &gKinds[gen() % kKinds];
        for (auto &item : p.items) item = &gNodes[gen() % 12];
        p.count = gen() % 5 ? 1 + gen() % 6 : 0;
        p.rangeNulls = gen() % 8 ? 0 : 1 + gen() % 3;
    }
    for (int i = 0; i < kObjects; ++i) gObjects[i] = {gObjectVT, 0x200u + i, gen() % 4, gen() % 4 ? &gPortals[gen() % kPortals] : nullptr};
    for (int i = 0; i < kMade; ++i) {
        std::memset(gMade[i].head, 0xa5, sizeof gMade[i].head);
        gMade[i].light = {gLightVT, 0x400u + i};
    }
    for (int i = 0; i < kLerps; ++i) {
        gLerps[i].vt = gLerpVT;
        for (uint32_t &x : gLerps[i].words) x = gen();
    }
    for (int i = 0; i < 3; ++i) gIters[i].portal = nullptr, gIters[i].pos = 0;
    w.portal = &gPortals[gen() % kPortals];
    gNextNode = gNextMade = 0;
    gLotSalt = gen(), gWorldSalt = gen(), gWorldCalls = 0, gTerrainSalt = gen(), gTerrainCalls = 0;
    gLitSalt = gen(), gGroundSalt = gen(), gSideSalt = gen(), gSideCalls = 0, gRoomCalls = 0, gPlacedNode = nullptr, gAddSide = nullptr;
    static const int32_t kLowest[] = {0, 1, -1, 0, 2};
    static const int32_t kHighest[] = {3, 4, 5, 6, 2, 7};
    gLowest = kLowest[gen() % std::size(kLowest)], gHighest = kHighest[gen() % std::size(kHighest)];
    gWallCount = gen() % 6 ? gen() % 7 : 0;
    gHighTags = gen() % 2;
    for (auto &wall : gWalls) {
        const WallSide s = DrawSide(gen);
        std::memcpy(wall, s.v, 24);
    }
    // the manager
    for (auto &b : gManager) b = uint8_t(gen());
    PutWord(gManager, 0, P(gManagerVT));
    PutWord(gManager, 0x18, P(&gLit));
    PutWord(gManager, 0x1c, P(&gTerrain));
    PutWord(gManager, 0x20, gen() % 12 ? P(&gWorld) : 0);
    static const float kOffsets[] = {0.25f, 0.5f, 0.0f, -0.25f, 1.0f, 0.1f};
    PutFloat(gManager, 0x78, gen() % 6 ? kOffsets[gen() % std::size(kOffsets)] : Wide(gen, 1.0f));
    // the sky samples
    auto *const samples = new (gManager + 0x1f4) std::vector<cSkySample>();
    const uint32_t count = gen() % 5;
    samples->reserve(count + (gen() % 3 ? gen() % 4 : 0));
    DrawGlobals(gen); // the samples' constructor reads the zero vector
    for (uint32_t i = 0; i < count; ++i) {
        cSkySample &s = samples->emplace_back();
        auto *const bytes = reinterpret_cast<uint8_t *>(&s);
        for (uint32_t k = 0; k < 0x6c; k += 4) PutFloat(bytes, k, Wide(gen, 4.0f));
        const uint32_t lights = gen() % 3;
        s.mLights.resize(lights);
        for (auto &l : s.mLights) l.mObject = &gNodes[gen() % kNodes];
        s.mLerp78.mSet = gen() % 3 ? reinterpret_cast<nTSSG::cLerpLightSet *>(&gLerps[gen() % kLerps]) : nullptr;
        s.mLerp88.mSet = gen() % 3 ? reinterpret_cast<nTSSG::cLerpLightSet *>(&gLerps[gen() % kLerps]) : nullptr;
        const uint32_t records = gen() % 4;
        s.mPortals.reserve(records + (gen() % 2 ? gen() % 3 : 0));
        s.mPortals.resize(records);
        for (auto &r : s.mPortals) {
            auto *const rb = reinterpret_cast<uint8_t *>(&r);
            for (uint32_t k = 8; k < sizeof(cPortalRecord); k += 4) PutWord(rb, k, gen());
            r.mObject.mObject = &gNodes[gen() % kNodes];
            r.mOther.mObject = gen() % 2 ? &gNodes[gen() % kNodes] : nullptr;
        }
    }
    return w;
}

void AppendRecord(Blob &o, const cPortalRecord &r)
{
    uint8_t e[sizeof(cPortalRecord)];
    std::memcpy(e, &r, sizeof e);
    // the padding: neither the constructor nor the copy writes it (copies are member by member)
    std::memset(e + 0x0a, 0, 2);
    std::memset(e + 0x3d, 0, 3);
    std::memset(e + 0x60 + offsetof(cPortalLight, mPad25), 0, 3);
    std::memset(e + 0x60 + offsetof(cPortalLight, mPad6A), 0, 2);
    o.push_back(Id(r.mObject.mObject)), o.push_back(Id(r.mOther.mObject));
    for (uint32_t k = 8; k < sizeof e; k += 4) o.push_back(Word<uint32_t>(e, k));
}

Blob Snapshot(const Globals &globals, long long blocks, long long bytes)
{
    Blob o = gLog;
    o.push_back(0xeeee);
    for (uint32_t i = 0; i < sizeof gManager; i += 4) {
        if (i >= 0x1f4 && i < 0x200) continue;
        o.push_back(Word<uint32_t>(gManager, i));
    }
    const std::vector<cSkySample> &samples = Samples();
    o.push_back(uint32_t(samples.size())), o.push_back(uint32_t(samples.capacity()));
    for (const cSkySample &s : samples) {
        const auto *const raw = reinterpret_cast<const uint8_t *>(&s);
        for (uint32_t k = 0; k < 0x6c; k += 4) o.push_back(Word<uint32_t>(raw, k));
        o.push_back(uint32_t(s.mLights.size())), o.push_back(uint32_t(s.mLights.capacity()));
        for (const auto &l : s.mLights) o.push_back(Id(l.mObject));
        o.push_back(Id(s.mLerp78.mSet)), o.push_back(Id(s.mLerp88.mSet));
        o.push_back(uint32_t(s.mPortals.size())), o.push_back(uint32_t(s.mPortals.capacity()));
        for (const cPortalRecord &r : s.mPortals) AppendRecord(o, r);
    }
    for (const FakeNode &n : gNodes) o.push_back(n.refs);
    for (const FakeObject &b : gObjects) o.push_back(b.refs);
    o.push_back(gNextNode), o.push_back(gNextMade);
    globals.Append(o);
    o.push_back(uint32_t(blocks)), o.push_back(uint32_t(bytes));
    return o;
}

void FreeAll()
{
    Samples().~vector(); // releases (logged, after the snapshot) and frees
}

struct Stands {
    shape::StandIn ground{0x7e4680, &worldheights::GroundHeightAt, &StandGround};
    shape::StandIn level{0x7ddd90, &worldheights::WithLevelOfGround, &StandLevel};
    shape::StandIn halfspace{0x3c7de0, &cHalfspacePointLight_Constructor, &MakeHalfspace};
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &GetFramework};
    shape::StandIn sprintf{0x1ad6f7, &cRZString::Sprintf, &StandSprintf};
    shape::CodePatch sideObject{0x7e37b0u, reinterpret_cast<const void *>(&StandSideObject)};
    shape::CodePatch sideRoom{0x7f2340u, reinterpret_cast<const void *>(&StandSideRoom)};
};

struct Scope {
    svcfx::AllocatorScope allocator;
    Globals globals;
    Stands stands;
    Scope()
    {
        FillTables();
        gLog.reserve(1 << 14);
    }
};

template <class Call> void Run(difftest::Context &ctx, const Scope &scope, const char *what, uint32_t cases, int mode, Call call)
{
    SidesAtBothPrecisions(ctx, what, cases, true, [&](uint32_t draw, int side) {
        const World w = Draw(draw, mode);
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        difftest::AllocationCounter heap;
        call(side, w);
        heap.Stop();
        Blob outcome = Snapshot(scope.globals, heap.Blocks(), heap.Bytes());
        FreeAll();
        return outcome;
    });
}

// The constructors on drawn bytes, with the globals they read drawn.
template <class Call> void RunConstruct(difftest::Context &ctx, const char *what, uint32_t size, Call call)
{
    Globals globals;
    alignas(16) uint8_t object[sizeof(cPortalRecord)];
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 2654435761u + 0x2cada0u);
            for (auto &b : object) b = uint8_t(gen());
            DrawGlobals(gen);
            ctx.Mark(side ? "ours" : "original");
            const void *const back = call(side, object);
            Blob &o = outcome[side];
            o.push_back(back == object);
            for (uint32_t k = 0; k < size; k += 4) o.push_back(Word<uint32_t>(object, k));
        }
        ++ctx.cases;
        svcfx::Same(ctx, outcome[0], outcome[1], what, seed);
    }
}

} // namespace

// 0x3c37b0: a cPortalLight's defaults.
DIFF_TEST(sg_lighting_room_portals_light, 0x3c37b0)
{
    auto original = ctx.Original<void *(__thiscall *)(void *)>(0x3c37b0);
    RunConstruct(ctx, "portal light", sizeof(cPortalLight), [&](int side, uint8_t *object) -> const void * {
        if (side == 0) return original(object);
        return reinterpret_cast<cPortalLight *>(object)->Construct();
    });
}

// 0x2cada0: a cPortalRecord's defaults (and its light's).
DIFF_TEST(sg_lighting_room_portals_record, 0x2cada0)
{
    auto original = ctx.Original<void *(__thiscall *)(void *)>(0x2cada0);
    RunConstruct(ctx, "portal record", sizeof(cPortalRecord), [&](int side, uint8_t *object) -> const void * {
        if (side == 0) return original(object);
        return reinterpret_cast<cPortalRecord *>(object)->Construct();
    });
}

// 0x2d5430: one portal's nodes to records of the samples of the rooms they face, or dropped.
DIFF_TEST(sg_lighting_room_portals_add, 0x2d5430)
{
    Scope scope;
    using Add = void(__thiscall *)(void *, void *, WallSide, WallSide);
    auto original = ctx.Original<Add>(0x2d5430);
    auto ours = reinterpret_cast<Add>(MemberAddress<&cLightingManager::AddRoomPortal>());
    Run(ctx, scope, "add portal", 4000, 0, [&](int side, const World &w) {
        gAddSide = &w.side;
        if (side == 0) original(gManager, w.portal, w.side, w.other);
        else ours(gManager, w.portal, w.side, w.other);
    });
}

// 0x2d50f0: the walls between rooms of value 100 to their portal objects (AddRoomPortal stood in).
DIFF_TEST(sg_lighting_room_portals_build, 0x2d50f0)
{
    Scope scope;
    shape::StandIn add{0x2d5430, &cLightingManager::AddRoomPortal, &StandAdd};
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2d50f0);
    Run(ctx, scope, "room portals", 4000, 1, [&](int side, const World &) {
        if (side == 0) original(gManager);
        else Manager()->BuildRoomPortals();
    });
}

// 0x3a97d0: a sky sample's key, over every low word with six upper halves (zero, all ones, the sign bit, junk).
DIFF_TEST(sg_lighting_room_portals_key, 0x3a97d0)
{
    auto original = ctx.Original<int32_t(__cdecl *)(int32_t)>(0x3a97d0);
    static const uint32_t kUppers[] = {0x00000000u, 0xffff0000u, 0x80000000u, 0x7fff0000u, 0x12340000u, 0xa5c30000u};
    for (uint32_t low = 0; low < 0x10000; ++low) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side)
            for (const uint32_t upper : kUppers) {
                const int32_t sample = static_cast<int32_t>(upper | low);
                outcome[side].push_back(
                    static_cast<uint32_t>(side == 0 ? original(sample) : nTSSG::SkySampleKey(sample)));
            }
        ++ctx.cases;
        svcfx::Same(ctx, outcome[0], outcome[1], "sky sample key", low);
    }
}
