// Differential test of nTSSG::cShadowManager's life (lane 261108u, scenegraph LG6c; src/sg/cShadowManager.h).
//
//   sg_shadow_manager_life  one manager's life on each side: built by the director's factory 0x2e7ba0 (the padding neither
//       writes then set alike) or by the constructor 0x2e63a0 on a block filled alike, sometimes switched to the
//       render-target mode (+0xd0) or disabled (+0xd1), then random calls through its two vtables: Init / Shutdown /
//       Update (3-5), SetOption (6), ResetResources (7), AcquireShadow (8: five nodes, four owners; a name the texture
//       service already has, bounds too small, unordered or NaN, each resolution band, an image that is not ready, a
//       render target refused), ReleaseShadow (9, held indices and others), MakeStaticShadow (10: positions, the zero
//       one too, each node, an image not ready, the snow setting), the small slots 11-16,
//       DoMessage (+4 slot 3: the two requeue messages, a wall's and a fence's segment, a level's lot rectangles, a
//       changed segment, others; the standard face missing for some), QueryInterface through each face, AddRef /
//       Release through each face (the +4 face's QueryInterface and Release are the adjustor thunks). After each call
//       the object is noted word by word (vtables left out), then what its containers hold: the two sets, each entry
//       (its name's text and string words, the bytes after it), the free indices, the hash map in list order with its
//       bucket vector, the queue and the blur brush's words. At the end the manager is deleted
//       (slot 17 with the destructor 0x2e6c70, or the last Release), then the fakes' counts and the heap.
//
//   sg_shadow_draw_helpers  the camera helpers 0x3ad230 / 0x3ad6b0 / 0x3ad080 / 0x3ace40 and PrepareNode 0x2e7020
//       called directly on the same world (masks, boxes, positions with zero and NaN, references, nodes).
//
// The drawing (lane 261109a, row LG6d) runs on real fakes: the nodes are a tree (links in value[]: AddChild takes a child
// from its old parent; boxes, flags and world transforms per call), the viewer (class 0xdca76dbb: its entry's
// projection, GetView answering its out or another transform), the camera owner's FindNode by name, shape holders
// reached through three counted iterators, shapes with part lods, parts, extensions, data lists (a virtual base) and
// items, the light holder (class 0x253d2018), the graphic system in cell 0x10ca87c (device, state), the texture
// manager's GetTexture (textures ready or not, or none).
// Stood in on both sides (code patches over the image's functions, which ours reach through OriginalFunction): the
// drawing's callees nobody has replaced (noted): the subset binder 0x3ad4a0, the brush's two draws 0x3ca190 / 0x3ca030,
// the animated face 0xae1ed0, the named node 0xae7490, the relative transform 0xaeb2c0 (its out, or another), the
// light's constructor 0x3a40d0 (the memory freed; the next of a pool of lights handed back), the getters 0x20dfb5 (the texture
// service), 0x20e0b2 (placement), 0x20de97 (wall partitions), 0x20defb (the camera's owner), 0xb38f60 (the common
// system), the director 0x1ae18d (it makes the images, the three resources), the block vector's FreeBlock 0x195720
// (the level message's rectangles: the original reaches it at its address, ours directly), RZMemory's Malloc / Free
// and the game's operator new / delete / free (to ours, so the original's blocks are counted and normalised like ours).
// Cells: the framework 0x10c9378 (GetSystemService hands out the message server and the settings), the message server
// cache 0x10f519c, the allocator service 0x10c95d4 / guard 0x10c95d8 (the settings keys' strings), the texture manager
// 0x10f8f94 (the same fake as the texture service: AddRefId / ReleaseId noted), the cached singleton 0x10cc288 (its
// object table and the found objects' faces, a holder with a virtual base).
#include "sgfx.h"

#include "code_patch.h"

#include "gz/cIGZPropertySet.h"
#include "rz/RZMemory.h"
#include "rz/StaticGetters.h"
#include "sg/SGUtilities.h"
#include "sg/cShadowManager.h"
#include "simantics/ServiceRefVector.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <random>
#include <set>
#include <string>
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
template <size_t N> constexpr std::array<Slot, N> Table()
{
    const At none[] = {{0, {2, Ret::Int, true, "QueryInterface", "ua"}}};
    return Table<N>(none);
}

// cIGZFrameWork: 5 GetSystemService.
constexpr At kFrameworkAt[] = {{5, {3, Ret::Int, true, "GetSystemService", "uua"}}};
constexpr auto kFrameworkSlots = Table<6>(kFrameworkAt);
constexpr drawfx::Iface kFramework{"FrameWork", 6, kFrameworkSlots.data(), false, drawfx::kTestTable};

// cIGZMessageServer2: 5 AddNotification, 6 RemoveNotification.
constexpr At kServerAt[] = {{5, {2, Ret::Int, true, "AddNotification", "au"}},
                            {6, {2, Ret::Int, true, "RemoveNotification", "au"}}};
constexpr auto kServerSlots = Table<9>(kServerAt);
constexpr drawfx::Iface kServer{"MessageServer2", 9, kServerSlots.data(), false, drawfx::kTestTable};

// The allocator service: 3 allocates, 4 frees.
constexpr At kAllocatorAt[] = {{3, {1, Ret::Int, true, "Allocate", "u"}}, {4, {1, Ret::Int, true, "Free", "a"}}};
constexpr auto kAllocatorSlots = Table<5>(kAllocatorAt);
constexpr drawfx::Iface kAllocator{"AllocatorService", 5, kAllocatorSlots.data(), false, drawfx::kTestTable};

// cIGZPropertySetString: 9 GetPropertyValue (key, any; the key's text noted by the handler).
constexpr At kSettingsAt[] = {{9, {2, Ret::Int, true, "GetPropertyValue", "aa"}}};
constexpr auto kSettingsSlots = Table<10>(kSettingsAt);
constexpr drawfx::Iface kSettings{"PropertySet", 10, kSettingsSlots.data(), false, drawfx::kTestTable};

// cIGZCOM: 3 GetClassObject.
constexpr At kDirectorAt[] = {{3, {3, Ret::Int, true, "GetClassObject", "uua"}}};
constexpr auto kDirectorSlots = Table<4>(kDirectorAt);
constexpr drawfx::Iface kDirector{"COM", 4, kDirectorSlots.data(), false, drawfx::kTestTable};

// The director's image (class 0x2b96cc2b): 6 ready, 9 made (size, format, fill).
constexpr At kImageAt[] = {{6, {0, Ret::Int, true, "Ready", ""}}, {9, {8, Ret::Int, true, "Make", "uuuuuuF4u"}}};
constexpr auto kImageSlots = Table<10>(kImageAt);
constexpr drawfx::Iface kImage{"Image", 10, kImageSlots.data(), false, drawfx::kTestTable};

// Class 0xdca76dbb (a cIViewerRefNode): 54 GetWorldTransform (hidden out), 65 SetRotation, 66, 91 (seven words), 94
// (the pass-1 texture), 97, 123 Render, 124, 131 SetPixelAspect, 136 SetOrtho (index, six floats), 139 GetEntry, 140
// GetView (hidden out, index).
constexpr At kBcAt[] = {{54, {1, Ret::Int, true, "GetWorldTransform", "a"}},
                        {65, {1, Ret::Int, true, "SetRotation", "F4"}},
                        {66, {1, Ret::Int, true, "Slot66", "u"}},
                        {91, {1, Ret::Int, true, "Slot91", "F7"}},
                        {94, {1, Ret::Int, true, "Slot94", "u"}},
                        {97, {1, Ret::Int, true, "Slot97", "u"}},
                        {123, {0, Ret::Int, true, "Render", ""}},
                        {124, {1, Ret::Int, true, "Slot124", "u"}},
                        {131, {2, Ret::Int, true, "SetPixelAspect", "uf"}},
                        {136, {7, Ret::Int, true, "Slot136", "fffffff"}},
                        {139, {1, Ret::Int, true, "GetEntry", "u"}},
                        {140, {2, Ret::Int, true, "GetView", "au"}}};
constexpr auto kBcSlots = Table<141>(kBcAt);
constexpr drawfx::Iface kBc{"ClassBC", 141, kBcSlots.data(), false, drawfx::kTestTable};

// A tree node (the shadowed nodes, their children and old parent, class 0x65246462 mRefC4, the shape holders): the
// composition-tree slots the drawing calls, cITransformNode's setters, cIBoundedNode's 70 / 72, cIShapeRefNode's
// 93-97, and 124 the name.
constexpr At kTreeAt[] = {{34, {0, Ret::Int, true, "GetParent", ""}},
                          {38, {0, Ret::Int, true, "GetNextSibling", ""}},
                          {42, {1, Ret::Int, true, "GetChildren", "a"}},
                          {44, {3, Ret::Int, true, "MakeIterator44", "aua"}},
                          {45, {1, Ret::Int, true, "AddChild", "a"}},
                          {46, {1, Ret::Int, true, "RemoveChild", "a"}},
                          {48, {1, Ret::Int, true, "TestFlag", "u"}},
                          {54, {1, Ret::Int, true, "GetWorldTransform", "a"}},
                          {63, {1, Ret::Int, true, "SetTransform", "F7"}},
                          {64, {1, Ret::Int, true, "SetTranslation", "F3"}},
                          {65, {1, Ret::Int, true, "SetRotation", "F4"}},
                          {70, {0, Ret::Int, true, "IsEverywhere", ""}},
                          {72, {1, Ret::Int, true, "GetBoundBox", "a"}},
                          {93, {1, Ret::Int, true, "GetShapes", "a"}},
                          {94, {1, Ret::Int, true, "AddShape", "a"}},
                          {95, {1, Ret::Int, true, "RemoveShape", "a"}},
                          {97, {0, Ret::Int, true, "AnyShapeSlot52", ""}},
                          {124, {0, Ret::Int, true, "Name", ""}}};
constexpr auto kTreeSlots = Table<125>(kTreeAt);
constexpr drawfx::Iface kTree{"TreeNode", 125, kTreeSlots.data(), false, drawfx::kTestTable};

// The graphic system in 0x10ca87c (0x18ae40's): 21 the current device's index, 24 a device; the device: 20 its state,
// 29 BeginScene, 30 EndScene; the state: 9 Reset.
constexpr At kGraphicsAt[] = {{21, {0, Ret::Int, true, "GetCurrentDevice", ""}}, {24, {1, Ret::Int, true, "GetDevice", "u"}}};
constexpr auto kGraphicsSlots = Table<25>(kGraphicsAt);
constexpr drawfx::Iface kGraphics{"GraphicSystem", 25, kGraphicsSlots.data(), false, drawfx::kTestTable};
constexpr At kDeviceAt[] = {{20, {0, Ret::Int, true, "GetStateConst", ""}},
                            {29, {0, Ret::Int, true, "BeginScene", ""}},
                            {30, {0, Ret::Int, true, "EndScene", ""}}};
