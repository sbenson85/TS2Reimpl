// Differential tests of nTSSG::cLightMapManager (lanes 261108q and 261108t, scenegraph LG6 / LG6b;
// src/sg/cLightMapManager.h).
//
//   SG_LightMapManagerLife  one manager's life on each side: built by the constructor 0x2c4950 on a block filled alike, or
//       by the director's factory 0x2c73f0 (the words neither writes then set alike; the floor and wall texture sizes,
//       the wall tile size and the two scales sometimes set alike to small or unequal values), then random calls through
//       its three vtables: Init / Shutdown / Start / Stop (3-6; Start builds the floor image 0x2c5340 and the wall atlas
//       0x2c5a60), the slots 7-11 (Update 0x2c7cd0 under the clock id or another, SetColor 0x2c6460 with clamped, NaN and
//       half-way channels, SetTransition 0x2c6eb0 with each detail level and a dword whose low byte is 0, UpdateIncidence
//       0x2c6ad0, Advance 0x2c7540 with NaN and negative steps), the walls (12 AcquireWall,
//       13 ReleaseWall on held faces only: an unknown face is the original's null read), the floors (14, 15), the lights
//       read back (16, 17), the rectangles and UVs (18-21), GetImage (22) and GetScale (23), DoMessage (+4 slot 3) with
//       the world-loaded message (the holder's property set read by the settings readers, then the change message posted
//       when started), the border setting's message and others, QueryInterface through each face with each interface
//       id (null `out` too), AddRef / Release. After each call the object is noted word by word (vtables left out),
//       then what its containers hold: the message ids, the eight image vectors, the two sets, the texture handles, both
//       hash maps (their elements in list order and their bucket vectors), the free tiles and each atlas's nodes. At the
//       end the manager is deleted (the +8 face's deleting destructor 0x2c5305 / 0x2c5310 with the destructor 0x2c4f50,
//       or the last Release), then the fakes' counts and the heap. After each step that can draw (Start, slots 7-12, 14)
//       and at the end, each live image's pixels are hashed (the whole arena, its margins included).
//   SG_LightMapManagerKeys  the maps' functors on keys both sides are given alike: the floor hash 0x2c5270, the wall hash
//       0x2c5290 and the wall equality 0x2c5100 (floats a few units in the last place apart, past the 32-unit window,
//       signed zeros, NaN, infinities, values past 2^23), against ours.
//   SG_LightMapManagerTable  MakeIncidenceTable 0x2c6950 over scales and clamps (NaN, infinities, negatives, 0, tiny and
//       huge) and random inverses: the table's 0x20020 bytes and its offset in its block; each side's table freed by the
//       other's FreeIncidenceTable 0x2c6d80, the heap balanced.
//   SG_LightMapManagerKernel  the incidence kernel 0x2c6890 and its adapter 0x2c6860 on random texels, sizes 1-9, pitches
//       with slack, and tables made or with random headers and entries: the whole destination with a row and its slack
//       around it compared.
//   SG_LightMapManagerBlend  BlendImage 0x2c7570 on image fakes: with and without pixels on either side, a null source,
//       a destination pitch that differs, t at 0, 1, NaN, out of range and at the rounding ties of t * 255.
//   SG_LightMapManagerHeight  the wall face height 0x2c6f20 on the world's keys: its result's bits and its calls.
//
// Stood in on both sides (code patches over the image's functions, which our code calls through OriginalFunction or as
// ours): the border test 0x7f2340 and the wall flag 0x7e2050 (answers chosen per call), the ground height 0x7e4680 (a
// table of doubles, NaN and 1e9 included), the settings readers 0xaf7110 / 0xaf7190 / 0xaf7210 (they note what they are
// given and change the value and the `changed` flag per call), the services 0x20e0b2 (the world DB fake: slots 85 / 86,
// 36 the roofs fake or null, 39 the partitions), 0x20de97 (the partition fake, slot 50), 0x20defb (the scene graph system
// fake: slot 49 the lighting fake, whose slot 28 writes a colour from a table) and 0x20df9a (the options fake: slot 21
// the detail level), the COM director 0x1ae18d (slot 3 hands out image fakes, class 0x1c4a276c, and texture fakes,
// class 0x2a956dad, from pools), the catalog tests 0x7ef8e0 / 0x7ef7b0 (and ours), RZMemory's Malloc / Free, the game's
// operator new / delete / free and malloc 0x8252b1 / free 0x831a54 (to ours, so the original's blocks are counted and
// normalised like ours). The image library's blend 0xb3c6c0 runs for real on both sides.
// Image fakes: Init (slot 6) sets the size (noted and clamped past 64) and a pitch of 4 * width, or one texel more one
// time in 8, and fills the pixel arena with noise; Fill / FillOpaque (18 / 17) fill pitch / 4 * height words from the
// colour's bits, uniform, with noise or with low noise; LevelData / LevelDataConst (9 / 8) give the arena; HasPixels,
// LevelWidth / Height / Pitch (24, 26-28) the state; GetPixelRGB (38) the texel's bytes, or words from the arguments.
// Cells: the framework 0x10c9378 (GetSystemService hands out the message server and the settings manager fakes, or no
// settings), the message server cache 0x10f519c, the allocator service 0x10c95d4 / guard 0x10c95d8 (the settings key's
// string), lmReserveBorderTiles' value 0xffd8e8, the texture manager 0x10f8f94 (a fake: AddRefId / ReleaseId, slots 18 /
// 19, noted; Add, 20, keeps the texture and answers its id; GetTexture, 23, the texture), GetCachedSingleton's 0x10cc288
// (slot 22: the changes fake, whose slot 20 gives the target fake, slot 6), GetCachedSlot23's 0x10cc2a8 (the clock fake:
// slot 5 the clock id, or another one time in 6) and the world DB 0x10cc45c.
// The kernel reads mm7 unset (src/sg/cLightMapManager.cpp): mm7 is cleared before the original's kernel and slot 10.
#include "sgfx.h"

#include "code_patch.h"
#include "free_recorder.h"

#include "gz/cIGZPropertySet.h"
#include "rz/RZMemory.h"
#include "rz/StaticGetters.h"
#include "sg/SGUtilities.h"
#include "sg/cLightMapManager.h"
#include "world/WorldCatalog.h"
#include "world/WorldHeights.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

using namespace nTSSG;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

struct At {
    unsigned slot;
    Slot s;
};
template <size_t N, size_t K> constexpr std::array<Slot, N> Table(const At (&at)[K])
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    t[1] = {0, Ret::Int, true, "AddRef", ""};
    t[2] = {0, Ret::Int, true, "Release", ""};
    for (const At &a : at) t[a.slot] = a.s;
    return t;
}

// nGZSceneGraph::cIImageData (src/sg/cImageData.h): 6 Init, 8 LevelDataConst, 9 LevelData, 17 FillOpaque, 18 Fill,
// 24 HasPixels, 26 LevelWidth, 27 LevelHeight, 28 LevelPitch, 38 GetPixelRGB (by value: the hidden out first).
constexpr At kImageAt[] = {{6, {6, Ret::Int, true, "Init", "uuuufu"}},
                           {8, {1, Ret::Int, true, "LevelDataConst", "u"}},
                           {9, {1, Ret::Int, true, "LevelData", "u"}},
                           {17, {2, Ret::Int, true, "FillOpaque", "F3u"}},
                           {18, {2, Ret::Int, true, "Fill", "F4u"}},
                           {24, {0, Ret::Int, true, "HasPixels", ""}},
                           {26, {1, Ret::Int, true, "LevelWidth", "u"}},
                           {27, {1, Ret::Int, true, "LevelHeight", "u"}},
                           {28, {1, Ret::Int, true, "LevelPitch", "u"}},
                           {38, {4, Ret::Int, true, "GetPixelRGB", "auuu"}}};
constexpr auto kImageSlots = Table<39>(kImageAt);
constexpr drawfx::Iface kImage{"ImageData", 39, kImageSlots.data(), false, drawfx::kTestTable};

// nGZSceneGraph::cITextureImage (src/sg/cTextureImage.h): 9 Load, 10 SetImage.
constexpr At kTextureAt[] = {{9, {2, Ret::Int, true, "Load", "au"}}, {10, {1, Ret::Int, true, "SetImage", "a"}}};
constexpr auto kTextureSlots = Table<11>(kTextureAt);
constexpr drawfx::Iface kTexture{"TextureImage", 11, kTextureSlots.data(), false, drawfx::kTestTable};

// cIGZCOM: 3 GetClassObject (class, iid, out).
constexpr At kComAt[] = {{3, {3, Ret::Int, true, "GetClassObject", "uua"}}};
constexpr auto kComSlots = Table<4>(kComAt);
constexpr drawfx::Iface kCom{"COM", 4, kComSlots.data(), false, drawfx::kTestTable};

// nTSSG::cTSSGSystem: 49 the lighting manager.
constexpr At kSystemAt[] = {{49, {0, Ret::Int, true, "LightingManager", ""}}};
constexpr auto kSystemSlots = Table<50>(kSystemAt);
constexpr drawfx::Iface kSystem{"TSSGSystem", 50, kSystemSlots.data(), false, drawfx::kTestTable};

// nTSSG::cLightingManager: 28 the light's colour (by value: the hidden out).
constexpr At kLightingAt[] = {{28, {1, Ret::Int, true, "Colour", "a"}}};
constexpr auto kLightingSlots = Table<29>(kLightingAt);
constexpr drawfx::Iface kLighting{"LightingManager", 29, kLightingSlots.data(), false, drawfx::kTestTable};

// The app options (0x20df9a's object): 21 the detail level.
constexpr At kOptionsAt[] = {{21, {0, Ret::Int, true, "Detail", ""}}};
constexpr auto kOptionsSlots = Table<22>(kOptionsAt);
constexpr drawfx::Iface kOptions{"Options", 22, kOptionsSlots.data(), false, drawfx::kTestTable};

// GetCachedSlot23's object: 5 the clock id.
constexpr At kClockAt[] = {{5, {0, Ret::Int, true, "Clock", ""}}};
constexpr auto kClockSlots = Table<6>(kClockAt);
constexpr drawfx::Iface kClock{"Clock", 6, kClockSlots.data(), false, drawfx::kTestTable};

// cITSGlobals: 22 the field at +0x58.
constexpr At kGlobalsAt[] = {{22, {0, Ret::Int, true, "GetField58", ""}}};
constexpr auto kGlobalsSlots = Table<23>(kGlobalsAt);
constexpr drawfx::Iface kGlobals{"TSGlobals", 23, kGlobalsSlots.data(), false, drawfx::kTestTable};

