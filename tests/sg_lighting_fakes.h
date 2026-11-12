// The lighting builders' shared fakes (scenegraph LG4; lane 261109s), in namespace lightfx inside an anonymous namespace
// (one copy per including test file). On tests/sg_lighting_common.h. Each fake logs a tag and its arguments to gLog, with
// references as ids through gIdOf (the test's own id function: set it before the first call).
// fakes: framework 0x1ae18d (slot 3 GetClassObject through gMake), name setter 0xae78f0, direction setter, scene
// manager (slot 7), lot value (manager slot 36 over gLotValues), FakeRef nodes (1/2, 78, 80, 81, 91, 93, 95), lights
// (0, 1/2, 40-43, 51, 52) and texture lights (1/2, 41), the light constructors 0x3c7fd0 / 0x3c8120 / 0x3c7de0 (gMade),
// the lerp sets (pool gLerps, Construct 0x3c0ec0 into gNewLerps, SetKeys, ClearActive, FadeOutUnused, AddLight,
// AddDirectional, AddExisting, AddPoint), cRZString::Sprintf 0x1ad6f7; for the shadow texture and the rebuilds (LG4h,
// tests/sg_lighting_shadow.cpp): the system 0x20defb (slot 40 the terrain), the terrain's grid (slots 7 size, 8 point,
// 9 normal), images by COM class 0x1c4a276c (1/2, 6 create, 9 lock, 18 fill, 28 pitch), the ambient maps (FakeMap: 26,
// 27, 38), the texture handle 0xae8730 and its face, the cached object 0xae8900, the manager's builders as stand-ins
// (Builders), the stopwatches (Stopwatches: scripted Elapsed readings), the scene flag setter 0xaeeea0 and the scene
// manager's slot 17 (tests/sg_lighting_step.cpp); for the lifecycle (LG3b, tests/sg_lighting_lifecycle.cpp; in
// lightfx::life): the terrain view (77, 78, 87 x87 floats; 83, 84), the parser (8 SetDirty), the lit object (6, 7), the
// system's slot 48, the message server 0x9c5a89 (4 MessagePost), RZMemory::Malloc / Free (RzHeap), EvaluateAmbient /
// EvaluateAmbientShaded (Ambients), the option lists 0x1d0c60 and cRZFile::PathExists (PathServices).
#pragma once

#include "sg_lighting_common.h"

#include "code_patch.h"

#include "app/AppInit.h"
#include "gz/cGZAny.h"
#include "hook/hooks.h"
#include "misc/UtilitiesE.h"
#include "rz/RZMemory.h"
#include "rz/RZMessageServer.h"
#include "rz/StaticGetters.h"
#include "rz/cRZFile.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZString.h"
#include "sg/SGUtilities.h"
#include "sg/cLerpLightSet.h"
#include "sg/cLightingManager.h"
#include "sg/cTextureManager.h"
#include "sg/nTSSGLights.h"

#include <cstdarg>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <list>
#include <new>