constexpr auto kDeviceSlots = Table<31>(kDeviceAt);
constexpr drawfx::Iface kDevice{"Device", 31, kDeviceSlots.data(), false, drawfx::kTestTable};
constexpr At kStateAt[] = {{9, {1, Ret::Int, true, "Reset", "u"}}};
constexpr auto kStateSlots = Table<10>(kStateAt);
constexpr drawfx::Iface kState{"DeviceState", 10, kStateSlots.data(), false, drawfx::kTestTable};

// The static shadow's light holder (class 0x253d2018, a cILightRefNode): 93 SetLight. The light (0x3a40d0's, stood
// in): 13 its matrix (sixteen floats, a word: the floats noted by LightMatrix only after a pass drew them), 14 its
// texture.
constexpr At kLightHolderAt[] = {{93, {1, Ret::Int, true, "SetLight", "a"}}};
constexpr auto kLightHolderSlots = Table<94>(kLightHolderAt);
constexpr drawfx::Iface kLightHolder{"LightHolder", 94, kLightHolderSlots.data(), false, drawfx::kTestTable};
constexpr At kLightAt[] = {{13, {2, Ret::Int, true, "SetMatrix", "au"}}, {14, {1, Ret::Int, true, "SetTexture", "u"}}};
constexpr auto kLightSlots = Table<15>(kLightAt);
constexpr drawfx::Iface kLight{"Light", 15, kLightSlots.data(), false, drawfx::kTestTable};

// An animated face (0xae1ed0's answer): 52 a bone by id.
constexpr At kAnimFaceAt[] = {{52, {1, Ret::Int, true, "Bone", "u"}}};
constexpr auto kAnimFaceSlots = Table<53>(kAnimFaceAt);
constexpr drawfx::Iface kAnimFace{"AnimatedFace", 53, kAnimFaceSlots.data(), false, drawfx::kTestTable};

// cIShape: 15 Clone (mode, iid, out, context), 31 GetReferrerCount, 34 BindMaterial, 48 PartObjectAtLod2, 56
// GetPartLods (the block vector filled).
constexpr At kShapeAt[] = {{15, {4, Ret::Int, true, "Clone", "uuaa"}},
                           {31, {0, Ret::Int, true, "GetReferrerCount", ""}},
                           {34, {2, Ret::Int, true, "BindMaterial", "ss"}},
                           {48, {1, Ret::Int, true, "PartObjectAtLod2", "u"}},
                           {56, {1, Ret::Int, true, "GetPartLods", "a"}}};
constexpr auto kShapeSlots = Table<57>(kShapeAt);
constexpr drawfx::Iface kShape{"Shape", 57, kShapeSlots.data(), false, drawfx::kTestTable};

// cIDataListExtension's own table (no cIGZUnknown slots: its cIExtension is a virtual base): 12 GetListName, 25
// GetItem (counted), 26 GetCount.
constexpr std::array<Slot, 27> DataListSlots()
{
    std::array<Slot, 27> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[12] = {0, Ret::Int, true, "GetListName", ""};
    t[25] = {2, Ret::Int, true, "GetItem", "ua"};
    t[26] = {0, Ret::Int, true, "GetCount", ""};
    return t;
}
constexpr auto kDataListSlots = DataListSlots();
constexpr drawfx::Iface kDataList{"DataListExtension", 27, kDataListSlots.data(), false, drawfx::kTestTable};

// cIDataListItem: 3 GetName, 5 GetType, 15 GetSint32.
constexpr At kItemAt[] = {{3, {0, Ret::Int, true, "GetName", ""}},
                          {5, {0, Ret::Int, true, "GetType", ""}},
                          {15, {0, Ret::Int, true, "GetSint32", ""}}};
constexpr auto kItemSlots = Table<16>(kItemAt);
constexpr drawfx::Iface kItem{"DataListItem", 16, kItemSlots.data(), false, drawfx::kTestTable};

// cISceneNodeIterator: 4 IsEqual, 5 Get, 6 Next.
constexpr At kIterAt[] = {{4, {1, Ret::Int, true, "IsEqual", "a"}},
                          {5, {0, Ret::Int, true, "Get", ""}},
                          {6, {0, Ret::Int, true, "Next", ""}}};
constexpr auto kIterSlots = Table<7>(kIterAt);
constexpr drawfx::Iface kIter{"SceneNodeIterator", 7, kIterSlots.data(), false, drawfx::kTestTable};

// Class 0x25232b11: 45 (an object added).
constexpr At kC0At[] = {{45, {1, Ret::Int, true, "Add", "a"}}};
constexpr auto kC0Slots = Table<46>(kC0At);
constexpr drawfx::Iface kC0{"ClassC0", 46, kC0Slots.data(), false, drawfx::kTestTable};

constexpr auto kUnknownSlots = Table<3>();
constexpr drawfx::Iface kUnknownFace{"Unknown", 3, kUnknownSlots.data(), false, drawfx::kTestTable};

// nGZSceneGraph::cITextureManager: 18 AddRefId, 19 ReleaseId, 20 Add, 23 GetTexture, 24 GetInfo (the hidden out first),
// 25 Has.
constexpr At kTexturesAt[] = {{18, {1, Ret::Int, true, "AddRefId", "u"}},
                              {19, {1, Ret::Int, true, "ReleaseId", "u"}},
                              {20, {3, Ret::Int, true, "Add", "asu"}},
                              {23, {1, Ret::Int, true, "GetTexture", "u"}},
                              {24, {2, Ret::Int, true, "GetInfo", "au"}},
                              {25, {1, Ret::Int, true, "Has", "s"}}};
constexpr auto kTexturesSlots = Table<26>(kTexturesAt);
constexpr drawfx::Iface kTextures{"TextureManager", 26, kTexturesSlots.data(), false, drawfx::kTestTable};

// The render-target manager: 5 a target (keys out, size), 6 let go, 7 its texture, 8 its offset and scale.
constexpr At kRendererAt[] = {{5, {4, Ret::Int, true, "Target", "auuu"}},
                              {6, {2, Ret::Int, true, "Free", "uu"}},
                              {7, {2, Ret::Int, true, "Texture", "uu"}},
                              {8, {4, Ret::Int, true, "Scale", "uuaa"}}};
constexpr auto kRendererSlots = Table<9>(kRendererAt);
constexpr drawfx::Iface kRenderer{"RenderTargets", 9, kRendererSlots.data(), false, drawfx::kTestTable};

// cITSCommonSystem: 5 GetRenderTargetTextureManager.
constexpr At kCommonAt[] = {{5, {0, Ret::Int, true, "RenderTargets", ""}}};
constexpr auto kCommonSlots = Table<6>(kCommonAt);
constexpr drawfx::Iface kCommon{"CommonSystem", 6, kCommonSlots.data(), false, drawfx::kTestTable};

// The placement service (0x20e0b2): 39 the lot, 65 a tile's object count, 67 an object, 71 the tile is valid.
constexpr At kPlacementAt[] = {{39, {0, Ret::Int, true, "Lot", ""}},
                               {65, {1, Ret::Int, true, "Count", "F3"}},
                               {67, {3, Ret::Int, true, "Object", "uF3u"}},
                               {71, {1, Ret::Int, true, "Valid", "F3"}}};
constexpr auto kPlacementSlots = Table<72>(kPlacementAt);
constexpr drawfx::Iface kPlacement{"Placement", 72, kPlacementSlots.data(), false, drawfx::kTestTable};

// The lot: 76 a level's rectangles.
constexpr At kLotAt[] = {{76, {3, Ret::Int, true, "Rects", "uau"}}};
constexpr auto kLotSlots = Table<77>(kLotAt);
constexpr drawfx::Iface kLot{"Lot", 77, kLotSlots.data(), false, drawfx::kTestTable};

// The wall partitions (0x20de97): 55 the segment count, 56 a segment.
constexpr At kPartitionsAt[] = {{55, {0, Ret::Int, true, "Count", ""}}, {56, {2, Ret::Int, true, "Segment", "au"}}};
constexpr auto kPartitionsSlots = Table<57>(kPartitionsAt);
constexpr drawfx::Iface kPartitions{"Partitions", 57, kPartitionsSlots.data(), false, drawfx::kTestTable};

// The camera's owner (0x20defb): 23 a node by name (name, 0, 0, iid, out, 0), 49 the camera.
constexpr At kCameraOwnerAt[] = {{23, {6, Ret::Int, true, "FindNode", "suuuau"}}, {49, {0, Ret::Int, true, "Camera", ""}}};
constexpr auto kCameraOwnerSlots = Table<50>(kCameraOwnerAt);
constexpr drawfx::Iface kCameraOwner{"CameraOwner", 50, kCameraOwnerSlots.data(), false, drawfx::kTestTable};

constexpr At kCameraAt[] = {{30, {1, Ret::Int, true, "Position", "a"}}};
constexpr auto kCameraSlots = Table<31>(kCameraAt);
constexpr drawfx::Iface kCamera{"Camera", 31, kCameraSlots.data(), false, drawfx::kTestTable};

// The cached singleton: 26 its object table; the table: 29 an object; the object: 12 its face.
constexpr At kAppAt[] = {{26, {0, Ret::Int, true, "Table", ""}}};
constexpr auto kAppSlots = Table<27>(kAppAt);
constexpr drawfx::Iface kApp{"App", 27, kAppSlots.data(), false, drawfx::kTestTable};
constexpr At kTableAt[] = {{29, {1, Ret::Int, true, "Find", "u"}}};
constexpr auto kTableSlots = Table<30>(kTableAt);
constexpr drawfx::Iface kObjectTable{"ObjectTable", 30, kTableSlots.data(), false, drawfx::kTestTable};
constexpr At kFoundAt[] = {{12, {3, Ret::Int, true, "Face", "uua"}}};
constexpr auto kFoundSlots = Table<13>(kFoundAt);
constexpr drawfx::Iface kFound{"Found", 13, kFoundSlots.data(), false, drawfx::kTestTable};

// The found face: 129 its shadow user; the user: 13 (a level changed), 15 (a segment changed).
constexpr At kSimFaceAt[] = {{129, {0, Ret::Int, true, "ShadowUser", ""}}};
constexpr auto kSimFaceSlots = Table<130>(kSimFaceAt);
constexpr drawfx::Iface kSimFace{"SimFace", 130, kSimFaceSlots.data(), false, drawfx::kTestTable};
constexpr At kUserAt[] = {{13, {0, Ret::Int, true, "LevelChanged", ""}}, {15, {0, Ret::Int, true, "SegmentChanged", ""}}};
constexpr auto kUserSlots = Table<16>(kUserAt);
constexpr drawfx::Iface kUser{"ShadowUser", 16, kUserSlots.data(), false, drawfx::kTestTable};