// cTSConfigurationChangeManager: 20 the target; the target: 6 (no arguments).
constexpr At kChangesAt[] = {{20, {0, Ret::Int, true, "Target", ""}}};
constexpr auto kChangesSlots = Table<21>(kChangesAt);
constexpr drawfx::Iface kChanges{"ChangeManager", 21, kChangesSlots.data(), false, drawfx::kTestTable};
constexpr At kTargetAt[] = {{6, {0, Ret::Int, true, "Changed", ""}}};
constexpr auto kTargetSlots = Table<7>(kTargetAt);
constexpr drawfx::Iface kTarget{"ChangeTarget", 7, kTargetSlots.data(), false, drawfx::kTestTable};

// nTSWorld::cWorldRoof: 22 the roof's height at a point (st0), 37 whether a roof is over the point.
constexpr At kRoofsAt[] = {{22, {2, Ret::Float, true, "RoofHeight", "W3u"}}, {37, {1, Ret::Int, true, "UnderRoof", "W3"}}};
constexpr auto kRoofsSlots = Table<38>(kRoofsAt);
constexpr drawfx::Iface kRoofs{"WorldRoof", 38, kRoofsSlots.data(), false, drawfx::kTestTable};

// cIGZFrameWork: 5 GetSystemService.
constexpr At kFrameworkAt[] = {{5, {3, Ret::Int, true, "GetSystemService", "uua"}}};
constexpr auto kFrameworkSlots = Table<6>(kFrameworkAt);
constexpr drawfx::Iface kFramework{"FrameWork", 6, kFrameworkSlots.data(), false, drawfx::kTestTable};

// cIGZMessageServer2: 4 MessagePost, 5 AddNotification, 6 RemoveNotification.
constexpr At kServerAt[] = {{4, {2, Ret::Int, true, "MessagePost", "ab"}},
                            {5, {2, Ret::Int, true, "AddNotification", "au"}},
                            {6, {2, Ret::Int, true, "RemoveNotification", "au"}}};
constexpr auto kServerSlots = Table<9>(kServerAt);
constexpr drawfx::Iface kServer{"MessageServer2", 9, kServerSlots.data(), false, drawfx::kTestTable};

// cIGZPropertyManagerString: 4 AddNotification (the key's text noted by the handler), 5 RemoveNotification.
constexpr At kSettingsAt[] = {{4, {4, Ret::Int, true, "AddNotification", "auaa"}},
                              {5, {3, Ret::Int, true, "RemoveNotification", "aua"}}};
constexpr auto kSettingsSlots = Table<7>(kSettingsAt);
constexpr drawfx::Iface kSettings{"PropertyManager", 7, kSettingsSlots.data(), false, drawfx::kTestTable};

// The allocator service: 3 allocates, 4 frees.
constexpr At kAllocatorAt[] = {{3, {1, Ret::Int, true, "Allocate", "u"}}, {4, {1, Ret::Int, true, "Free", "a"}}};
constexpr auto kAllocatorSlots = Table<5>(kAllocatorAt);
constexpr drawfx::Iface kAllocator{"AllocatorService", 5, kAllocatorSlots.data(), false, drawfx::kTestTable};

// nTSWorld::cIWorldDB: 36 GetRoofs, 39 GetPartitions, 85 GetWidth, 86 GetHeight.
constexpr At kWorldAt[] = {{36, {0, Ret::Int, true, "GetRoofs", ""}},
                           {39, {0, Ret::Int, true, "GetPartitions", ""}},
                           {85, {0, Ret::Int, true, "GetWidth", ""}},
                           {86, {0, Ret::Int, true, "GetHeight", ""}}};
constexpr auto kWorldSlots = Table<87>(kWorldAt);
constexpr drawfx::Iface kWorld{"WorldDB", 87, kWorldSlots.data(), false, drawfx::kTestTable};

// nTSWorld::cPartitionManager: 50 the face's partition type (its key noted by the handler).
constexpr At kPartitionsAt[] = {{50, {1, Ret::Int, true, "PartitionType", "a"}}};
constexpr auto kPartitionsSlots = Table<51>(kPartitionsAt);
constexpr drawfx::Iface kPartitions{"PartitionManager", 51, kPartitionsSlots.data(), false, drawfx::kTestTable};

// cIGZMessage2Standard: 4 GetType, 13 HasData2, 15 GetVoid2, 32 GetIGZUnknown.
constexpr At kMessageAt[] = {{4, {0, Ret::Int, true, "GetType", ""}},
                             {13, {0, Ret::Int, true, "HasData2", ""}},
                             {15, {0, Ret::Int, true, "GetVoid2", ""}},
                             {32, {0, Ret::Int, true, "GetIGZUnknown", ""}}};
constexpr auto kMessageSlots = Table<37>(kMessageAt);
constexpr drawfx::Iface kMessage{"Message2Standard", 37, kMessageSlots.data(), false, drawfx::kTestTable};

constexpr At kUnknownAt[] = {{0, {2, Ret::Int, true, "QueryInterface", "ua"}}};
constexpr auto kUnknownSlots = Table<3>(kUnknownAt);
constexpr drawfx::Iface kUnknownFace{"Unknown", 3, kUnknownSlots.data(), false, drawfx::kTestTable};

// nGZSceneGraph::cITextureManager: 18 AddRefId, 19 ReleaseId (the handles copied as the vectors grow count -1 too),
// 20 Add, 23 GetTexture.
constexpr At kTexturesAt[] = {{18, {1, Ret::Int, true, "AddRefId", "u"}},
                              {19, {1, Ret::Int, true, "ReleaseId", "u"}},
                              {20, {3, Ret::Int, true, "Add", "asu"}},
                              {23, {1, Ret::Int, true, "GetTexture", "u"}}};
constexpr auto kTexturesSlots = Table<24>(kTexturesAt);
constexpr drawfx::Iface kTextureManager{"TextureManager", 24, kTexturesSlots.data(), false, drawfx::kTestTable};

template <unsigned kSlot, class R, class... Args> R Virtual(void *self, Args... args)
{
    using F = R(__thiscall *)(void *, Args...);
    return (*reinterpret_cast<F *const *>(self))[kSlot](self, args...);
}

constexpr uint32_t kFrameworkCell = 0x10c9378, kServerCell = 0x10f519c, kAllocatorCell = 0x10c95d4,
                   kAllocatorGuard = 0x10c95d8, kBorderValue = 0xffd8e8, kTexturesCell = 0x10f8f94,
                   kGlobalsCell = 0x10cc288, kClockCell = 0x10cc2a8, kWorldCell = 0x10cc45c;
constexpr uint32_t kServerId = 0x04fa845b, kSettingsId = 0x0ac1887e;
constexpr uint32_t kHolderIid = 0xab7cc174;
constexpr uint32_t kImageClass = 0x1c4a276c, kImageIid = 0xfc49f6ee, kTextureClass = 0x2a956dad, kTextureIid = 0x2a956da8;
constexpr uint32_t kSize = sizeof(cLightMapManager);
constexpr uint32_t kBufferTag = 0x7d000000, kPropsTag = 0x7c000000;
constexpr unsigned kImages = 192, kTexturePool = 128;
constexpr unsigned kArena = 65 * 64; // an image's pixel words: up to 64 rows of a 64-wide image with a slack texel

const float kNan = std::bit_cast<float>(0x7fc00000u);
const float kInf = std::bit_cast<float>(0x7f800000u);

// The holder the world-loaded message's object answers kHolderIid with: a class with virtual bases, its vbtable at +8
// ([1] the cIGZUnknown base, [2] the property set the readers take).
struct Holder {
    uint32_t w0, w1;
    const int32_t *vbptr; // +8
    uint32_t pad;
    Fake unknown;
    uint8_t props[16];
};
const int32_t kHolderBases[3] = {0, int32_t(offsetof(Holder, unknown)) - 8, int32_t(offsetof(Holder, props)) - 8};

// What the calls are given (one block, registered, so its pointers log alike).
struct Buffers {
    WallKey wall;
    FloorUse floor;
    float position[3];
    float rgb[3];
    float uv[4];
    int32_t rect[4];
    uint32_t index;
    uint8_t border[8];
};

struct World {
    Fake framework, server, settings, allocator, world, partitions, message, object, ref, textures;
    Fake com, system, lighting, options, clock, globals, changes, target, roofs;
    Fake images[kImages];
    Fake texturePool[kTexturePool];
    unsigned nextImage, nextTexture;
    Holder holder;
    Buffers buf;
    std::vector<WallKey> walls;
    std::vector<FloorKey> floors;
    bool settingsUp;
    uint32_t queryMode;
    uint32_t lcg;
    uint32_t Next() { return lcg = lcg * 1664525u + 1013904223u, lcg >> 8; }
};
World gw;
// Each pool image's pixels (value[4] its row here): value[2] its pitch in bytes, value[0] / value[1] its size.
uint32_t gPixels[kImages][kArena];
void NoteWords(const char *label, const void *p, unsigned count, const char *kinds);

// ---- fakes ----