namespace {
namespace lightfx {

using nTSSG::cLerpLightSet;
using nTSSG::cLightingManager;

// ---- ids ----

uint32_t (*gIdOf)(const void *);
uint32_t IdOf(const void *p) { return gIdOf ? gIdOf(p) : P(p); }

struct Plain {
    void **vt;
};

// ---- nodes, lights and texture lights (one struct for the three) ----

struct FakeRef {
    void **vt;
    uint32_t id, refs;
    float rgb[3];
    FakeRef *light;     // a node's light (GetLight)
    FakeRef *found;     // a light's standard face (QueryInterface)
    uint32_t answers;   // bit 0: QueryInterface says yes; bit 2: it writes the face; the rest junk in its answer
    float intensity;    // GetIntensity
    float other[3];     // GetColor's answer when `elsewhere`
    uint32_t elsewhere;
};
void *gNodeVT[100], *gLightVT[60], *gTextureVT[50];

// A test's look at its world whenever a reference is counted or released (what the caller had stored by then).
void (*gOnRef)();
void OnRef()
{
    if (gOnRef) gOnRef();
}
uint32_t __fastcall AddRef(FakeRef *self, void *) { return L(1), L(IdOf(self)), OnRef(), ++self->refs; }
uint32_t __fastcall Release(FakeRef *self, void *) { return L(2), L(IdOf(self)), OnRef(), --self->refs; }
void __fastcall NodeAddLayerBits(FakeRef *n, void *, uint32_t v) { L(78), L(IdOf(n)), L(v); }
void __fastcall NodeRemoveLayerBits(FakeRef *n, void *, uint32_t v) { L(80), L(IdOf(n)), L(v); }
void __fastcall NodeSetLayerTag(FakeRef *n, void *, uint32_t v) { L(81), L(IdOf(n)), L(v); }
void __fastcall NodeSetLightFlag(FakeRef *n, void *, uint32_t v) { L(91), L(IdOf(n)), L(v & 0xff); }
void __fastcall NodeSetLight(FakeRef *n, void *, const void *light) { L(93), L(IdOf(n)), L(IdOf(light)); }
FakeRef *__fastcall NodeGetLight(FakeRef *n, void *) { return L(95), L(IdOf(n)), n->light; }

uint32_t __fastcall LightQuery(FakeRef *l, void *, uint32_t iid, void **out)
{
    L(0), L(IdOf(l)), L(iid), L(IdOf(*out));
    if (l->answers & 5) {
        *out = l->found;
        if (l->found) ++l->found->refs;
    }
    return (l->answers & ~0xffu) | (l->answers & 1);
}
void __fastcall LightSetName(FakeRef *l, void *, const char *name) { L(40), L(IdOf(l)), L(Hash(name)); }
void __fastcall LightSetColor(FakeRef *l, void *, const float *rgb)
{
    L(41), L(IdOf(l)), LF(rgb, 3);
    std::memcpy(l->rgb, rgb, 12);
}
void __fastcall LightSetIntensity(FakeRef *l, void *, float v)
{
    L(42), L(IdOf(l)), L(U(v));
    l->intensity = v;
}
void __fastcall LightSetFactor(FakeRef *l, void *, float v) { L(43), L(IdOf(l)), L(U(v)); }
float *__fastcall LightGetColor(FakeRef *l, void *, float *out)
{
    L(51), L(IdOf(l));
    std::memcpy(out, l->rgb, 12);
    return l->elsewhere ? l->other : out;
}
float __fastcall LightGetIntensity(FakeRef *l, void *) { return L(52), L(IdOf(l)), l->intensity; }
void __fastcall TextureSetColor(FakeRef *l, void *, uint32_t index, const float *rgba)
{
    L(0x341), L(IdOf(l)), L(index), LF(rgba, 4);
    std::memcpy(l->rgb, rgba + 1, 12);
}

// ---- the framework, the scene manager, the name and direction setters, the lot value ----

// The test's maker for a class id: an object with one more reference, or null.
void *(*gMake)(uint32_t cls);
Plain gCom, gSceneManager;
void *gComVT[8], *gSceneManagerVT[20], *gManagerVT[48];

uint32_t __fastcall ComGetClassObject(Plain *, void *, uint32_t cls, uint32_t iid, void **out)
{
    L(0xc0), L(cls), L(iid), L(IdOf(*out));
    void *const made = gMake ? gMake(cls) : nullptr;
    *out = made;
    return made ? 1 : 0;
}
void *__cdecl GetFramework() { return L(0xf0), &gCom; }
void __fastcall SceneAdd(Plain *, void *, const void *node) { L(0x307), L(IdOf(node)); }
void __cdecl SetName(const void *node, const char *name) { L(0xae7), L(IdOf(node)), L(Hash(name)); }
void __cdecl SetDirection(const void *node, const float *d) { L(0xaee), L(IdOf(node)), LF(d, 3); }

// The manager's lot value (slot 36): one of the test's values, picked by the room and gLotSalt.
uint32_t gLotSalt;
const uint32_t *gLotValues;
size_t gLotValueCount;
template <size_t N> void LotValues(const uint32_t (&values)[N]) { gLotValues = values, gLotValueCount = N; }
uint32_t __fastcall LotValue(void *self, void *, uint32_t room)
{
    L(0x2d94), L(P(self)), L(room);
    return gLotValues[Mix2(gLotSalt, room) % gLotValueCount];
}

// ---- the light constructors: the block freed, a pool object returned whose +0x2c is a light ----

constexpr int kMade = 8;
struct Made {
    uint8_t head[0x2c];
    FakeRef light;
};
static_assert(offsetof(Made, light) == 0x2c, "Made");
Made gMade[kMade];
uint32_t gNextMade;

uint8_t *NextMade(void *memory)
{
    ::operator delete(memory);
    return reinterpret_cast<uint8_t *>(&gMade[gNextMade++ % kMade]);
}
uint8_t *__fastcall MakeHeightMapLight(void *memory, void *, const void *manager, float value, uint32_t one)
{
    L(0xc7), L(P(manager)), L(U(value)), L(one);
    return NextMade(memory);
}
uint8_t *__fastcall MakeTerrainShade(void *memory, void *, const void *manager, uint32_t one)
{
    L(0xc8), L(P(manager)), L(one);
    return NextMade(memory);
}
uint8_t *__fastcall MakeHalfspace(void *memory, void *, const float *normal, const float *point, uint32_t one)
{
    L(0x3c7d), LF(normal, 3), LF(point, 3), L(one);
    return NextMade(memory);
}

// ---- the lerp sets: a pool, and the ones Construct makes (in their order) ----

constexpr int kLerps = 4;
struct FakeLerp {
    void **vt;
    uint32_t words[6];
};
FakeLerp gLerps[kLerps];
void *gLerpVT[4];
void *gNewLerps[16];
uint32_t gNewLerpCount;

void __fastcall LerpRelease(void *self, void *) { L(0x611), L(IdOf(self)), OnRef(); }
void __fastcall LerpAddRef(void *self, void *) { L(0x612), L(IdOf(self)), OnRef(); }
cLerpLightSet *__fastcall LerpConstruct(cLerpLightSet *self, void *)
{
    if (gNewLerpCount < std::size(gNewLerps)) gNewLerps[gNewLerpCount++] = self;
    auto *const words = reinterpret_cast<uint32_t *>(self);
    words[0] = P(gLerpVT);
    for (int i = 1; i < 7; ++i) words[i] = 0x1e000000u + i;
    L(0x3c0e), L(IdOf(self));
    return self;
}
void __fastcall LerpSetKeys(cLerpLightSet *self, void *, uint32_t tag, uint32_t flag) { L(0x3c2e), L(IdOf(self)), L(tag), L(flag); }
void __fastcall LerpClearActive(cLerpLightSet *self, void *) { L(0x601), L(IdOf(self)); }
void __fastcall LerpFadeOut(cLerpLightSet *self, void *) { L(0x602), L(IdOf(self)); }
void __fastcall LerpAddLight(cLerpLightSet *self, void *, uint32_t group, const char *name, uint32_t kind,
                             const float *color, float scale, float a6, float a7)
{
    L(0x3c1), L(IdOf(self)), L(group), L(Hash(name)), L(kind), LF(color, 3), L(U(scale)), L(U(a6)), L(U(a7));
}
void __fastcall LerpAddDirectional(cLerpLightSet *self, void *, uint32_t group, const char *name, uint32_t kind,
                                   const float *color, float scale, const float *direction, float a7, float a8)
{
    L(0x3c2), L(IdOf(self)), L(group), L(Hash(name)), L(kind), LF(color, 3), L(U(scale)), LF(direction, 3), L(U(a7)),
        L(U(a8));
}
void __fastcall LerpAddExisting(cLerpLightSet *self, void *, uint32_t group, const char *name, void *node,
                                uint32_t unused, const float *color, float scale, float value)
{
    L(0x3c1a), L(IdOf(self)), L(group), L(Hash(name)), L(IdOf(node)), L(IdOf(reinterpret_cast<void *>(uintptr_t(unused))));
    LF(color, 3), L(U(scale)), L(U(value));
}
void __fastcall LerpAddPoint(cLerpLightSet *self, void *, uint32_t group, const char *name, const float *transform,
                             const float *direction, const float *position, const float *color, float scale, float value)
{
    L(0x3c1d), L(IdOf(self)), L(group), L(Hash(name));
    LF(transform, 7), LF(direction, 3), LF(position, 3), LF(color, 3), L(U(scale)), L(U(value));
}

// cRZString::Sprintf: the format and the text, hashed.
cRZString *__cdecl StandSprintf(cRZString *self, const char *fmt, ...)
{
    char text[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    L(0x1ad6), L(Hash(fmt)), L(Hash(text));
    self->mString = text;
    return self;
}

// ---- for the shadow texture and the rebuilds (LG4h) ----

// The terrain behind the system's slot 40 (null: no terrain): its grid size (slot 7), a grid point (slot 8, its height
// at [2]) and normal (slot 9), drawn from gGridSalt; NaN now and then.
struct FakeGrid {
    void **vt;
    uint32_t id;
    uint32_t width, height; // GridSize's answers
    uint32_t salt;
};
FakeGrid gGrid;
FakeGrid *gSystemTerrain; // the system's answer
Plain gSystem;
void *gSystemVT[50], *gGridVT[12];

float GridValue(uint32_t salt, uint32_t x, uint32_t y, uint32_t k)
{
    const uint32_t h = Mix2(salt ^ k, x * 0x10001u + y);
    if (h % 97 == 0) return Fl(0x7fc00000u | (h & 0x803fffffu));
    return float(int32_t(h & 0xffff) - 0x8000) / 2048.0f;
}
void *__fastcall SystemTerrain(Plain *, void *) { return L(0x20d0), L(IdOf(gSystemTerrain)), gSystemTerrain; }
void *__cdecl StandSystem() { return L(0x20de), &gSystem; }
void __fastcall GridSize(FakeGrid *g, void *, uint32_t *width, uint32_t *height)
{
    L(0x5007), L(IdOf(g));
    *width = g->width, *height = g->height;
}
void __fastcall GridPoint(FakeGrid *g, void *, uint32_t x, uint32_t y, float *out)
{
    L(0x5008), L(x), L(y);
    out[0] = float(x), out[1] = float(y), out[2] = GridValue(g->salt, x, y, 0);
}
void __fastcall GridNormal(FakeGrid *g, void *, uint32_t x, uint32_t y, float *out)
{
    L(0x5009), L(x), L(y);
    for (uint32_t k = 0; k < 3; ++k) out[k] = GridValue(g->salt, x, y, 1 + k) / 16.0f;
    // some cells zero or tiny (squares underflow to 0): a bent normal of length 0 is left as it is
    const uint32_t h = Mix2(g->salt ^ 0x5a5a, x * 0x10001u + y);
    if (h % 13 == 0) out[0] = out[1] = out[2] = h & 0x100 ? -0.0f : 0.0f;
    else if (h % 13 == 1) out[0] = 1e-25f, out[1] = -3e-24f, out[2] = h & 0x100 ? 2e-26f : 0.0f;
}

// The textures (class 0x1c4a276c through gMake: MakeTexture): Create records its arguments, Lock hands out the pixels
// (zeroed at the first lock), Fill logs the colour, Pitch the row bytes.
constexpr int kShadowTextures = 4;
constexpr uint32_t kTexturePixels = 128 * 128; // room for 64 rows of a 64-wide image with pitch junk
struct FakeTexture {
    void **vt;
    uint32_t id, refs;
    uint32_t width, height, levels, format, flags;
    float scale;
    uint32_t locks;
    uint32_t pitchJunk; // added to Pitch's answer, 0 mostly
    uint32_t pixels[kTexturePixels];
};
FakeTexture gShadowTextures[kShadowTextures];
uint32_t gNextShadowTexture;
void *gTextureObjectVT[32];

uint32_t __fastcall TextureAddRef(FakeTexture *t, void *) { return L(0x1c01), L(IdOf(t)), ++t->refs; }
uint32_t __fastcall TextureRelease(FakeTexture *t, void *) { return L(0x1c02), L(IdOf(t)), --t->refs; }
uint32_t __fastcall TextureCreate(FakeTexture *t, void *, uint32_t width, uint32_t height, uint32_t levels,
                                  uint32_t format, float scale, uint32_t flags)
{
    L(0x1c06), L(IdOf(t)), L(width), L(height), L(levels), L(format), L(U(scale)), L(flags);
    t->width = width, t->height = height, t->levels = levels, t->format = format, t->scale = scale, t->flags = flags;
    return 1;
}
uint32_t *__fastcall TextureLock(FakeTexture *t, void *, uint32_t level)
{
    L(0x1c09), L(IdOf(t)), L(level);
    if (!t->locks++) std::memset(t->pixels, 0, sizeof t->pixels);
    return t->pixels;
}
void __fastcall TextureFill(FakeTexture *t, void *, const float *color, uint32_t level)
{
    L(0x1c12), L(IdOf(t)), L(IdOf(color)), LF(color, 4), L(level);
}
uint32_t __fastcall TexturePitch(FakeTexture *t, void *, uint32_t level)
{
    L(0x1c1c), L(IdOf(t)), L(level);
    return t->width * 4 + t->pitchJunk;
}
FakeTexture *MakeTexture()
{
    FakeTexture *const t = &gShadowTextures[gNextShadowTexture++ % kShadowTextures];
    ++t->refs;
    return t;
}

// The ambient maps (cIImageData: slot 26 level width, 27 level height, 38 GetPixelRGB(out, x, y, level)): the colour
// drawn from the texel and the map's salt (the pool kColours: 0, +-0, 1, rounding edges of the byte packing, NaN, large,
// negative; or wide), written to `out`, and `out` returned or, when `elsewhere`, the map's own other colour.
struct FakeMap {
    void **vt;
    uint32_t id;
    uint32_t width, height; // slots 26 and 27's answers
    uint32_t salt;
    uint32_t elsewhere;
    float other[3];
};
void *gMapVT[40];

float MapColour(uint32_t salt, uint32_t x, uint32_t y, uint32_t k)
{
    static const float kColours[] = {0.0f, -0.0f, 1.0f, 0.5f, 0.5f / 256, 1.5f / 256, 254.5f / 256, 255.5f / 256,
                                     255.0f / 256, 1.0f / 256, -0.5f / 256, 2.0f, -1.0f, 1e30f, 0.25f, 0.75f};
    const uint32_t h = Mix2(salt ^ (k * 0x9e3779b9u), x * 0x10001u + y);
    if (h % 61 == 0) return Fl(0x7fc00000u | (h & 0x803fffffu));
    if (h % 3 == 0) return kColours[(h >> 4) % std::size(kColours)];
    return float(h >> 12 & 0x3ff) / 768.0f;
}
uint32_t __fastcall MapWidth(FakeMap *m, void *, uint32_t level) { return L(0x5a1a), L(IdOf(m)), L(level), m->width; }
uint32_t __fastcall MapHeight(FakeMap *m, void *, uint32_t level) { return L(0x5a1b), L(IdOf(m)), L(level), m->height; }
const float *__fastcall MapPixel(FakeMap *m, void *, float *out, uint32_t x, uint32_t y, uint32_t level)
{
    L(0x5a26), L(IdOf(m)), L(x), L(y), L(level);
    for (uint32_t k = 0; k < 3; ++k) out[k] = MapColour(m->salt, x, y, k);
    return m->elsewhere && Mix2(m->salt, x + y) % 2 ? m->other : out;
}

// The texture handle (cTextureHandle::Get 0xae8730: logs the handle's word, answers gHandleObject or null), the handle
// object's QueryInterface (slot 0: writes gTextureFace when gFaceAnswer's bit 0 or 2, answers bit 0 with the junk above
// the low byte), the face (2 release, 9 load (image, levels), 10 set image, 11 current image: gFaceCurrent[0] at the
// first call, [1] after) and the cached object (0xae8900, slot 24 logs).
Plain gHandleObject, gTextureFace, gCached;
Plain *gHandleAnswer;
uint32_t gFaceAnswer, gFaceCurrent[2], gFaceCalls;
void *gHandleVT[4], *gFaceVT[16], *gCachedVT[28];

void *__fastcall StandHandleGet(const uint32_t *self, void *) { return L(0xae87), L(*self), gHandleAnswer; }
uint32_t __fastcall HandleQuery(Plain *, void *, uint32_t iid, void **out)
{
    L(0xae80), L(iid), L(IdOf(*out));
    if (gFaceAnswer & 5) *out = &gTextureFace;
    return (gFaceAnswer & ~0xffu) | (gFaceAnswer & 1);
}
void __fastcall FaceRelease(Plain *, void *) { L(0xfa02); }
void __fastcall FaceSet9(Plain *, void *, const void *texture, uint32_t a) { L(0xfa09), L(IdOf(texture)), L(a); }
void __fastcall FaceSet10(Plain *, void *, const void *texture) { L(0xfa0a), L(IdOf(texture)); }
uint32_t __fastcall FaceCurrent(Plain *, void *) { return L(0xfa0b), gFaceCurrent[gFaceCalls++ ? 1 : 0]; }
void *__cdecl StandCached() { return L(0xae89), &gCached; }
void __fastcall Cached24(Plain *, void *, uint32_t a, uint32_t b) { L(0xca24), L(a), L(b); }

// The manager's builders and rebuild steps as stand-ins on both sides (one logging fake per builder: its tag, the
// manager and the argument), the shadow texture's builder 0x2d2a70 among them. A test
// makes one Builders for the rebuilds; BuildSunShadowTexture's own test leaves them out.
// gAfterBuilder (when set) runs after each: a test may change the world there (say a switch byte) to show which reads
// come after which call.
void (*gAfterBuilder)(uint32_t tag);
void Builder(uint32_t tag, const void *self, uint32_t arg)
{
    L(0xb000 + tag), L(P(self)), L(arg);
    if (gAfterBuilder) gAfterBuilder(tag);
}
#define LIGHTFX_BUILDER(n, name) \
    void __fastcall name##Stand(void *self, void *) { Builder(n, self, 0); }
LIGHTFX_BUILDER(0, RemoveBuiltLights)
LIGHTFX_BUILDER(1, RemoveSampleLights)
LIGHTFX_BUILDER(3, RefreshPortalObjects)
LIGHTFX_BUILDER(4, RelightSceneObjects)
LIGHTFX_BUILDER(5, RebuildSunHeights)
LIGHTFX_BUILDER(6, SamplePortals)
LIGHTFX_BUILDER(7, BlendLotEdges)
LIGHTFX_BUILDER(9, ApplyPortalLights)
LIGHTFX_BUILDER(10, BuildSunHeightMapLights)
LIGHTFX_BUILDER(11, BuildRoomLights)
LIGHTFX_BUILDER(13, BuildRoomPortals)
LIGHTFX_BUILDER(14, BuildSunShadowTexture)
#undef LIGHTFX_BUILDER
void __fastcall EvaluateLerpLightsStand(void *self, void *, float t) { Builder(2, self, U(t)); }
// the portal lights as an id (a caller's vector on the stack has no address both sides share)
void __fastcall BuildPortalLightsStand(void *self, void *, void *lights) { Builder(8, self, IdOf(lights)); }
// a bool: only the low byte is the argument
void __fastcall BuildFlatOutdoorLightsStand(void *self, void *, uint32_t lerp) { Builder(12, self, lerp & 0xff); }

struct Builders {
    shape::StandIn removeBuilt{0x2dff00, &cLightingManager::RemoveBuiltLights, &RemoveBuiltLightsStand};
    shape::StandIn removeSamples{0x2dffd0, &cLightingManager::RemoveSampleLights, &RemoveSampleLightsStand};
    shape::StandIn evaluate{0x2dfc80, &cLightingManager::EvaluateLerpLights, &EvaluateLerpLightsStand};
    shape::StandIn refresh{0x2d2830, &cLightingManager::RefreshPortalObjects, &RefreshPortalObjectsStand};
    shape::StandIn relight{0x2e4b50, &cLightingManager::RelightSceneObjects, &RelightSceneObjectsStand};
    shape::StandIn sunHeights{0x2e4300, &cLightingManager::RebuildSunHeights, &RebuildSunHeightsStand};
    shape::StandIn samplePortals{0x2d8b40, &cLightingManager::SamplePortals, &SamplePortalsStand};
    shape::StandIn blend{0x2da3b0, &cLightingManager::BlendLotEdges, &BlendLotEdgesStand};
    shape::StandIn portalLights{0x2e0480, &cLightingManager::BuildPortalLights, &BuildPortalLightsStand};
    shape::StandIn applyPortals{0x2e1c30, &cLightingManager::ApplyPortalLights, &ApplyPortalLightsStand};
    shape::StandIn sun{0x2cfd60, &cLightingManager::BuildSunHeightMapLights, &BuildSunHeightMapLightsStand};
    shape::StandIn rooms{0x2d0250, &cLightingManager::BuildRoomLights, &BuildRoomLightsStand};
    shape::StandIn flat{0x2e1560, &cLightingManager::BuildFlatOutdoorLights, &BuildFlatOutdoorLightsStand};
    shape::StandIn roomPortals{0x2d50f0, &cLightingManager::BuildRoomPortals, &BuildRoomPortalsStand};
    shape::StandIn shadow{0x2d2a70, &cLightingManager::BuildSunShadowTexture, &BuildSunShadowTextureStand};
};

// The stopwatches (cRZStopwatch: Construct 0x1c9030, Start 0x1c982b, Elapsed 0x1c9256 on both sides; Restart 0x1c95ec,
// not ours yet, on the image's). Construct numbers each watch in its +4 word (which the original leaves alone) in
// construction order; a watch inside gWatchHome's object logs as its offset there instead. Elapsed answers the test's
// readings in turn (0 once they run out).
uint32_t gWatchCount;
const uint8_t *gWatchHome;
size_t gWatchHomeSize;
const uint32_t *gReadings;
size_t gReadingCount, gNextReading;
template <size_t N> void Readings(const uint32_t (&values)[N], size_t count) { gReadings = values, gReadingCount = count < N ? count : N, gNextReading = 0; }

uint32_t WatchId(const cRZStopwatch *w)
{
    const auto *const at = reinterpret_cast<const uint8_t *>(w);
    if (gWatchHome && at >= gWatchHome && at < gWatchHome + gWatchHomeSize) return 0x57000000u + uint32_t(at - gWatchHome);
    return w->mUnused;
}
cRZStopwatch *__fastcall WatchConstruct(cRZStopwatch *w, void *, int32_t mode)
{
    w->mMode = mode, w->mUnused = 0x5700 + ++gWatchCount, w->mStart = 0, w->mAccumulated = 0;
    L(0x9030), L(w->mUnused), L(uint32_t(mode));
    return w;
}
void __fastcall WatchStart(cRZStopwatch *w, void *) { L(0x982b), L(WatchId(w)), L(uint32_t(w->mMode)); }
uint32_t __fastcall WatchElapsed(const cRZStopwatch *w, void *)
{
    const uint32_t reading = gNextReading < gReadingCount ? gReadings[gNextReading] : 0;
    ++gNextReading;
    L(0x9256), L(WatchId(w)), L(uint32_t(w->mMode)), L(reading);
    return reading;
}
void __fastcall WatchRestart(cRZStopwatch *w, void *) { L(0x95ec), L(WatchId(w)), L(uint32_t(w->mMode)); }

struct Stopwatches {
    shape::StandIn construct{0x1c9030, &cRZStopwatch::Construct, &WatchConstruct};
    shape::StandIn start{0x1c982b, &cRZStopwatch::Start, &WatchStart};
    shape::StandIn elapsed{0x1c9256, &cRZStopwatch::Elapsed, &WatchElapsed};
    shape::CodePatch restart{0x1c95ecu, shape::CodeOf(&WatchRestart)};
};

// The scene flag setter 0xaeeea0 (cdecl (scene, flags); ours, nGZSceneGraph::RebuildLightBounds: stood in on both sides) and the scene manager's slot 17 (the
// scene: gSceneAnswer).
void *gSceneAnswer;
void __cdecl StandSceneFlags(const void *scene, uint32_t flags) { L(0xaeee), L(IdOf(scene)), L(flags); }
void *__fastcall SceneManagerScene(Plain *, void *) { return L(0x317), L(IdOf(gSceneAnswer)), gSceneAnswer; }

// The shadow texture's services: the system getter (the image's; not ours yet), the handle's Get and the cached object
// (both sides).
struct ShadowServices {
    shape::CodePatch system{0x20defbu, shape::CodeOf(&StandSystem)};
    shape::StandIn handle{0xae8730u, &nGZSceneGraph::cTextureHandle::Get, &StandHandleGet};
    shape::StandIn cached{0xae8900u, &GetCachedQueriedObject, &StandCached};
};

// ---- for the lifecycle (LG3b; tests/sg_lighting_lifecycle.cpp): in lightfx::life, out of reach of the tests that
// use namespace lightfx whole ----

namespace life {

// The terrain view behind mTerrain (class not identified): slots 77 / 78 / 87 answer floats on the x87 stack (the
// grid's columns, rows, vertices per world unit inverted), 83 / 84 words; each logs its slot.
struct FakeView {
    void **vt;
    float floats[3]; // slots 77, 78, 87
    int32_t ints[2]; // slots 83, 84
};
FakeView gView;
void *gViewVT[90];
float __fastcall ViewColumns(FakeView *v, void *) { return L(77), v->floats[0]; }
float __fastcall ViewRows(FakeView *v, void *) { return L(78), v->floats[1]; }
float __fastcall ViewScale(FakeView *v, void *) { return L(87), v->floats[2]; }
int32_t __fastcall ViewWidth(FakeView *v, void *) { return L(83), v->ints[0]; }
int32_t __fastcall ViewHeight(FakeView *v, void *) { return L(84), v->ints[1]; }

// The lighting script's parser (+0x30, nGZCommandParser::cIFileParser): slot 8 SetDirty logs the bool's byte.
Plain gParser;
void *gParserVT[24];
void __fastcall ParserSetDirty(Plain *p, void *, uint32_t dirty) { L(0x9a08), L(IdOf(p)), L(dirty & 0xff); }

// The manager's lit object (+0x18, class not identified): slot 6 logs; slot 7 logs its pointer and the 0x30 bytes
// there (a light set's mLitParams1D0, cLightSet +0x1d0..+0x200).
Plain gLit;
void *gLitVT[30];
void __fastcall LitSlot6(Plain *p, void *) { L(0x1106), L(IdOf(p)); }
void __fastcall LitSlot7(Plain *p, void *, const uint32_t *params)
{
    L(0x1107), L(IdOf(p)), L(P(params));
    for (int i = 0; i < 12; ++i) L(params[i]);
}

// The scene graph system's slot 48 (the lightmaps): gLightmaps.
void *gLightmaps;
void *__fastcall SystemLightmaps(Plain *, void *) { return L(0x2030), L(IdOf(gLightmaps)), gLightmaps; }

// The message server (GetCachedMessageServer 0x9c5a89, both sides: gServer): slot 4 MessagePost logs the message's
// words +4..+0x28 (cRZMessage2Standard, 0x2c bytes: its count, flags, id, values) and the urgent flag's byte; true.
Plain gServer;
void *gServerVT[12];
void *__cdecl StandServer() { return L(0x9c5a), &gServer; }
uint32_t __fastcall ServerPost(Plain *, void *, const uint32_t *message, uint32_t urgent)
{
    L(0x5e04), L(message != nullptr);
    if (message)
        for (int i = 1; i < 11; ++i) L(message[i]);
    L(urgent & 0xff);
    return 1;
}

// RZMemory::Malloc 0x1be3b0 and Free 0x1be3a1 on both sides (the messages' blocks; 0x1c03b7 tail-calls Malloc): our
// heap's blocks, filled with 0xa5 and numbered in allocation order (ResetRz before each side); each call logs the
// size or the block's number.
void *gRzBlocks[16];
uint32_t gRzCount;
void ResetRz() { gRzCount = 0; }
uint32_t RzId(const void *p)
{
    for (uint32_t i = 0; i < gRzCount && i < std::size(gRzBlocks); ++i)
        if (gRzBlocks[i] == p) return 0x4200 + i;
    return P(p);
}
void *__cdecl RzMalloc(uint32_t bytes)
{
    void *const p = ::operator new(bytes ? bytes : 1);
    std::memset(p, 0xa5, bytes);
    if (gRzCount < std::size(gRzBlocks)) gRzBlocks[gRzCount] = p;
    ++gRzCount;
    L(0x3b0), L(bytes);
    return p;
}
void __cdecl RzFree(void *p)
{
    L(0x3a1), L(p ? RzId(p) : 0);
    ::operator delete(p);
}
struct RzHeap {
    shape::StandIn malloc{0x1be3b0u, &RZMemory::Malloc, &RzMalloc};
    shape::StandIn free{0x1be3a1u, &RZMemory::Free, &RzFree};
};

// EvaluateAmbient 0x2d6740 (normal, out; ret 8) and EvaluateAmbientShaded 0x2d74b0 (normal, shade, out; ret 0xc) on
// both sides: each logs the manager, the normal's address and (shaded) the shade's bits, and stores three floats to
// out as the two do (out[0..2]), gAmbients' rows in turn.
float gAmbients[4][3];
uint32_t gAmbientCalls;
void PutAmbient(float *out)
{
    const float *const c = gAmbients[gAmbientCalls++ % 4];
    out[0] = c[0], out[1] = c[1], out[2] = c[2];
}
void __fastcall AmbientStand(void *self, void *, const float *normal, float *out)
{
    L(0x6740), L(IdOf(self)), L(P(normal));
    PutAmbient(out);
}
void __fastcall AmbientShadedStand(void *self, void *, const float *normal, float shade, float *out)
{
    L(0x74b0), L(IdOf(self)), L(P(normal)), L(U(shade));
    PutAmbient(out);
}
struct Ambients {
    shape::StandIn plain{0x2d6740u, &cLightingManager::EvaluateAmbient, &AmbientStand};
    shape::StandIn shaded{0x2d74b0u, &cLightingManager::EvaluateAmbientShaded, &AmbientShadedStand};
};

// The app's option lists (cTSAppCommonImpl slot 5, appinit::CommonImplSlot5 0x1d0c60, both sides): the option and the
// other words logged, gPaths pushed onto the list (GZStrings: the allocator service's). cRZFile::PathExists 0x1c1242
// (both sides): the path hashed and logged; call n answers bit n of gExists.
const char *gPaths[6];
uint32_t gPathCount, gExists, gExistsCalls;
uint32_t __cdecl StandPaths(uint32_t option, uint32_t list, uint32_t c, uint32_t d, uint32_t e)
{
    L(0x1d0c), L(option), L(c), L(d), L(e);
    auto *const paths = reinterpret_cast<std::list<GZString, GZServiceAllocator<GZString>> *>(uintptr_t(list));
    for (uint32_t i = 0; i < gPathCount && i < std::size(gPaths); ++i) paths->push_back(GZString(gPaths[i]));
    return 1;
}
bool __cdecl StandExists(const cIGZString &path)
{
    const std::string &s = reinterpret_cast<const cRZString &>(path).mString;
    const bool yes = (gExists >> (gExistsCalls++ & 31) & 1) != 0;
    L(0x1242), L(Hash(s.c_str())), L(uint32_t(s.size())), L(yes);
    return yes;
}
struct PathServices {
    shape::StandIn paths{0x1d0c60u, &appinit::CommonImplSlot5, &StandPaths};
    shape::StandIn exists{0x1c1242u, &cRZFile::PathExists, &StandExists};
};

} // namespace life

// ---- the tables ----

void FillShared()
{
    gNodeVT[1] = reinterpret_cast<void *>(&AddRef);
    gNodeVT[2] = reinterpret_cast<void *>(&Release);
    gNodeVT[78] = reinterpret_cast<void *>(&NodeAddLayerBits);
    gNodeVT[80] = reinterpret_cast<void *>(&NodeRemoveLayerBits);
    gNodeVT[81] = reinterpret_cast<void *>(&NodeSetLayerTag);
    gNodeVT[91] = reinterpret_cast<void *>(&NodeSetLightFlag);
    gNodeVT[93] = reinterpret_cast<void *>(&NodeSetLight);
    gNodeVT[95] = reinterpret_cast<void *>(&NodeGetLight);
    gLightVT[0] = reinterpret_cast<void *>(&LightQuery);
    gLightVT[1] = reinterpret_cast<void *>(&AddRef);
    gLightVT[2] = reinterpret_cast<void *>(&Release);
    gLightVT[40] = reinterpret_cast<void *>(&LightSetName);
    gLightVT[41] = reinterpret_cast<void *>(&LightSetColor);
    gLightVT[42] = reinterpret_cast<void *>(&LightSetIntensity);
    gLightVT[43] = reinterpret_cast<void *>(&LightSetFactor);
    gLightVT[51] = reinterpret_cast<void *>(&LightGetColor);
    gLightVT[52] = reinterpret_cast<void *>(&LightGetIntensity);
    gTextureVT[1] = reinterpret_cast<void *>(&AddRef);
    gTextureVT[2] = reinterpret_cast<void *>(&Release);
    gTextureVT[41] = reinterpret_cast<void *>(&TextureSetColor);
    gComVT[3] = reinterpret_cast<void *>(&ComGetClassObject);
    gSceneManagerVT[7] = reinterpret_cast<void *>(&SceneAdd);
    gLerpVT[1] = reinterpret_cast<void *>(&LerpRelease);
    gLerpVT[2] = reinterpret_cast<void *>(&LerpAddRef);
    gManagerVT[36] = reinterpret_cast<void *>(&LotValue);
    gCom.vt = gComVT, gSceneManager.vt = gSceneManagerVT;
    // LG4h's
    gSystemVT[40] = reinterpret_cast<void *>(&SystemTerrain);
    gGridVT[7] = reinterpret_cast<void *>(&GridSize);
    gGridVT[8] = reinterpret_cast<void *>(&GridPoint);
    gGridVT[9] = reinterpret_cast<void *>(&GridNormal);
    gTextureObjectVT[1] = reinterpret_cast<void *>(&TextureAddRef);
    gTextureObjectVT[2] = reinterpret_cast<void *>(&TextureRelease);
    gTextureObjectVT[6] = reinterpret_cast<void *>(&TextureCreate);
    gTextureObjectVT[9] = reinterpret_cast<void *>(&TextureLock);
    gTextureObjectVT[18] = reinterpret_cast<void *>(&TextureFill);
    gTextureObjectVT[28] = reinterpret_cast<void *>(&TexturePitch);
    gHandleVT[0] = reinterpret_cast<void *>(&HandleQuery);
    gFaceVT[2] = reinterpret_cast<void *>(&FaceRelease);
    gFaceVT[9] = reinterpret_cast<void *>(&FaceSet9);
    gFaceVT[10] = reinterpret_cast<void *>(&FaceSet10);
    gFaceVT[11] = reinterpret_cast<void *>(&FaceCurrent);
    gCachedVT[24] = reinterpret_cast<void *>(&Cached24);
    gMapVT[26] = reinterpret_cast<void *>(&MapWidth);
    gMapVT[27] = reinterpret_cast<void *>(&MapHeight);
    gMapVT[38] = reinterpret_cast<void *>(&MapPixel);
    gSystem.vt = gSystemVT, gGrid.vt = gGridVT, gHandleObject.vt = gHandleVT, gTextureFace.vt = gFaceVT;
    gCached.vt = gCachedVT;
    gSceneManagerVT[17] = reinterpret_cast<void *>(&SceneManagerScene);
    // the lifecycle's (lightfx::life)
    life::gViewVT[77] = reinterpret_cast<void *>(&life::ViewColumns);
    life::gViewVT[78] = reinterpret_cast<void *>(&life::ViewRows);
    life::gViewVT[83] = reinterpret_cast<void *>(&life::ViewWidth);
    life::gViewVT[84] = reinterpret_cast<void *>(&life::ViewHeight);
    life::gViewVT[87] = reinterpret_cast<void *>(&life::ViewScale);
    life::gParserVT[8] = reinterpret_cast<void *>(&life::ParserSetDirty);
    life::gLitVT[6] = reinterpret_cast<void *>(&life::LitSlot6);
    life::gLitVT[7] = reinterpret_cast<void *>(&life::LitSlot7);
    life::gServerVT[4] = reinterpret_cast<void *>(&life::ServerPost);
    gSystemVT[48] = reinterpret_cast<void *>(&life::SystemLightmaps);
    life::gView.vt = life::gViewVT, life::gParser.vt = life::gParserVT, life::gLit.vt = life::gLitVT;
    life::gServer.vt = life::gServerVT;
}

} // namespace lightfx
} // namespace