// cIGZMessage2Standard: 4 GetType, 9 GetData1, 32 GetIGZUnknown.
constexpr At kMessageAt[] = {{4, {0, Ret::Int, true, "GetType", ""}},
                             {9, {0, Ret::Int, true, "GetData1", ""}},
                             {32, {0, Ret::Int, true, "GetIGZUnknown", ""}}};
constexpr auto kMessageSlots = Table<33>(kMessageAt);
constexpr drawfx::Iface kMessage{"Message2Standard", 33, kMessageSlots.data(), false, drawfx::kTestTable};

template <unsigned kSlot, class R, class... Args> R Virtual(void *self, Args... args)
{
    using F = R(__thiscall *)(void *, Args...);
    return (*reinterpret_cast<F *const *>(self))[kSlot](self, args...);
}
template <unsigned kSlot, class R = uint32_t, class... Args> R Slot0(void *o, Args... args)
{
    return Virtual<kSlot, R>(o, args...);
}

constexpr uint32_t kFrameworkCell = 0x10c9378, kServerCell = 0x10f519c, kAllocatorCell = 0x10c95d4,
                   kAllocatorGuard = 0x10c95d8, kTexturesCell = 0x10f8f94, kSingletonCell = 0x10cc288;
constexpr uint32_t kServerId = 0x04fa845b, kSettingsId = 0x0ac1887e;
constexpr uint32_t kMessageIid = 0x65297976;
constexpr uint32_t kSize = sizeof(cShadowManager);
constexpr uint32_t kEntrySize = sizeof(cShadowEntry);
constexpr uint32_t kBufferTag = 0x7d000000;
constexpr unsigned kImages = 16, kNodes = 5;
constexpr unsigned kKids = 3, kHolders = 3, kShapes = 4, kCopies = 2, kParts = 2, kExtensions = 3, kLists = 2, kItems = 4,
                   kBones = 3, kLights = 4, kTextureObjects = 2;
constexpr uint32_t kGraphicsCell = 0x10ca87c;
constexpr uint32_t kLightTag = 0x7e000000;
constexpr uint32_t kDataListIid = 0x6a836d51;

const float kNan = std::bit_cast<float>(0x7fc00000u);

// The static shadow's light (what the stand-in for its constructor 0x3a40d0 hands out): its table, the vbtable at
// +0x1c ([1] the cIGZUnknown base's offset from it), the base at +0x20; 0xe4 bytes in all, as the game's.
struct LightBlock {
    void *table;
    uint8_t pad[0x18];
    const int32_t *vbptr; // +0x1c
    void *baseTable;      // +0x20
    uint8_t rest[0xe4 - 0x24];
};
static_assert(offsetof(LightBlock, vbptr) == 0x1c && offsetof(LightBlock, baseTable) == 0x20 && sizeof(LightBlock) == 0xe4);
const int32_t kLightBases[2] = {0, 4};

// A data list: cIDataListExtension's own table, then the vbtable at +4 ([1] the cIExtension virtual base).
struct DataList {
    void *table;
    const int32_t *vbptr; // +4
    uint32_t pad[2];
    Fake base;
};
const int32_t kListBases[2] = {0, int32_t(offsetof(DataList, base)) - 4};

// The viewer's entry 0 (GetEntry's answer): its projection at +0x74.
struct ViewerEntry {
    uint32_t head[0x74 / 4];
    float projection[16];
};

// The found object's face (what Lookup hands out): a class with a virtual base, its vbtable at +4 ([1] the cIGZUnknown
// base); its first word is the sim face's table.
struct Holder {
    void *faceTable;
    const int32_t *vbptr; // +4
    uint32_t pad[2];
    Fake unknown;
};
const int32_t kHolderBases[2] = {0, int32_t(offsetof(Holder, unknown)) - 4};

// A wall's or fence's segment face: its table, then the words (the wall's six from +0x14, the fence's from +0xc).
struct SegBox {
    void *table;
    uint32_t words[10];
};

// What the calls are given (one block, registered, so its pointers log alike).
struct Buffers {
    float position[4];
    uint32_t handle;
    float matrix[16];
    uint32_t stamp;
    float offset[2];
    float scale[2];
    cShadowBox box;
    uint32_t bits;
};

struct World {
    Fake framework, server, allocator, settings, director, bc, c0, c4, textures, renderer, common, placement, lot,
        partitions, cameraOwner, camera, app, table, found, simFace, user, message, standard, segObject, segFace;
    Fake images[kImages];
    Fake nodes[kNodes];
    // The drawing's (lane 261109a).
    Fake kids[kKids], parent, holders[kHolders], shapes[kShapes], copies[kCopies], parts[kParts], extensions[kExtensions],
        listFaces[kLists], items[kItems], iters[3], bones[kBones], animFace, lightHolder, lightObjs[kLights],
        lightBases[kLights], textureObjects[kTextureObjects], graphics, device, state;
    DataList lists[kLists];
    LightBlock lights[kLights];
    unsigned nextLight;
    bool drawn; // the static pass drew (the brush into the texture) since the light was made
    ViewerEntry entry;
    float view[7];     // GetView's other answer (not its out)
    float relative[7]; // 0xaeb2c0's other answer
    Fake *shapeList[kShapes];
    uint32_t iterPos, iterCount;
    unsigned nextImage;
    Holder holder;
    SegBox segBox;
    Buffers buf;
    float cam[3];
    uint32_t lcg;
    uint32_t Next() { return lcg = lcg * 1664525u + 1013904223u, lcg >> 8; }
};
World gw;

uint8_t *Bytes(void *o) { return static_cast<uint8_t *>(o); }
uint32_t W(const void *o, uint32_t at) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(o) + at); }
template <class T> T *Arg(uint32_t a) { return reinterpret_cast<T *>(uintptr_t(a)); }

// ---- fakes ----

uint32_t SystemService(Fake &, const uint32_t *a)
{
    void **const out = Arg<void *>(a[2]);
    Fake *answer = a[0] == kServerId ? &gw.server : a[0] == kSettingsId ? &gw.settings : nullptr;
    *out = answer;
    if (answer) ++answer->refs;
    return answer != nullptr;
}
uint32_t Allocate(Fake &, const uint32_t *a) { return drawfx::P(::operator new(a[0] ? a[0] : 1)); }
uint32_t FreeBlock(Fake &, const uint32_t *a)
{
    ::operator delete(Arg<void>(a[0]));
    return 0;
}

bool Is(const char *key, const char *name) { return std::strcmp(key, name) == 0; }

// GetPropertyValue: 0 not found, 1 the value written but not found, 2-3 found.
uint32_t SettingsGet(Fake &, const uint32_t *a)
{
    const char *const key = Arg<const GZServiceString>(a[0])->c_str();
    sgfx::NoteText("setting", key);
    const uint32_t mode = gw.Next() % 4;
    if (mode == 0) return 0;
    uint32_t *const value = Arg<uint32_t>(a[1] + 4);
    const uint32_t r = gw.Next();
    if (Is(key, "shadowSize") || Is(key, "shadowCameraDistance") || Is(key, "shadowH0") || Is(key, "shadowW0") ||
        Is(key, "shadowH1") || Is(key, "shadowW1")) {
        static const float kFloats[] = {1.0f, 0.5f, 2.0f, 10.0f, -1.0f, 0.7f, kNan};
        *value = std::bit_cast<uint32_t>(kFloats[r % std::size(kFloats)]);
    } else if (Is(key, "outdoorShadowBlurRadius")) {
        static const uint32_t kRadii[] = {2, 1, 4};
        *value = kRadii[r % std::size(kRadii)];
    } else if (Is(key, "shadowResolution")) {
        static const uint32_t kSizes[] = {0x80, 0x40, 0x100, 0x20, 0};
        *value = kSizes[r % std::size(kSizes)];
    } else if (Is(key, "baseShadowResolution")) {
        static const uint32_t kSizes[] = {0x80, 0x40, 0x200, 0x10, 0x81}; // 0x81: one above a resolution
        *value = kSizes[r % std::size(kSizes)];
    } else if (Is(key, "useSnowForShadows")) {
        static const uint32_t kFlags[] = {0, 1, 0x100, 2}; // 0x100: the flag is the low byte, not the word
        *value = kFlags[r % std::size(kFlags)];
    } else {
        static const uint32_t kRadii[] = {3, 1, 0, 5};
        *value = kRadii[r % std::size(kRadii)];
    }
    return mode >= 2;
}

Fake *NextImage()
{
    Fake *const f = &gw.images[gw.nextImage++ % kImages];
    f->Returns(6, uint32_t(gw.Next() % 8 != 0));
    return f;
}
uint32_t ClassObject(Fake &, const uint32_t *a)
{
    Fake *answer = nullptr;
    switch (a[0]) {
    case 0x2b96cc2b: answer = NextImage(); break;
    case 0xdca76dbb: answer = &gw.bc; break;
    case 0x25232b11: answer = &gw.c0; break;
    case 0x65246462: answer = &gw.c4; break;
    case 0x253d2018: answer = &gw.lightHolder; break;
    default: break;
    }
    *Arg<void *>(a[2]) = answer;
    if (!answer) return 0;
    ++answer->refs;
    return 1;
}

uint32_t TextureAdd(Fake &self, const uint32_t *) { return 0x100 + self.value[0]++; }
uint32_t TextureInfo(Fake &, const uint32_t *a)
{
    Arg<uint32_t>(a[0])[0] = a[1] ^ 0x77;
    return a[0];
}
uint32_t TextureHas(Fake &, const uint32_t *) { return gw.Next() % 8 == 0; }

uint32_t RendererTarget(Fake &, const uint32_t *a)
{
    static const int32_t kKeys[] = {-1, 0, 1, 0, 2, 3, 0, 5}; // 0 often: the draw tests the first key >= 0
    int32_t *const keys = Arg<int32_t>(a[0]);
    keys[0] = kKeys[gw.Next() % std::size(kKeys)];
    keys[1] = int32_t(gw.Next() % 4);
    return a[0];
}
uint32_t RendererTexture(Fake &, const uint32_t *a) { return 0x200 + a[0]; }
uint32_t RendererScale(Fake &, const uint32_t *a)
{
    float *const offset = Arg<float>(a[2]);
    float *const scale = Arg<float>(a[3]);
    offset[0] = float(a[0]) * 0.25f, offset[1] = float(a[1]) * 0.5f;
    scale[0] = 0.5f + float(gw.Next() % 4), scale[1] = 2.0f;
    return 0;
}