uint32_t Word0(Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t Word1(Fake &self, const uint32_t *) { return self.value[1]; }
uint32_t *Pixels(const Fake &image) { return gPixels[image.value[4]]; }
uint32_t PixelRgb(Fake &self, const uint32_t *a)
{
    float *const out = reinterpret_cast<float *>(uintptr_t(a[0]));
    const uint32_t x = a[1], y = a[2];
    if (x < self.value[0] && y < self.value[1]) {
        const uint32_t texel = Pixels(self)[y * (self.value[2] / 4) + x];
        out[0] = float((texel >> 16) & 0xff) / 255.0f;
        out[1] = float((texel >> 8) & 0xff) / 255.0f;
        out[2] = float(texel & 0xff) / 255.0f;
    } else {
        out[0] = float(self.id) + float(int32_t(x)) * 0.5f;
        out[1] = float(int32_t(y)) * 0.25f - 3.0f;
        out[2] = float(self.value[2]) + 0.125f;
    }
    return a[0];
}
// Init(width, height, levels, format, scale, entries): the size (2..64, noted when clamped; sometimes a row more, so
// the height tests fail), the pitch (a slack texel sometimes, so the pitch tests fail), pixels; the arena
// filled with noise.
uint32_t ImageInit(Fake &self, const uint32_t *a)
{
    auto clamp = [](uint32_t v) { return int32_t(v) < 2 ? 2u : v > 64 ? 64u : v; };
    if (clamp(a[0]) != a[0] || clamp(a[1]) != a[1]) drawfx::Note("image size clamped", {a[0], a[1]});
    self.value[0] = clamp(a[0]);
    self.value[1] = clamp(a[1]);
    // Never a row less: DrawFloorTile runs the rows bottom up from the image's height, so a floor image shorter than its
    // tiles would be written before its arena (the original's too; the game's images are always the texture's size).
    if (gw.Next() % 12 == 0 && self.value[1] < 64) ++self.value[1];
    self.value[2] = self.value[0] * 4 + (gw.Next() % 8 == 0 ? 4 : 0);
    self.value[3] = 1;
    uint32_t noise = gw.Next() | 1;
    for (uint32_t &p : gPixels[self.value[4]]) p = noise = noise * 0x2545f491u + 0x9e3779b9u;
    return 1;
}
uint32_t ImageData(Fake &self, const uint32_t *) { return drawfx::P(Pixels(self)); }
// Fill(colour, level) / FillOpaque(rgb, level): a word from the colour's bits over the pitch times the height, uniform or
// with noise (so a later blend or kernel has texels that differ).
uint32_t FillWith(Fake &self, const float *colour, unsigned n, uint32_t top)
{
    uint32_t word = top;
    for (unsigned i = 0; i < n; ++i) word = (word ^ std::bit_cast<uint32_t>(colour[i])) * 0x01000193u;
    word |= top;
    const uint32_t count = self.value[2] / 4 * self.value[1], mode = gw.Next() % 3, seed = gw.Next();
    uint32_t *const p = Pixels(self);
    uint32_t noise = seed | 1;
    for (uint32_t i = 0; i < count && i < kArena; ++i) {
        noise = noise * 0x2545f491u + 0x9e3779b9u;
        p[i] = mode == 0 ? word : mode == 1 ? word ^ noise : word ^ (noise & 0x0f0f0f0fu);
    }
    return 1;
}
uint32_t ImageFill(Fake &self, const uint32_t *a) { return FillWith(self, reinterpret_cast<const float *>(uintptr_t(a[0])), 4, 0); }
uint32_t ImageFillOpaque(Fake &self, const uint32_t *a)
{
    return FillWith(self, reinterpret_cast<const float *>(uintptr_t(a[0])), 3, 0xff000000u);
}

// The pool: each object the COM director makes is the next image or texture fake, reset (counted once: refs 1001).
Fake *HandOutImage()
{
    difftest::NotCounted bookkeeping;
    const unsigned i = gw.nextImage++ % kImages;
    if (gw.nextImage > kImages) drawfx::Note("image pool reused", i);
    Fake &f = gw.images[i];
    f.Reset<kImage>(700 + i);
    f.On(6, &ImageInit);
    f.On(8, &ImageData);
    f.On(9, &ImageData);
    f.On(17, &ImageFillOpaque);
    f.On(18, &ImageFill);
    f.On(24, &sgfx::detail::GetWord<3>);
    f.On(26, &Word0);
    f.On(27, &Word1);
    f.On(28, &sgfx::detail::GetWord<2>);
    f.On(38, &PixelRgb);
    f.value[4] = i;
    std::memset(gPixels[i], 0, sizeof gPixels[i]);
    ++f.refs;
    return &f;
}
Fake *HandOutTexture()
{
    difftest::NotCounted bookkeeping;
    const unsigned i = gw.nextTexture++ % kTexturePool;
    if (gw.nextTexture > kTexturePool) drawfx::Note("texture pool reused", i);
    Fake &t = gw.texturePool[i];
    t.Reset<kTexture>(900 + i);
    t.AddFace(kTextureIid, t);
    t.value[4] = i;
    ++t.refs;
    return &t;
}
uint32_t ComGetClassObject(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    Fake *made = nullptr;
    if (a[0] == kImageClass && a[1] == kImageIid) made = HandOutImage();
    else if (a[0] == kTextureClass && a[1] == kTextureIid) made = HandOutTexture();
    if (out) *out = made;
    return made != nullptr;
}
// The texture manager: Add(texture, name, flags) holds the texture and answers 0x100 + its pool index; GetTexture(id)
// that texture.
uint32_t TextureAdd(Fake &, const uint32_t *a)
{
    Fake *const t = reinterpret_cast<Fake *>(uintptr_t(a[0]));
    ++t->refs;
    return 0x100 + t->value[4];
}
uint32_t TextureGet(Fake &, const uint32_t *a)
{
    const uint32_t i = a[0] - 0x100;
    return i < kTexturePool ? drawfx::P(&gw.texturePool[i]) : 0;
}
// The lighting manager's colour (slot 28, the out first): channels from a table (the bytes' rounding edges, 2.0 and
// over, negatives, NaN, infinity).
uint32_t LightingColour(Fake &, const uint32_t *a)
{
    static const float kChannels[] = {0.5f, 0.25f, 1.0f, 1.5f, 2.0f, 1.9921875f, 1.99609375f, 1.98828125f, 0.00390625f,
                                      0.01171875f, -0.25f, 0.0f, -0.0f, kNan, kInf, 0.7f, 0.3333f};
    float *const out = reinterpret_cast<float *>(uintptr_t(a[0]));
    for (int i = 0; i < 3; ++i) out[i] = kChannels[gw.Next() % std::size(kChannels)];
    return a[0];
}
uint32_t Detail(Fake &, const uint32_t *)
{
    static const int32_t kDetails[] = {0, 1, 2, 3, 4, 5, -1};
    return uint32_t(kDetails[gw.Next() % std::size(kDetails)]);
}
uint32_t Clock(Fake &, const uint32_t *) { return gw.Next() % 6 == 0 ? 0x0bea28c3u : 0x0bea28c2u; }
uint32_t GetRoofs(Fake &, const uint32_t *) { return gw.Next() % 4 == 0 ? 0 : drawfx::P(&gw.roofs); }
uint32_t UnderRoof(Fake &, const uint32_t *) { return gw.Next() % 3 == 0; }
const double kHeights[] = {0.0, 0.1, 1.0 / 3.0, 1.5, 3.0, -2.0, 1e9, std::bit_cast<double>(0x7ff8000000000000ull), 7.25,
                           double(0.3f), 2.0000001};
uint32_t RoofHeight(Fake &, const uint32_t *)
{
    return std::bit_cast<uint32_t>(float(kHeights[gw.Next() % std::size(kHeights)]) + 0.5f);
}
uint32_t SystemService(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    Fake *answer = a[0] == kServerId ? &gw.server : a[0] == kSettingsId && gw.settingsUp ? &gw.settings : nullptr;
    *out = answer;
    if (answer) ++answer->refs;
    return answer != nullptr;
}
uint32_t SettingsAdd(Fake &, const uint32_t *a)
{
    sgfx::NoteText("add key", reinterpret_cast<const GZServiceString *>(uintptr_t(a[2]))->c_str());
    return 1;
}
uint32_t SettingsRemove(Fake &, const uint32_t *a)
{
    sgfx::NoteText("remove key", reinterpret_cast<const GZServiceString *>(uintptr_t(a[2]))->c_str());
    return 1;
}
uint32_t Allocate(Fake &, const uint32_t *a) { return drawfx::P(::operator new(a[0] ? a[0] : 1)); }
uint32_t FreeBlock(Fake &, const uint32_t *a)
{
    ::operator delete(reinterpret_cast<void *>(uintptr_t(a[0])));
    return 0;
}
uint32_t Post(Fake &, const uint32_t *a)
{
    void *const message = reinterpret_cast<void *>(uintptr_t(a[0]));
    // receiver: the posted cIGZMessage2Standard (either side's cRZMessage2Standard): 4 GetType, 9 GetData1
    drawfx::Note("posted", {Virtual<4, uint32_t>(message), Virtual<9, uint32_t>(message)});
    return 1;
}
uint32_t PartitionType(Fake &, const uint32_t *a)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    drawfx::Call c;
    c.note = "partition key";
    drawfx::Words(c.words, key, 6, "u");
    drawfx::gLog.push_back(std::move(c));
    static const uint32_t kTypes[] = {0, 1, 2, 3, 0x17, 9, 16, 23, 12, 0x16, 0x18, 7};
    return kTypes[gw.Next() % std::size(kTypes)];
}
uint32_t ObjectQuery(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[1]));
    drawfx::Note("object query", {a[0], gw.queryMode});
    const bool give = gw.queryMode & 1, ok = (gw.queryMode & 2) != 0;
    if (give) {
        *out = &gw.holder;
        ++gw.holder.unknown.refs;
    }
    return ok;
}

// ---- stand-ins ----

uint8_t *Bytes(void *o) { return static_cast<uint8_t *>(o); }
RefPtrVector *Group(void *o, uint32_t at, unsigned i) { return reinterpret_cast<RefPtrVector *>(Bytes(o) + at + i * 12); }

auto *Textures(void *o, unsigned i) { return reinterpret_cast<std::vector<nGZSceneGraph::cTextureHandle> *>(Bytes(o) + 0xe8 + i * 12); }
auto *FreeTiles(void *o) { return reinterpret_cast<std::vector<FloorTile> *>(Bytes(o) + 0x138); }
auto *Atlases(void *o) { return reinterpret_cast<std::vector<nGZGraphic4::cTextureAtlas> *>(Bytes(o) + 0x170); }
auto *Set(void *o, uint32_t at) { return reinterpret_cast<std::set<uint32_t> *>(Bytes(o) + at); }
using FloorMap = std::unordered_map<FloorKey, FloorTile, FloorKeyHash, FloorKeyEqual>;
using WallMap = std::unordered_map<WallKey, WallRegion, WallKeyHash, WallKeyEqual>;
auto *Floors(void *o) { return reinterpret_cast<FloorMap *>(Bytes(o) + 0x118); }
auto *Walls(void *o) { return reinterpret_cast<WallMap *>(Bytes(o) + 0x150); }

uint32_t __cdecl StandBorder(const WallKey *key, int32_t arg)
{
    drawfx::Note("border", {drawfx::Ptr(key), uint32_t(arg)});
    static const uint32_t kAnswers[] = {0, 1, 0x100, 0x10000, 0xffff0000u, 0, 1};
    return kAnswers[gw.Next() % std::size(kAnswers)];
}
bool __cdecl StandSlot80Is2(const uint32_t *id)
{
    drawfx::Note("slot80 is 2", *id);
    return *id % 7 == 2 && *id != 0x17; // never the tall kind 0x17, so AcquireWall's own test of it decides
}
bool __cdecl StandIsKind3(const uint32_t *id)
{
    drawfx::Note("kind is 3", *id);
    return *id % 5 == 3 && *id != 0x17;
}

void *gObject; // the side's manager, for the readers' notes