uint32_t PlacementCount(Fake &, const uint32_t *) { return gw.Next() % 4; }
uint32_t PlacementObject(Fake &, const uint32_t *) { return 0x1000 + gw.Next() % 8; }
uint32_t PlacementValid(Fake &, const uint32_t *) { return gw.Next() % 3 != 0; }

// The lot's rectangles: always written (first, last, end), 0..3 of {x, y, level, extra}.
uint32_t LotRects(Fake &, const uint32_t *a)
{
    ServiceBlockVector *const v = Arg<ServiceBlockVector>(a[1]);
    const unsigned n = gw.Next() % 4;
    if (n == 0) {
        v->mFirst = v->mLast = v->mEnd = nullptr;
        return 0;
    }
    int32_t *const block = static_cast<int32_t *>(::operator new(n * 16));
    for (unsigned i = 0; i < n; ++i) {
        block[i * 4 + 0] = int32_t(gw.Next() % 9);
        block[i * 4 + 1] = int32_t(gw.Next() % 9);
        block[i * 4 + 2] = int32_t(a[0] & 0xffff);
        block[i * 4 + 3] = int32_t(gw.Next());
    }
    v->mFirst = block, v->mLast = block + n * 4, v->mEnd = block + n * 4;
    return 0;
}

float Coordinate()
{
    static const float kCoords[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.5f, 7.0f, -1.0f, 10.0f};
    return kCoords[gw.Next() % std::size(kCoords)];
}
uint32_t SegmentId()
{
    static const uint32_t kIds[] = {1, 2, 3, 7};
    return kIds[gw.Next() % std::size(kIds)];
}
uint32_t PartitionCount(Fake &, const uint32_t *) { return gw.Next() % 5; }
uint32_t PartitionSegment(Fake &, const uint32_t *a)
{
    uint32_t *const seg = Arg<uint32_t>(a[0]);
    seg[0] = std::bit_cast<uint32_t>(Coordinate());
    seg[1] = std::bit_cast<uint32_t>(Coordinate());
    seg[2] = SegmentId();
    seg[3] = std::bit_cast<uint32_t>(Coordinate());
    seg[4] = std::bit_cast<uint32_t>(Coordinate());
    seg[5] = SegmentId();
    return 0;
}

uint32_t CameraPosition(Fake &, const uint32_t *a)
{
    static const float kNudges[] = {0.0f, 1e-4f, 5e-4f, 1e-3f, 2e-3f, 1.0f};
    gw.cam[gw.Next() % 3] += kNudges[gw.Next() % std::size(kNudges)];
    float *const out = Arg<float>(a[0]);
    out[0] = gw.cam[0], out[1] = gw.cam[1], out[2] = gw.cam[2];
    return a[0];
}

uint32_t AppTable(Fake &, const uint32_t *) { return gw.Next() % 5 == 0 ? 0 : drawfx::P(&gw.table); }
uint32_t TableFind(Fake &, const uint32_t *) { return gw.Next() % 4 == 0 ? 0 : drawfx::P(&gw.found); }
uint32_t FoundFace(Fake &, const uint32_t *a)
{
    const uint32_t r = gw.Next() % 4;
    if (r != 0) {
        *Arg<void *>(a[2]) = &gw.holder;
        ++gw.holder.unknown.refs;
    }
    return r >= 2;
}
uint32_t SimUser(Fake &, const uint32_t *) { return gw.Next() % 3 == 0 ? 0 : drawfx::P(&gw.user); }

uint32_t SegmentQuery(Fake &, const uint32_t *a)
{
    *Arg<void *>(a[1]) = &gw.segBox;
    ++gw.segFace.refs;
    return 1;
}

// ---- the drawing's fakes (lane 261109a) ----

const char kLongName[] = "a_rather_long_lamp_name_7";
const char *const kNodeNames[kNodes] = {"chair", "lamp_1", kLongName, nullptr, "chair"};

// A tree node's links in value[]: its parent, its first child, its next sibling.
constexpr unsigned kParentWord = 0, kChildWord = 1, kSiblingWord = 2;
Fake *Link(uint32_t word) { return Arg<Fake>(word); }

// Takes the node from its parent's child list (no parent: nothing).
void Unlink(Fake &child)
{
    Fake *const parent = Link(child.value[kParentWord]);
    if (!parent) return;
    uint32_t *link = &parent->value[kChildWord];
    while (*link && *link != drawfx::P(&child)) link = &Link(*link)->value[kSiblingWord];
    if (*link) *link = child.value[kSiblingWord];
    child.value[kParentWord] = child.value[kSiblingWord] = 0;
    --child.refs;
}
// Puts the child first under the parent (taken from its old parent first, as a tree does).
void Attach(Fake &parent, Fake &child)
{
    Unlink(child);
    child.value[kSiblingWord] = parent.value[kChildWord];
    child.value[kParentWord] = drawfx::P(&parent);
    parent.value[kChildWord] = drawfx::P(&child);
    ++child.refs;
}

uint32_t TreeParent(Fake &self, const uint32_t *) { return self.value[kParentWord]; }
uint32_t TreeSibling(Fake &self, const uint32_t *) { return self.value[kSiblingWord]; }
// GetChildren (hidden out cChildRange: current, begin, end).
uint32_t TreeChildren(Fake &self, const uint32_t *a)
{
    uint32_t *const range = Arg<uint32_t>(a[0]);
    range[0] = range[1] = self.value[kChildWord];
    range[2] = 0;
    return a[0];
}
uint32_t TreeAdd(Fake &self, const uint32_t *a)
{
    Attach(self, *Link(a[0]));
    return 0;
}
uint32_t TreeRemove(Fake &self, const uint32_t *a)
{
    Fake &child = *Link(a[0]);
    if (child.value[kParentWord] == drawfx::P(&self)) Unlink(child);
    return 0;
}
uint32_t TreeFlag(Fake &, const uint32_t *) { return gw.Next() % 4 != 0; }
uint32_t TreeEverywhere(Fake &, const uint32_t *) { return gw.Next() % 4 == 0; }
uint32_t TreeAnyShape(Fake &, const uint32_t *) { return gw.Next() % 5 != 0; }

// World transforms (translation, then the quaternion x, y, z, w): the identity, a translation, a quarter turn about z,
// a general one.
const float kTransforms[4][7] = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
                                 {1.0f, 2.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f},
                                 {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.70710677f, 0.70710677f},
                                 {0.5f, -1.0f, 2.0f, 0.18257418f, 0.36514837f, 0.54772256f, 0.73029674f}};
uint32_t TreeWorld(Fake &, const uint32_t *a)
{
    std::memcpy(Arg<float>(a[0]), kTransforms[gw.Next() % 4], sizeof(kTransforms[0]));
    return a[0];
}
// GetBoundBox (hidden out, six floats; always written): sized per call, sometimes flat, NaN or inside out.
uint32_t TreeBox(Fake &, const uint32_t *a)
{
    float *const b = Arg<float>(a[0]);
    const uint32_t r = gw.Next() % 8;
    // 0.0081 and 0.0085 sit either side of the camera fit's smallest box (squared extent 1e-4: 1.5 s^2 here).
    static const float kSizes[] = {0.0081f, 0.0085f, 0.1f, 0.1499999f, 0.15f, 0.3f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 3.5f};
    const float s = kSizes[gw.Next() % std::size(kSizes)];
    const unsigned axis = gw.Next() % 3;
    for (unsigned i = 0; i < 3; ++i) {
        const float base = float(int32_t(gw.Next() % 7) - 3);
        b[i] = base;
        b[i + 3] = r == 0 ? base : base + (i == axis ? s : s * 0.5f);
    }
    if (r == 1) {
        b[gw.Next() % 6] = kNan;
    } else if (r == 2) {
        const unsigned k = gw.Next() % 3;
        const float t = b[k];
        b[k] = b[k + 3], b[k + 3] = t;
    }
    return a[0];
}
// GetShapes (hidden out cShapeRange: first, current, end): a run of the shape list, the current word at its end (the
// shapes are walked from the first).
uint32_t TreeShapes(Fake &, const uint32_t *a)
{
    const uint32_t first = gw.Next() % kShapes, count = gw.Next() % (kShapes - first + 1);
    uint32_t *const range = Arg<uint32_t>(a[0]);
    range[0] = drawfx::P(&gw.shapeList[first]);
    range[1] = range[2] = drawfx::P(&gw.shapeList[first + count]);
    return a[0];
}
uint32_t TreeAddShape(Fake &, const uint32_t *a)
{
    ++Link(a[0])->refs;
    return 0;
}
uint32_t TreeRemoveShape(Fake &, const uint32_t *a)
{
    --Link(a[0])->refs;
    return 0;
}
// MakeIterator44 (hidden out: three counted iterators, the cursor, the begin, the end): the filter's mask and iid noted.
uint32_t TreeIterate(Fake &, const uint32_t *a)
{
    drawfx::Note("filter", {W(Arg<void>(a[2]), 4), W(Arg<void>(a[2]), 8)});
    uint32_t *const range = Arg<uint32_t>(a[0]);
    for (unsigned i = 0; i < 3; ++i) {
        range[i] = drawfx::P(&gw.iters[i]);
        ++gw.iters[i].refs;
    }
    gw.iterPos = 0;
    gw.iterCount = gw.Next() % 4;
    return a[0];
}
uint32_t IterEqual(Fake &, const uint32_t *) { return gw.iterPos >= gw.iterCount; }
uint32_t IterGet(Fake &, const uint32_t *) { return drawfx::P(&gw.holders[(gw.iterPos + gw.Next()) % kHolders]); }
uint32_t IterNext(Fake &, const uint32_t *)
{
    ++gw.iterPos;
    return 0;
}

void MakeTree(Fake &f, uint32_t id)
{
    f.Reset<kTree>(id);
    f.On(34, &TreeParent);
    f.On(38, &TreeSibling);
    f.On(42, &TreeChildren);
    f.On(44, &TreeIterate);
    f.On(45, &TreeAdd);
    f.On(46, &TreeRemove);
    f.On(48, &TreeFlag);
    f.On(54, &TreeWorld);
    f.On(70, &TreeEverywhere);
    f.On(72, &TreeBox);
    f.On(93, &TreeShapes);
    f.On(94, &TreeAddShape);
    f.On(95, &TreeRemoveShape);
    f.On(97, &TreeAnyShape);
}

// The viewer's GetView (hidden out, index): written, and its answer the out or another transform.
uint32_t ViewerView(Fake &, const uint32_t *a)
{
    std::memcpy(Arg<float>(a[0]), kTransforms[gw.Next() % 4], sizeof(kTransforms[0]));
    return gw.Next() % 3 == 0 ? drawfx::P(gw.view) : a[0];
}

// The camera owner's FindNode (name, 0, 0, iid, out, 0): the first node of that name, counted; sometimes none.
uint32_t FindNode(Fake &, const uint32_t *a)
{
    const char *const name = Arg<const char>(a[0]);
    Fake *found = nullptr;
    for (unsigned i = 0; i < kNodes && !found; ++i)
        if (kNodeNames[i] && name && Is(kNodeNames[i], name)) found = &gw.nodes[i];
    if (gw.Next() % 6 == 0) found = nullptr;
    *Arg<Fake *>(a[4]) = found;
    if (found) ++found->refs;
    return found != nullptr;
}

// The texture manager's GetTexture (uncounted): one of two textures, or none.
uint32_t TextureGet(Fake &, const uint32_t *a) { return gw.Next() % 5 == 0 ? 0 : drawfx::P(&gw.textureObjects[a[0] % kTextureObjects]); }
uint32_t TextureReady(Fake &, const uint32_t *) { return gw.Next() % 4 != 0; }

// A shape's part lods (the block vector filled again: the old block freed), the part at a lod (sometimes none), its
// referrers, its copy (always made, counted).
uint32_t ShapeLods(Fake &, const uint32_t *a)
{
    ServiceBlockVector *const v = Arg<ServiceBlockVector>(a[0]);
    ::operator delete(v->mFirst);
    v->mFirst = v->mLast = v->mEnd = nullptr;
    if (const unsigned n = gw.Next() % 3) {
        uint32_t *const block = static_cast<uint32_t *>(::operator new(n * 4));
        for (unsigned i = 0; i < n; ++i) block[i] = gw.Next();
        v->mFirst = block, v->mLast = block + n, v->mEnd = block + n;
    }
    return 0;
}
uint32_t ShapePart(Fake &, const uint32_t *a) { return gw.Next() % 4 == 0 ? 0 : drawfx::P(&gw.parts[(a[0] + gw.Next()) % kParts]); }
uint32_t ShapeReferrers(Fake &, const uint32_t *)
{
    static const uint32_t kCounts[] = {1, 2, 0, 3, 0xffffffffu};
    return kCounts[gw.Next() % std::size(kCounts)];
}
uint32_t ShapeClone(Fake &, const uint32_t *a)
{
    Fake *const copy = &gw.copies[gw.Next() % kCopies];
    ++copy->refs;
    *Arg<Fake *>(a[2]) = copy;
    return 1;
}

// A part's first extension (sometimes none); each extension's QueryInterface for the data list: a list (counted,
// through its virtual base), sometimes written but refused, sometimes none.
uint32_t PartExtension(Fake &, const uint32_t *)
{
    const uint32_t r = gw.Next() % 4;
    return r == 0 ? 0 : drawfx::P(&gw.extensions[r - 1]);
}
uint32_t ExtensionQuery(Fake &, const uint32_t *a)
{
    if (a[0] != kDataListIid) return 0;
    const uint32_t r = gw.Next() % 6;
    if (r == 0) return 0;
    DataList *const list = &gw.lists[gw.Next() % kLists];
    ++list->base.refs;
    *Arg<DataList *>(a[1]) = list;
    return r != 1;
}
uint32_t ListName(Fake &, const uint32_t *)
{
    static const char *const kNames[] = {"tsNoShadow", "TSNOSHADOW", "tsNoShadows", "tsNoShado", "materials"};
    return drawfx::P(kNames[gw.Next() % std::size(kNames)]);
}
uint32_t ListCount(Fake &, const uint32_t *) { return gw.Next() % 4; }
uint32_t ListItem(Fake &, const uint32_t *a)
{
    Fake *const item = &gw.items[(a[0] + gw.Next()) % kItems];
    ++item->refs;
    *Arg<Fake *>(a[1]) = item;
    return 1;
}
uint32_t ItemType(Fake &, const uint32_t *)
{
    static const uint32_t kTypes[] = {2, 2, 1, 0, 3, 0x102};
    return kTypes[gw.Next() % std::size(kTypes)];
}
uint32_t ItemValue(Fake &, const uint32_t *)
{
    static const uint32_t kValues[] = {1, 0, 7, 0xffffffffu};
    return kValues[gw.Next() % std::size(kValues)];
}

// The light's SetMatrix: the sixteen floats when the static pass drew them (with no ready texture both sides hand over
// their stack's leftovers).
uint32_t LightMatrix(Fake &, const uint32_t *a)
{
    drawfx::Call c;
    c.note = gw.drawn ? "light matrix" : "light matrix unset";
    if (gw.drawn) drawfx::Words(c.words, Arg<const void>(a[0]), 16, "u");
    drawfx::gLog.push_back(std::move(c));
    return 0;
}

// The animated face's bones by id (sometimes none).
uint32_t AnimBone(Fake &, const uint32_t *) { return gw.Next() % 3 == 0 ? 0 : drawfx::P(&gw.bones[gw.Next() % kBones]); }

// ---- stand-ins ----

cShadowEntry *Entries(void *o) { return reinterpret_cast<cShadowEntry *>(uintptr_t(W(o, 0x54))); }
uint32_t EntryCount(void *o) { return (W(o, 0x58) - W(o, 0x54)) / kEntrySize; }
uint8_t *EntryAt(void *o, int32_t index)
{
    if (index < 0 || uint32_t(index) >= EntryCount(o)) return nullptr;
    return Bytes(Entries(o)) + uint32_t(index) * kEntrySize;
}

// The drawing's callees nobody has replaced yet, noted (lane 261109a).
void __cdecl StandBindSubset(void *node, const char *material, const char *subset)
{
    drawfx::Note("bind subset", drawfx::Ptr(node));
    sgfx::NoteText("material", material);
    sgfx::NoteText("subset", subset);
}
void __cdecl StandBrushTexture(void *brush, void *texture)
{
    drawfx::Note("brush to texture", {drawfx::Ptr(brush), drawfx::Ptr(texture)});
    gw.drawn = true;
}
void __cdecl StandBrushTarget(void *brush, void *renderer, int32_t key0, int32_t key1)
{
    drawfx::Note("brush to target", {drawfx::Ptr(brush), drawfx::Ptr(renderer), uint32_t(key0), uint32_t(key1)});
}
void *__cdecl StandAnimated(void *node)
{
    drawfx::Note("animated face", drawfx::Ptr(node));
    return gw.Next() % 3 == 0 ? nullptr : &gw.animFace;
}
void *__cdecl StandNamed(const char *name, void *node, uint32_t mask)
{
    sgfx::NoteText("named node", name);
    drawfx::Note("named in", {drawfx::Ptr(node), mask});
    return gw.Next() % 3 == 0 ? &gw.bones[gw.Next() % kBones] : nullptr;
}
const float *__cdecl StandRelative(float *out, void *bone, void *node)
{
    drawfx::Note("relative", {drawfx::Ptr(out), drawfx::Ptr(bone), drawfx::Ptr(node)});
    std::memcpy(out, kTransforms[gw.Next() % 4], sizeof(kTransforms[0]));
    return gw.Next() % 3 == 0 ? gw.relative : out;
}
// The light's constructor (thiscall, the memory and 1): the memory given back, and the next of the pool's lights (at
// one address on both sides, so its "shadowLight_%x" name is alike).
void *__fastcall StandLight(void *memory, void *, uint32_t flag)
{
    drawfx::Note("light", {drawfx::Ptr(memory), flag});
    gw.drawn = false;
    ::operator delete(memory);
    return &gw.lights[gw.nextLight++ % kLights];
}

void *__cdecl StandTextures()
{
    drawfx::Note("texture service", 0);
    return &gw.textures;
}
void *__cdecl StandPlacement()
{
    drawfx::Note("placement", 0);
    return &gw.placement;
}
void *__cdecl StandPartitions()
{
    drawfx::Note("partitions", 0);
    return &gw.partitions;
}
void *__cdecl StandCameraOwner()
{
    drawfx::Note("camera owner", 0);
    return &gw.cameraOwner;
}
void *__cdecl StandCommon()
{
    drawfx::Note("common system", 0);
    return &gw.common;
}
void *__cdecl StandDirector()
{
    drawfx::Note("director", 0);
    return &gw.director;
}
// ServiceBlockVector::FreeBlock (0x195720, verified elsewhere): the original reaches it at its address, ours directly.
void __fastcall StandFreeBlock(ServiceBlockVector *v, void *)
{
    drawfx::Note("free block", drawfx::Ptr(v->mFirst));
    if (!v->mFirst) return;
    ::operator delete(v->mFirst);
    v->mFirst = v->mLast = v->mEnd = nullptr;
}
void *__cdecl TestRzMalloc(uint32_t bytes) { return ::operator new(bytes ? bytes : 1); }
void __cdecl TestRzFree(void *p) { ::operator delete(p); }
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void __cdecl GameFree(void *p) { ::operator delete(p); }

struct Stands {
    shape::CodePatch bindSubset{0x3ad4a0u, shape::CodeOf(&StandBindSubset)};
    shape::CodePatch brushTexture{0x3ca190u, shape::CodeOf(&StandBrushTexture)};
    shape::CodePatch brushTarget{0x3ca030u, shape::CodeOf(&StandBrushTarget)};
    shape::StandIn animated{0xae1ed0u, &nGZSceneGraph::AnimatedFaceOf, &StandAnimated};
    shape::StandIn named{0xae7490u, &nGZSceneGraph::FindNodeByName, &StandNamed};
    shape::StandIn relative{0xaeb2c0u, &nGZSceneGraph::RelativeTransform, &StandRelative};
    shape::CodePatch light{0x3a40d0u, shape::CodeOf(&StandLight)};
    shape::CodePatch textures{0x20dfb5u, shape::CodeOf(&StandTextures)};
    shape::CodePatch placement{0x20e0b2u, shape::CodeOf(&StandPlacement)};
    shape::CodePatch partitions{0x20de97u, shape::CodeOf(&StandPartitions)};
    shape::CodePatch cameraOwner{0x20defbu, shape::CodeOf(&StandCameraOwner)};
    shape::CodePatch common{0xb38f60u, shape::CodeOf(&StandCommon)};
    shape::StandIn director{0x1ae18du, &GetDirectorFrameworkObject, &StandDirector};
    shape::StandIn freeBlock{0x195720u, &ServiceBlockVector::FreeBlock, &StandFreeBlock};
    shape::StandIn rzMalloc{0x1be3b0u, &RZMemory::Malloc, &TestRzMalloc};
    shape::StandIn rzFree{0x1be3a1u, &RZMemory::Free, &TestRzFree};
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    shape::CodePatch gameFree{0x82540fu, shape::CodeOf(&GameFree)};
};