void ReaderNote(const char *what, void *properties, const char *name, void *field, uint8_t *changed)
{
    drawfx::Call c;
    c.note = what;
    c.words = {drawfx::Ptr(properties), uint32_t(Bytes(field) - Bytes(gObject)), *changed};
    c.text = name;
    drawfx::gLog.push_back(std::move(c));
}
// The reader's effect: 0 none, 1 changed (value and flag), 2 the flag cleared, 3 the value alone.
uint32_t ReaderMode()
{
    return gw.Next() % 4;
}
uint32_t __cdecl StandReadBool(void *properties, const char *name, uint8_t *value, uint8_t *changed)
{
    ReaderNote("read bool", properties, name, value, changed);
    const uint32_t mode = ReaderMode();
    if (mode == 1 || mode == 3) *value = uint8_t(gw.Next() % 3 == 0 ? 0 : 1);
    if (mode == 1) *changed = 1;
    if (mode == 2) *changed = 0;
    return mode;
}
uint32_t __cdecl StandReadFloat(void *properties, const char *name, float *value, uint8_t *changed)
{
    ReaderNote("read float", properties, name, value, changed);
    static const float kValues[] = {20.0f, 1.0f, 0.5f, 2.0f, 0.0f, -1.0f, 0.1f, kNan, 0.75f};
    const uint32_t mode = ReaderMode();
    if (mode == 1 || mode == 3) *value = kValues[gw.Next() % std::size(kValues)];
    if (mode == 1) *changed = 1;
    if (mode == 2) *changed = 0;
    return mode;
}
uint32_t __cdecl StandReadInt(void *properties, const char *name, int32_t *value, uint8_t *changed)
{
    ReaderNote("read int", properties, name, value, changed);
    const uint32_t mode = ReaderMode();
    if (mode == 1 || mode == 3) {
        // Tile sizes stay non-zero (the world size's read divides by them); the texture sizes stay at most 64 (the
        // image fakes' arenas, and the floors drawn only for 64-wide images).
        static const int32_t kTextures[] = {64, 32, 64, 16, 8, 12, 4};
        static const int32_t kTiles[] = {8, 4, 16, 2, 7, 1, 64, 3};
        static const int32_t kSteps[] = {60, 30, 0, 7, -4, 1, 4, 3, -1};
        const uint32_t r = gw.Next();
        if (std::strcmp(name, "floorTextureSize") == 0) *value = kTextures[r % std::size(kTextures)];
        else if (std::strcmp(name, "floorTileSize") == 0) *value = kTiles[r % std::size(kTiles)];
        else *value = kSteps[r % std::size(kSteps)];
    }
    if (mode == 1) *changed = 1;
    if (mode == 2) *changed = 0;
    return mode;
}

void *__cdecl StandWorld()
{
    drawfx::Note("world db", 0);
    return &gw.world;
}
void *__cdecl StandPartitions()
{
    drawfx::Note("partitions", 0);
    return &gw.partitions;
}
void *__cdecl TestRzMalloc(uint32_t bytes) { return ::operator new(bytes ? bytes : 1); }
void __cdecl TestRzFree(void *p) { ::operator delete(p); }
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void __cdecl GameFree(void *p) { ::operator delete(p); }
// The incidence table's allocation (the original's CRT malloc 0x8252b1 and free 0x831a54): the test's CRT, as ours.
void *__cdecl CrtMalloc(size_t n) { return std::malloc(n); }
void __cdecl CrtFree(void *p) { std::free(p); }

void *__cdecl StandCom()
{
    drawfx::Note("com", 0);
    return &gw.com;
}
void *__cdecl StandSystem()
{
    drawfx::Note("system", 0);
    return &gw.system;
}
void *__cdecl StandOptions()
{
    drawfx::Note("options", 0);
    return &gw.options;
}
bool __cdecl StandWallFlag(const WallKey *key)
{
    NoteWords("wall flag", key, 6, "u");
    return gw.Next() % 3 == 0;
}
double __cdecl StandGround(const nTSWorld::WorldPoint *point)
{
    NoteWords("ground", point, 3, "u");
    return kHeights[gw.Next() % std::size(kHeights)];
}

struct Stands {
    shape::CodePatch border{0x7f2340u, shape::CodeOf(&StandBorder)};
    shape::StandIn readBool{0xaf7110u, &nGZSceneGraph::ReadBoolProperty, &StandReadBool};
    shape::StandIn readFloat{0xaf7190u, &nGZSceneGraph::ReadFloatProperty, &StandReadFloat};
    shape::StandIn readInt{0xaf7210u, &nGZSceneGraph::ReadIntProperty, &StandReadInt};
    shape::CodePatch world{0x20e0b2u, shape::CodeOf(&StandWorld)};
    shape::CodePatch system{0x20defbu, shape::CodeOf(&StandSystem)};
    shape::CodePatch options{0x20df9au, shape::CodeOf(&StandOptions)};
    shape::CodePatch wallFlag{0x7e2050u, shape::CodeOf(&StandWallFlag)};
    shape::StandIn ground{0x7e4680u, &worldheights::GroundHeightAt, &StandGround};
    shape::StandIn com{0x1ae18du, &GetDirectorFrameworkObject, &StandCom};
    shape::CodePatch crtMalloc{0x8252b1u, shape::CodeOf(&CrtMalloc)};
    shape::CodePatch crtFree{0x831a54u, shape::CodeOf(&CrtFree)};
    shape::CodePatch partitions{0x20de97u, shape::CodeOf(&StandPartitions)};
    shape::StandIn slot80{0x7ef8e0u, &worldcatalog::Slot80Is2, &StandSlot80Is2};
    shape::StandIn kind3{0x7ef7b0u, &worldcatalog::IsKind3, &StandIsKind3};
    shape::StandIn rzMalloc{0x1be3b0u, &RZMemory::Malloc, &TestRzMalloc};
    shape::StandIn rzFree{0x1be3a1u, &RZMemory::Free, &TestRzFree};
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    shape::CodePatch gameFree{0x82540fu, shape::CodeOf(&GameFree)};
};

// ---- the world ----

float Coordinate(std::mt19937 &rng)
{
    static const float kOdd[] = {0.0f, -0.0f, 1e10f, -1e10f, kNan, kInf, 8388608.0f, 16777216.5f, -3.5f, 255.5f, 127.5f};
    const uint32_t r = rng();
    if (r % 9 == 0) return kOdd[(r / 9) % std::size(kOdd)];
    return float(int32_t((r >> 4) % 12) - 2) * 0.5f;
}

void MakeKeys(std::mt19937 &rng)
{
    gw.walls.clear();
    const unsigned walls = 4 + rng() % 8;
    for (unsigned i = 0; i < walls; ++i) {
        WallKey k{Coordinate(rng), Coordinate(rng), rng() % 3, Coordinate(rng), Coordinate(rng), rng() % 3};
        gw.walls.push_back(k);
        if (rng() % 3 == 0) {
            // The same face a few units in the last place off (inside the equality's window or just past it).
            static const uint32_t kUlps[] = {1, 5, 31, 32, 33, 0xffffffffu, 0xffffffe1u, 0xffffffe0u};
            WallKey nudged = k;
            float *const coords[] = {&nudged.x0, &nudged.y0, &nudged.x1, &nudged.y1};
            float *const c = coords[rng() % 4];
            *c = std::bit_cast<float>(std::bit_cast<uint32_t>(*c) + kUlps[rng() % std::size(kUlps)]);
            gw.walls.push_back(nudged);
        }
        if (rng() % 5 == 0) {
            WallKey other = k;
            (rng() % 2 ? other.word2 : other.word5) ^= 1 + rng() % 2;
            gw.walls.push_back(other);
        }
    }
    gw.floors.clear();
    const unsigned floors = 4 + rng() % 10;
    static const int32_t kLevels[] = {0, 1, 2, 1, 2, 3, -3, 0};
    for (unsigned i = 0; i < floors; ++i)
        gw.floors.push_back(FloorKey{int32_t(rng() % 5), int32_t(rng() % 5), kLevels[rng() % std::size(kLevels)]});
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    gw.lcg = seed * 2654435761u + 7;
    gw.framework.Reset<kFramework>(460);
    gw.framework.On(5, &SystemService);
    gw.server.Reset<kServer>(461);
    gw.server.On(4, &Post);
    gw.settings.Reset<kSettings>(462);
    gw.settings.On(4, &SettingsAdd);
    gw.settings.On(5, &SettingsRemove);
    gw.allocator.Reset<kAllocator>(463);
    gw.allocator.On(3, &Allocate);
    gw.allocator.On(4, &FreeBlock);
    gw.world.Reset<kWorld>(464);
    gw.world.On(85, &Word0);
    gw.world.On(86, &Word1);
    gw.partitions.Reset<kPartitions>(465);
    gw.partitions.On(50, &PartitionType);
    gw.message.Reset<kMessage>(466);
    gw.object.Reset<kUnknownFace>(467);
    gw.object.On(0, &ObjectQuery);
    gw.ref.Reset<kUnknownFace>(468);
    gw.textures.Reset<kTextureManager>(470);
    gw.textures.On(20, &TextureAdd);
    gw.textures.On(23, &TextureGet);
    gw.holder.unknown.Reset<kUnknownFace>(469);
    gw.holder.vbptr = kHolderBases;
    gw.holder.w0 = gw.holder.w1 = gw.holder.pad = 0;
    std::memset(gw.holder.props, 0, sizeof gw.holder.props);
    gw.com.Reset<kCom>(471);
    gw.com.On(3, &ComGetClassObject);
    gw.system.Reset<kSystem>(472);
    gw.system.Returns(49, static_cast<const void *>(&gw.lighting));
    gw.lighting.Reset<kLighting>(473);
    gw.lighting.On(28, &LightingColour);
    gw.options.Reset<kOptions>(474);
    gw.options.On(21, &Detail);
    gw.clock.Reset<kClock>(475);
    gw.clock.On(5, &Clock);
    gw.globals.Reset<kGlobals>(476);
    gw.globals.Returns(22, static_cast<const void *>(&gw.changes));
    gw.changes.Reset<kChanges>(477);
    gw.changes.Returns(20, static_cast<const void *>(&gw.target));
    gw.target.Reset<kTarget>(478);
    gw.roofs.Reset<kRoofs>(479);
    gw.roofs.On(22, &RoofHeight);
    gw.roofs.On(37, &UnderRoof);
    gw.world.On(36, &GetRoofs);
    gw.world.Returns(39, static_cast<const void *>(&gw.partitions));
    gw.nextImage = gw.nextTexture = 0;
    gw.settingsUp = seed % 5 != 0;
    std::mt19937 rng(seed ^ 0x5bd1e995u);
    static const uint32_t kWorldSides[] = {64, 128, 0, 1, 256, 0x80000000u};
    gw.world.value[0] = kWorldSides[rng() % std::size(kWorldSides)];
    gw.world.value[1] = kWorldSides[rng() % std::size(kWorldSides)];
    MakeKeys(rng);
    std::memset(&gw.buf, 0, sizeof gw.buf);
    drawfx::AddRange(&gw.buf, sizeof gw.buf, kBufferTag);
    drawfx::AddRange(gw.holder.props, sizeof gw.holder.props, kPropsTag);
    cells.Set(kFrameworkCell, &gw.framework);
    cells.Set(kServerCell, &gw.server);
    cells.Set(kTexturesCell, &gw.textures);
    cells.Set(kAllocatorCell, &gw.allocator);
    cells.Set(kAllocatorGuard, 0x80000000u);
    cells.Set(kBorderValue, seed % 2);
    cells.Set(kGlobalsCell, &gw.globals);
    cells.Set(kClockCell, &gw.clock);
    cells.Set(kWorldCell, &gw.world);
}