// ---- the world ----

// The drawing's fakes (lane 261109a): the nodes' tree (node 0 holds kids 0 and 1, kid 1 holds kid 2; nodes 1 and 2
// start under the parent, 3 and 4 nowhere), the shape holders, shapes and their copies, parts, extensions, data lists
// and items, the iterators, the bones, the light holder and the pool's lights, the textures, the graphic system.
void BuildDrawing(uint32_t seed, drawfx::Cells &cells)
{
    for (unsigned i = 0; i < kKids; ++i) MakeTree(gw.kids[i], 900 + i);
    MakeTree(gw.parent, 905);
    Attach(gw.nodes[0], gw.kids[1]);
    Attach(gw.nodes[0], gw.kids[0]);
    Attach(gw.kids[1], gw.kids[2]);
    Attach(gw.parent, gw.nodes[2]);
    Attach(gw.parent, gw.nodes[1]);
    for (unsigned i = 0; i < kHolders; ++i) MakeTree(gw.holders[i], 910 + i);
    for (unsigned i = 0; i < kShapes + kCopies; ++i) {
        Fake &f = i < kShapes ? gw.shapes[i] : gw.copies[i - kShapes];
        f.Reset<kShape>(920 + i);
        f.On(15, &ShapeClone);
        f.On(31, &ShapeReferrers);
        f.On(48, &ShapePart);
        f.On(56, &ShapeLods);
    }
    for (unsigned i = 0; i < kShapes; ++i) gw.shapeList[i] = &gw.shapes[(i + seed) % kShapes];
    for (unsigned i = 0; i < kParts; ++i) {
        gw.parts[i].Reset<sgfx::kNode>(930 + i);
        gw.parts[i].On(20, &PartExtension);
    }
    for (unsigned i = 0; i < kExtensions; ++i) {
        gw.extensions[i].Reset<sgfx::kExtension>(935 + i);
        gw.extensions[i].On(0, &ExtensionQuery);
        gw.extensions[i].Returns(20, i + 1 < kExtensions ? static_cast<const void *>(&gw.extensions[i + 1]) : nullptr);
    }
    for (unsigned i = 0; i < kLists; ++i) {
        DataList &list = gw.lists[i];
        gw.listFaces[i].Reset<kDataList>(940 + i);
        gw.listFaces[i].On(12, &ListName);
        gw.listFaces[i].On(25, &ListItem);
        gw.listFaces[i].On(26, &ListCount);
        gw.listFaces[i].Embed(&list);
        list.vbptr = kListBases;
        list.pad[0] = list.pad[1] = 0;
        list.base.Reset<kUnknownFace>(945 + i);
    }
    static const char *const kItemNames[kItems] = {"shadowA", "groundshadow", "", "Shadow_B"};
    for (unsigned i = 0; i < kItems; ++i) {
        gw.items[i].Reset<kItem>(950 + i);
        gw.items[i].Returns(3, static_cast<const void *>(kItemNames[i]));
        gw.items[i].On(5, &ItemType);
        gw.items[i].On(15, &ItemValue);
    }
    for (unsigned i = 0; i < 3; ++i) {
        gw.iters[i].Reset<kIter>(955 + i);
        gw.iters[i].On(4, &IterEqual);
        gw.iters[i].On(5, &IterGet);
        gw.iters[i].On(6, &IterNext);
    }
    gw.iterPos = gw.iterCount = 0;
    for (unsigned i = 0; i < kBones; ++i) gw.bones[i].Reset<kUnknownFace>(960 + i);
    gw.animFace.Reset<kAnimFace>(965);
    gw.animFace.On(52, &AnimBone);
    gw.lightHolder.Reset<kLightHolder>(966);
    std::memset(gw.lights, 0, sizeof gw.lights);
    for (unsigned i = 0; i < kLights; ++i) {
        gw.lightObjs[i].Reset<kLight>(970 + i);
        gw.lightObjs[i].On(13, &LightMatrix);
        gw.lightBases[i].Reset<kUnknownFace>(975 + i);
        gw.lightObjs[i].Embed(&gw.lights[i]);
        gw.lightBases[i].Embed(&gw.lights[i].baseTable);
        gw.lights[i].vbptr = kLightBases;
    }
    gw.nextLight = seed % kLights;
    gw.drawn = false;
    drawfx::AddRange(gw.lights, sizeof gw.lights, kLightTag);
    for (unsigned i = 0; i < kTextureObjects; ++i) {
        gw.textureObjects[i].Reset<kImage>(980 + i);
        gw.textureObjects[i].On(6, &TextureReady);
    }
    std::memset(&gw.entry, 0, sizeof gw.entry);
    for (unsigned i = 0; i < 16; ++i) gw.entry.projection[i] = i % 5 == 0 ? 1.0f + float(seed % 3) * 0.5f : float(i % 3) * 0.125f;
    std::memcpy(gw.view, kTransforms[3], sizeof gw.view);
    gw.view[0] = 3.0f;
    std::memcpy(gw.relative, kTransforms[2], sizeof gw.relative);
    gw.relative[1] = -4.0f;
    gw.graphics.Reset<kGraphics>(985);
    gw.device.Reset<kDevice>(986);
    gw.state.Reset<kState>(987);
    gw.graphics.Returns(21, 0u);
    gw.graphics.Returns(24, static_cast<const void *>(&gw.device));
    gw.device.Returns(20, static_cast<const void *>(&gw.state));
    cells.Set(kGraphicsCell, &gw.graphics);
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    gw.lcg = seed * 2654435761u + 7;
    gw.framework.Reset<kFramework>(500);
    gw.framework.On(5, &SystemService);
    gw.server.Reset<kServer>(501);
    gw.allocator.Reset<kAllocator>(502);
    gw.allocator.On(3, &Allocate);
    gw.allocator.On(4, &FreeBlock);
    gw.settings.Reset<kSettings>(503);
    gw.settings.On(9, &SettingsGet);
    gw.director.Reset<kDirector>(504);
    gw.director.On(3, &ClassObject);
    gw.bc.Reset<kBc>(505);
    gw.bc.On(54, &TreeWorld);
    gw.bc.Returns(139, static_cast<const void *>(&gw.entry));
    gw.bc.On(140, &ViewerView);
    gw.c0.Reset<kC0>(506);
    MakeTree(gw.c4, 507);
    gw.textures.Reset<kTextures>(508);
    gw.textures.On(20, &TextureAdd);
    gw.textures.On(23, &TextureGet);
    gw.textures.On(24, &TextureInfo);
    gw.textures.On(25, &TextureHas);
    gw.renderer.Reset<kRenderer>(509);
    gw.renderer.On(5, &RendererTarget);
    gw.renderer.On(7, &RendererTexture);
    gw.renderer.On(8, &RendererScale);
    gw.common.Reset<kCommon>(510);
    gw.common.Returns(5, seed % 5 == 0 ? nullptr : static_cast<const void *>(&gw.renderer));
    gw.placement.Reset<kPlacement>(511);
    gw.placement.Returns(39, static_cast<const void *>(&gw.lot));
    gw.placement.On(65, &PlacementCount);
    gw.placement.On(67, &PlacementObject);
    gw.placement.On(71, &PlacementValid);
    gw.lot.Reset<kLot>(512);
    gw.lot.On(76, &LotRects);
    gw.partitions.Reset<kPartitions>(513);
    gw.partitions.On(55, &PartitionCount);
    gw.partitions.On(56, &PartitionSegment);
    gw.cameraOwner.Reset<kCameraOwner>(514);
    gw.cameraOwner.Returns(49, static_cast<const void *>(&gw.camera));
    gw.cameraOwner.On(23, &FindNode);
    gw.camera.Reset<kCamera>(515);
    gw.camera.On(30, &CameraPosition);
    gw.app.Reset<kApp>(516);
    gw.app.On(26, &AppTable);
    gw.table.Reset<kObjectTable>(517);
    gw.table.On(29, &TableFind);
    gw.found.Reset<kFound>(518);
    gw.found.On(12, &FoundFace);
    gw.simFace.Reset<kSimFace>(519);
    gw.simFace.On(129, &SimUser);
    gw.user.Reset<kUser>(520);
    gw.message.Reset<kMessage>(521);
    gw.standard.Reset<kMessage>(522);
    gw.segObject.Reset<kUnknownFace>(523);
    gw.segObject.On(0, &SegmentQuery);
    gw.segFace.Reset<kUnknownFace>(524);
    gw.holder.unknown.Reset<kUnknownFace>(525);
    for (unsigned i = 0; i < kImages; ++i) gw.images[i].Reset<kImage>(700 + i);
    for (unsigned i = 0; i < kNodes; ++i) {
        MakeTree(gw.nodes[i], 800 + i);
        gw.nodes[i].Returns(124, static_cast<const void *>(kNodeNames[i]));
    }
    BuildDrawing(seed, cells);
    gw.nextImage = 0;
    gw.holder.vbptr = kHolderBases;
    gw.holder.pad[0] = gw.holder.pad[1] = 0;
    gw.simFace.Embed(&gw.holder);
    std::memset(gw.segBox.words, 0, sizeof gw.segBox.words);
    gw.segFace.Embed(&gw.segBox);
    std::memset(&gw.buf, 0, sizeof gw.buf);
    gw.cam[0] = 1.0f, gw.cam[1] = 2.0f, gw.cam[2] = float(seed % 3);
    drawfx::AddRange(&gw.buf, sizeof gw.buf, kBufferTag);
    cells.Set(kFrameworkCell, &gw.framework);
    cells.Set(kServerCell, &gw.server);
    cells.Set(kTexturesCell, &gw.textures);
    cells.Set(kAllocatorCell, &gw.allocator);
    cells.Set(kAllocatorGuard, 0x80000000u);
    cells.Set(kSingletonCell, &gw.app);
}

void NoteFakes()
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "fake refs";
    for (Fake *f : {&gw.framework, &gw.server, &gw.allocator, &gw.settings, &gw.director, &gw.bc, &gw.c0, &gw.c4,
                    &gw.textures, &gw.renderer, &gw.common, &gw.placement, &gw.lot, &gw.partitions, &gw.cameraOwner,
                    &gw.camera, &gw.app, &gw.table, &gw.found, &gw.simFace, &gw.user, &gw.message, &gw.standard,
                    &gw.segObject, &gw.segFace, &gw.holder.unknown})
        c.words.push_back(uint32_t(f->refs));
    for (const Fake &f : gw.images) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.nodes) c.words.push_back(uint32_t(f.refs));
    c.words.push_back(0xdddd);
    for (Fake *f : {&gw.parent, &gw.animFace, &gw.lightHolder, &gw.graphics, &gw.device, &gw.state}) c.words.push_back(uint32_t(f->refs));
    for (const Fake &f : gw.kids) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.holders) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.shapes) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.copies) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.parts) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.extensions) c.words.push_back(uint32_t(f.refs));
    for (const DataList &l : gw.lists) c.words.push_back(uint32_t(l.base.refs));
    for (const Fake &f : gw.items) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.iters) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.bones) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.lightBases) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.textureObjects) c.words.push_back(uint32_t(f.refs));
    drawfx::gLog.push_back(std::move(c));
}