void NoteFakes()
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "fake refs";
    for (Fake *f : {&gw.framework, &gw.server, &gw.settings, &gw.allocator, &gw.world, &gw.partitions, &gw.message, &gw.textures,
                    &gw.object, &gw.ref, &gw.holder.unknown, &gw.com, &gw.system, &gw.lighting, &gw.options, &gw.clock,
                    &gw.globals, &gw.changes, &gw.target, &gw.roofs})
        c.words.push_back(uint32_t(f->refs));
    c.words.push_back(gw.nextImage);
    for (unsigned i = 0; i < gw.nextImage && i < kImages; ++i) c.words.push_back(uint32_t(gw.images[i].refs));
    c.words.push_back(gw.nextTexture);
    for (unsigned i = 0; i < gw.nextTexture && i < kTexturePool; ++i) c.words.push_back(uint32_t(gw.texturePool[i].refs));
    drawfx::gLog.push_back(std::move(c));
}

// Each image handed out on this side: its size, pitch and a hash of its whole arena (margins and slack included).
void NotePixels(const char *label)
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = label;
    for (unsigned i = 0; i < gw.nextImage && i < kImages; ++i) {
        const Fake &f = gw.images[i];
        uint32_t hash = 2166136261u;
        for (uint32_t p : gPixels[i]) hash = (hash ^ p) * 16777619u;
        c.words.insert(c.words.end(), {i, f.value[0], f.value[1], f.value[2], f.value[3], hash});
    }
    drawfx::gLog.push_back(std::move(c));
}

// ---- the object ----

uint32_t W(const void *o, uint32_t at) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(o) + at); }

// Each word's kind: the vtables left out, pointers (vectors, tree heads, list heads, the counted ref) strict.
const char *ObjectKinds()
{
    static char kinds[kSize / 4 + 1];
    if (kinds[0]) return kinds;
    std::memset(kinds, 'u', kSize / 4);
    kinds[0] = kinds[1] = kinds[2] = '-';
    auto pointer = [](uint32_t at, unsigned n = 1) {
        for (unsigned i = 0; i < n; ++i) kinds[at / 4 + i] = 'a';
    };
    pointer(0x14);              // the message ids' head
    pointer(0x78, 24);          // the image vectors
    pointer(0xd8), pointer(0xe0);
    pointer(0xe8, 12);          // the texture vectors
    pointer(0x11c), pointer(0x124, 3); // map A: the list's head, the bucket vector
    pointer(0x138, 3);          // the free tiles
    pointer(0x154), pointer(0x15c, 3); // map B
    pointer(0x170, 3);          // the atlases
    pointer(0x18c);
    return kinds;
}

void NoteWords(const char *label, const void *p, unsigned count, const char *kinds)
{
    drawfx::Call c;
    c.note = label;
    drawfx::Words(c.words, p, count, kinds);
    drawfx::gLog.push_back(std::move(c));
}

template <class Map> void NoteMap(const char *label, void *o, uint32_t at, Map *map)
{
    drawfx::Call c;
    c.note = label;
    for (const auto &[key, value] : *map) {
        drawfx::Words(c.words, &key, sizeof key / 4, "u");
        drawfx::Words(c.words, &value, sizeof value / 4, "u");
    }
    // The bucket vector (pairs of list iterators: the head or a node).
    const uint32_t first = W(o, at + 0xc), last = W(o, at + 0x10);
    drawfx::Words(c.words, reinterpret_cast<const void *>(uintptr_t(first)), (last - first) / 4, "a");
    drawfx::gLog.push_back(std::move(c));
}

void NoteObject(void *o)
{
    difftest::NotCounted bookkeeping;
    // A vector's storage of 4096 bytes or more is carved 32-aligned from a larger block by both sides' std::allocator
    // (the block's address kept in the word before it), so where it starts in its block follows the heap's address.
    // Each such vector's three pointers are noted as offsets into its block: the block, then the size and capacity.
    uint32_t copy[kSize / 4];
    std::memcpy(copy, o, kSize);
    for (uint32_t at : {0x78u, 0x84u, 0x90u, 0x9cu, 0xa8u, 0xb4u, 0xc0u, 0xccu, 0xe8u, 0xf4u, 0x100u, 0x10cu, 0x124u,
                        0x138u, 0x15cu, 0x170u}) {
        uint32_t *const v = copy + at / 4;
        if (v[0] == 0 || v[2] - v[0] < 4096) continue;
        const uint32_t block = *reinterpret_cast<const uint32_t *>(uintptr_t(v[0]) - 4);
        if (v[0] - block < 4 || v[0] - block > 39) continue; // not carved: left as it is
        v[1] = block + (v[1] - v[0]);
        v[2] = block + (v[2] - v[0]);
        v[0] = block;
    }
    NoteWords("object", copy, kSize / 4, ObjectKinds());
    {
        drawfx::Call c;
        c.note = "message ids";
        for (uint32_t id : *Set(o, 0x14)) c.words.push_back(id);
        c.words.push_back(0xeeee);
        for (uint32_t id : *Set(o, 0xd8)) c.words.push_back(id);
        c.words.push_back(0xeeee);
        for (uint32_t id : *Set(o, 0xe0)) c.words.push_back(id);
        drawfx::gLog.push_back(std::move(c));
    }
    {
        drawfx::Call c;
        c.note = "images";
        for (unsigned i = 0; i < 8; ++i) {
            const RefPtrVector *const v = Group(o, 0x78, i);
            c.words.push_back(v->Size());
            drawfx::Words(c.words, v->mFirst, v->Size(), "a");
        }
        for (unsigned i = 0; i < 4; ++i) {
            const auto *const t = Textures(o, i);
            c.words.push_back(uint32_t(t->size()));
            for (const auto &h : *t) c.words.push_back(h.mId);
        }
        drawfx::gLog.push_back(std::move(c));
    }
    NoteMap("floors", o, 0x118, Floors(o));
    NoteMap("walls", o, 0x150, Walls(o));
    {
        drawfx::Call c;
        c.note = "tiles, atlases";
        for (const FloorTile &t : *FreeTiles(o)) c.words.push_back(std::bit_cast<uint32_t>(t));
        for (const auto &a : *Atlases(o)) {
            c.words.push_back(0xaaaa);
            c.words.push_back(a.mRegions);
            c.words.push_back(uint32_t(a.mFreeArea));
            for (const auto &n : a.mNodes) {
                drawfx::Words(c.words, &n, 7, "u");
                c.words.push_back(n.used);
            }
            for (uint32_t f : a.mFreeNodes) c.words.push_back(f);
        }
        drawfx::gLog.push_back(std::move(c));
    }
    drawfx::Note("border value", *GameAddress<uint32_t>(kBorderValue));
}

template <unsigned kSlot, class R = uint32_t, class... Args> R Slot0(void *o, Args... args)
{
    return Virtual<kSlot, R>(o, args...);
}

struct Run {
    difftest::Context &ctx;
    bool original;
    void *o;
    // Shutdown clears the images but keeps the atlases and floor tiles (only Stop clears those): until a Stop, the
    // original's drawers (AcquireWall / AcquireFloor on a face or tile met before) index the image vectors past their
    // end. The game shuts the manager down only at exit, so those two calls are left out until the next Stop.
    bool shut = false;

    WallKey *PickWall(std::mt19937 &rng)
    {
        gw.buf.wall = gw.walls[rng() % gw.walls.size()];
        return &gw.buf.wall;
    }
    FloorUse *PickFloor(std::mt19937 &rng)
    {
        static const uint8_t kFlags[] = {1, 2, 4, 3, 0x80, 0, 6, 1};
        gw.buf.floor.key = gw.floors[rng() % gw.floors.size()];
        gw.buf.floor.flags = kFlags[rng() % std::size(kFlags)];
        std::memset(gw.buf.floor.pad, 0xa5, sizeof gw.buf.floor.pad);
        return &gw.buf.floor;
    }
    void Outs()
    {
        for (float &f : gw.buf.uv) f = -7.0f;
        for (int32_t &r : gw.buf.rect) r = 0x5a5a5a5a;
        gw.buf.index = 0xa5a5a5a5u;
    }
    void NoteOuts(const char *label, uint32_t result)
    {
        drawfx::Call c;
        c.note = label;
        c.words.push_back(result);
        drawfx::Words(c.words, &gw.buf.uv, 9, "u");
        drawfx::gLog.push_back(std::move(c));
    }
    void Query(std::mt19937 &rng)
    {
        static const uint32_t kIids[] = {cILightMapManager::kIid, cLightMapManager::kClass, 1u, 0x0bf077fdu, 0u, kHolderIid};
        const uint32_t iid = kIids[rng() % std::size(kIids)];
        void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
        void **const where = rng() % 6 == 0 ? nullptr : &out;
        const uint32_t face = (rng() % 3) * 4;
        const bool found = Virtual<0, uint8_t>(Bytes(o) + face, iid, where) != 0;
        drawfx::Note("query", {iid, face, uint32_t(found), found && where ? drawfx::Ptr(out) : 0x5a5a, W(o, 0xc)});
        if (found) Slot0<2>(o);
    }
    void Message(std::mt19937 &rng)
    {
        static const uint32_t kTypes[] = {cLightMapManager::kWorldLoaded, cLightMapManager::kBorderSetting, 0x12345678,
                                          cLightMapManager::kChanged, cLightMapManager::kWorldLoaded};
        const uint32_t type = kTypes[rng() % std::size(kTypes)];
        gw.message.Returns(4, type);
        gw.message.Returns(13, uint32_t(rng() % 3 != 0));
        for (uint8_t &b : gw.buf.border) b = uint8_t(rng() % 3);
        gw.message.Returns(15, rng() % 5 == 0 ? nullptr : static_cast<const void *>(gw.buf.border));
        gw.message.Returns(32, static_cast<const void *>(&gw.object)); // never null (the original reads through it)
        gw.queryMode = rng() % 4;
        // receiver: our cIGZMessageTarget2 face (+4), slot 3 DoMessage
        const uint32_t r = Virtual<3, uint8_t>(Bytes(o) + 4, static_cast<void *>(&gw.message));
        drawfx::Note("message", {type, r});
    }