// ---- the object ----

// Each word's kind: the vtables left out; the tree heads, the renderer, the vectors, the map's list head and bucket
// vector, the queue's map words, the three resources and the brush strict pointers.
const char *ObjectKinds()
{
    static char kinds[kSize / 4 + 1];
    if (kinds[0]) return kinds;
    std::memset(kinds, 'u', kSize / 4);
    // The shadow vector's three words (21-23) too: its block is the library's 32-byte aligned large allocation, whose
    // offset in the heap block follows the heap's address; NoteObject notes its size and capacity instead.
    kinds[0] = kinds[1] = kinds[21] = kinds[22] = kinds[23] = '-';
    for (unsigned w : {4u, 6u, 20u, 24u, 25u, 26u, 28u, 30u, 31u, 32u, 35u, 36u, 47u, 48u, 49u, 51u})
        kinds[w] = 'a';
    return kinds;
}

void NoteWords(const char *label, const void *p, unsigned count, const char *kinds)
{
    drawfx::Call c;
    c.note = label;
    drawfx::Words(c.words, p, count, kinds);
    drawfx::gLog.push_back(std::move(c));
}

using NameMap = decltype(cShadowManager::mIndexByName);

void NoteObject(void *o)
{
    difftest::NotCounted bookkeeping;
    // In groups of eight words, so that a report shows where the object differs.
    static const char *const kGroups[] = {"object +0x00", "object +0x20", "object +0x40", "object +0x60",
                                          "object +0x80", "object +0xa0", "object +0xc0"};
    for (unsigned g = 0; g * 8 < kSize / 4; ++g) {
        const unsigned first = g * 8, count = kSize / 4 - first < 8 ? kSize / 4 - first : 8;
        char kinds[9] = {};
        std::memcpy(kinds, ObjectKinds() + first, count);
        NoteWords(kGroups[g], Bytes(o) + first * 4, count, kinds);
    }
    {
        drawfx::Call c;
        c.note = "sets";
        for (int32_t id : *reinterpret_cast<std::set<int32_t> *>(Bytes(o) + 0x10)) c.words.push_back(uint32_t(id));
        c.words.push_back(0xeeee);
        for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(Bytes(o) + 0x18)) c.words.push_back(id);
        drawfx::gLog.push_back(std::move(c));
    }
    const uint32_t count = EntryCount(o);
    drawfx::Note("shadows", {count, (W(o, 0x5c) - W(o, 0x54)) / kEntrySize, uint32_t(W(o, 0x54) != 0)});
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *const e = EntryAt(o, int32_t(i));
        const auto &name = reinterpret_cast<const nGZLog::LogString *>(e + 4)->mString;
        sgfx::NoteText("entry name", name.c_str());
        drawfx::Call c;
        c.note = "entry";
        c.words.push_back(i);
        c.words.push_back(W(e, 0));
        c.words.push_back(W(e, 0x18));
        c.words.push_back(W(e, 0x1c));
        c.words.push_back(W(e, 0x20));
        c.words.push_back(W(e, 0x24));
        c.words.push_back(e[0x28]);
        drawfx::Words(c.words, e + 0x2c, 27, "u");
        drawfx::gLog.push_back(std::move(c));
    }
    {
        drawfx::Call c;
        c.note = "free indices";
        const auto *const v = reinterpret_cast<const std::vector<int32_t> *>(Bytes(o) + 0x60);
        c.words.push_back(uint32_t(v->size()));
        c.words.push_back(uint32_t(v->capacity()));
        for (int32_t index : *v) c.words.push_back(uint32_t(index));
        drawfx::gLog.push_back(std::move(c));
    }
    {
        const auto *const map = reinterpret_cast<const NameMap *>(Bytes(o) + 0x6c);
        for (const auto &[key, value] : *map) {
            sgfx::NoteText("key", key.mString.c_str());
            drawfx::Note("key words", {W(&key, 0x1c), uint32_t(value)});
        }
        // The bucket vector (pairs of list iterators: the head or a node).
        drawfx::Call c;
        c.note = "buckets";
        const uint32_t first = W(o, 0x6c + 0xc), last = W(o, 0x6c + 0x10);
        drawfx::Words(c.words, reinterpret_cast<const void *>(uintptr_t(first)), (last - first) / 4, "a");
        drawfx::gLog.push_back(std::move(c));
    }
    {
        drawfx::Call c;
        c.note = "queue";
        for (uint32_t index : *reinterpret_cast<const std::deque<uint32_t> *>(Bytes(o) + 0x8c)) c.words.push_back(index);
        drawfx::gLog.push_back(std::move(c));
    }
    if (const uint32_t brush = W(o, 0xcc)) NoteWords("brush", reinterpret_cast<const uint32_t *>(uintptr_t(brush)) + 1, 10, "uuauuuuuuu");
}

struct Run {
    difftest::Context &ctx;
    bool original;
    void *o;
    std::vector<int32_t> held;

    int32_t PickIndex(std::mt19937 &rng, bool take)
    {
        if (!held.empty() && rng() % 3 != 0) {
            const size_t at = rng() % held.size();
            const int32_t index = held[at];
            if (take) held.erase(held.begin() + ptrdiff_t(at));
            return index;
        }
        static const int32_t kIndices[] = {-1, 0, 1, 2, 63, 64, 70, -2};
        return kIndices[rng() % std::size(kIndices)];
    }
    void Query(std::mt19937 &rng)
    {
        static const uint32_t kIids[] = {1u, cShadowManager::kIid, 0x452294aau, cShadowManager::kClass, 0u, 0x65297976u};
        const uint32_t iid = kIids[rng() % std::size(kIids)];
        void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
        void **const where = rng() % 6 == 0 ? nullptr : &out;
        const uint32_t face = (rng() % 2) * 4;
        const bool found = Virtual<0, uint8_t>(Bytes(o) + face, iid, where) != 0;
        drawfx::Note("query", {iid, face, uint32_t(found), found && where ? drawfx::Ptr(out) : 0x5a5a, W(o, 0xc)});
        if (found) Slot0<2>(o);
    }
    void Message(std::mt19937 &rng)
    {
        static const uint32_t kTypes[] = {0x522ced95u, 0xb23b7d6fu, 0x0b4cf313u, 0x2b4cf319u,
                                          0x2b4d18fbu, 0xcb46c21du, 0x12345678u, 0u};
        const uint32_t type = kTypes[rng() % std::size(kTypes)];
        gw.message.Returns(4, type);
        static const uint32_t kData[] = {1, 2, 3, 7, 9, 0x10002, 0x102}; // 0x102: the level is the low word, not byte
        gw.standard.Returns(9, kData[rng() % std::size(kData)]);
        gw.standard.Returns(32, static_cast<const void *>(&gw.segObject));
        for (uint32_t &w : gw.segBox.words) w = rng() % 4 == 0 ? SegmentId() : std::bit_cast<uint32_t>(Coordinate());
        gw.message.faceCount = 0;
        // The standard face is always there for the messages whose paths read through it (the original does not test it).
        const bool reads = type == 0x0b4cf313u || type == 0x2b4cf319u || type == 0x2b4d18fbu || type == 0xcb46c21du;
        if (reads || rng() % 4 != 0) gw.message.AddFace(kMessageIid, gw.standard);
        // receiver: our cIGZMessageTarget2 face (+4), slot 3 DoMessage
        const uint32_t r = Virtual<3, uint8_t>(Bytes(o) + 4, static_cast<void *>(&gw.message));
        drawfx::Note("message", {type, r});
    }