    bool Step(std::mt19937 &rng)
    {
        const unsigned what = rng() % 40;
        if (what < 2) {
            Slot0<3, uint8_t>(o);
        } else if (what < 3) {
            Slot0<4, uint8_t>(o);
            shut = true;
        } else if (what < 5) {
            Slot0<5, void>(o);
        } else if (what < 6) {
            Slot0<6, void>(o);
            shut = false;
        } else if (what < 7) {
            const unsigned slot = rng() % 20;
            if (slot < 7) {
                // Update runs each frame in the game: a few in a row, so a transition meets its last steps.
                for (unsigned n = 1 + rng() % 4; n; --n) Slot0<7, void>(o);
            } else if (slot < 10) {
                static const float kChannels[] = {0.5f, 0.25f, 1.0f, 2.0f, 0.0f, -0.5f, 1.9921875f, kNan, 0.00390625f, 0.998f};
                for (float &c : gw.buf.rgb) c = kChannels[rng() % std::size(kChannels)];
                Slot0<8, void>(o, static_cast<const float *>(gw.buf.rgb));
            } else if (slot < 14) {
                // `on` is tested as its low byte: the high bytes set alone must read as false.
                static const uint32_t kOn[] = {0, 1, 0x100, 0x101};
                Slot0<9, void>(o, kOn[rng() % std::size(kOn)]);
            } else if (slot < 17) {
                // The original's kernel reads mm7 without setting it (cLightMapManager.cpp's IncidenceKernel): 0 here,
                // as ours takes it, and nothing in between loads a double onto the x87 stack.
                __asm { pxor mm7, mm7 }
                __asm { emms }
                Slot0<10, void>(o);
            } else {
                static const float kSteps[] = {0.0f, 0.25f, 0.5f, 1.0f, 1.5f, -0.5f, kNan, 0.00196f};
                Slot0<11, void>(o, kSteps[rng() % std::size(kSteps)]);
            }
        } else if (what < 13) {
            if (shut) {
                drawfx::Note("no acquire after shutdown", 12);
                return false;
            }
            drawfx::Note("acquire wall", uint32_t(Slot0<12, int32_t>(o, static_cast<const WallKey *>(PickWall(rng)))));
        } else if (what < 16) {
            const WallKey *const key = PickWall(rng);
            if (Walls(o)->find(*key) != Walls(o)->end()) Slot0<13, void>(o, key);
        } else if (what < 21) {
            if (shut) {
                drawfx::Note("no acquire after shutdown", 14);
                return false;
            }
            drawfx::Note("acquire floor", uint32_t(Slot0<14, int32_t>(o, static_cast<const FloorUse *>(PickFloor(rng)))));
        } else if (what < 24) {
            Slot0<15, void>(o, static_cast<const FloorUse *>(PickFloor(rng)));
        } else if (what < 26) {
            const FloorKey &k = gw.floors[rng() % gw.floors.size()];
            static const float kFractions[] = {0.0f, 0.5f, 0.25f, 0.999f, 0.0625f, 0.4375f, 0.75f, 0.1f};
            gw.buf.position[0] = float(k.x) + kFractions[rng() % std::size(kFractions)];
            gw.buf.position[1] = float(k.y) + kFractions[rng() % std::size(kFractions)];
            if (rng() % 8 == 0) gw.buf.position[rng() % 2] = Coordinate(rng);
            gw.buf.position[2] = std::bit_cast<float>(k.level);
            float out[3] = {-1.0f, -1.0f, -1.0f};
            float *const r = Slot0<16, float *>(o, static_cast<float *>(out), static_cast<const float *>(gw.buf.position));
            drawfx::Note("floor light", {uint32_t(r == out), std::bit_cast<uint32_t>(out[0]), std::bit_cast<uint32_t>(out[1]),
                                         std::bit_cast<uint32_t>(out[2])});
        } else if (what < 28) {
            float out[3] = {-1.0f, -1.0f, -1.0f};
            float *const r = Slot0<17, float *>(o, static_cast<float *>(out), static_cast<const WallKey *>(PickWall(rng)));
            drawfx::Note("wall light", {uint32_t(r == out), std::bit_cast<uint32_t>(out[0]), std::bit_cast<uint32_t>(out[1]),
                                        std::bit_cast<uint32_t>(out[2])});
        } else if (what < 30) {
            Outs();
            const uint32_t r = Slot0<18, uint8_t>(o, static_cast<const FloorUse *>(PickFloor(rng)), &gw.buf.uv[0], &gw.buf.uv[2]);
            NoteOuts("floor uv", r);
        } else if (what < 32) {
            Outs();
            int32_t *const rc = gw.buf.rect;
            const uint32_t r = Slot0<19, uint8_t>(o, static_cast<const FloorUse *>(PickFloor(rng)), &rc[0], &rc[1], &rc[2],
                                                  &rc[3], &gw.buf.index);
            NoteOuts("floor rect", r);
        } else if (what < 34) {
            Outs();
            uint32_t *const atlas = rng() % 4 == 0 ? nullptr : &gw.buf.index;
            const uint32_t r = Slot0<20, uint8_t>(o, static_cast<const WallKey *>(PickWall(rng)), &gw.buf.uv[0], &gw.buf.uv[2], atlas);
            NoteOuts("wall uv", r);
        } else if (what < 35) {
            Outs();
            int32_t *const rc = gw.buf.rect;
            const uint32_t r = Slot0<21, uint8_t>(o, static_cast<const WallKey *>(PickWall(rng)), &rc[0], &rc[1], &rc[2],
                                                  &rc[3], &gw.buf.index);
            NoteOuts("wall rect", r);
        } else if (what < 36) {
            static const uint32_t kIndices[] = {0, 1, 2, 3, 0xffffffffu, 0x80000000u};
            const uint32_t group = rng() % 6, index = kIndices[rng() % std::size(kIndices)];
            drawfx::Note("image", {group, index, drawfx::Ptr(Slot0<22, void *>(o, group, index))});
            static const int32_t kWhich[] = {0, 1, 2, -1};
            const int32_t which = kWhich[rng() % std::size(kWhich)];
            drawfx::Note("scale", {uint32_t(which), std::bit_cast<uint32_t>(Slot0<23, float>(o, which))});
        } else if (what < 38) {
            Message(rng);
        } else if (what < 39) {
            Query(rng);
        } else {
            drawfx::Note("addref, release", {Slot0<1>(o), Slot0<2>(o)});
        }
        return what >= 3 && what < 21; // Start, the slots 7-11, the walls and floors: the steps that make or draw images
    }
};

void RunLife(difftest::Context &ctx, uint32_t seed, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    void *o;
    if (seed % 4 == 0) {
        o = original ? ctx.Original<void *(__cdecl *)()>(0x2c73f0)() : static_cast<void *>(cLightMapManager::Create());
        // The bytes neither constructor writes (the padding after +0x10, +0x24 and +0x3a, and the unset words), set alike.
        for (uint32_t at : {0x1cu, 0x184u, 0x188u, 0x19cu, 0x24u}) *reinterpret_cast<uint32_t *>(Bytes(o) + at) = rng();
        for (uint32_t at : {0x11u, 0x12u, 0x13u, 0x3au, 0x3bu}) Bytes(o)[at] = uint8_t(rng());
    } else {
        o = ::operator new(kSize);
        for (uint32_t i = 0; i < kSize; ++i) Bytes(o)[i] = uint8_t(rng());
        if (original) ctx.Original<void *(__thiscall *)(void *)>(0x2c4950)(o);
        else cLightMapManager::Construct(o, nullptr);
    }
    if (rng() % 3 == 0) {
        // The floor and wall light scales (+0x68, +0x6c): only the constructor writes them (both 2.0), so set them alike
        // to two other values for the reads in slots 16 / 17 to tell them apart.
        *reinterpret_cast<float *>(Bytes(o) + 0x68) = 0.75f;
        *reinterpret_cast<float *>(Bytes(o) + 0x6c) = 3.5f;
    }
    // lightMapDebug (+0x39) and the incidence map scale (+0x70): the readers set them only when a settings read happens,
    // so set them alike too (the debug colours in slot 8; a scale of 0, -0, tiny or infinite in slot 10).
    if (rng() % 3 == 0) Bytes(o)[0x39] = 1;
    if (rng() % 2 == 0) {
        // The transition's step count (+0x194; 60 from the constructor) set alike to a few steps, so slot 7 meets the
        // last step (+0x190 at 1) and the quarter of slot 9's detail 3 rounds to 0 within one life.
        static const int32_t kSteps[] = {1, 2, 3, 4, 7};
        *reinterpret_cast<int32_t *>(Bytes(o) + 0x194) = kSteps[rng() % std::size(kSteps)];
    }
    if (rng() % 3 == 0) {
        static const float kScales[] = {0.0f, -0.0f, 1e-30f, kInf, 0.5f};
        *reinterpret_cast<float *>(Bytes(o) + 0x70) = kScales[rng() % std::size(kScales)];
    }
    {
        // The light map sizes (+0x44 floors, +0x4c walls; 0x200 from the constructor) set alike to at most 64 (the image
        // fakes' arenas; the settings change only the floors'), sometimes the walls' tile size (+0x50) and the floor and
        // wall scales (+0x60, +0x64; equal from the constructor, so the drawers' choice of scale shows).
        static const int32_t kFloorSizes[] = {64, 32, 64, 16, 8, 64};
        static const int32_t kWallSizes[] = {16, 32, 64, 32, 8};
        *reinterpret_cast<int32_t *>(Bytes(o) + 0x44) = kFloorSizes[rng() % std::size(kFloorSizes)];
        *reinterpret_cast<int32_t *>(Bytes(o) + 0x4c) = kWallSizes[rng() % std::size(kWallSizes)];
        if (rng() % 3 == 0) *reinterpret_cast<int32_t *>(Bytes(o) + 0x50) = 4;
        if (rng() % 3 == 0) {
            *reinterpret_cast<float *>(Bytes(o) + 0x60) = 0.75f;
            *reinterpret_cast<float *>(Bytes(o) + 0x64) = 0.25f;
        }
    }
    drawfx::Note("made", drawfx::Ptr(o));
    gObject = o;
    if (rng() % 3 == 0) {
        // The counted object LG6b keeps at +0x18c.
        *reinterpret_cast<void **>(Bytes(o) + 0x18c) = &gw.ref;
        ++gw.ref.refs;
    }
    Run run{ctx, original, o};
    NoteObject(o);
    Slot0<1>(o);
    const unsigned steps = 4 + rng() % 60;
    for (unsigned i = 0; i < steps; ++i) {
        sgfx::MarkStep(ctx, "step");
        if (run.Step(rng)) NotePixels("pixels");
        NoteObject(o);
    }
    NotePixels("pixels before delete");
    sgfx::MarkStep(ctx, "delete");
    if (rng() % 2) {
        drawfx::Note("released", Slot0<2>(o));
    } else {
        // receiver: the +8 rz::cRZUnknown face, slot 3 (the deleting destructor's +8 thunk)
        void *const result = Virtual<3, void *>(Bytes(o) + 8, 1u);
        drawfx::Note("deleted", uint32_t(Bytes(result) - Bytes(o)));
    }
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

// ---- the functors ----

float KeyFloat(std::mt19937 &rng)
{
    static const float kOdd[] = {0.0f, -0.0f, 0.5f, -0.5f, 1.5f, 255.5f, 256.0f, 8388607.5f, 8388608.0f, 16777217.0f,
                                 -8388609.0f, 1e10f, -1e10f, kInf, -kInf, kNan, std::bit_cast<float>(0xffc00001u),
                                 std::bit_cast<float>(1u), std::bit_cast<float>(0x80000001u), 127.49999f, -127.5f};
    const uint32_t r = rng();
    if (r % 3 == 0) return kOdd[(r / 3) % std::size(kOdd)];
    return float(int32_t(r % 4000) - 2000) * 0.125f;
}

} // namespace