    void Step(std::mt19937 &rng)
    {
        const unsigned what = rng() % 44;
        if (what < 2) {
            drawfx::Note("init", Slot0<3, uint8_t>(o));
        } else if (what < 3) {
            drawfx::Note("shutdown", Slot0<4, uint8_t>(o));
        } else if (what < 6) {
            Slot0<5, void>(o);
        } else if (what < 7) {
            static const uint8_t kOn[] = {0, 1, 2};
            Slot0<6, void>(o, uint32_t(rng() % 12), kOn[rng() % std::size(kOn)]);
        } else if (what < 8) {
            Slot0<7, void>(o);
        } else if (what < 16) {
            if (Bytes(o)[0xd0] && !W(o, 0x50)) return; // the original's null renderer read
            static const int32_t kOwners[] = {0, 1, 7, -1};
            Fake *const node = &gw.nodes[rng() % kNodes];
            const int32_t index = Slot0<8, int32_t>(o, static_cast<void *>(node), kOwners[rng() % std::size(kOwners)]);
            drawfx::Note("acquire", uint32_t(index));
            if (index >= 0) held.push_back(index);
        } else if (what < 20) {
            const int32_t index = PickIndex(rng, true);
            if (const uint8_t *e = EntryAt(o, index))
                if (W(e, 0) == 1 && int32_t(W(e, 0x34)) >= 0 && !W(o, 0x50)) return; // the null renderer read
            Slot0<9, void>(o, index);
        } else if (what < 21 || what >= 40) {
            // The original uses mRefBC and mRefC4 without a null test, and writes the handle without one.
            if (!W(o, 0xbc) || !W(o, 0xc4)) return;
            for (float &f : gw.buf.position) f = Coordinate();
            if (rng() % 8 == 0) gw.buf.position[0] = gw.buf.position[1] = gw.buf.position[2] = 0.0f;
            gw.buf.handle = 0xa5a5a5a5u;
            Fake *const node = &gw.nodes[rng() % kNodes];
            const uint32_t r = Slot0<10, uint8_t>(o, static_cast<const float *>(gw.buf.position), static_cast<void *>(node), &gw.buf.handle);
            drawfx::Note("slot 10", {r, r ? drawfx::Ptr(gw.buf.handle) : gw.buf.handle});
        } else if (what < 22) {
            gw.buf.bits = 0xa5a5a5a5u;
            Slot0<11, void>(o, &gw.buf.bits);
            drawfx::Note("bits", gw.buf.bits);
        } else if (what < 23) {
            int32_t *const count = Slot0<12, int32_t *>(o);
            drawfx::Note("change count", {uint32_t(Bytes(count) - Bytes(o)), uint32_t(*count)});
        } else if (what < 25) {
            const int32_t index = PickIndex(rng, false);
            drawfx::Note("texture", {uint32_t(index), Slot0<13, uint32_t>(o, index)});
        } else if (what < 27) {
            const int32_t index = PickIndex(rng, false);
            for (float &f : gw.buf.matrix) f = -3.0f;
            uint32_t *const stamp = rng() % 3 == 0 ? nullptr : &gw.buf.stamp;
            if (const uint8_t *e = EntryAt(o, index); e && rng() % 2) gw.buf.stamp = W(e, 0x2c);
            else gw.buf.stamp = 0x5bad5badu;
            const uint32_t r = Slot0<14, uint8_t>(o, index, static_cast<float *>(gw.buf.matrix), stamp);
            drawfx::Call c;
            c.note = "matrix";
            c.words = {uint32_t(index), r, gw.buf.stamp};
            drawfx::Words(c.words, gw.buf.matrix, 16, "u");
            drawfx::gLog.push_back(std::move(c));
        } else if (what < 28) {
            const int32_t index = PickIndex(rng, false);
            if (const uint8_t *e = EntryAt(o, index))
                if (int32_t(W(e, 0)) > 0 && !W(o, 0x50)) return; // the null renderer read
            gw.buf.offset[0] = gw.buf.offset[1] = gw.buf.scale[0] = gw.buf.scale[1] = -5.0f;
            Slot0<15, void>(o, index, static_cast<float *>(gw.buf.offset), static_cast<float *>(gw.buf.scale));
            drawfx::Note("scale", {uint32_t(index), std::bit_cast<uint32_t>(gw.buf.offset[0]), std::bit_cast<uint32_t>(gw.buf.offset[1]),
                                   std::bit_cast<uint32_t>(gw.buf.scale[0]), std::bit_cast<uint32_t>(gw.buf.scale[1])});
        } else if (what < 29) {
            const int32_t index = PickIndex(rng, false);
            std::memset(&gw.buf.box, 0xab, sizeof gw.buf.box);
            cShadowBox *const r = Slot0<16, cShadowBox *>(o, &gw.buf.box, index);
            drawfx::Call c;
            c.note = "box";
            c.words = {uint32_t(index), drawfx::Ptr(r)};
            drawfx::Words(c.words, &gw.buf.box, 7, "u");
            drawfx::gLog.push_back(std::move(c));
        } else if (what < 36) {
            Message(rng);
        } else if (what < 38) {
            Query(rng);
        } else if (rng() % 2) {
            drawfx::Note("addref, release", {Slot0<1>(o), Slot0<2>(o)});
        } else {
            // receiver: our cIGZMessageTarget2 face (+4), slots 1 AddRef and 2 Release (the adjustor thunks)
            const uint32_t added = Virtual<1, uint32_t>(Bytes(o) + 4);
            const uint32_t released = Virtual<2, uint32_t>(Bytes(o) + 4);
            drawfx::Note("face addref, release", {added, released});
        }
    }
};

void RunLife(difftest::Context &ctx, uint32_t seed, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    void *o;
    if (seed % 4 == 0) {
        o = original ? ctx.Original<void *(__cdecl *)()>(0x2e7ba0)() : static_cast<void *>(cShadowManager::Create());
        // The bytes neither constructor writes (the padding after +0x08 and +0xd1), set alike.
        for (uint32_t at : {0x09u, 0x0au, 0x0bu, 0xd2u, 0xd3u}) Bytes(o)[at] = uint8_t(rng());
    } else {
        o = ::operator new(kSize);
        for (uint32_t i = 0; i < kSize; ++i) Bytes(o)[i] = uint8_t(rng());
        if (original) ctx.Original<void *(__thiscall *)(void *)>(0x2e63a0)(o);
        else cShadowManager::Construct(o, nullptr);
    }
    if (rng() % 2 == 0) Bytes(o)[0xd0] = 1; // the render-target mode (only the constructor writes it)
    if (rng() % 8 == 0) Bytes(o)[0xd1] = 0; // disabled
    drawfx::Note("made", drawfx::Ptr(o));
    Run run{ctx, original, o, {}};
    NoteObject(o);
    Slot0<1>(o);
    const unsigned steps = 4 + rng() % 40;
    for (unsigned i = 0; i < steps; ++i) {
        sgfx::MarkStep(ctx, "step");
        run.Step(rng);
        NoteObject(o);
    }
    sgfx::MarkStep(ctx, "delete");
    if (rng() % 2) {
        drawfx::Note("released", Slot0<2>(o));
    } else {
        void *const result = Virtual<17, void *>(o, 1u); // receiver: the deleting destructor
        drawfx::Note("deleted", uint32_t(Bytes(result) - Bytes(o)));
    }
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: the factory 0x2e7ba0, the constructor 0x2e63a0, the destructor 0x2e6c70 with the deleting destructor 0x2e6e10,
// Init 0x2e7c10, Shutdown 0x2e8de0, Update 0x2e80d0, ResetResources 0x2e8a00, AcquireShadow 0x2e8e90, ReleaseShadow
// 0x2e8a30, DoMessage 0x2e77c0, and the helpers CreateResources 0x2e7d10, ReleaseResources 0x2e8e30, Queue 0x2e6f40,
// QueueAll 0x2e6e40, Unqueue 0x2e8bf0, DrainQueue 0x2e8920, TrackCamera 0x2e9700, SegmentChanged 0x2e9690, Invalidate
// 0x2e97d0 and Lookup 0x2e8d00, the +4 face's adjustor thunks QueryInterface 0x2e89ed and Release 0x2e8a25, and
// cShadowEntry's constructor 0x2e6940, copy constructor 0x2e6830 and destructor 0x2e6db0 (with the entries' and the
// map's STL code, library); lane 261109a's slot 10 MakeStaticShadow 0x2e7380, DrawShadow 0x2e9920, RenderStatic
// 0x2e8650, RenderTarget 0x2e87c0 and PrepareNode 0x2e7020, with the camera helpers they reach.
DIFF_TEST(sg_shadow_manager_life, 0x2e8e90)
{
    Stands stands;
    uint32_t current = 0;
    sgfx::CheckSides(
        ctx, "shadow manager", 2400,
        [&](uint32_t seed, drawfx::Cells &cells) {
            current = seed;
            Build(seed, cells);
        },
        [&](bool original, std::mt19937 &rng) {
            RunLife(ctx, current, original, rng);
            ++ctx.cases;
        });
}

namespace {

// The camera helpers and PrepareNode called directly (lane 261109a): each call's result and the box or rotation noted.
void RunHelpers(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    Fake *const trees[] = {&gw.c4, &gw.nodes[0], &gw.nodes[1], &gw.nodes[3], &gw.kids[1], &gw.parent};
    // Some of the nodes under mRefC4 first, as a drawing leaves them.
    for (unsigned i = 0; i < kNodes; ++i)
        if (rng() % 3 == 0) Attach(gw.c4, gw.nodes[i]);
    const unsigned steps = 4 + rng() % 24;
    for (unsigned i = 0; i < steps; ++i) {
        sgfx::MarkStep(ctx, "step");
        auto *const node = reinterpret_cast<cIGZUnknown *>(trees[rng() % std::size(trees)]);
        const unsigned what = rng() % 5;
        if (what == 0) {
            float *const p = gw.buf.position;
            for (unsigned k = 0; k < 3; ++k) p[k] = Coordinate();
            const uint32_t r = rng() % 8;
            if (r == 0) p[0] = p[1] = p[2] = 0.0f;
            else if (r == 1) p[rng() % 3] = kNan;
            else if (r == 2) p[0] = p[1] = 0.0f;
            for (float &f : gw.buf.matrix) f = -2.0f;
            using Fn = float *(__cdecl *)(float *, const float *);
            float *const out = original ? ctx.Original<Fn>(0x3ad6b0)(gw.buf.matrix, p) : ShadowDirection(gw.buf.matrix, p);
            drawfx::Call c;
            c.note = "direction";
            c.words = {drawfx::Ptr(out)};
            drawfx::Words(c.words, gw.buf.matrix, 5, "u");
            drawfx::gLog.push_back(std::move(c));
        } else if (what == 1) {
            static const int32_t kMasks[] = {-1, 0, 4, 0xf, 3, -2};
            const int32_t mask = kMasks[rng() % std::size(kMasks)];
            float *const box = gw.buf.matrix;
            const uint32_t r = rng() % 4;
            for (unsigned k = 0; k < 6; ++k)
                box[k] = r == 0 ? (k < 3 ? 3.4e38f : -3.4e38f) : r == 1 ? kNan : float(int32_t(rng() % 5) - 2);
            auto *const reference = rng() % 4 == 0 ? reinterpret_cast<cIGZUnknown *>(&gw.kids[0]) : reinterpret_cast<cIGZUnknown *>(&gw.bc);
            using Fn = void(__cdecl *)(float *, int32_t, cIGZUnknown *, cIGZUnknown *);
            if (original) ctx.Original<Fn>(0x3ad230)(box, mask, reference, node);
            else ShadowBounds(box, mask, reference, node);
            drawfx::Call c;
            c.note = "bounds";
            c.words = {uint32_t(mask)};
            drawfx::Words(c.words, box, 6, "u");
            drawfx::gLog.push_back(std::move(c));
        } else if (what == 2) {
            using Fn = uint8_t(__cdecl *)(cIGZUnknown *, cIGZUnknown *);
            auto *const viewer = reinterpret_cast<cIGZUnknown *>(&gw.bc);
            drawfx::Note("fit", original ? ctx.Original<Fn>(0x3ad080)(viewer, node) : FitShadowCamera(viewer, node));
        } else if (what == 3) {
            using Fn = void *(__cdecl *)(cIGZUnknown *);
            drawfx::Note("root bone", drawfx::Ptr(original ? ctx.Original<Fn>(0x3ace40)(node) : FindRootBone(node)));
        } else {
            using Fn = void(__cdecl *)(cIGZUnknown *);
            if (original) ctx.Original<Fn>(0x2e7020)(node);
            else cShadowManager::PrepareNode(node);
        }
    }
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: the camera helpers ShadowBounds 0x3ad230, ShadowDirection 0x3ad6b0, FitShadowCamera 0x3ad080 and
// FindRootBone 0x3ace40, and PrepareNode 0x2e7020, called directly (the life test reaches them through slot 10 and the
// drawing).
DIFF_TEST(sg_shadow_draw_helpers, 0x3ad230)
{
    Stands stands;
    sgfx::CheckSides(
        ctx, "shadow helpers", 400, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &rng) {
            RunHelpers(ctx, original, rng);
            ++ctx.cases;
        });
}