// Verifies: the constructor 0x2c4950, the factory 0x2c73f0, the destructor 0x2c4f50 with the deleting destructor 0x2c5310
// and its +8 thunk 0x2c5305, QueryInterface 0x2c7db0 as this class's vtables have it, Init 0x2c7460, Shutdown 0x2c8140,
// Start 0x2c7bb0, Stop 0x2c7bf0, the forwarders of slots 7-11, AcquireWall 0x2c6080, ReleaseWall 0x2c7fa0, AcquireFloor
// 0x2c5990, ReleaseFloor 0x2c7ed0, GetFloorLight 0x2c76a0, GetWallLight 0x2c78e0, GetFloorUV 0x2c71a0, GetFloorRect 0x2c72d0,
// GetWallUV 0x2c88f0, GetWallRect 0x2c8aa0, GetImage 0x2c7660, GetScale 0x2c7640, DoMessage 0x2c6d90, the helpers
// 0x2c7110, 0x2c8820, 0x2c8890, 0x2c7a20, 0x2c7aa0, 0x2c7e00, 0x2c7e40, 0x2c7ea0, 0x2c8070, 0x2c80f0, 0x2c83e0, and the
// hash-map code (0x2c4430, 0x2c44c0, 0x2c4690, 0x2c8b70, 0x2c8cd0, 0x2c8ea0); and (lane 261108t, LG6b) the slots 7-11, Update
// 0x2c7cd0, SetColor 0x2c6460, SetTransition 0x2c6eb0, UpdateIncidence 0x2c6ad0 and Advance 0x2c7540, the floor image
// 0x2c5340, the wall atlas 0x2c5a60, the drawers 0x2c6200 and 0x2c6630, Blend 0x2c8280, Upload 0x2c85c0, and through them
// WallFaceHeight 0x2c6f20, MakeIncidenceTable 0x2c6950, Incidence 0x2c6860, the kernel 0x2c6890, FreeIncidenceTable
// 0x2c6d80 and BlendImage 0x2c7570 (each also tested directly below).
DIFF_TEST(SG_LightMapManagerLife, 0x2c6080)
{
    Stands stands;
    uint32_t current = 0;
    sgfx::CheckSides(
        ctx, "light map manager", 2000,
        [&](uint32_t seed, drawfx::Cells &cells) {
            current = seed;
            Build(seed, cells);
        },
        [&](bool original, std::mt19937 &rng) {
            RunLife(ctx, current, original, rng);
            ++ctx.cases;
        });
}

// Verifies the functors 0x2c5270, 0x2c5290 and 0x2c5100 (called as (searched, stored)).
DIFF_TEST(SG_LightMapManagerKeys, 0x2c5290)
{
    const auto floorHash = ctx.Original<uint32_t(__thiscall *)(void *, const FloorKey *)>(0x2c5270);
    const auto wallHash = ctx.Original<uint32_t(__thiscall *)(void *, const WallKey *)>(0x2c5290);
    const auto wallEqual = ctx.Original<uint8_t(__thiscall *)(void *, const WallKey *, const WallKey *)>(0x2c5100);
    std::mt19937 rng(0x2c5290);
    uint32_t functor = 0;
    for (unsigned i = 0; i < 200000; ++i) {
        const FloorKey fk{int32_t(rng()), int32_t(rng() % 5000) - 100, int32_t(rng() % 7) - 2};
        const uint32_t theirsF = floorHash(&functor, &fk), oursF = uint32_t(FloorKeyHash{}(fk));
        WallKey a{KeyFloat(rng), KeyFloat(rng), rng() % 3, KeyFloat(rng), KeyFloat(rng), rng() % 3};
        WallKey b = a;
        float *const coords[] = {&b.x0, &b.y0, &b.x1, &b.y1};
        switch (rng() % 6) {
        case 0: break;
        case 1: {
            static const uint32_t kUlps[] = {1, 2, 31, 32, 33, 0xffffffffu, 0xffffffe1u, 0xffffffe0u, 0xffffffdfu, 0x80000000u};
            float *const c = coords[rng() % 4];
            *c = std::bit_cast<float>(std::bit_cast<uint32_t>(*c) + kUlps[rng() % std::size(kUlps)]);
            break;
        }
        case 2: (rng() % 2 ? b.word2 : b.word5) += 1; break;
        case 3: *coords[rng() % 4] = KeyFloat(rng); break;
        case 4: *coords[rng() % 4] = -*coords[rng() % 4]; break;
        default: b = WallKey{KeyFloat(rng), KeyFloat(rng), rng() % 3, KeyFloat(rng), KeyFloat(rng), rng() % 3}; break;
        }
        const uint32_t theirsW = wallHash(&functor, &a), oursW = uint32_t(WallKeyHash{}(a));
        const bool theirsE = wallEqual(&functor, &a, &b) != 0, oursE = WallKeyEqual{}(a, b);
        ++ctx.cases;
        if (theirsF != oursF) {
            ctx.Fail("floor hash of (%d, %d, %d): theirs %08x, ours %08x", fk.x, fk.y, fk.level, theirsF, oursF);
            return;
        }
        if (theirsW != oursW || theirsE != oursE) {
            ctx.Fail("wall key %08x %08x %u %08x %08x %u vs %08x %08x %u %08x %08x %u: hash %08x / %08x, equal %d / %d",
                     std::bit_cast<uint32_t>(a.x0), std::bit_cast<uint32_t>(a.y0), a.word2, std::bit_cast<uint32_t>(a.x1),
                     std::bit_cast<uint32_t>(a.y1), a.word5, std::bit_cast<uint32_t>(b.x0), std::bit_cast<uint32_t>(b.y0),
                     b.word2, std::bit_cast<uint32_t>(b.x1), std::bit_cast<uint32_t>(b.y1), b.word5, theirsW, oursW,
                     int(theirsE), int(oursE));
            return;
        }
    }
}

// Verifies MakeIncidenceTable 0x2c6950 and FreeIncidenceTable 0x2c6d80: the whole table (header, offsets, the 0x4000
// entries) for chosen and random scales and clamps (0, negatives, NaN, infinity, the cut's edges), the block it is carved
// from, and each side's free of the other's table (the counter sees no block left).
DIFF_TEST(SG_LightMapManagerTable, 0x2c6950)
{
    shape::CodePatch crtMalloc{0x8252b1u, shape::CodeOf(&CrtMalloc)};
    shape::CodePatch crtFree{0x831a54u, shape::CodeOf(&CrtFree)};
    const auto make = ctx.Original<uint8_t *(__cdecl *)(float, float)>(0x2c6950);
    const auto release = ctx.Original<void(__cdecl *)(uint8_t *)>(0x2c6d80);
    static const float kScales[] = {20.0f, 1.0f, 0.5f, 2.0f, 0.1f, 100.0f, 1e-3f, 3.3f, 7.0f, 1e6f, kNan, kInf, -1.0f,
                                    0.25f, 1e-20f, 40.0f, 5000.0f};
    static const float kClamps[] = {1.0f, 0.8f, 0.0f, 0.5f, -1.0f, 2.0f, kNan, 0.99f, 0.3f};
    std::mt19937 rng(0x2c6950);
    const unsigned chosen = unsigned(std::size(kScales) * std::size(kClamps));
    for (unsigned i = 0; i < chosen + 400; ++i) {
        float inverse, clamp;
        if (i < chosen) {
            inverse = 2.0f / kScales[i / std::size(kClamps)];
            clamp = kClamps[i % std::size(kClamps)];
        } else {
            const uint32_t r = rng();
            inverse = r % 5 == 0 ? float(int32_t(rng() % 2001) - 1000) * 0.01f : float(rng() % 1000000) * 1e-4f;
            clamp = r % 7 == 0 ? kClamps[rng() % std::size(kClamps)] : float(int32_t(rng() % 3001) - 1000) * 0.001f;
        }
        difftest::AllocationCounter counter;
        uint8_t *const theirs = make(inverse, clamp);
        uint8_t *const ours = cLightMapManager::MakeIncidenceTable(inverse, clamp);
        const ptrdiff_t theirsCarve = theirs - static_cast<uint8_t *>(reinterpret_cast<void **>(theirs)[-1]);
        const ptrdiff_t oursCarve = ours - static_cast<uint8_t *>(reinterpret_cast<void **>(ours)[-1]);
        unsigned at = 0;
        while (at < 0x20020 && theirs[at] == ours[at]) ++at;
        // Each frees the other's table: the block's address is kept where both look for it.
        if (i % 2) {
            release(ours);
            cLightMapManager::FreeIncidenceTable(theirs);
        } else {
            release(theirs);
            cLightMapManager::FreeIncidenceTable(ours);
        }
        counter.Stop();
        ++ctx.cases;
        if (at != 0x20020 || theirsCarve != oursCarve || counter.Blocks() != 0) {
            ctx.Fail("table for inverse %08x clamp %08x: first difference at +%#x, carved at +%d / +%d, %d blocks left",
                     std::bit_cast<uint32_t>(inverse), std::bit_cast<uint32_t>(clamp), at, int(theirsCarve),
                     int(oursCarve), int(counter.Blocks()));
            return;
        }
    }
}

// Verifies the incidence kernel 0x2c6890 and its cdecl wrapper Incidence 0x2c6860: random texels, sizes 1..9 with slack
// in both pitches, made tables (the weights that keep the index in range), tables with random header words and entries
// (weights within +-15, so no sum saturates and the index stays in the table) and made tables with wide weights (+-31 or
// +-32: the sums saturate with full-range texels, and the index still stays in the table); dst compared with a row of
// margin before and after.
DIFF_TEST(SG_LightMapManagerKernel, 0x2c6890)
{
    using KernelFn = void(__stdcall *)(uint8_t *, int32_t, const uint8_t *, int32_t, int32_t, int32_t, const uint8_t *);
    using WrapperFn = void(__cdecl *)(uint8_t *, int32_t, const uint8_t *, int32_t, int32_t, int32_t, const uint8_t *);
    const auto kernel = ctx.Original<KernelFn>(0x2c6890);
    const auto wrapper = ctx.Original<WrapperFn>(0x2c6860);
    static const float kScales[] = {20.0f, 1.0f, 0.5f, 2.0f, 7.0f, 100.0f, 0.1f};
    static const float kClamps[] = {1.0f, 0.8f, 0.0f, 0.5f};
    alignas(8) static uint8_t table[0x20020];
    constexpr unsigned kMaxSrc = (2 * 9 + 2) * 4 * (2 * 9), kMaxDst = (9 + 2) * 4 * (9 + 2);
    static uint8_t src[kMaxSrc], theirs[kMaxDst], ours[kMaxDst];
    std::mt19937 rng(0x2c6890);
    for (unsigned round = 0; round < 300; ++round) {
        uint8_t *const made = cLightMapManager::MakeIncidenceTable(2.0f / kScales[rng() % std::size(kScales)],
                                                                  kClamps[rng() % std::size(kClamps)]);
        std::memcpy(table, made, sizeof table);
        cLightMapManager::FreeIncidenceTable(made);
        const unsigned shape = rng() % 5;
        auto word = [&](unsigned at, int16_t v) { std::memcpy(table + at, &v, 2); };
        if (shape == 2 || shape == 3) {
            for (unsigned c = 0; c < 4; ++c) {
                word(2 * c, int16_t(int32_t(rng() % 31) - 15));
                if (rng() % 2) word(8 + 2 * c, int16_t(rng()));
                if (rng() % 2) word(0x10 + 2 * c, int16_t(rng()));
                if (rng() % 2) word(0x18 + 2 * c, int16_t(rng()));
            }
        }
        if (shape == 4) {
            // Wide weights (+-32 or near it, the magnitudes summing to at most 128): the sums saturate with full-range
            // texels, yet the two halves can't both reach -32768, so the index stays in the table.
            for (unsigned c = 0; c < 4; ++c) {
                const int32_t magnitude = 32 - (rng() % 3 == 0 ? 1 : 0);
                word(2 * c, int16_t(rng() % 2 ? magnitude : -magnitude));
            }
        }
        if (shape == 3)
            for (unsigned at = 0x20; at < sizeof table; at += 4) {
                const uint32_t r = rng();
                std::memcpy(table + at, &r, 4);
            }
        for (unsigned call = 0; call < 40; ++call) {
            const int32_t w = int32_t(1 + rng() % 9), h = int32_t(1 + rng() % 9);
            const int32_t srcSlack = int32_t(rng() % 3), dstSlack = int32_t(rng() % 3);
            const int32_t srcPitch = (2 * w + srcSlack) * 4, dstPitch = (w + dstSlack) * 4;
            // Texels: random bytes, or (with the wide weights, half the calls) each byte 0 or 255.
            const bool extreme = shape == 4 && rng() % 2 != 0;
            for (unsigned i = 0; i < unsigned(srcPitch * 2 * h); ++i)
                src[i] = extreme ? uint8_t(rng() % 2 ? 0xff : 0) : uint8_t(rng() >> (rng() % 2 ? 0 : 24));
            const unsigned dstBytes = unsigned(dstPitch * (h + 2));
            for (unsigned i = 0; i < dstBytes; ++i) theirs[i] = ours[i] = uint8_t(rng());
            const bool wrapped = rng() % 2 != 0;
            __asm { pxor mm7, mm7 }
            __asm { emms }
            if (wrapped) wrapper(theirs + dstPitch, w + dstSlack, src, 2 * w + srcSlack, w, h, table);
            else kernel(theirs + dstPitch, dstPitch, src, srcPitch, w, h, table);
            if (wrapped) cLightMapManager::Incidence(ours + dstPitch, w + dstSlack, src, 2 * w + srcSlack, w, h, table);
            else cLightMapManager::IncidenceKernel(ours + dstPitch, dstPitch, src, srcPitch, w, h, table);
            ++ctx.cases;
            if (std::memcmp(theirs, ours, dstBytes) != 0) {
                unsigned at = 0;
                while (theirs[at] == ours[at]) ++at;
                ctx.Fail("round %u call %u (table shape %u, %s): %dx%d, pitches %d / %d: first difference at byte %u "
                         "(%02x / %02x)",
                         round, call, shape, wrapped ? "wrapper" : "kernel", w, h, srcPitch, dstPitch, at, theirs[at],
                         ours[at]);
                return;
            }
        }
    }
}

// Verifies BlendImage 0x2c7570: two pool images with random sizes, pitches and texels; a null source, either without
// pixels (the destination then made to the source's size by Init), a destination whose pitch doesn't fit; t at 0, 1,
// halves, out of range, NaN and the alpha's rounding ties ((k + 0.5) / 255 and their neighbours). The image library's
// blend 0xb3c6c0 runs for real on both sides; every handed-out image's arena is hashed after.
DIFF_TEST(SG_LightMapManagerBlend, 0x2c7570)
{
    const auto blend = ctx.Original<void(__cdecl *)(void *, void *, float)>(0x2c7570);
    sgfx::CheckSides(
        ctx, "blend image", 3000, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &rng) {
            Fake *const src = HandOutImage();
            Fake *const dst = HandOutImage();
            for (Fake *f : {src, dst}) {
                const uint32_t w = 1 + rng() % 64, h = 1 + rng() % 64;
                f->value[0] = w, f->value[1] = h;
                f->value[2] = w * 4 + (rng() % 6 == 0 ? 4 : 0);
                f->value[3] = rng() % 7 != 0;
                for (uint32_t &p : gPixels[f->value[4]]) p = rng();
            }
            if (rng() % 3 == 0) {
                // The destination at the source's size (its pitch then fits unless slack was added).
                dst->value[0] = src->value[0];
                dst->value[1] = rng() % 2 ? src->value[1] : dst->value[1];
                dst->value[2] = dst->value[0] * 4 + (rng() % 5 == 0 ? 4 : 0);
            }
            float t;
            const uint32_t pick = rng() % 4;
            if (pick == 0) {
                static const float kTs[] = {0.0f, 1.0f, 0.5f, 0.25f, kNan, -1.0f, 2.0f, 0.75f, -0.0f};
                t = kTs[rng() % std::size(kTs)];
            } else {
                t = (float(rng() % 256) + 0.5f) / 255.0f;
                if (pick == 2) t = std::bit_cast<float>(std::bit_cast<uint32_t>(t) + 1);
                if (pick == 3) t = std::bit_cast<float>(std::bit_cast<uint32_t>(t) - 1);
            }
            void *const from = rng() % 10 == 0 ? nullptr : static_cast<void *>(src);
            drawfx::Note("blend", {drawfx::Ptr(from), std::bit_cast<uint32_t>(t)});
            if (original) blend(from, dst, t);
            else cLightMapManager::BlendImage(static_cast<nGZSceneGraph::cIImageData *>(from),
                                              reinterpret_cast<nGZSceneGraph::cIImageData *>(dst), t);
            NotePixels("blended");
            ++ctx.cases;
        });
}

// Verifies Blend 0x2c8280 (thiscall on dst, ret 8) on groups the manager never makes: each of the four destination
// vectors 0-4 images short of the source's 0-3, or longer, so the growth loop makes several images (through the COM
// director's image class) or none; then each pair blended. Each vector's images (pool indices), the pixels and the fakes'
// counts noted after; both groups destroyed.
DIFF_TEST(SG_LightMapManagerBlendGroups, 0x2c8280)
{
    const auto blend = ctx.Original<void(__thiscall *)(void *, void *, float)>(0x2c8280);
    Stands stands;
    sgfx::CheckSides(
        ctx, "blend groups", 1500, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &rng) {
            difftest::AllocationCounter counter;
            {
                LightMapImages src[4], dst[4];
                auto push = [&](LightMapImages &v) {
                    Fake *const f = HandOutImage();
                    const uint32_t w = 2 + rng() % 63, h = 2 + rng() % 63;
                    f->value[0] = w, f->value[1] = h;
                    f->value[2] = w * 4 + (rng() % 6 == 0 ? 4 : 0);
                    f->value[3] = rng() % 5 != 0;
                    for (uint32_t &p : gPixels[f->value[4]]) p = rng();
                    void *const image = f;
                    v.EmplaceReallocate(v.mLast, &image);
                    --f->refs; // the vector holds it now
                };
                for (unsigned k = 0; k < 4; ++k) {
                    const unsigned n = rng() % 4, m = rng() % 4 == 0 ? n + 1 : rng() % (n + 1);
                    for (unsigned i = 0; i < n; ++i) push(src[k]);
                    for (unsigned i = 0; i < m; ++i) push(dst[k]);
                }
                static const float kTs[] = {0.0f, 1.0f, 0.5f, 0.25f, kNan, 0.75f};
                const float t = kTs[rng() % std::size(kTs)];
                drawfx::Note("blend", std::bit_cast<uint32_t>(t));
                if (original) blend(dst, src, t);
                else cLightMapManager::Blend(dst, src, t);
                for (const LightMapImages *group : {src, dst}) {
                    drawfx::Call c;
                    c.note = "group";
                    for (unsigned k = 0; k < 4; ++k) {
                        c.words.push_back(group[k].Size());
                        for (uint32_t i = 0; i < group[k].Size(); ++i)
                            c.words.push_back(uint32_t(static_cast<Fake *>(group[k].mFirst[i]) - gw.images));
                    }
                    drawfx::gLog.push_back(std::move(c));
                }
                NotePixels("blended");
            }
            NoteFakes();
            counter.Stop();
            drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
            ++ctx.cases;
        });
}

// Verifies ClassId 0x2c7450 (the class id the nTSSG COM director registers).
DIFF_TEST(SG_LightMapManagerClassId, 0x2c7450)
{
    const uint32_t theirs = ctx.Original<uint32_t(__cdecl *)()>(0x2c7450)(), ours = cLightMapManager::ClassId();
    ++ctx.cases;
    if (theirs != ours) ctx.Fail("class id %08x, ours %08x", theirs, ours);
}

// Verifies WallFaceHeight 0x2c6f20: about 20 keys a seed (the world's walls and random ones), the ground heights
// (0x7e4680), the wall flag (0x7e2050), the partition type (slot 50), the roofs (none, under one or not, their heights)
// and the kind test (0x7ef7b0) from tables with NaN and 1e9; the result's double bits noted.
DIFF_TEST(SG_LightMapManagerHeight, 0x2c6f20)
{
    Stands stands;
    const auto height = ctx.Original<double(__cdecl *)(const WallKey *)>(0x2c6f20);
    sgfx::CheckSides(
        ctx, "wall face height", 3000, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &rng) {
            const unsigned keys = 10 + rng() % 20;
            for (unsigned i = 0; i < keys; ++i) {
                if (rng() % 3) gw.buf.wall = gw.walls[rng() % gw.walls.size()];
                else gw.buf.wall = WallKey{Coordinate(rng), Coordinate(rng), rng() % 4, Coordinate(rng), Coordinate(rng), rng() % 4};
                const double r = original ? height(&gw.buf.wall) : cLightMapManager::WallFaceHeight(&gw.buf.wall);
                const uint64_t bits = std::bit_cast<uint64_t>(r);
                drawfx::Note("height", {uint32_t(bits), uint32_t(bits >> 32)});
                ++ctx.cases;
            }
        });
}
